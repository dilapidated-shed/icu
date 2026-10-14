#include "../native/transport.c"
#include <sys/wait.h>

static int test_proxy_tunnel(const char *response, int expected) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        return 0;
    }
    pid_t child = fork();
    if (child < 0) {
        return 0;
    }
    if (child == 0) {
        close(pair[0]);
        char received[2048];
        size_t used = 0;
        while (used < sizeof(received) - 1) {
            if (recv(pair[1], received + used, 1, 0) != 1) {
                _exit(1);
            }
            ++used;
            if (used >= 4 && memcmp(received + used - 4, "\r\n\r\n", 4) == 0) {
                break;
            }
        }
        received[used] = '\0';
        if (strcmp(received, "CONNECT example.com:443 HTTP/1.0\r\nHost: example.com:443\r\n\r\n") != 0) {
            _exit(1);
        }
        if (send(pair[1], response, strlen(response), 0) != (ssize_t)strlen(response)) {
            _exit(1);
        }
        close(pair[1]);
        _exit(0);
    }
    close(pair[1]);
    int result = establish_proxy_tunnel(pair[0], "example.com", 443);
    close(pair[0]);
    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return 0;
    }
    return result == expected;
}

static int test_proxy_configuration(void) {
    configured_proxy proxy;
    unsetenv("HTTPS_PROXY");
    unsetenv("https_proxy");
    if (!proxy_configuration("example.com", 1, &proxy) || proxy.enabled) {
        return 0;
    }
    setenv("HTTPS_PROXY", "http://127.0.0.1:3128", 1);
    setenv("https_proxy", "http://other.invalid:1234", 1);
    if (!proxy_configuration("example.com", 1, &proxy) || !proxy.enabled ||
        proxy.port != 3128 || strcmp(proxy.host, "127.0.0.1") != 0) {
        return 0;
    }
    if (!proxy_configuration("example.com", 0, &proxy) || proxy.enabled) {
        return 0;
    }
    const char *local[] = {"127.0.0.1", "::1", "LOCALHOST"};
    for (size_t index = 0; index < sizeof(local) / sizeof(local[0]); ++index) {
        if (!proxy_configuration(local[index], 1, &proxy) || proxy.enabled) {
            return 0;
        }
    }
    setenv("HTTPS_PROXY", "http://proxy.invalid/", 1);
    if (!proxy_configuration("example.com", 1, &proxy) || proxy.port != 80) {
        return 0;
    }
    setenv("HTTPS_PROXY", "http://[::1]:8080", 1);
    if (!proxy_configuration("example.com", 1, &proxy) || proxy.port != 8080 || strcmp(proxy.host, "::1") != 0) {
        return 0;
    }
    unsetenv("HTTPS_PROXY");
    if (!proxy_configuration("example.com", 1, &proxy) || proxy.port != 1234) {
        return 0;
    }
    const char *invalid[] = {"http://", "http://proxy:", "http://proxy:0",
        "http://proxy:65536", "http://proxy:9999999999999", "http://proxy:-1",
        "http://user:secret@proxy", "http://proxy/path", "http://proxy?query",
        "http://proxy#fragment", "http://proxy\r\nInjected", "https://proxy", "socks5://proxy",
        "http://[not-ipv6]:80", "http://proxy\\invalid:80", "http://proxy:1:2"};
    for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        setenv("HTTPS_PROXY", invalid[index], 1);
        if (proxy_configuration("example.com", 1, &proxy)) {
            return 0;
        }
    }
    unsetenv("HTTPS_PROXY");
    unsetenv("https_proxy");
    return test_proxy_tunnel("HTTP/1.0 200 Connection established\r\n\r\n", 1) &&
        test_proxy_tunnel("HTTP/1.1 204 Tunnel\r\n\r\n", 1) &&
        test_proxy_tunnel("HTTP/1.0 407 Proxy authentication required\r\n\r\n", 0) &&
        test_proxy_tunnel("HTTP/1.0 2000 Invalid status\r\n\r\n", 0) &&
        test_proxy_tunnel("garbage\r\n\r\n", 0);
}

static int contains(const char *text, const char *piece) {
    return strstr(text, piece) != NULL;
}

static endpoint test_endpoint(int use_tls, int port, const char *host, const char *target) {
    endpoint value;
    memset(&value, 0, sizeof(value));
    value.use_tls = use_tls;
    value.port = port;
    strcpy(value.host, host);
    strcpy(value.target, target);
    return value;
}

int main(void) {
    if (!test_proxy_configuration()) {
        fputs("configured proxy or CONNECT boundary failed\n", stderr);
        return 1;
    }
    endpoint current = test_endpoint(1, 443, "example.com", "/start");
    endpoint same = test_endpoint(1, 443, "EXAMPLE.COM", "/next");
    endpoint other_host = test_endpoint(1, 443, "other.example", "/next");
    endpoint other_port = test_endpoint(1, 444, "example.com", "/next");
    endpoint other_scheme = test_endpoint(0, 443, "example.com", "/next");

    if (!same_origin(&current, &same) ||
        same_origin(&current, &other_host) ||
        same_origin(&current, &other_port) ||
        same_origin(&current, &other_scheme)) {
        fputs("origin comparison failed\n", stderr);
        return 1;
    }

    const char *request =
        "GET /start HTTP/1.0\r\n"
        "Host: example.com\r\n"
        "Authorization: Bearer secret\r\n"
        "Cookie: session=secret\r\n"
        "X-Api-Key: synthetic-secret\r\n"
        "X-Trace: keep-me\r\n"
        "\r\n";
    const char *declared_credentials = "x-api-key\n";

    char *same_request = rewrite_get_request_with_policy(
        request, &same, 1, declared_credentials);
    if (same_request == NULL ||
        !contains(same_request, "Authorization: Bearer secret\r\n") ||
        !contains(same_request, "Cookie: session=secret\r\n") ||
        !contains(same_request, "X-Api-Key: synthetic-secret\r\n") ||
        !contains(same_request, "X-Trace: keep-me\r\n")) {
        free(same_request);
        fputs("same-origin credentials were not preserved\n", stderr);
        return 1;
    }
    free(same_request);

    char *cross_request = rewrite_get_request_with_policy(
        request, &other_host, 0, declared_credentials);
    if (cross_request == NULL ||
        contains(cross_request, "Authorization:") ||
        contains(cross_request, "Cookie:") ||
        contains(cross_request, "X-Api-Key:") ||
        !contains(cross_request, "X-Trace: keep-me\r\n") ||
        !contains(cross_request, "Host: other.example\r\n")) {
        free(cross_request);
        fputs("cross-origin credential stripping failed\n", stderr);
        return 1;
    }
    free(cross_request);

    puts("PASS configured HTTPS proxy, CONNECT framing and redirect credentials");
    return 0;
}
