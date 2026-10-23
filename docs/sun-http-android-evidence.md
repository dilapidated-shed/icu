# HTTP and Android continuation — 2026-10-23

This is a partial executable continuation, not ICU Android completion.

## Implementation

Reconciles the capture/final-URL work from PR 35 with the chunked/version/range
work from PR 36 on the current `icu` line. The ordinary command still emits
binary response bytes; explicit HTTP/1.0 and HTTP/1.1 choices survive redirects.
The public Idriç range API checks status, exact bounds/total and entity length.

The native provider refuses malformed/conflicting framing on redirects as well
as final responses, unexpected encodings, truncated/extra declared bytes and
unsafe capture paths. Private output walks directory components without
following symlinks. Metadata follows successful body framing. Valid HTTP
optional whitespace is accepted and normalized before TSV serialization.
Unsupported redirect schemes cannot become relative HTTP paths. Temporary
capture directories honor `TMPDIR`, including Android runtime environments.

The maintained JNI implementation is `android/jni_transport.c`. It retains the
original ABI names and the direct producer's `Idric.Generated` names, with one
strict UTF-16/UTF-8 conversion and explicit native result codes. Unpaired
surrogates, NUL host/header metadata and wrong body byte counts fail. Body bytes
may contain NUL. No Java/Kotlin/Gradle/d8 production stage or new refC dependency
was added. The inherited native provider still enforces redirects/framing;
the planned reusable provider move to Idric-Net is not claimed complete.
Android default trust reads PEM roots from the platform certificate stores
without assuming that their filenames use OpenSSL's hash convention. Explicit
`SSL_CERT_FILE`/`SSL_CERT_DIR` retain their meaning. This Android trust path is
built, but remains unexecuted until ART/device acceptance is available.

## Source pins

| Input | Revision |
| --- | --- |
| ICU default base | `468355b780d17f9b0838c8ac975962596811c484` |
| Capture branch input | `0da041fbe906657f4f705da45a842d96832e5ee1` |
| Chunk/range branch input | `2f46fb326b6a75c50fd420d3aca4a7e7d59cd33a` |
| Idriç host compiler | `0e8cfdd1106ebdeffd01b6c519880ea3afc7dc35` |
| Compiler error-reporting repair | `740bbf2b1d9e811fd00c719c3473eede17ceea3b` |
| Idric-Net finite HTTP outcomes | `b21801b72acb8ab835c297bbe298af8599550134` |
| Direct DEX backend | `6ecf378dd3e8dac59091ce8d7f3c3f315f43e4a5` |
| ICK | `fbe86e23d55cfec2000c08e61deea2a407fd7175` |
| ICK GCC base | `6294f1d9e7536e5ffcde09d1528c918d63abfef5` |
| ICK qualification recipes | `isomorphisms/ai-ci@32cb45b3a3a257aeb07881c75f3b846495cfb2c9` |
| Independent ICU acceptance | `isomorphisms/ai-ci@bd834cd7ac965f050493c072f83f221c4b400dd3` |
| OpenSSL | `f4dc4d58b48d346a8270183f89acf826d459b0ca` |
| Android NDK / API | `27.2.12479018` / `24` |

Native acceptance ran on Ubuntu 24.04 x86-64 with the pinned ICK frontend.
Stock GCC was used only to bootstrap ICK. Debug sections were stripped from
the installed compiler to recover build space, then its semantic qualification
was rerun. Android compilation/linking used the declared NDK stage.

## Executed boundaries

| Boundary | Result |
| --- | --- |
| Checked ICU CLI, response library and response probe | PASS |
| Existing pure ICU HTTP-model groups | 8 PASS |
| Idric-Net network groups, including all new outcome mappings | 17 PASS |
| Native framing, length, target, redirect and path checks | 55 PASS |
| JNI Unicode and argument/body ABI checks on host | 10 PASS |
| Independent capture phase | 41 checks; includes HTTP/HTTPS, binary, UTF-8, truncation, timeouts, size and private capture |
| Independent proxy phase | 55 checks; includes origin certificate/hostname and credential controls |
| Independent real command/public range phase | 19 PASS |
| Compiled provider with an incorrect 16 MiB limit | REJECTED at `limit` |
| ARMv7 and x86-64 JNI libraries and test executables | BUILD PASS; ELF/symbol/dynamic-dependency inspection PASS |
| Actual ICU direct DEX | BLOCKED at `Main.main : IO ()` source ABI |
| Emulator execution | NOT_RUN: image required 7372.80 MB writable userdata space; available space was insufficient |
| Cat Food archive / publication / installation | NOT_RUN: complete ICU DEX/JNI package does not exist |
| MIRO A1 HTTP/HTTPS | NOT_RUN: no physical device channel was available |

The compiler repair was tested with a deliberately rejecting generator (exit
1), version output (exit 0), and explicit Chez generation plus execution (exit
0, expected text). Rebuilding the actual DEX producer against that Driver
changes ICU's rejection from a misleading exit 0 to exit 1. The existing
supported arithmetic control still emits a DEX accepted by Android `dexdump`.
That control is compiler regression evidence, not a substitute ICU program.

## Commands executed

The verified executable paths were supplied explicitly to these interfaces.
These are build receipts, not phone installation instructions.

- Idriç `--install idric-net.ipkg`, `--build icu.ipkg`,
  `--install icu-response.ipkg`, `--build response-probe.ipkg`,
  `--build protocol-version-probe.ipkg` and the two repositories' model tests.
- `make` with `ICK` pointing to the qualified ICK frontend: native library and
  `check-native`; the JNI oracle and independent AICI fixture/verifier were
  also compiled with this same ICK.
- AICI `icu/accept.grease` for `capture` and `proxy`,
  `icu/command-accept.grease`, and `icu/reject-limit-mutant.grease`.
- `make -f android/Makefile all tests` for `armeabi-v7a` and `x86_64`, with
  the exact NDK and OpenSSL source supplied. Both use static OpenSSL.
- Direct DEX producer `--cg dex -p idric_net --source-dir src src/Main.idric`.
  No ICU DEX was produced; the unsupported ABI remains a real blocker.

## Historical work retained

The archived diffs for PRs 3, 10 and 11 were inspected, not merged. Useful
status/header, five redirect forms, invalid authority/target, binary/UTF-8,
truncation, resource and first-failure controls are carried into current native,
Idric-Net and independent AICI tests. The old permissive status parser is not
an oracle for the hardened parser. The old 28-case C-versus-TransportModel
equivalence run and network-through-document ingestion receipts were not
rerun or presented as current acceptance. No obsolete TransportModel or DOM
implementation was made a new runtime dependency.

## Remaining boundaries

The DEX compiler still needs executable IO, reachable definitions and runtime
lowering for the real command, followed by ART/native-library loading and HTTP
and HTTPS execution. No fabricated DEX or Cat Food package fills that gap.
Physical ARMv7 acceptance remains separate from the native library build.
SMTP, MQTT and SDF IMAP/POP3 remain independent later tasks.
