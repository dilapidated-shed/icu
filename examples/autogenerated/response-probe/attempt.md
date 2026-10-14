# Response probe

Inputs: a checked Idric-Net URL, GET/POST text, an optional output filename.
Outputs: HTTP status plus decoded text, or status plus exact captured bytes.
Effects: socket/TLS transport, private capture, file output, process exit.
The production boundary remains ICU → Idric-Net → existing native transport.
fetch_bytes_response preserves a Buffer, status, encoding and UTF-8 validation
separately. response_text checks encoding before decoding; fetch_response keeps
the existing client signature. No refC, curl invocation or substitute provider.

The probe uses the maintained Chez backend and installed Idric-Net. Independent
AICI fixtures exercise its actual executable, including hostile binary bytes.
Native fixture TLS uses OpenSSL within the existing transport test boundary.
Exact commands/results belong to the Flexible Pipes execution receipts.
