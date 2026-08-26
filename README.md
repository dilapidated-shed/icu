# ICU

ICU is a deliberately small HTTP client written in Idriç.

The inherited curl source is preserved unchanged under `old/` as reference
material. The new build does not compile or link curl.

## Scope

Exactly two URL schemes are represented:

- `http://`
- `https://`

Exactly two requests are represented:

- `GET`
- `POST`

Unsupported protocols and methods are absent from the request model rather than
stored as strings and rejected much later.

```idris
record Port where
  constructor MkPort
  bits : Bits16

choice url one_of
  http_url String Port String
  https_url String Port String

choice request one_of
  get url
  post url String
```

`Port` is bounded by representation to the complete unsigned 16-bit range.
`mk_port : Integer → Maybe Port` is the checked conversion from a wider integer.
The URL parser deliberately keeps its existing destination policy of accepting
textual ports 1 through 65535, while the type itself faithfully represents
0 through 65535.

`src/Http.idric` owns URL parsing and HTTP request rendering.
`src/Transport.idric` chooses plain TCP or verified TLS from the URL choice.
`native/transport.c` owns sockets, OpenSSL, response framing, and the small
amount of response-header handling needed to keep GET useful on the web.
`src/Main.idric` owns the tiny command-line grammar.

The C boundary independently refuses wire requests that do not begin with GET
or POST. There is no protocol registry or generic method string in the active
Edriç model.

## Commands

```text
icu get https://example.com/
icu post https://example.com/message hello
```

The transport uses HTTP/1.0 plus `Connection: close`. Requests advertise
`Accept-Encoding: identity`, so HTML and image bodies can be consumed directly
without first adding gzip/brotli decoding. Response headers are bounded to
64 KiB. The status line and `Location` are parsed before the final response body
is streamed byte-for-byte to stdout; other response headers remain internal for
now.

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
- POST bodies are ASCII-only until the Edriç boundary carries explicit UTF-8 bytes

## Build

Requirements: the Edriç/Idriç compiler fork, a C11 compiler, and OpenSSL.

```sh
make IDRIC=/opt/Idric/build/exec/idris2
```

Run the executable from the repository root so the Scheme C FFI can find the
repo-local transport library:

```sh
./build/exec/icu get https://example.com/
./build/exec/icu post https://example.com/message hello
```

`make check-native` compiles the native boundary with warnings promoted to
errors. `make check-idric-vocabulary` rejects the upstream natural-number
spelling from active `.idric` source, tests, and this README; the default build
runs that check before compiling ICU.

## Reference source

`old/` is the complete previous repository tree, currently curl at commit
`5c61e168698a72b87437d58ed728bcdea6d5db42`. Its original licensing and
attribution remain intact there; the root `COPYING` is retained as well.
