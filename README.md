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
choice url one_of
  http_url String Nat String
  https_url String Nat String

choice request one_of
  get url
  post url String
```

`src/Http.idric` owns URL parsing and HTTP request rendering.
`src/Transport.idric` chooses plain TCP or verified TLS from the URL choice.
`native/transport.c` owns only sockets, OpenSSL, and response-body streaming.
`src/Main.idric` owns the tiny command-line grammar.

The C boundary independently refuses wire requests that do not begin with GET
or POST. There is no protocol registry or generic method string in the active
Edriç model.

## Commands

```text
icu get https://example.com/
icu post https://example.com/message hello
```

The first transport slice deliberately uses HTTP/1.0 plus `Connection: close`.
That avoids chunked-response machinery while keeping ordinary HTTP and HTTPS
GET/POST useful. Response headers are discarded and the response body is
streamed to stdout.

Current intentional limits:

- no redirects, proxies, cookies, authentication helpers, compression, or custom methods
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
errors.

## Reference source

`old/` is the complete previous repository tree, currently curl at commit
`5c61e168698a72b87437d58ed728bcdea6d5db42`. Its original licensing and
attribution remain intact there; the root `COPYING` is retained as well.
