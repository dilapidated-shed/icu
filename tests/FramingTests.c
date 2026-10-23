/* Native provider tests retain its ordinary C ABI/source profile. */
#include "../native/transport.c"

static int checks = 0;
#define CHECK(value) do { ++checks; if (!(value)) { fprintf(stderr, "FAIL line %d\n", __LINE__); return 1; } } while (0)

static int response_case(const char *wire, size_t length, int expected) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return 0;
    if (write(pair[1], wire, length) != (ssize_t)length) return 0;
    close(pair[1]);
    connection opened = {0};
    opened.socket_fd = pair[0];
    response_head head;
    char prefix[16384];
    size_t used;
    int parsed = read_response_head(&opened, &head, prefix, sizeof(prefix), &used);
    close(pair[0]);
    return (parsed && !head.invalid_framing) == expected;
}

static int length_case(const char *body, size_t length, long long declared, int expected) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return 0;
    close(pair[1]);
    connection opened = {0};
    opened.socket_fd = pair[0];
    FILE *output = tmpfile();
    if (output == NULL) return 0;
    size_t used;
    int passed = copy_remaining_body(&opened, output, body, length, &used, declared);
    fclose(output);
    close(pair[0]);
    return passed == expected && (passed || opened.failure_code == 9);
}

int main(void) {
    const char *valid[] = {
        "HTTP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 302 Found\r\nlOcAtIoN:\t /next?q=1 \t\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
        "HTTP/1.1 206 Partial\r\nContent-Range: bytes 10-14/100\r\nContent-Length: 5\r\n\r\nhello"
    };
    const char *invalid[] = {
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\na",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 302 Found\r\nLocation: /next\r\nLocation: /other\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length : 1\r\n\r\na",
        "HTTP/1.1 200 OK\r\n Content-Length: 1\r\n\r\na",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\n\r\na",
        "HTTP/1.1 200 OK\r\nContent-Length: 999999999999999999999\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: a\r\nContent-Type: b\r\n\r\n",
        "HTTP/1.1 200 OK\r\nX: value\vinjected\r\n\r\n",
        "HTTP/2.0 200 OK\r\n\r\n"
    };
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
        CHECK(response_case(valid[i], strlen(valid[i]), 1));
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        CHECK(response_case(invalid[i], strlen(invalid[i]), 0));
    static const char nul[] = "HTTP/1.1 200 OK\r\nContent-Length: 1\0x\r\n\r\na";
    CHECK(response_case(nul, sizeof(nul) - 1, 0));
    CHECK(length_case("hello", 5, 5, 1));
    CHECK(length_case("hell", 4, 5, 0));
    CHECK(length_case("hello!", 6, 5, 0));
    CHECK(length_case("", 0, 0, 1));
    char target[64];
    CHECK(request_target("POST /post?x=1 HTTP/1.1\r\n\r\n", target, sizeof(target)) && strcmp(target, "/post?x=1") == 0);
    CHECK(!request_target("GET / HTTP/2.0\r\n\r\n", target, sizeof(target)));
    CHECK(!request_target("GET /bad\tpath HTTP/1.1\r\n\r\n", target, sizeof(target)));
    char field[128];
    const char *whitespace = "HTTP/1.1 200 OK\r\nContent-Type:\t text/plain;\tcharset=utf-8 \t\r\n\r\n";
    CHECK(copy_header_value(whitespace, strlen(whitespace), "Content-Type", field, sizeof(field)) &&
          strcmp(field, "text/plain; charset=utf-8") == 0);
    endpoint current = {0}, resolved;
    current.use_tls = 1; current.port = 443;
    strcpy(current.host, "example.com"); strcpy(current.target, "/a/start");
    const char *request = "GET /a/start HTTP/1.1\r\nHost: example.com\r\n\r\n";
    const char *locations[] = {"https://other.example:444/final#fragment", "//other.example/final", "/final", "next", "?q=1"};
    const char *targets[] = {"/final", "/final", "/final", "/a/next", "/a/start?q=1"};
    for (size_t i = 0; i < 5; ++i) {
        CHECK(resolve_location(&current, request, locations[i], &resolved));
        CHECK(strcmp(resolved.target, targets[i]) == 0 && resolved.use_tls == 1);
    }
    const char *unsafe[] = {"https://user@host/", "ftp://host/", "http://[::1]/", "http://host:0/", "/next\r\nInjected"};
    for (size_t i = 0; i < 5; ++i) CHECK(!resolve_location(&current, request, unsafe[i], &resolved));
    char directory[4096];
    const char *temporary = getenv("TMPDIR");
    CHECK(snprintf(directory, sizeof(directory), "%s/icu-path-tests-XXXXXX",
                   temporary == NULL ? "/tmp" : temporary) < (int)sizeof(directory));
    CHECK(mkdtemp(directory) != NULL);
    char body[4128], metadata[4128], link[4128], alias[4128];
    snprintf(body, sizeof(body), "%s/body", directory);
    snprintf(metadata, sizeof(metadata), "%s/metadata", directory);
    snprintf(link, sizeof(link), "%s/link", directory);
    snprintf(alias, sizeof(alias), "%s/link/body", directory);
    CHECK(capture_paths_valid(body, metadata));
    CHECK(!capture_paths_valid(body, body));
    CHECK(!capture_paths_valid(body, NULL));
    CHECK(!capture_paths_valid("relative", metadata));
    CHECK(symlink(directory, link) == 0);
    CHECK(!capture_paths_valid(alias, metadata));
    CHECK(chmod(directory, 0755) == 0);
    CHECK(!capture_paths_valid(body, metadata));
    CHECK(chmod(directory, 0700) == 0);
    FILE *file = private_output(body, "wb");
    CHECK(file != NULL);
    CHECK(fclose(file) == 0);
    struct stat state;
    CHECK(stat(body, &state) == 0 && (state.st_mode & 0777) == 0600);
    CHECK(!capture_paths_valid(body, metadata));
    CHECK(private_output(body, "wb") == NULL);
    CHECK(unlink(body) == 0 && unlink(link) == 0 && rmdir(directory) == 0);
    printf("PASS %d native framing, length, request-target and private-path checks\n", checks);
    return 0;
}
