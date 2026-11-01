# IB worker native build continuation, 2026-11-01

The direct Android Make recipe configured OpenSSL with the selected NDK on
PATH, but did not preserve that PATH for its recursive build. Calling the
recipe from an ordinary host shell therefore failed to find the configured
compiler basename. In the failed x86_64 directory an assembly-generation probe
also ran without that compiler and generated incompatible assembly. That failed
directory was retained; qualification used a fresh configuration/build directory.

The recipe now gives the recursive build the same explicit NDK PATH. Linking
also explicitly requests 16 KiB ELF segment alignment. Sources and TLS/framing
policy are unchanged. The declared Android recipe uses NDK Clang; the host ICK
profile and its previous acceptance remain separate.

Native compile, link and required JNI-symbol checks pass for these generic
profiles, using source `efec28ba28c129a471b8862cb81e56eef9160a51` with this
Make change, NDK `27.2.12479018`, API floor 29, and OpenSSL
`f4dc4d58b48d346a8270183f89acf826d459b0ca`:

| ABI | libicu_transport.so SHA-256 |
| --- | --- |
| x86_64 | `52c660e2c02a0f07a5944f5c94d6445a34f30f2398043283f587fec0afbafdc8` |
| armeabi-v7a | `9f0b6ce3c5102f0779d315ee0541acc68f9b2976cb48cf0329cd5704c9fd68ce` |
| arm64-v8a | `7cf28d4828bcef19d40cc0f6adfdb5307116bf0ca19eb56b9ea1f6ad2ed3268a` |

These local hashes identify inputs for subsequent worker qualification; build
paths/stamps can change ELF bytes on a fresh build. No emulator execution of
these particular ELF files, installed Shizuku worker transaction or physical
TAB_P10 acceptance is claimed here. Each remains NOT_RUN. The independent
consumer must qualify actual generated DEX, JNI, network framing, source
publication/readback and suitable corruption controls together.
