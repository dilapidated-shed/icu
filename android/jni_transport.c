#include <jni.h>
#include <stdint.h>
#include <stdlib.h>

int icu_send_http(const char *host, int port, const char *request_headers,
                  const char *request_body, int request_body_length);
int icu_send_https(const char *host, int port, const char *request_headers,
                   const char *request_body, int request_body_length);

typedef struct {
    char *bytes;
    size_t length;
} utf8_text;

static void release_utf8(utf8_text *text) {
    free(text->bytes);
    text->bytes = NULL;
    text->length = 0;
}

static int append_code_point(char *output, size_t capacity, size_t *length,
                             uint32_t code_point) {
    if (code_point <= 0x7fu) {
        if (*length + 1 >= capacity) return 0;
        output[(*length)++] = (char)code_point;
    } else if (code_point <= 0x7ffu) {
        if (*length + 2 >= capacity) return 0;
        output[(*length)++] = (char)(0xc0u | (code_point >> 6));
        output[(*length)++] = (char)(0x80u | (code_point & 0x3fu));
    } else if (code_point <= 0xffffu) {
        if (*length + 3 >= capacity) return 0;
        output[(*length)++] = (char)(0xe0u | (code_point >> 12));
        output[(*length)++] = (char)(0x80u | ((code_point >> 6) & 0x3fu));
        output[(*length)++] = (char)(0x80u | (code_point & 0x3fu));
    } else if (code_point <= 0x10ffffu) {
        if (*length + 4 >= capacity) return 0;
        output[(*length)++] = (char)(0xf0u | (code_point >> 18));
        output[(*length)++] = (char)(0x80u | ((code_point >> 12) & 0x3fu));
        output[(*length)++] = (char)(0x80u | ((code_point >> 6) & 0x3fu));
        output[(*length)++] = (char)(0x80u | (code_point & 0x3fu));
    } else {
        return 0;
    }
    return 1;
}

static int jstring_to_utf8(JNIEnv *environment, jstring value, utf8_text *result) {
    result->bytes = NULL;
    result->length = 0;
    if (value == NULL) return 0;

    jsize units = (*environment)->GetStringLength(environment, value);
    const jchar *characters =
        (*environment)->GetStringChars(environment, value, NULL);
    if (characters == NULL) return 0;

    size_t capacity = (size_t)units * 4u + 1u;
    char *output = malloc(capacity);
    if (output == NULL) {
        (*environment)->ReleaseStringChars(environment, value, characters);
        return 0;
    }

    size_t length = 0;
    int valid = 1;
    for (jsize index = 0; index < units && valid; ++index) {
        uint32_t code_point = characters[index];
        if (code_point >= 0xd800u && code_point <= 0xdbffu) {
            if (index + 1 >= units) {
                valid = 0;
                break;
            }
            uint32_t low = characters[++index];
            if (low < 0xdc00u || low > 0xdfffu) {
                valid = 0;
                break;
            }
            code_point = 0x10000u + ((code_point - 0xd800u) << 10) +
                         (low - 0xdc00u);
        } else if (code_point >= 0xdc00u && code_point <= 0xdfffu) {
            valid = 0;
            break;
        }
        valid = append_code_point(output, capacity, &length, code_point);
    }

    (*environment)->ReleaseStringChars(environment, value, characters);
    if (!valid) {
        free(output);
        return 0;
    }
    output[length] = '\0';
    result->bytes = output;
    result->length = length;
    return 1;
}

static jint send_request_from_java(JNIEnv *environment, int use_tls,
                                   jstring host_text, jint port,
                                   jstring request_headers_text,
                                   jstring request_body_text,
                                   jint declared_body_length) {
    utf8_text host = {0};
    utf8_text request_headers = {0};
    utf8_text request_body = {0};
    jint result = -1;

    if (!jstring_to_utf8(environment, host_text, &host) ||
        !jstring_to_utf8(environment, request_headers_text, &request_headers) ||
        !jstring_to_utf8(environment, request_body_text, &request_body)) {
        goto done;
    }
    if (declared_body_length < 0 ||
        (size_t)declared_body_length != request_body.length) {
        goto done;
    }

    if (use_tls) {
        result = (jint)icu_send_https(host.bytes, (int)port,
                                      request_headers.bytes, request_body.bytes,
                                      (int)request_body.length);
    } else {
        result = (jint)icu_send_http(host.bytes, (int)port,
                                     request_headers.bytes, request_body.bytes,
                                     (int)request_body.length);
    }

done:
    release_utf8(&request_body);
    release_utf8(&request_headers);
    release_utf8(&host);
    return result;
}

JNIEXPORT jint JNICALL
Java_org_isomorphisms_icu_ICU_nativeSendHttp(
    JNIEnv *environment, jclass klass, jstring host, jint port,
    jstring request_headers, jstring request_body, jint request_body_length) {
    (void)klass;
    return send_request_from_java(environment, 0, host, port, request_headers,
                                  request_body, request_body_length);
}

JNIEXPORT jint JNICALL
Java_org_isomorphisms_icu_ICU_nativeSendHttps(
    JNIEnv *environment, jclass klass, jstring host, jint port,
    jstring request_headers, jstring request_body, jint request_body_length) {
    (void)klass;
    return send_request_from_java(environment, 1, host, port, request_headers,
                                  request_body, request_body_length);
}
