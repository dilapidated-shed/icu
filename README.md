# ICU

ICU is a deliberately small HTTP client written in Idriç.

The old curl source is preserved unchanged under `old/` as reference material.
The new program does not try to preserve curl's feature surface.

## Scope

Exactly two URL schemes are represented:

- `http://`
- `https://`

Exactly two requests are represented:

- `GET`
- `POST`

Unsupported protocols and methods are not stored as strings waiting to fail later;
they are absent from the request model.

## Shape

```idris
choice url one_of
  http_url String Nat String
  https_url String Nat String

choice request one_of
  get url
  post url String
```

A URL therefore carries its scheme, host, port, and request target. A request can
only be GET or POST.

`src/Http.idric` owns URL parsing and HTTP/1.1 wire rendering. `src/Main.idric`
owns the tiny command-line grammar. Transport is a separate boundary: this first
slice deliberately does not hide the archived curl implementation behind the
new API. The next transport slice can be small sockets for HTTP and one explicit
TLS implementation for HTTPS without changing the semantic model.

## Commands

```text
icu get https://example.com/
icu post https://example.com/message hello
```

For now the executable renders the exact HTTP/1.1 request that the transport will
send. POST bodies are restricted to ASCII in this slice so `Content-Length` is
unambiguous before a byte-oriented transport type is added.

## Build

Build the Edriç compiler in `isomorphisms/Idric`, then use its Idris 2 executable
to build `icu.ipkg`.

The source is ordinary Idris 2 plus the filename-scoped Edriç `choice ... one_of`
syntax and Unicode function arrows.

## Reference source

`old/` is the complete previous repository tree, currently curl at commit
`5c61e168698a72b87437d58ed728bcdea6d5db42`. Its original licensing and attribution
remain with that tree.
