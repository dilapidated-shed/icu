#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_HEADER_BYTES (64u * 1024u)
#define MAX_LOCATION_BYTES 4096u
#define MAX_HOST_BYTES 1024u
#define MAX_TARGET_BYTES (16u * 1024u)
#define MAX_REDIRECTS 8

typedef struct {
    int socket_fd;
    SSL_CTX *tls_context;
    SSL *tls;
} connection;

typedef struct {
    int status;
    int has_location;
    char location[MAX_LOCATION_BYTES];
    long long content_length;
    char content_type[256];
    char content_encoding[64];
} response_head;

typedef struct {
    int use_tls;
    int port;
    char host[MAX_HOST_BYTES];
    char target[MAX_TARGET_BYTES];
} endpoint;

static void complain(const char *message) {
    fprintf(stderr, "icu: %s\n", message);
}

static int connect_tcp(const char *host, int port) {
    if (port < 1 || port > 65535) {
        return -1;
    }

    char service[6];
    if (snprintf(service, sizeof(service), "%d", port) < 1) {
        return -1;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *addresses = NULL;
    if (getaddrinfo(host, service, &hints, &addresses) != 0) {
        return -1;
    }

    int socket_fd = -1;
    for (struct addrinfo *address = addresses; address != NULL; address = address->ai_next) {
        socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd < 0) {
            continue;
        }
        if (connect(socket_fd, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        close(socket_fd);
        socket_fd = -1;
    }

    freeaddrinfo(addresses);
    return socket_fd;
}

static int host_is_ip_address(const char *host) {
    unsigned char storage[sizeof(struct in6_addr)];
    return inet_pton(AF_INET, host, storage) == 1 ||
           inet_pton(AF_INET6, host, storage) == 1;
}

static void close_connection(connection *connection) {
    if (connection->tls != NULL) {
        SSL_shutdown(connection->tls);
        SSL_free(connection->tls);
    }
    if (connection->tls_context != NULL) {
        SSL_CTX_free(connection->tls_context);
    }
    if (connection->socket_fd >= 0) {
        close(connection->socket_fd);
    }
    connection->socket_fd = -1;
    connection->tls_context = NULL;
    connection->tls = NULL;
}

static int open_connection(const char *host, int port, int use_tls, connection *result) {
    result->socket_fd = connect_tcp(host, port);
    result->tls_context = NULL;
    result->tls = NULL;

    if (result->socket_fd < 0) {
        complain("could not connect");
        return 0;
    }
    if (!use_tls) {
        return 1;
    }

    result->tls_context = SSL_CTX_new(TLS_client_method());
    if (result->tls_context == NULL || SSL_CTX_set_default_verify_paths(result->tls_context) != 1) {
        complain("could not initialize TLS trust");
        close_connection(result);
        return 0;
    }
    SSL_CTX_set_verify(result->tls_context, SSL_VERIFY_PEER, NULL);
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
    SSL_CTX_set_options(result->tls_context, SSL_OP_IGNORE_UNEXPECTED_EOF);
#endif

    result->tls = SSL_new(result->tls_context);
    if (result->tls == NULL) {
        complain("could not initialize TLS");
        close_connection(result);
        return 0;
    }

    X509_VERIFY_PARAM *verify = SSL_get0_param(result->tls);
    if (host_is_ip_address(host)) {
        if (X509_VERIFY_PARAM_set1_ip_asc(verify, host) != 1) {
            complain("could not configure TLS address verification");
            close_connection(result);
            return 0;
        }
    } else if (SSL_set_tlsext_host_name(result->tls, host) != 1 || SSL_set1_host(result->tls, host) != 1) {
        complain("could not configure TLS hostname verification");
        close_connection(result);
        return 0;
    }

    if (SSL_set_fd(result->tls, result->socket_fd) != 1 || SSL_connect(result->tls) != 1) {
        complain("TLS handshake failed");
        close_connection(result);
        return 0;
    }
    return 1;
}

static int write_all(connection *connection, const char *bytes, size_t length) {
    size_t written = 0;
    while (written < length) {
        if (connection->tls != NULL) {
            size_t remaining = length - written;
            int amount = SSL_write(connection->tls, bytes + written,
                                   (int)(remaining > 16384u ? 16384u : remaining));
            if (amount <= 0) {
                int error = SSL_get_error(connection->tls, amount);
                if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                    continue;
                }
                return 0;
            }
            written += (size_t)amount;
        } else {
#ifdef MSG_NOSIGNAL
            int flags = MSG_NOSIGNAL;
#else
            int flags = 0;
#endif
            ssize_t amount = send(connection->socket_fd, bytes + written, length - written, flags);
            if (amount < 0 && errno == EINTR) {
                continue;
            }
            if (amount <= 0) {
                return 0;
            }
            written += (size_t)amount;
        }
    }
    return 1;
}

static ssize_t read_some(connection *connection, char *buffer, size_t capacity) {
    if (connection->tls != NULL) {
        for (;;) {
            int amount = SSL_read(connection->tls, buffer,
                                  (int)(capacity > 16384u ? 16384u : capacity));
            if (amount > 0) {
                return amount;
            }
            int error = SSL_get_error(connection->tls, amount);
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                continue;
            }
            return error == SSL_ERROR_ZERO_RETURN ? 0 : -1;
        }
    }

    for (;;) {
        ssize_t amount = recv(connection->socket_fd, buffer, capacity, 0);
        if (amount < 0 && errno == EINTR) {
            continue;
        }
        return amount;
    }
}

static char *find_header_end(char *bytes, size_t length) {
    for (size_t index = 0; index + 3 < length; ++index) {
        if (bytes[index] == '\r' && bytes[index + 1] == '\n' &&
            bytes[index + 2] == '\r' && bytes[index + 3] == '\n') {
            return bytes + index + 4;
        }
    }
    return NULL;
}

static int write_output(FILE *output, const char *bytes, size_t length) {
    return fwrite(bytes, 1, length, output) == length;
}

static int parse_status(const char *headers, size_t length) {
    const char *line_end = memchr(headers, '\n', length);
    size_t line_length = line_end != NULL ? (size_t)(line_end - headers) : length;
    const char *space = memchr(headers, ' ', line_length);
    if (space == NULL || (size_t)(headers + line_length - space) < 4u) {
        return -1;
    }
    if (!isdigit((unsigned char)space[1]) ||
        !isdigit((unsigned char)space[2]) ||
        !isdigit((unsigned char)space[3])) {
        return -1;
    }
    return (space[1] - '0') * 100 + (space[2] - '0') * 10 + (space[3] - '0');
}

static int copy_header_value(const char *headers, size_t header_length,
                             const char *wanted, char *output, size_t output_capacity) {
    size_t wanted_length = strlen(wanted);
    const char *cursor = headers;
    const char *end = headers + header_length;

    const char *first_line = memchr(cursor, '\n', (size_t)(end - cursor));
    if (first_line == NULL) {
        return 0;
    }
    cursor = first_line + 1;

    while (cursor < end) {
        const char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        if (line_end == NULL) {
            line_end = end;
        }
        const char *trimmed_end = line_end;
        if (trimmed_end > cursor && trimmed_end[-1] == '\r') {
            --trimmed_end;
        }
        if (trimmed_end == cursor) {
            return 0;
        }

        const char *colon = memchr(cursor, ':', (size_t)(trimmed_end - cursor));
        if (colon != NULL && (size_t)(colon - cursor) == wanted_length &&
            strncasecmp(cursor, wanted, wanted_length) == 0) {
            const char *value = colon + 1;
            while (value < trimmed_end && (*value == ' ' || *value == '\t')) {
                ++value;
            }
            while (trimmed_end > value &&
                   (trimmed_end[-1] == ' ' || trimmed_end[-1] == '\t')) {
                --trimmed_end;
            }
            size_t value_length = (size_t)(trimmed_end - value);
            if (value_length == 0 || value_length >= output_capacity) {
                return 0;
            }
            memcpy(output, value, value_length);
            output[value_length] = '\0';
            return 1;
        }
        cursor = line_end < end ? line_end + 1 : end;
    }
    return 0;
}

static long long parse_content_length(const char *text) {
    if (text[0] == '\0') return -1;
    long long value = 0;
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
        if (!isdigit(*cursor)) return -1;
        if (value > 1073741824LL) return -1;
        value = value * 10 + (*cursor - '0');
    }
    return value;
}

static int read_response_head(connection *connection, response_head *head,
                              char *body_prefix, size_t body_capacity,
                              size_t *body_length) {
    char headers[MAX_HEADER_BYTES + 1];
    size_t header_bytes = 0;
    char buffer[16384];

    *body_length = 0;
    memset(head, 0, sizeof(*head));
    head->content_length = -1;
    strcpy(head->content_encoding, "identity");

    while (header_bytes < MAX_HEADER_BYTES) {
        ssize_t amount = read_some(connection, buffer, sizeof(buffer));
        if (amount <= 0) {
            complain(amount == 0 ? "response ended before its headers" : "response read failed");
            return 0;
        }

        size_t bytes_read = (size_t)amount;
        if (header_bytes + bytes_read > MAX_HEADER_BYTES) {
            complain("response headers are too large");
            return 0;
        }
        memcpy(headers + header_bytes, buffer, bytes_read);
        header_bytes += bytes_read;
        headers[header_bytes] = '\0';

        char *body = find_header_end(headers, header_bytes);
        if (body != NULL) {
            size_t head_length = (size_t)(body - headers);
            head->status = parse_status(headers, head_length);
            if (head->status < 100 || head->status > 999) {
                complain("invalid HTTP status line");
                return 0;
            }
            head->has_location = copy_header_value(
                headers, head_length, "Location", head->location, sizeof(head->location));
            char length_text[32];
            if (copy_header_value(headers, head_length, "Content-Length",
                                  length_text, sizeof(length_text))) {
                head->content_length = parse_content_length(length_text);
            }
            (void)copy_header_value(headers, head_length, "Content-Type",
                                    head->content_type, sizeof(head->content_type));
            (void)copy_header_value(headers, head_length, "Content-Encoding",
                                    head->content_encoding, sizeof(head->content_encoding));

            size_t available_body = header_bytes - head_length;
            if (available_body > body_capacity) {
                complain("response prefix is too large");
                return 0;
            }
            if (available_body != 0) {
                memcpy(body_prefix, body, available_body);
            }
            *body_length = available_body;
            return 1;
        }
    }

    complain("response headers are too large");
    return 0;
}

static int copy_remaining_body(connection *connection, FILE *output,
                               const char *prefix, size_t prefix_length,
                               size_t *received_length) {
    char buffer[16384];
    *received_length = 0;
    if (prefix_length != 0 && !write_output(output, prefix, prefix_length)) {
        complain("could not write response body");
        return 0;
    }
    *received_length = prefix_length;
    for (;;) {
        ssize_t amount = read_some(connection, buffer, sizeof(buffer));
        if (amount == 0) {
            return fflush(output) == 0;
        }
        if (amount < 0 || !write_output(output, buffer, (size_t)amount)) {
            complain(amount < 0 ? "response read failed" : "could not write response body");
            return 0;
        }
        *received_length += (size_t)amount;
    }
}

static int continuation(unsigned char byte) {
    return byte >= 0x80u && byte <= 0xbfu;
}

static int valid_utf8_bytes(const unsigned char *bytes, size_t length) {
    size_t index = 0;
    while (index < length) {
        unsigned char first = bytes[index++];
        if (first <= 0x7fu) continue;
        if (first >= 0xc2u && first <= 0xdfu) {
            if (index >= length || !continuation(bytes[index++])) return 0;
            continue;
        }
        if (first >= 0xe0u && first <= 0xefu) {
            if (index + 1u >= length) return 0;
            unsigned char second = bytes[index++];
            unsigned char third = bytes[index++];
            if (!continuation(third)) return 0;
            if (first == 0xe0u) {
                if (second < 0xa0u || second > 0xbfu) return 0;
            } else if (first == 0xedu) {
                if (second < 0x80u || second > 0x9fu) return 0;
            } else if (!continuation(second)) {
                return 0;
            }
            continue;
        }
        if (first >= 0xf0u && first <= 0xf4u) {
            if (index + 2u >= length) return 0;
            unsigned char second = bytes[index++];
            unsigned char third = bytes[index++];
            unsigned char fourth = bytes[index++];
            if (!continuation(third) || !continuation(fourth)) return 0;
            if (first == 0xf0u) {
                if (second < 0x90u || second > 0xbfu) return 0;
            } else if (first == 0xf4u) {
                if (second < 0x80u || second > 0x8fu) return 0;
            } else if (!continuation(second)) {
                return 0;
            }
            continue;
        }
        return 0;
    }
    return 1;
}

static int valid_utf8_file(const char *path) {
    if (path == NULL) return -1;
    FILE *file = fopen(path, "rb");
    if (file == NULL) return -1;
    unsigned char buffer[16384];
    unsigned char *all = NULL;
    size_t used = 0;
    for (;;) {
        size_t amount = fread(buffer, 1, sizeof(buffer), file);
        if (amount != 0) {
            unsigned char *grown = realloc(all, used + amount);
            if (grown == NULL) {
                free(all);
                fclose(file);
                return -1;
            }
            all = grown;
            memcpy(all + used, buffer, amount);
            used += amount;
        }
        if (amount < sizeof(buffer)) {
            if (ferror(file)) {
                free(all);
                fclose(file);
                return -1;
            }
            break;
        }
    }
    fclose(file);
    int valid = valid_utf8_bytes(all, used);
    free(all);
    return valid;
}

static int write_response_metadata(const char *path, const response_head *head,
                                   size_t received_length, const char *body_path) {
    if (path == NULL) return 1;
    FILE *file = fopen(path, "w");
    if (file == NULL) return 0;
    int ok = fprintf(file,
        "http_status\t%d\n"
        "content_type\t%s\n"
        "content_encoding\t%s\n"
        "declared_length\t%lld\n"
        "received_length\t%zu\n"
        "utf8\t%s\n",
        head->status,
        head->content_type[0] == '\0' ? "unknown" : head->content_type,
        head->content_encoding[0] == '\0' ? "identity" : head->content_encoding,
        head->content_length,
        received_length,
        valid_utf8_file(body_path) == 1 ? "valid" : "invalid") > 0;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int redirect_status(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static int visible_url_bytes(const char *text) {
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
        if (*cursor <= 32 || *cursor >= 127) {
            return 0;
        }
    }
    return 1;
}

static void drop_fragment(char *text) {
    char *fragment = strchr(text, '#');
    if (fragment != NULL) {
        *fragment = '\0';
    }
}

static int parse_port_text(const char *text, int *port) {
    if (*text == '\0') {
        return 0;
    }
    long value = 0;
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
        if (!isdigit(*cursor)) {
            return 0;
        }
        value = value * 10 + (*cursor - '0');
        if (value > 65535) {
            return 0;
        }
    }
    if (value < 1) {
        return 0;
    }
    *port = (int)value;
    return 1;
}

static int parse_authority(const char *authority, int default_port, endpoint *result) {
    size_t length = strlen(authority);
    if (length == 0 || length >= MAX_HOST_BYTES || strchr(authority, '@') != NULL ||
        authority[0] == '[' || strchr(authority, ']') != NULL) {
        return 0;
    }

    const char *colon = strrchr(authority, ':');
    if (colon != NULL) {
        size_t host_length = (size_t)(colon - authority);
        if (host_length == 0 || host_length >= sizeof(result->host)) {
            return 0;
        }
        memcpy(result->host, authority, host_length);
        result->host[host_length] = '\0';
        if (!parse_port_text(colon + 1, &result->port)) {
            return 0;
        }
    } else {
        memcpy(result->host, authority, length + 1);
        result->port = default_port;
    }
    return 1;
}

static int set_target(endpoint *result, const char *target) {
    if (*target == '\0') {
        target = "/";
    }
    if (strlen(target) >= sizeof(result->target)) {
        return 0;
    }
    strcpy(result->target, target);
    return 1;
}

static int parse_absolute_location(const char *location, int default_tls, endpoint *result) {
    const char *rest = NULL;
    int default_port = 0;
    if (strncasecmp(location, "http://", 7) == 0) {
        result->use_tls = 0;
        default_port = 80;
        rest = location + 7;
    } else if (strncasecmp(location, "https://", 8) == 0) {
        result->use_tls = 1;
        default_port = 443;
        rest = location + 8;
    } else if (strncmp(location, "//", 2) == 0) {
        result->use_tls = default_tls;
        default_port = default_tls ? 443 : 80;
        rest = location + 2;
    } else {
        return 0;
    }

    const char *separator = strpbrk(rest, "/?");
    size_t authority_length = separator != NULL ? (size_t)(separator - rest) : strlen(rest);
    if (authority_length == 0 || authority_length >= MAX_HOST_BYTES) {
        return 0;
    }
    char authority[MAX_HOST_BYTES];
    memcpy(authority, rest, authority_length);
    authority[authority_length] = '\0';
    if (!parse_authority(authority, default_port, result)) {
        return 0;
    }

    if (separator == NULL) {
        return set_target(result, "/");
    }
    if (*separator == '?') {
        char target[MAX_TARGET_BYTES];
        int written = snprintf(target, sizeof(target), "/%s", separator);
        return written > 0 && (size_t)written < sizeof(target) && set_target(result, target);
    }
    return set_target(result, separator);
}

static int request_target(const char *request, char *output, size_t capacity) {
    if (strncmp(request, "GET ", 4) != 0) {
        return 0;
    }
    const char *start = request + 4;
    const char *space = strchr(start, ' ');
    if (space == NULL) {
        return 0;
    }
    size_t length = (size_t)(space - start);
    if (length == 0 || length >= capacity) {
        return 0;
    }
    memcpy(output, start, length);
    output[length] = '\0';
    return 1;
}

static int resolve_location(const endpoint *current, const char *request,
                            const char *raw_location, endpoint *result) {
    if (!visible_url_bytes(raw_location) || strlen(raw_location) >= MAX_LOCATION_BYTES) {
        return 0;
    }
    char location[MAX_LOCATION_BYTES];
    strcpy(location, raw_location);
    drop_fragment(location);
    if (location[0] == '\0') {
        return 0;
    }

    if (strncasecmp(location, "http://", 7) == 0 ||
        strncasecmp(location, "https://", 8) == 0 ||
        strncmp(location, "//", 2) == 0) {
        return parse_absolute_location(location, current->use_tls, result);
    }

    *result = *current;
    if (location[0] == '/') {
        return set_target(result, location);
    }

    char current_target[MAX_TARGET_BYTES];
    if (!request_target(request, current_target, sizeof(current_target))) {
        return 0;
    }
    char *query = strchr(current_target, '?');
    if (query != NULL) {
        *query = '\0';
    }

    char combined[MAX_TARGET_BYTES];
    if (location[0] == '?') {
        int written = snprintf(combined, sizeof(combined), "%s%s", current_target, location);
        return written > 0 && (size_t)written < sizeof(combined) && set_target(result, combined);
    }

    char *last_slash = strrchr(current_target, '/');
    size_t directory_length = last_slash != NULL ? (size_t)(last_slash - current_target + 1) : 1u;
    if (directory_length >= sizeof(combined)) {
        return 0;
    }
    memcpy(combined, current_target, directory_length);
    combined[directory_length] = '\0';
    if (strlen(location) >= sizeof(combined) - directory_length) {
        return 0;
    }
    strcat(combined, location);
    return set_target(result, combined);
}

static int endpoint_authority(const endpoint *value, char *output, size_t capacity) {
    int default_port = value->use_tls ? 443 : 80;
    int written = value->port == default_port
        ? snprintf(output, capacity, "%s", value->host)
        : snprintf(output, capacity, "%s:%d", value->host, value->port);
    return written > 0 && (size_t)written < capacity;
}

static int same_origin(const endpoint *left, const endpoint *right) {
    return left->use_tls == right->use_tls &&
           left->port == right->port &&
           strcasecmp(left->host, right->host) == 0;
}

static int header_line_named(const char *line, size_t line_length, const char *name) {
    size_t name_length = strlen(name);
    return line_length > name_length && line[name_length] == ':' &&
           strncasecmp(line, name, name_length) == 0;
}

static char *rewrite_get_request_with_policy(const char *request, const endpoint *next,
                                             int preserve_sensitive) {
    const char *line_end = strstr(request, "\r\n");
    if (line_end == NULL) {
        return NULL;
    }
    const char *headers = line_end + 2;
    size_t input_length = strlen(request);
    size_t capacity = input_length + strlen(next->target) + strlen(next->host) + 128u;
    char *output = malloc(capacity);
    if (output == NULL) {
        return NULL;
    }

    char authority[MAX_HOST_BYTES + 16];
    if (!endpoint_authority(next, authority, sizeof(authority))) {
        free(output);
        return NULL;
    }

    int written = snprintf(output, capacity, "GET %s HTTP/1.0\r\n", next->target);
    if (written < 0 || (size_t)written >= capacity) {
        free(output);
        return NULL;
    }
    size_t used = (size_t)written;
    int replaced_host = 0;
    const char *cursor = headers;
    while (*cursor != '\0') {
        const char *next_line = strstr(cursor, "\r\n");
        if (next_line == NULL) {
            free(output);
            return NULL;
        }
        size_t line_length = (size_t)(next_line - cursor);
        if (line_length == 0) {
            break;
        }

        if (header_line_named(cursor, line_length, "Host")) {
            written = snprintf(output + used, capacity - used, "Host: %s\r\n", authority);
            replaced_host = 1;
            if (written < 0 || (size_t)written >= capacity - used) {
                free(output);
                return NULL;
            }
            used += (size_t)written;
        } else if (!preserve_sensitive &&
                   (header_line_named(cursor, line_length, "Authorization") ||
                    header_line_named(cursor, line_length, "Cookie"))) {
            /* Match curl: do not forward these credentials to another origin. */
        } else {
            if (line_length + 2 >= capacity - used) {
                free(output);
                return NULL;
            }
            memcpy(output + used, cursor, line_length);
            used += line_length;
            memcpy(output + used, "\r\n", 2);
            used += 2;
            output[used] = '\0';
        }
        cursor = next_line + 2;
    }

    if (!replaced_host) {
        written = snprintf(output + used, capacity - used, "Host: %s\r\n", authority);
        if (written < 0 || (size_t)written >= capacity - used) {
            free(output);
            return NULL;
        }
        used += (size_t)written;
    }
    if (used + 2 >= capacity) {
        free(output);
        return NULL;
    }
    memcpy(output + used, "\r\n", 3);
    return output;
}

static char *rewrite_get_request(const char *request, const endpoint *next) {
    return rewrite_get_request_with_policy(request, next, 1);
}

static int valid_wire_request(const char *request) {
    return strncmp(request, "GET ", 4) == 0 || strncmp(request, "POST ", 5) == 0;
}

static int one_request(const endpoint *target, const char *request_headers,
                       const char *request_body, size_t request_body_length,
                       response_head *head, char *body_prefix, size_t body_capacity,
                       size_t *body_length, connection *opened) {
    if (!open_connection(target->host, target->port, target->use_tls, opened)) {
        return target->use_tls ? 5 : 4;
    }
    if (!write_all(opened, request_headers, strlen(request_headers)) ||
        (request_body_length != 0 &&
         !write_all(opened, request_body, request_body_length))) {
        complain("request write failed");
        close_connection(opened);
        return 6;
    }
    if (!read_response_head(opened, head, body_prefix, body_capacity, body_length)) {
        close_connection(opened);
        return 7;
    }
    return 0;
}

static int send_request(const char *host, int port, const char *request_headers,
                        const char *request_body, int request_body_length, int use_tls,
                        const char *body_output_path, const char *metadata_output_path) {
    if (request_body_length < 0 || (request_body_length != 0 && request_body == NULL)) {
        complain("invalid request body length");
        return 2;
    }
    if (!valid_wire_request(request_headers)) {
        complain("transport accepts only GET and POST requests");
        return 2;
    }

    int is_get = strncmp(request_headers, "GET ", 4) == 0;
    if (is_get && request_body_length != 0) {
        complain("GET request body must be empty");
        return 2;
    }

    endpoint current;
    memset(&current, 0, sizeof(current));
    current.use_tls = use_tls;
    current.port = port;
    if (strlen(host) >= sizeof(current.host)) {
        complain("host is too long");
        return 3;
    }
    strcpy(current.host, host);
    if (!request_target(request_headers, current.target, sizeof(current.target)) && is_get) {
        complain("invalid GET request target");
        return 3;
    }

    char *current_request = strdup(request_headers);
    if (current_request == NULL) {
        complain("out of memory");
        return 3;
    }

    size_t request_bytes = (size_t)request_body_length;
    for (int redirects = 0; ; ++redirects) {
        response_head head;
        char body_prefix[16384];
        size_t response_body_length = 0;
        connection opened;
        int result = one_request(&current, current_request, request_body, request_bytes,
                                 &head, body_prefix, sizeof(body_prefix),
                                 &response_body_length, &opened);
        if (result != 0) {
            free(current_request);
            return result;
        }

        int can_follow = strncmp(current_request, "GET ", 4) == 0 &&
                         redirect_status(head.status) && head.has_location;
        if (!can_follow) {
            FILE *output = stdout;
            if (body_output_path != NULL) {
                output = fopen(body_output_path, "wb");
                if (output == NULL) {
                    close_connection(&opened);
                    free(current_request);
                    complain("could not open response body output");
                    return 7;
                }
            }
            size_t received_length = 0;
            int copied = copy_remaining_body(&opened, output, body_prefix,
                                             response_body_length, &received_length);
            if (body_output_path != NULL && fclose(output) != 0) copied = 0;
            int metadata_written = write_response_metadata(metadata_output_path, &head,
                                                           received_length,
                                                           body_output_path);
            close_connection(&opened);
            free(current_request);
            if (!copied || !metadata_written) return 7;
            if (head.content_length >= 0 &&
                (unsigned long long)head.content_length != (unsigned long long)received_length) {
                complain("response body length does not match Content-Length");
                return 9;
            }
            if (head.status < 200 || head.status >= 300) return 10;
            return 0;
        }

        if (redirects >= MAX_REDIRECTS) {
            close_connection(&opened);
            free(current_request);
            complain("too many redirects");
            return 8;
        }

        endpoint next;
        if (!resolve_location(&current, current_request, head.location, &next)) {
            close_connection(&opened);
            free(current_request);
            complain("unsupported redirect location");
            return 8;
        }
        char *next_request = rewrite_get_request_with_policy(
            current_request, &next, same_origin(&current, &next));
        if (next_request == NULL) {
            close_connection(&opened);
            free(current_request);
            complain("could not construct redirected GET request");
            return 8;
        }

        close_connection(&opened);
        free(current_request);
        current_request = next_request;
        current = next;
    }
}

int icu_send_http(const char *host, int port, const char *request_headers,
                  const char *request_body, int request_body_length) {
    return send_request(host, port, request_headers, request_body, request_body_length,
                        0, NULL, NULL);
}

int icu_send_https(const char *host, int port, const char *request_headers,
                   const char *request_body, int request_body_length) {
    return send_request(host, port, request_headers, request_body, request_body_length,
                        1, NULL, NULL);
}

int icu_fetch_http(const char *host, int port, const char *request_headers,
                   const char *request_body, int request_body_length,
                   const char *body_output_path, const char *metadata_output_path) {
    return send_request(host, port, request_headers, request_body, request_body_length,
                        0, body_output_path, metadata_output_path);
}

int icu_fetch_https(const char *host, int port, const char *request_headers,
                    const char *request_body, int request_body_length,
                    const char *body_output_path, const char *metadata_output_path) {
    return send_request(host, port, request_headers, request_body, request_body_length,
                        1, body_output_path, metadata_output_path);
}
