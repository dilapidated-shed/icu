# Response capture

The existing text client entrypoint remains fetch_response. Binary callers use
fetch_bytes_response, whose result retains a Buffer, HTTP status, content
encoding, content type and UTF-8 validation independently. response_text rejects
unsupported content encodings and charsets before decoding. Invalid UTF-8 has
its own failure. HTTP rejection statuses retain their body and exact status.

CaptureFailure distinguishes invalid request, connection, TLS, request write,
malformed/unsupported framing, redirect, truncation, timeout, resource limit,
capture I/O, invalid UTF-8 and unsupported encoding. The native integer result
is converted at this boundary. Stdout commands keep their existing outcomes.

Capture directories use mkdtemp (0700). Exclusive, non-symlink file creation
uses 0600 regardless of umask. Both files and the directory are removed before
returning a response/failure. Captured bodies are bounded to 8 MiB; both declared
length and streaming size are checked. Invalid/duplicate Content-Length and
conflicting Transfer-Encoding are rejected explicitly. A single chunked coding
is decoded within the 8 MiB capture bound; extensions, trailers, truncated chunks
and extra bytes fail. Capture paths must have no symlink components and must
name distinct absent files in an owner-private directory. Metadata is written
only after complete body framing passes. Declared-length mismatch also fails
for the stdout command. POST metadata retains its actual request target.
Unexpected content encodings fail without a hidden decoder or runtime fallback.
GET/POST support HTTP/1.0 and HTTP/1.1, identity encoding and Connection: close.

Socket connection, reads and writes have 15-second bounds. DNS resolution uses
the host resolver and has no ICU-specific deadline yet. This remaining limit
does not make a DNS failure a successful empty response. TLS still verifies
the destination identity and system trust; no insecure mode was introduced.

The current native socket/OpenSSL provider remains in place. Idric-Net issue
#11 should move reusable connection establishment, TLS identity/trust, bounded
HTTP framing and byte streaming behind its existing transport interface.
Preserve these exact executable fixtures while moving ownership; keep ICU's
client result interpretation here. Do not delete this provider before installed
ICU acceptance passes against the replacement.

AICI owns `icu/accept.grease`, `icu/command-accept.grease` and independent
disposable HTTP/TLS fixtures. This continuation is direct one-off work.
Historical Kitchen/Flexible Pipes receipts remain historical. Android A1 and
C67 physical execution is NOT_RUN.
