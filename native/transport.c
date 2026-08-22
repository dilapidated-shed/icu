#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_HEADER_BYTES (64u * 1024u)

typedef struct {
    int socket_fd;
    SSL_CTX *tls_context;
    SSL *tls;
} connection;

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
        complain( "TLS handshake failed");
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

static int write_stdout(const char *bytes, size_t length) {
    return fwrite(bytes, 1, length, stdout) == length;
}

static int copy_response_body(connection *connection) {
    char headers[MAX_HEADER_BYTES];
    size_t header_bytes = 0;
    char buffer[16384];

    while (header_bytes < sizeof(headers)) {
        ssize_t amount = read_some(connection, buffer, sizeof(buffer));
        if (amount <= 0) {
            complain(amount == 0 ? "response ended before its headers" : "response read failed");
            return 0;
        }

        size_t bytes_read = (size_t)amount;
        if (header_bytes + bytes_read > sizeof(headers)) {
            break;
        }
        memcpy(headers + header_bytes, buffer, bytes_read);
        header_bytes += bytes_read;

        char *body = find_header_end(headers, header_bytes);
        if (body != NULL) {
            size_t body_bytes = header_bytes - (size_t)(body - headers);
            if (body_bytes != 0 && !write_stdout(body, body_bytes)) {
                complain("could not write response body");
                return 0;
            }
            for (;;) {
                amount = read_some(connection, buffer, sizeof(buffer));
                if (amount == 0) {
                    return fflush(stdout) == 0;
                }
                if (amount < 0 || !write_stdout(buffer, (size_t)amount)) {
                    complain(amount < 0 ? "response read failed" : "could not write response body");
                    return 0;
                }
            }
        }
    }

    complain("response headers are too large");
    return 0;
}

static int valid_wire_request(const char *request) {
    return strncmp(request, "GET ", 4) == 0 || strncmp(request, "POST ", 5) == 0;
}

static int send_request(const char *host, int port, const char *request, int use_tls) {
    if (!valid_wire_request(request)) {
        complain("transport accepts only GET and POST requests");
        return 2;
    }

    connection connection;
    if (!open_connection(host, port, use_tls, &connection)) {
        return use_tls ? 5 : 4;
    }

    int result = 0;
    if (!write_all(&connection, request, strlen(request))) {
        complain("request write failed");
        result = 6;
    } else if (!copy_response_body(&connection)) {
        result = 7;
    }

    close_connection(&connection);
    return result;
}

int icu_send_http(const char *host, int port, const char *request) {
    return send_request(host, port, request, 0);
}

int icu_send_https(const char *host, int port, const char *request) {
    return send_request(host, port, request, 1);
}
