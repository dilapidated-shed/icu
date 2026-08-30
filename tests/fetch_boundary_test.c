#include <stdio.h>
#include <stdlib.h>

int icu_fetch_http(const char *, int, const char *, const char *, int,
                   const char *, const char *);

int main(int argc, char **argv) {
    if (argc != 6) return 2;
    int port = atoi(argv[1]);
    char request[1024];
    int count = snprintf(request, sizeof(request),
        "GET %s HTTP/1.0\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\nAccept-Encoding: identity\r\n\r\n",
        argv[2], port);
    if (count < 0 || (size_t)count >= sizeof(request)) return 2;
    int result = icu_fetch_http("127.0.0.1", port, request, "", 0, argv[3], argv[4]);
    return result == atoi(argv[5]) ? 0 : result + 20;
}
