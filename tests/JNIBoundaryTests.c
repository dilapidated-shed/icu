/* Isolated native-boundary oracle. No Java source or alternate HTTP client. */
#include "../android/jni_transport.c"

typedef struct { jsize count; const jchar *units; } text_fixture;
static jsize JNICALL string_length(JNIEnv *env, jstring text) {
    (void)env; return ((const text_fixture *)text)->count;
}
static const jchar *JNICALL string_chars(JNIEnv *env, jstring text, jboolean *copy) {
    (void)env; (void)copy; return ((const text_fixture *)text)->units;
}
static void JNICALL release_chars(JNIEnv *env, jstring text, const jchar *units) {
    (void)env; (void)text; (void)units;
}
static jboolean JNICALL no_exception(JNIEnv *env) { (void)env; return JNI_FALSE; }
static int sent = 0;
int icu_send_http(const char *host, int port, const char *head, const char *body, int length) {
    (void)host; (void)port; (void)head; (void)body; (void)length; ++sent; return 0;
}
int icu_send_https(const char *host, int port, const char *head, const char *body, int length) {
    return icu_send_http(host, port, head, body, length);
}
static int checks = 0;
#define CHECK(value) do { ++checks; if (!(value)) { fprintf(stderr, "FAIL JNI line %d\n", __LINE__); return 1; } } while (0)
int main(void) {
#if defined(__ANDROID__) || defined(ICU_TEST_ANDROID_JNI_HEADER)
    static const struct JNINativeInterface functions = {
#else
    static const struct JNINativeInterface_ functions = {
#endif
        .GetStringLength = string_length, .GetStringChars = string_chars,
        .ReleaseStringChars = release_chars, .ExceptionCheck = no_exception
    };
    JNIEnv env = &functions;
    const jchar unicode[] = {'c', 'a', 'f', 0xe9, 0xd83d, 0xde00};
    text_fixture text = {6, unicode};
    utf8_text encoded = {0};
    CHECK(jstring_to_utf8(&env, (jstring)&text, &encoded));
    CHECK(encoded.length == 9 && memcmp(encoded.bytes, "caf\xc3\xa9\xf0\x9f\x98\x80", 9) == 0);
    release_utf8(&encoded);
    const jchar high[] = {0xd800}, low[] = {0xdc00}, broken[] = {0xd800, 'x'};
    text_fixture invalid[] = {{1, high}, {1, low}, {2, broken}};
    for (size_t i = 0; i < 3; ++i) CHECK(!jstring_to_utf8(&env, (jstring)&invalid[i], &encoded));
    const jchar host_units[] = {'x'}, head_units[] = {'G'}, nul_units[] = {'x', 0, 'y'};
    text_fixture host = {1, host_units}, head = {1, head_units}, nul = {3, nul_units};
    CHECK(send_request_from_java(&env, 0, (jstring)&host, 80, (jstring)&head, (jstring)&text, 9) == 0 && sent == 1);
    CHECK(send_request_from_java(&env, 0, (jstring)&host, 80, (jstring)&head, (jstring)&text, 6) == 2 && sent == 1);
    CHECK(send_request_from_java(&env, 0, (jstring)&nul, 80, (jstring)&head, (jstring)&text, 9) == 2 && sent == 1);
    CHECK(send_request_from_java(&env, 0, (jstring)&host, 80, (jstring)&nul, (jstring)&text, 9) == 2 && sent == 1);
    CHECK(send_request_from_java(&env, 1, (jstring)&host, 443, (jstring)&head, (jstring)&nul, 3) == 0 && sent == 2);
    printf("PASS %d JNI UTF-8 and transport ABI checks\n", checks);
    return 0;
}
