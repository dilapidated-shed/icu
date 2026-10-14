#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_HEADER_BYTES (64u * 1024u)
#define MAX_LOCATION_BYTES 4096u
#define MAX_HOST_BYTES 1024u
#define MAX_TARGET_BYTES (16u * 1024u)
#define MAX_REDIRECTS 8
#define MAX_CAPTURE_BYTES (8u * 1024u * 1024u)

const char *icu_response_directory(void) {
    static char path[64];
    strcpy(path, "/tmp/icu-response-XXXXXX");
    return mkdtemp(path) == NULL ? "" : path;
}

typedef struct {
    int socket_fd;
    SSL_CTX *tls_context;
    SSL *tls;
    int failure_code;
} connection;

typedef struct {
    int status;
    int has_location;
    char location[MAX_LOCATION_BYTES];
    long long content_length;
    char content_type[256];
    char content_encoding[64];
    int invalid_framing;
} response_head;

/* Captures are private even when the caller's umask is permissive. */
static FILE *private_output(const char *path, const char *mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return NULL;
    FILE *file = fdopen(fd, mode);
    if (file == NULL) close(fd);
    return file;
}

typedef struct {
    int use_tls;
    int port;
    char host[MAX_HOST_BYTES];
    char target[MAX_TARGET_BYTES];
} endpoint;

typedef struct {
    int enabled;
    int port;
    char host[MAX_HOST_BYTES];
} configured_proxy;

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
        int flags = fcntl(socket_fd, F_GETFL, 0);
        if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(socket_fd);
            socket_fd = -1;
            continue;
        }
        int connected = connect(socket_fd, address->ai_addr, address->ai_addrlen) == 0;
        if (!connected && errno == EINPROGRESS) {
            struct pollfd waiting = {socket_fd, POLLOUT, 0};
            int ready;
            do { ready = poll(&waiting, 1, 15000); } while (ready < 0 && errno == EINTR);
            int error = 0;
            socklen_t error_size = sizeof(error);
            connected = ready > 0 && getsockopt(socket_fd, SOL_SOCKET, SO_ERROR,
                                                &error, &error_size) == 0 && error == 0;
            if (ready == 0) errno = ETIMEDOUT;
        }
        if (connected && fcntl(socket_fd, F_SETFL, flags) == 0) {
            struct timeval timeout = {15, 0};
            (void)setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            (void)setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
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

/* A caller-configured HTTP proxy is a transport hop, never the TLS identity.
 * No proxy credentials are accepted or copied into origin request headers. */
static int proxy_configuration(const char *origin_host, int use_tls,
                               configured_proxy *result) {
    memset(result, 0, sizeof(*result));
    if (!use_tls || strcmp(origin_host, "127.0.0.1") == 0 || strcmp(origin_host, "::1") == 0 ||
        strcasecmp(origin_host, "localhost") == 0) {
        return 1;
    }
    const char *value = getenv("HTTPS_PROXY");
    if (value == NULL || *value == '\0') {
        value = getenv("https_proxy");
    }
    if (value == NULL || *value == '\0') {
        return 1;
    }
    if (strncasecmp(value, "http://", 7) != 0 || strlen(value) >= MAX_HOST_BYTES + 32u) {
        return 0;
    }
    for (const unsigned char *letter = (const unsigned char *)value; *letter; ++letter) {
        if (*letter <= 32 || *letter == 127) {
            return 0;
        }
    }
    const char *authority = value + 7;
    if (strchr(authority, '@') || strchr(authority, '?') || strchr(authority, '#')) {
        return 0;
    }
    const char *end = strchr(authority, '/');
    if (end != NULL && strcmp(end, "/") != 0) {
        return 0;
    }
    if (end == NULL) {
        end = authority + strlen(authority);
    }
    const char *host_start = authority;
    const char *host_end;
    const char *port_start = NULL;
    if (*host_start == '[') {
        ++host_start;
        host_end = strchr(host_start, ']');
        if (host_end == NULL || host_end >= end) {
            return 0;
        }
        if (host_end + 1 < end) {
            if (host_end[1] != ':') {
                return 0;
            }
            port_start = host_end + 2;
        }
    } else {
        host_end = memchr(host_start, ':', (size_t)(end - host_start));
        if (host_end == NULL) {
            host_end = end;
        } else {
            port_start = host_end + 1;
        }
    }
    size_t host_length = (size_t)(host_end - host_start);
    if (host_length == 0 || host_length >= sizeof(result->host)) {
        return 0;
    }
    memcpy(result->host, host_start, host_length);
    result->host[host_length] = '\0';
    if (*authority == '[') {
        struct in6_addr address;
        if (inet_pton(AF_INET6, result->host, &address) != 1) return 0;
    } else {
        for (const unsigned char *letter = (const unsigned char *)result->host; *letter; ++letter)
            if (*letter >= 127 || *letter == ':' || *letter == '\\' || *letter == '[' || *letter == ']')
                return 0;
    }
    result->port = 80;
    if (port_start != NULL) {
        if (port_start == end) {
            return 0;
        }
        int port = 0;
        for (const char *letter = port_start; letter < end; ++letter) {
            if (*letter < '0' || *letter > '9' || port > 6553) {
                return 0;
            }
            port = port * 10 + (*letter - '0');
        }
        if (port < 1 || port > 65535) {
            return 0;
        }
        result->port = port;
    }
    result->enabled = 1;
    return 1;
}

static int establish_proxy_tunnel(int socket_fd, const char *host, int port) {
    char request[MAX_HOST_BYTES * 2u + 128u];
    int written = strchr(host, ':') != NULL
        ? snprintf(request, sizeof(request), "CONNECT [%s]:%d HTTP/1.0\r\nHost: [%s]:%d\r\n\r\n", host, port, host, port)
        : snprintf(request, sizeof(request), "CONNECT %s:%d HTTP/1.0\r\nHost: %s:%d\r\n\r\n", host, port, host, port);
    if (written < 0 || (size_t)written >= sizeof(request)) {
        return 0;
    }
    size_t sent = 0;
    while (sent < (size_t)written) {
        ssize_t count = send(socket_fd, request + sent, (size_t)written - sent, 0);
        if (count <= 0) {
            return 0;
        }
        sent += (size_t)count;
    }
    char response[MAX_HEADER_BYTES + 1u];
    size_t used = 0;
    while (used < MAX_HEADER_BYTES) {
        if (recv(socket_fd, response + used, 1, 0) != 1) {
            return 0;
        }
        ++used;
        if (used >= 4 && memcmp(response + used - 4, "\r\n\r\n", 4) == 0) {
            response[used] = '\0';
            return used >= 14 && memcmp(response, "HTTP/1.", 7) == 0 &&
                (response[7] == '0' || response[7] == '1') && response[8] == ' ' &&
                response[9] == '2' && isdigit((unsigned char)response[10]) &&
                isdigit((unsigned char)response[11]) &&
                (response[12] == ' ' || response[12] == '\r');
        }
    }
    return 0;
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
    result->failure_code = 4;
    result->socket_fd = -1;
    result->tls_context = NULL;
    result->tls = NULL;
    configured_proxy proxy;
    if (!proxy_configuration(host, use_tls, &proxy)) {
        result->failure_code = 14;
        complain("unsupported HTTP proxy configuration");
        return 0;
    }
    result->socket_fd = connect_tcp(proxy.enabled ? proxy.host : host,
                                    proxy.enabled ? proxy.port : port);
    result->tls_context = NULL;
    result->tls = NULL;

    if (result->socket_fd < 0) {
        if (errno == ETIMEDOUT) result->failure_code = 11;
        complain("could not connect");
        return 0;
    }
    if (!use_tls) {
        return 1;
    }
    if (proxy.enabled && !establish_proxy_tunnel(result->socket_fd, host, port)) {
        result->failure_code = (errno == EAGAIN || errno == EWOULDBLOCK ||
                                errno == ETIMEDOUT) ? 11 : 14;
        complain("HTTP proxy refused or malformed the TLS tunnel");
        close_connection(result);
        return 0;
    }

    result->failure_code = 5;

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
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)
            result->failure_code = 11;
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
                connection->failure_code = 11;
                return -1;
            }
            if (error != SSL_ERROR_ZERO_RETURN) connection->failure_code =
                (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) ? 11 : 7;
            return error == SSL_ERROR_ZERO_RETURN ? 0 : -1;
        }
    }

    for (;;) {
        ssize_t amount = recv(connection->socket_fd, buffer, capacity, 0);
        if (amount < 0 && errno == EINTR) {
            continue;
        }
        if (amount < 0) connection->failure_code =
            (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) ? 11 : 7;
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
    if (line_length < 13 || memcmp(headers, "HTTP/1.", 7) != 0 ||
        (headers[7] != '0' && headers[7] != '1') || headers[8] != ' ') return -1;
    const char *space = headers + 8;
    if (space == NULL || (size_t)(headers + line_length - space) < 4u) {
        return -1;
    }
    if (!isdigit((unsigned char)space[1]) ||
        !isdigit((unsigned char)space[2]) ||
        !isdigit((unsigned char)space[3]) ||
        (space[4] != ' ' && space[4] != '\r')) {
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
        if (value > (LLONG_MAX - (*cursor - '0')) / 10) return -1;
        value = value * 10 + (*cursor - '0');
    }
    return value;
}

static int header_count(const char *headers, size_t length, const char *name) {
    const char *end = headers + length;
    const char *line = memchr(headers, '\n', length);
    int count = 0;
    size_t name_length = strlen(name);
    while (line != NULL && ++line < end) {
        const char *next = memchr(line, '\n', (size_t)(end - line));
        if (next == NULL) break;
        if ((size_t)(next - line) > name_length && line[name_length] == ':' &&
            strncasecmp(line, name, name_length) == 0) ++count;
        line = next;
    }
    return count;
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
    connection->failure_code = 13;

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
            if (head->status < 100 || head->status > 599) {
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
            int lengths = header_count(headers, head_length, "Content-Length");
            head->invalid_framing = lengths > 1 ||
                (lengths == 1 && head->content_length < 0) ||
                header_count(headers, head_length, "Transfer-Encoding") != 0;
            (void)copy_header_value(headers, head_length, "Content-Type",
                                    head->content_type, sizeof(head->content_type));
            int encoding_copied = copy_header_value(headers, head_length, "Content-Encoding",
                                    head->content_encoding, sizeof(head->content_encoding));
            int encodings = header_count(headers, head_length, "Content-Encoding");
            if (encodings > 1 || (encodings == 1 && !encoding_copied))
                strcpy(head->content_encoding, "unsupported");

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
    if (output != stdout && prefix_length > MAX_CAPTURE_BYTES) {
        connection->failure_code = 12;
        return 0;
    }
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
        if (output != stdout && amount > 0 &&
            (size_t)amount > MAX_CAPTURE_BYTES - *received_length) {
            complain("captured response exceeds 8 MiB");
            connection->failure_code = 12;
            return 0;
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
    FILE *file = private_output(path, "w");
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

static int header_line_named_in_list(const char *line, size_t line_length,
                                     const char *names) {
    if (names == NULL) {
        return 0;
    }
    const char *cursor = names;
    while (*cursor != '\0') {
        const char *line_end = strchr(cursor, '\n');
        size_t name_length = line_end == NULL
            ? strlen(cursor)
            : (size_t)(line_end - cursor);
        if (name_length != 0 && line_length > name_length &&
            line[name_length] == ':' &&
            strncasecmp(line, cursor, name_length) == 0) {
            return 1;
        }
        if (line_end == NULL) {
            break;
        }
        cursor = line_end + 1;
    }
    return 0;
}

static char *rewrite_get_request_with_policy(const char *request, const endpoint *next,
                                             int preserve_sensitive,
                                             const char *credential_header_names) {
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
                    header_line_named(cursor, line_length, "Cookie") ||
                    header_line_named_in_list(
                      cursor, line_length, credential_header_names))) {
            /* Do not forward credentials to another origin. */
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
    return rewrite_get_request_with_policy(request, next, 1, "");
}

static int valid_wire_request(const char *request) {
    return strncmp(request, "GET ", 4) == 0 || strncmp(request, "POST ", 5) == 0;
}

static int one_request(const endpoint *target, const char *request_headers,
                       const char *request_body, size_t request_body_length,
                       response_head *head, char *body_prefix, size_t body_capacity,
                       size_t *body_length, connection *opened) {
    if (!open_connection(target->host, target->port, target->use_tls, opened)) {
        return opened->failure_code;
    }
    if (!write_all(opened, request_headers, strlen(request_headers)) ||
        (request_body_length != 0 &&
         !write_all(opened, request_body, request_body_length))) {
        complain("request write failed");
        close_connection(opened);
        return 6;
    }
    if (!read_response_head(opened, head, body_prefix, body_capacity, body_length)) {
        int failure = opened->failure_code;
        close_connection(opened);
        return failure;
    }
    return 0;
}

static int send_request(const char *host, int port, const char *request_headers,
                        const char *request_body, int request_body_length, int use_tls,
                        const char *credential_header_names,
                        const char *body_output_path, const char *metadata_output_path) {
    if (request_body_length < 0 || (request_body_length != 0 && request_body == NULL)) {
        complain("invalid request body length");
        return 2;
    }
    if (!valid_wire_request(request_headers)) {
        complain("transport accepts only GET and POST requests");
        return 2;
    }
    if (credential_header_names == NULL) {
        credential_header_names = "";
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
            if (body_output_path == NULL && (result == 4 || result == 14) && current.use_tls) return 5;
            if (body_output_path == NULL && result >= 11) return 7;
            return result;
        }

        int can_follow = strncmp(current_request, "GET ", 4) == 0 &&
                         redirect_status(head.status) && head.has_location;
        if (!can_follow) {
            FILE *output = stdout;
            if (body_output_path != NULL) {
                if (head.invalid_framing) {
                    close_connection(&opened);
                    free(current_request);
                    complain("unsupported or malformed HTTP body framing");
                    return 13;
                }
                if (head.content_length > MAX_CAPTURE_BYTES) {
                    close_connection(&opened);
                    free(current_request);
                    complain("captured response exceeds 8 MiB");
                    return 12;
                }
                output = private_output(body_output_path, "wb");
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
            int copy_failure = opened.failure_code;
            if (body_output_path != NULL && fclose(output) != 0) copied = 0;
            int metadata_written = write_response_metadata(metadata_output_path, &head,
                                                           received_length,
                                                           body_output_path);
            close_connection(&opened);
            free(current_request);
            if (!copied || !metadata_written) return body_output_path != NULL &&
                !copied && copy_failure >= 11 ? copy_failure : 7;
            if (body_output_path != NULL && head.content_length >= 0 &&
                (unsigned long long)head.content_length != (unsigned long long)received_length) {
                complain("response body length does not match Content-Length");
                return 9;
            }
            if (body_output_path != NULL && (head.status < 200 || head.status >= 300)) return 10;
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
        char *next_request = same_origin(&current, &next)
            ? rewrite_get_request(current_request, &next)
            : rewrite_get_request_with_policy(
                current_request, &next, 0, credential_header_names);
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
                        0, "", NULL, NULL);
}

int icu_send_https(const char *host, int port, const char *request_headers,
                   const char *request_body, int request_body_length) {
    return send_request(host, port, request_headers, request_body, request_body_length,
                        1, "", NULL, NULL);
}

int icu_send_http_with_credentials(const char *host, int port,
                                   const char *request_headers,
                                   const char *request_body,
                                   int request_body_length,
                                   const char *credential_header_names) {
    return send_request(host, port, request_headers, request_body, request_body_length,
                        0, credential_header_names, NULL, NULL);
}

int icu_send_https_with_credentials(const char *host, int port,
                                    const char *request_headers,
                                    const char *request_body,
                                    int request_body_length,
                                    const char *credential_header_names) {
    return send_request(host, port, request_headers, request_body, request_body_length,
                        1, credential_header_names, NULL, NULL);
}

int icu_fetch_http(const char *host, int port, const char *request_headers,
                   const char *request_body, int request_body_length,
                   const char *body_output_path, const char *metadata_output_path) {
    return send_request(host, port, request_headers, request_body, request_body_length,
                        0, "", body_output_path, metadata_output_path);
}

int icu_fetch_https(const char *host, int port, const char *request_headers,
                    const char *request_body, int request_body_length,
                    const char *body_output_path, const char *metadata_output_path) {
    return send_request(host, port, request_headers, request_body, request_body_length,
                        1, "", body_output_path, metadata_output_path);
}

int icu_fetch_http_with_credentials(const char *host, int port,
                                    const char *request_headers,
                                    const char *request_body,
                                    int request_body_length,
                                    const char *body_output_path,
                                    const char *metadata_output_path,
                                    const char *credential_header_names) {
    return send_request(host, port, request_headers, request_body, request_body_length,
                        0, credential_header_names, body_output_path, metadata_output_path);
}

int icu_fetch_https_with_credentials(const char *host, int port,
                                     const char *request_headers,
                                     const char *request_body,
                                     int request_body_length,
                                     const char *body_output_path,
                                     const char *metadata_output_path,
                                     const char *credential_header_names) {
    return send_request(host, port, request_headers, request_body, request_body_length,
                        1, credential_header_names, body_output_path, metadata_output_path);
}
