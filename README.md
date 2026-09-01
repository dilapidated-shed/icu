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

The request model currently renders HTTP/1.0 plus `Connection: close` and asks
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

Response bodies are binary-safe: an image can be fetched directly to a file,
for example:

```sh
icu get https://example.com/cover.jpg > cover.jpg
```

Current intentional limits:

- no proxies, cookies, authentication helpers, or custom methods
- no compressed-response decoder; requests currently ask servers for identity encoding
- POST redirects are not followed
- no FTP, SMTP, file URLs, or other curl protocols
- URL authority and request target must be visible ASCII; spaces and Unicode must be percent-encoded
- URL userinfo and IPv6 literals are not supported yet

## Checked hostile-input boundary

The `hostile-ingestion-receipts` CI job pins the canonical corpus and verifier
from `isomorphisms/ai-ci` at an exact commit. It runs all ten inputs through two
separate paths:

- the implementation under test: an Idriç receipt emitter using ICU's current
  native C/OpenSSL transport boundary and an Idriç `document_log_subset_v0`;
- the oracle: curl, gzip, iconv, and libxml2.

The candidate is traced at `execve`. AICI rejects the receipt if that trace
invokes the oracle, if candidate metadata claims an oracle identity or fallback,
or if any later stage reports success after the first real failure.

This is not yet a one-language completion claim. The receipt names
`idric+icu-native-c` wherever the current transport or raw-byte validation still
crosses the native boundary. It also marks gzip as the first unimplemented
candidate boundary and calls the document representation a subset log rather
than a browser DOM.

## Build

Requirements: the Edriç/Idriç compiler fork, an installed `idric_net` package,
a C11 compiler, and OpenSSL.

Install Idric-Net with the same compiler/prefix used for ICU:

```sh
cd ../Idric-Net
idris2 --install idric-net.ipkg
cd ../icu
make IDRIC=/opt/Idric/build/exec/idris2
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

## Reference source

`old/` is the complete previous repository tree, currently curl at commit
`5c61e168698a72b87437d58ed728bcdea6d5db42`. Its original licensing and
attribution remain intact there; the root `COPYING` is retained as well.
