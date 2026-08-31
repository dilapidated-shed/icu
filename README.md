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

choice request_header one_of
  http_header String String
```

`src/Http.idric` owns URL parsing, checked caller-supplied headers, and HTTP
request rendering. `src/Transport.idric` chooses plain TCP or verified TLS from
the URL choice. `native/transport.c` owns sockets, OpenSSL, response framing,
and the small amount of response-header handling needed to keep GET useful on
the web. `src/TransportModel.idric` is the side-by-side Idriç translation of the
pure parts of that native transport: status and header parsing, redirect
classification and resolution, request-target validation, and redirected GET
rewriting. The live transport still uses the C copies while their equivalence
boundary is established. `src/OpenAI.idric` owns the Responses API request JSON,
authentication header, response extraction, and fresh-chat command behavior.
`src/Main.idric` owns the tiny command-line grammar.

The C boundary independently refuses wire requests that do not begin with GET
or POST. There is no protocol registry or generic method string in the active
Edriç model.

## Commands

```text
icu get https://example.com/
icu get -H 'X-Api-Key: secret' https://example.com/data
icu post -H 'Content-Type: application/json' https://example.com/message '{}'
```

`-H` and `--header` may be repeated before the URL. This restores the useful
caller-header part of curl's old command surface without reviving curl itself.
A caller header with the same case-insensitive name as ICU's default `Host`,
`Accept-Encoding`, `User-Agent`, or POST `Content-Type` replaces that default.
ICU still owns request framing: caller attempts to set `Connection`,
`Content-Length`, or `Transfer-Encoding` are refused. Header names are currently
a safe ASCII subset (letters, digits, `-`, `_`), and values may contain only
visible ASCII plus horizontal tab; CR/LF injection is rejected. Curl's `-H
@file` form is not implemented.

The transport uses HTTP/1.0 plus `Connection: close`. Requests advertise
`Accept-Encoding: identity` unless the caller replaces it, so HTML and image
bodies can normally be consumed directly without first adding gzip/brotli
decoding. POST text is encoded as UTF-8: Idriç computes `Content-Length` from
the encoded byte count, then the native transport writes request headers and
body separately using that explicit body length. Response headers are bounded
to 64 KiB. The status line and `Location` are parsed before the final response
body is streamed byte-for-byte to stdout; other response headers remain internal
for now.

### OpenAI Responses API

`icu openai` is the real fresh-request command intended for the Blackball A/B
runner. The prompt is the complete UTF-8 stdin stream. A successful invocation
writes only the first `output_text` answer to stdout; diagnostics and failures go
to stderr and return nonzero.

```sh
printf 'Explain the employment evidence for this degree.\n' | \
  OPENAI_API_KEY="$OPENAI_API_KEY" \
  ./build/exec/icu openai
```

The default endpoint is `https://api.openai.com/v1/responses` and the default
model is `gpt-5.6-sol`. `OPENAI_RESPONSES_URL` and `OPENAI_MODEL` can override
those values for a pinned experiment or deterministic local fixture.

Every invocation creates one Responses API request with no conversation ID or
`previous_response_id`; the request also sets `store` to false. No tool list is
supplied. The API key is read only from `OPENAI_API_KEY`, rejected if it cannot
safely form one HTTP header value, and is never written to stdout, stderr, the
response files, or reproducibility metadata.

This is intentionally dogfooded through ICU. The OpenAI-specific code is Idriç
and calls ICU's existing checked file-backed transport seam. It does not invoke
curl, Python, an OpenAI SDK, or another HTTP client. The current sockets/TLS
owner remains the already-declared `native/transport.c` C/OpenSSL boundary; this
change does not mislabel that older boundary as one-language transport.

The JSON work is also local: Idriç escapes the prompt/model request strings and
extracts an `output_text` string from the returned Responses JSON, including
standard JSON escapes and Unicode surrogate pairs. Temporary response and
metadata files are namespaced by process ID under `TMPDIR` (default `/tmp`) and
removed before the command returns.

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
or verified TLS from the destination. This inherited redirect behavior means a
caller-supplied credential header on GET is also preserved today; unlike the old
curl `--header` behavior, ICU has not yet restored curl's cross-origin
`Authorization`/`Cookie` stripping. Do not use credential-bearing GET headers on
requests that may cross origins until that policy is implemented. POST redirects
are deliberately not followed yet because 301/302/303 method rewriting needs an
explicit Idriç policy.

Response bodies are binary-safe: an image can be fetched directly to a file,
for example:

```sh
icu get https://example.com/cover.jpg > cover.jpg
```

Current intentional limits:

- no proxies, cookies, authentication helper layer, or custom methods
- custom header files (`-H @file`) are not supported
- cross-origin redirect stripping for caller credentials is not implemented yet
- no compressed-response decoder; requests normally ask servers for identity encoding
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

`make check-transport-model` runs every case in
`tests/transport-fixtures.json` through a C oracle built directly from
`native/transport.c` and through the Idriç model, then compares exit status,
stdout, and stderr byte-for-byte. Fixtures cover valid and malformed status
lines, case-insensitive and empty redirect headers, redirect status codes,
visible-ASCII validation, request targets, five supported redirect forms,
rejected authorities and ports, and Host-header rewriting.

## Reference source

`old/` is the complete previous repository tree, currently curl at commit
`5c61e168698a72b87437d58ed728bcdea6d5db42`. Its original licensing and
attribution remain intact there; the root `COPYING` is retained as well.
