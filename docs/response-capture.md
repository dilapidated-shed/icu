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
Transfer-Encoding are rejected explicitly rather than decoded as body bytes.
GET/POST requests use HTTP/1.0, identity encoding, and Connection: close.

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

Kitchen owns tasks/icu-finish/build.grease. AICI owns icu/accept.grease and
independent disposable HTTP/TLS fixtures. Flexible Pipes holds opening, build,
acceptance and merge receipts. Android A1 and C67 physical execution is NOT_RUN.
