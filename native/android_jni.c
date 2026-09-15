#include <jni.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Existing transport boundary implemented by native/transport.c. */
int icu_send_http(const char *host, int port, const char *request_headers,
                  const char *request_body, int request_body_length);
int icu_send_https(const char *host, int port, const char *request_headers,
                   const char *request_body, int request_body_length);

static jobjectArray captured_arguments = NULL;

static void bridge_abort(const char *message) {
    fprintf(stderr, "icu: Android JNI bridge: %s\n", message);
    fflush(stderr);
    exit(1);
}

static int clear_exception(JNIEnv *env, const char *stage) {
    if (!(*env)->ExceptionCheck(env)) {
        return 1;
    }
    (*env)->ExceptionClear(env);
    fprintf(stderr, "icu: Android JNI bridge failed during %s\n", stage);
    fflush(stderr);
    return 0;
}

/*
 * JNI's GetStringUTFChars uses modified UTF-8, while ICU's checked Idriç layer
 * computes ordinary UTF-8 byte counts. Ask java.lang.String for ordinary UTF-8
 * bytes so the existing transport ABI receives exactly those bytes.
 */
static int copy_java_utf8(JNIEnv *env, jstring value,
                          unsigned char **result, jsize *result_length) {
    if (value == NULL || result == NULL || result_length == NULL) {
        return 0;
    }

    jclass string_class = (*env)->FindClass(env, "java/lang/String");
    if (string_class == NULL || !clear_exception(env, "finding String")) {
        return 0;
    }
    jmethodID get_bytes = (*env)->GetMethodID(
        env, string_class, "getBytes", "(Ljava/lang/String;)[B");
    if (get_bytes == NULL || !clear_exception(env, "finding String.getBytes")) {
        (*env)->DeleteLocalRef(env, string_class);
        return 0;
    }
    jstring utf8 = (*env)->NewStringUTF(env, "UTF-8");
    if (utf8 == NULL || !clear_exception(env, "selecting UTF-8")) {
        (*env)->DeleteLocalRef(env, string_class);
        return 0;
    }
    jbyteArray bytes = (jbyteArray)(*env)->CallObjectMethod(
        env, value, get_bytes, utf8);
    if (bytes == NULL || !clear_exception(env, "encoding UTF-8")) {
        (*env)->DeleteLocalRef(env, utf8);
        (*env)->DeleteLocalRef(env, string_class);
        return 0;
    }

    jsize length = (*env)->GetArrayLength(env, bytes);
    if (!clear_exception(env, "measuring UTF-8")) {
        (*env)->DeleteLocalRef(env, bytes);
        (*env)->DeleteLocalRef(env, utf8);
        (*env)->DeleteLocalRef(env, string_class);
        return 0;
    }
    unsigned char *copy = malloc((size_t)length + 1u);
    if (copy == NULL) {
        (*env)->DeleteLocalRef(env, bytes);
        (*env)->DeleteLocalRef(env, utf8);
        (*env)->DeleteLocalRef(env, string_class);
        return 0;
    }
    if (length != 0) {
        (*env)->GetByteArrayRegion(env, bytes, 0, length, (jbyte *)copy);
        if (!clear_exception(env, "copying UTF-8")) {
            free(copy);
            (*env)->DeleteLocalRef(env, bytes);
            (*env)->DeleteLocalRef(env, utf8);
            (*env)->DeleteLocalRef(env, string_class);
            return 0;
        }
    }
    copy[length] = 0;

    (*env)->DeleteLocalRef(env, bytes);
    (*env)->DeleteLocalRef(env, utf8);
    (*env)->DeleteLocalRef(env, string_class);
    *result = copy;
    *result_length = length;
    return 1;
}

/*
 * app_process gives main only the arguments after the class name. System.getArgs
 * expects argv[0], so the bridge supplies one synthetic argv[0] followed by the
 * exact Java String values. Main.main still discards argv[0] and performs the
 * real ICU command parsing in checked Idriç.
 */
JNIEXPORT void JNICALL
Java_Idric_Generated_captureArguments(JNIEnv *env, jclass cls,
                                      jobjectArray arguments) {
    (void)cls;
    if (arguments == NULL) {
        bridge_abort("app_process supplied no argument array");
    }
    jobjectArray saved = (jobjectArray)(*env)->NewGlobalRef(env, arguments);
    if (saved == NULL || !clear_exception(env, "capturing arguments")) {
        bridge_abort("could not retain app_process arguments");
    }
    if (captured_arguments != NULL) {
        (*env)->DeleteGlobalRef(env, captured_arguments);
    }
    captured_arguments = saved;
}

JNIEXPORT jint JNICALL
Java_Idric_Generated_idricArgumentCount(JNIEnv *env, jclass cls) {
    (void)cls;
    if (captured_arguments == NULL) {
        bridge_abort("arguments were read before captureArguments");
    }
    jsize count = (*env)->GetArrayLength(env, captured_arguments);
    if (!clear_exception(env, "counting arguments") || count == INT_MAX) {
        bridge_abort("could not count arguments");
    }
    return count + 1;
}

JNIEXPORT jstring JNICALL
Java_Idric_Generated_idricArgument(JNIEnv *env, jclass cls, jint index) {
    (void)cls;
    if (captured_arguments == NULL) {
        bridge_abort("arguments were read before captureArguments");
    }
    if (index == 0) {
        jstring executable = (*env)->NewStringUTF(env, "icu");
        if (executable == NULL || !clear_exception(env, "creating argv[0]")) {
            bridge_abort("could not create argv[0]");
        }
        return executable;
    }

    jsize count = (*env)->GetArrayLength(env, captured_arguments);
    if (!clear_exception(env, "checking argument index") ||
        index < 1 || index > count) {
        bridge_abort("argument index is out of range");
    }
    jstring value = (jstring)(*env)->GetObjectArrayElement(
        env, captured_arguments, index - 1);
    if (value == NULL || !clear_exception(env, "reading argument")) {
        bridge_abort("could not read argument");
    }
    return value;
}

JNIEXPORT void JNICALL
Java_Idric_Generated_idricPutString(JNIEnv *env, jclass cls, jstring text) {
    (void)cls;
    unsigned char *bytes = NULL;
    jsize length = 0;
    if (!copy_java_utf8(env, text, &bytes, &length)) {
        bridge_abort("could not encode stdout text");
    }
    if (length != 0 &&
        fwrite(bytes, 1, (size_t)length, stdout) != (size_t)length) {
        free(bytes);
        bridge_abort("stdout write failed");
    }
    free(bytes);
    fflush(stdout);
}

JNIEXPORT void JNICALL
Java_Idric_Generated_idricExit(JNIEnv *env, jclass cls, jint status) {
    (void)env;
    (void)cls;
    fflush(NULL);
    exit((int)status);
}

static jint send(JNIEnv *env, jstring host_value, jint port,
                 jstring head_value, jstring body_value, jint body_length,
                 int secure) {
    unsigned char *host = NULL;
    unsigned char *head = NULL;
    unsigned char *body = NULL;
    jsize host_length = 0;
    jsize head_length = 0;
    jsize actual_body_length = 0;
    jint result = 255;

    if (body_length < 0 ||
        !copy_java_utf8(env, host_value, &host, &host_length) ||
        !copy_java_utf8(env, head_value, &head, &head_length) ||
        !copy_java_utf8(env, body_value, &body, &actual_body_length)) {
        fprintf(stderr, "icu: Android JNI bridge could not encode transport text\n");
        goto done;
    }

    /* Host and request head are checked visible ASCII before this boundary. */
    if (memchr(host, 0, (size_t)host_length) != NULL ||
        memchr(head, 0, (size_t)head_length) != NULL) {
        fprintf(stderr, "icu: Android JNI bridge rejected embedded NUL metadata\n");
        goto done;
    }
    if (actual_body_length != body_length) {
        fprintf(stderr,
                "icu: Android JNI bridge UTF-8 length disagrees with checked byte count\n");
        goto done;
    }

    if (secure) {
        result = (jint)icu_send_https(
            (const char *)host, (int)port, (const char *)head,
            (const char *)body, (int)body_length);
    } else {
        result = (jint)icu_send_http(
            (const char *)host, (int)port, (const char *)head,
            (const char *)body, (int)body_length);
    }

done:
    free(body);
    free(head);
    free(host);
    fflush(stdout);
    fflush(stderr);
    return result;
}

JNIEXPORT jint JNICALL
Java_Idric_Generated_icuSendHttp(JNIEnv *env, jclass cls, jstring host,
                                 jint port, jstring head, jstring body,
                                 jint body_length) {
    (void)cls;
    return send(env, host, port, head, body, body_length, 0);
}

JNIEXPORT jint JNICALL
Java_Idric_Generated_icuSendHttps(JNIEnv *env, jclass cls, jstring host,
                                  jint port, jstring head, jstring body,
                                  jint body_length) {
    (void)cls;
    return send(env, host, port, head, body, body_length, 1);
}
