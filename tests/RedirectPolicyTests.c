#include "../native/transport.c"

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
        "X-Trace: keep-me\r\n"
        "\r\n";

    char *same_request = rewrite_get_request_with_policy(request, &same, 1);
    if (same_request == NULL ||
        !contains(same_request, "Authorization: Bearer secret\r\n") ||
        !contains(same_request, "Cookie: session=secret\r\n") ||
        !contains(same_request, "X-Trace: keep-me\r\n")) {
        free(same_request);
        fputs("same-origin credentials were not preserved\n", stderr);
        return 1;
    }
    free(same_request);

    char *cross_request = rewrite_get_request_with_policy(request, &other_host, 0);
    if (cross_request == NULL ||
        contains(cross_request, "Authorization:") ||
        contains(cross_request, "Cookie:") ||
        !contains(cross_request, "X-Trace: keep-me\r\n") ||
        !contains(cross_request, "Host: other.example\r\n")) {
        free(cross_request);
        fputs("cross-origin credential stripping failed\n", stderr);
        return 1;
    }
    free(cross_request);

    return 0;
}
