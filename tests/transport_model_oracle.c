#define main icu_transport_unused_main
#include "../native/transport.c"
#undef main

static int fixture_integer(const char *text, int *value) {
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (text[0] == '\0' || end == NULL || *end != '\0' || parsed < 0 || parsed > 65535) {
        return 0;
    }
    *value = (int)parsed;
    return 1;
}

static int fixture_endpoint(int argc, char **argv, int offset, endpoint *value) {
    if (offset + 3 >= argc || !fixture_integer(argv[offset + 2], &value->port)) {
        return 0;
    }
    if (strcmp(argv[offset], "https") == 0) {
        value->use_tls = 1;
    } else if (strcmp(argv[offset], "http") == 0) {
        value->use_tls = 0;
    } else {
        return 0;
    }
    if (strlen(argv[offset + 1]) >= sizeof(value->host) ||
        strlen(argv[offset + 3]) >= sizeof(value->target)) {
        return 0;
    }
    strcpy(value->host, argv[offset + 1]);
    strcpy(value->target, argv[offset + 3]);
    return 1;
}

static void print_bool(int value) {
    fputs(value ? "true" : "false", stdout);
}

static void print_endpoint(const endpoint *value) {
    printf("%s|%s|%d|%s", value->use_tls ? "https" : "http",
           value->host, value->port, value->target);
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "parse-status") == 0) {
        int status = parse_status(argv[2], strlen(argv[2]));
        if (status < 0) {
            fputs("invalid", stdout);
        } else {
            printf("%d", status);
        }
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "header") == 0) {
        char value[MAX_LOCATION_BYTES];
        if (copy_header_value(argv[2], strlen(argv[2]), argv[3], value, sizeof(value))) {
            fputs(value, stdout);
        } else {
            fputs("missing", stdout);
        }
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "redirect-status") == 0) {
        int status = 0;
        if (!fixture_integer(argv[2], &status)) {
            fputs("invalid", stdout);
        } else {
            print_bool(redirect_status(status));
        }
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "visible-url") == 0) {
        print_bool(visible_url_bytes(argv[2]));
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "valid-request") == 0) {
        print_bool(valid_wire_request(argv[2]));
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "request-target") == 0) {
        char target[MAX_TARGET_BYTES];
        if (request_target(argv[2], target, sizeof(target))) {
            fputs(target, stdout);
        } else {
            fputs("invalid", stdout);
        }
        return 0;
    }
    if (argc == 8 && strcmp(argv[1], "resolve") == 0) {
        endpoint current;
        endpoint result;
        if (!fixture_endpoint(argc, argv, 2, &current) ||
            !resolve_location(&current, argv[6], argv[7], &result)) {
            fputs("invalid", stdout);
        } else {
            print_endpoint(&result);
        }
        return 0;
    }
    if (argc == 7 && strcmp(argv[1], "rewrite") == 0) {
        endpoint next;
        if (!fixture_endpoint(argc, argv, 2, &next)) {
            fputs("invalid", stdout);
            return 0;
        }
        char *request = rewrite_get_request(argv[6], &next);
        if (request == NULL) {
            fputs("invalid", stdout);
        } else {
            fputs(request, stdout);
            free(request);
        }
        return 0;
    }
    fputs("invalid invocation", stdout);
    return 0;
}
