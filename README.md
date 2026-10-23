# ICU

ICU is a deliberately small HTTP client written in Idriç and built on
[`Idric-Net`](https://github.com/isomorphisms/Idric-Net) for reusable networking
semantics.

The inherited curl source is preserved unchanged under `old/` as reference
material. The new build does not compile or link curl.

## Scope

ICU's command surface deliberately exposes only:

- `http://` and `https://` URLs;
- `GET` and `POST` requests.

The reusable meanings are not defined again inside ICU. `Idric-Net` supplies the
typed URL, host, destination-port, request-target, HTTP body, byte-count, request,
request-head, and transport-result values. ICU chooses only the small subset it
wants to expose as a command-line program.

The current boundary is:

```text
ICU command grammar
       |
       v
Idric-Net URL / HTTP / transport types
       |
       v
ICU native transport adapter
       |
       v
OS sockets + OpenSSL
```

`src/Main.idric` parses the command line into Idric-Net values.
`src/Transport.idric` converts those semantic values to the current native ABI
only at the FFI boundary. `native/transport.c` still owns the present socket,
OpenSSL, response-framing, redirect, and response-streaming implementation.

Native integer transport outcomes are converted immediately to Idric-Net's
finite `transport_result`; they are not the application-level transport model.
The C boundary independently refuses wire requests that do not begin with GET
or POST.

## Commands

```text
icu get https://example.com/
icu post https://example.com/message hello
```

The command defaults to HTTP/1.0. `icu --http1.1 get URL` and
`icu --http1.1 post URL BODY` select HTTP/1.1; `--http1.0` selects the default
explicitly. Both versions send `Connection: close` and ask
for `Accept-Encoding: identity`, so HTML and image bodies can be consumed
directly without first adding gzip/brotli decoding. POST text is UTF-8.
Idric-Net computes the encoded `ByteCount`; ICU unwraps that value only when it
calls the native transport, which writes request headers and body separately
using the explicit byte count.

Response headers are bounded to 64 KiB. The status line and `Location` are
parsed before the final response body is streamed byte-for-byte to stdout;
other response headers remain internal for now.

### Redirects

GET follows at most eight redirects for status 301, 302, 303, 307, or 308.
Supported `Location` forms are:

- absolute `http://` and `https://` URLs;
- scheme-relative URLs beginning `//`;
- root-relative paths beginning `/`;
- ordinary relative paths;
- query-only redirects.

Fragments are removed before the redirected request. Redirected GET preserves
the original request headers except that the request target and `Host` header
are rewritten for the new endpoint. Cross-scheme redirects reselect plain TCP
or verified TLS from the destination. POST redirects are deliberately not
followed yet because 301/302/303 method rewriting needs an explicit Idriç policy.

The library transport surface also accepts checked caller headers from Idric-Net.
A caller may mark an arbitrary header as credential-sensitive without changing
its wire spelling. Same-origin redirects retain those headers. When scheme,
host, or port changes, ICU strips `Authorization`, `Cookie`, and every
caller-declared credential header while retaining unrelated custom headers.
The command-line surface does not yet expose a general authentication command.

Response bodies are binary-safe: an image can be fetched directly to a file,
for example:

```sh
icu get https://example.com/cover.jpg > cover.jpg
```

### Configured HTTPS proxy hop

HTTPS requests honor `HTTPS_PROXY` (or `https_proxy`) when it names an HTTP
proxy with an explicit hostname and optional port. A CONNECT tunnel carries
the original HTTPS request; TLS still verifies the original destination's
certificate and hostname. Proxy credentials in URLs, SOCKS/HTTPS proxy schemes,
proxy paths/query/fragment values and malformed ports fail closed without
printing the supplied value. Loopback destinations remain direct so local
acceptance is independent of the build host's proxy environment.

This bounded addition supports the configured egress hop required by restricted
build hosts. It does not change the Idric-Net request, JSON decoding, redirect
credential policy or the application command surface. Plain HTTP proxying and
general `NO_PROXY` matching beyond loopback are not implemented by this slice.

Current intentional limits:

- no plain HTTP proxying, cookies, authentication helpers, or custom methods
- no compressed-response decoder; requests currently ask servers for identity encoding
- POST redirects are not followed
- no FTP, SMTP, file URLs, or other curl protocols
- URL authority and request target must be visible ASCII; spaces and Unicode must be percent-encoded
- URL userinfo and IPv6 literals are not supported yet

## Optional private response capture

The ordinary command writes exact response bytes to stdout. Browser acquisition
can instead set **both** `ICU_CAPTURE_BODY_PATH` and `ICU_CAPTURE_METADATA_PATH`
to different, absolute, initially absent paths inside an owner-private directory,
then run the **same ICU command**:

```sh
ICU_CAPTURE_BODY_PATH="$private/body" \
ICU_CAPTURE_METADATA_PATH="$private/metadata" \
  icu get https://example.com/
```

The existing binary-safe transport captures at most 8 MiB, refuses unsupported
framing and truncated declared lengths, and creates mode-0600 files without
following an existing output symlink. A nonzero exit never authorizes these bytes
as a resource. The metadata includes `http_status`, `content_type`,
`content_encoding`, `received_length`, `utf8`, `resolved_url` and `redirect_count`.
The final URL reflects ICU's actual followed endpoint, not displayed HTML.

This is a trusted process boundary, not a renderer API. A browser must own and
validate its private directory, requested URL, lifetime, freshness, size and
result integrity. Physical Android/DEX, Shizuku, Arena and UI handoff are separate.

## Build

Requirements: the maintained Idriç compiler, an installed `idric_net` package,
the pinned ICK native compiler, and OpenSSL. Pass ICK's verified executable
as the Make variable `ICK`; the native build refuses an absent compiler.

Install Idric-Net with the same compiler/prefix used for ICU:

```sh
cd ../Idric-Net
idris2 --install idric-net.ipkg
cd ../icu
make ICK=/verified/ick-stage/bin/gcc IDRIC=/verified/Idric/_/build/exec/idris2
```

CI pins the exact Idric-Net commit it installs before compiling ICU, so the
cross-repository boundary is reproducible rather than dependent on whatever a
branch happens to contain later.

Run the executable from the repository root so the Scheme C FFI can find the
repo-local transport library:

```sh
./build/exec/icu get https://example.com/
./build/exec/icu post https://example.com/message hello
```

`make check-native` compiles the native boundary with warnings promoted to
errors.

The `icu_response` library also exposes `fetch_bytes_response_at_version`
and `fetch_range_response`. Exact ranges use HTTP/1.1 and reject wrong status,
offsets, final offset/total, encodings and entity-byte counts in Idriç.
`HTTPBytesResponse` retains the actual resolved URL, redirect count and
Content-Range alongside the bytes. Rebuild consumers of this source record.

Android uses one adapter, `android/jni_transport.c`, and one NDK build recipe,
`android/Makefile`, optionally invoked through `android/build-native.grease`.
The original `org.isomorphisms.icu.ICU` native names remain ABI aliases for
existing callers; direct DEX uses `Idric.Generated`. Both share the same strict
UTF-16 to UTF-8 conversion and HTTP provider. No Java source is generated.
Building this library does not establish a working ICU DEX or phone package.

## Reference source

`old/` is the complete previous repository tree, currently curl at commit
`5c61e168698a72b87437d58ed728bcdea6d5db42`. Its original licensing and
attribution remain intact there; the root `COPYING` is retained as well.
