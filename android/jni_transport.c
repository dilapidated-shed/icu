#include <jni.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int icu_send_http(const char *host, int port, const char *request_headers,
                  const char *request_body, int request_body_length);
int icu_send_https(const char *host, int port, const char *request_headers,
                   const char *request_body, int request_body_length);

typedef struct {
    char *bytes;
    size_t length;
} utf8_text;

static jobjectArray captured_arguments = NULL;

static void bridge_abort(const char *message) {
    fprintf(stderr, "icu: Android JNI bridge: %s\n", message);
    fflush(stderr);
    exit(1);
}

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

    if (units < 0 || (size_t)units > (SIZE_MAX - 1u) / 4u) {
        (*environment)->ReleaseStringChars(environment, value, characters);
        return 0;
    }
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
    jint result = 2;

    if (!jstring_to_utf8(environment, host_text, &host) ||
        !jstring_to_utf8(environment, request_headers_text, &request_headers) ||
        !jstring_to_utf8(environment, request_body_text, &request_body)) {
        goto done;
    }
    if (memchr(host.bytes, 0, host.length) != NULL ||
        memchr(request_headers.bytes, 0, request_headers.length) != NULL) goto done;
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
    if ((*environment)->ExceptionCheck(environment)) {
        (*environment)->ExceptionClear(environment);
        result = 2;
    }
    if (result == 2) fputs("icu: invalid UTF-8 transport arguments or byte count\n", stderr);
    release_utf8(&request_body);
    release_utf8(&request_headers);
    release_utf8(&host);
    return result;
}

/* app_process omits argv[0]. Keep the actual argument strings and let the
 * checked ICU command parse them, exactly as on the host. */
JNIEXPORT void JNICALL
Java_Idric_Generated_captureArguments(JNIEnv *environment, jclass klass, jobjectArray arguments) {
    (void)klass;
    if (arguments == NULL) bridge_abort("missing command arguments");
    jobjectArray saved = (jobjectArray)(*environment)->NewGlobalRef(environment, arguments);
    if (saved == NULL) bridge_abort("could not retain command arguments");
    if (captured_arguments != NULL) (*environment)->DeleteGlobalRef(environment, captured_arguments);
    captured_arguments = saved;
}

JNIEXPORT jint JNICALL
Java_Idric_Generated_idricArgumentCount(JNIEnv *environment, jclass klass) {
    (void)klass;
    if (captured_arguments == NULL) bridge_abort("arguments read before capture");
    jsize count = (*environment)->GetArrayLength(environment, captured_arguments);
    if ((*environment)->ExceptionCheck(environment) || count == INT_MAX)
        bridge_abort("could not count arguments");
    return count + 1;
}

JNIEXPORT jstring JNICALL
Java_Idric_Generated_idricArgument(JNIEnv *environment, jclass klass, jint index) {
    (void)klass;
    if (captured_arguments == NULL) bridge_abort("arguments read before capture");
    if (index == 0) return (*environment)->NewStringUTF(environment, "icu");
    jsize count = (*environment)->GetArrayLength(environment, captured_arguments);
    if (index < 1 || index > count) bridge_abort("argument index out of range");
    jstring value = (jstring)(*environment)->GetObjectArrayElement(environment, captured_arguments, index - 1);
    if (value == NULL || (*environment)->ExceptionCheck(environment)) bridge_abort("could not read argument");
    return value;
}

JNIEXPORT void JNICALL
Java_Idric_Generated_idricPutString(JNIEnv *environment, jclass klass, jstring value) {
    (void)klass;
    utf8_text text = {0};
    if (!jstring_to_utf8(environment, value, &text)) bridge_abort("could not encode stdout text");
    int ok = fwrite(text.bytes, 1, text.length, stdout) == text.length && fflush(stdout) == 0;
    release_utf8(&text);
    if (!ok) bridge_abort("stdout write failed");
}

JNIEXPORT void JNICALL
Java_Idric_Generated_idricExit(JNIEnv *environment, jclass klass, jint status) {
    (void)environment;
    (void)klass;
    fflush(NULL);
    exit(status);
}

JNIEXPORT jint JNICALL
Java_Idric_Generated_icuSendHttp(JNIEnv *environment, jclass klass, jstring host, jint port,
                                jstring head, jstring body, jint length) {
    (void)klass;
    return send_request_from_java(environment, 0, host, port, head, body, length);
}

JNIEXPORT jint JNICALL
Java_Idric_Generated_icuSendHttps(JNIEnv *environment, jclass klass, jstring host, jint port,
                                 jstring head, jstring body, jint length) {
    (void)klass;
    return send_request_from_java(environment, 1, host, port, head, body, length);
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
