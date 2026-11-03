# Diagnose the observed HTTP 426 responses

The actual installed ICU probe returned HTTP 426 and the same sixteen bytes,
`Upgrade Required`, for the original FSU URL and the exact Common Crawl index
query. These are rejected responses, not original syllabi or capture pointers.
ICU/Idric-Net currently renders HTTP/1.0. Do not assume that version explains
the failure until an otherwise matching request exercises HTTP/1.1.

## Type sketch

Input is one checked HTTPS URL and two owned output paths. The pure operation
`promote_version : List Char → List Char` changes the request's HTTP version in
the already rendered GET header. The effect calls the existing ICU native
transport with that wire head and empty body, and retains the exact response
metadata/body. Port, host and target remain Idric-Net values until the native
ABI. No curl, requests, host HTTP library or alternate TLS provider is used.

This is a diagnostic Idriç program using the real ICU provider, not acceptance
of a new public ICU client or proof that any source was acquired. The request
contains no credentials. The fixed source URLs do not contain the version
token in their target paths. Main/HTTP/1.0 behavior remains the passing control.

Compiler: actual Idriç 2734ac5a86610eddfab73b1644d72b4ac85a65ee;
Idric-Net: 2f6ea92c25c9b5a7286d1ca226fa3d1e12fe303e;
ICU provider: 468355b780d17f9b0838c8ac975962596811c484.
Compilation and execution results must be recorded separately below.

## Actual continuation, 2026-10-19

The initial list rewrite was diagnostic only and is preserved above as the attempt history. The maintained candidate now calls Idric-Net's finite `http_version` choice through `render_request_parts_at_version http_one_one`. Compiler attempt `http_1_0` failed because choice alternatives currently reject digits; the spelled-out alternatives compile. Natural subtraction in the first range sketch also failed (`Neg Nat`); the program uses `minus` only to render the final byte index and checks the endpoint relationship by addition.

Optional OFFSET LENGTH inputs are checked natural numbers, length 1..8 MiB. `make_header` constructs the Range field; the same ICU native provider retains exclusive body/metadata paths. Acceptance requires status 206, identity content encoding, exact declared/received length, exact start/end, and a total larger than the final byte. It refuses failed native calls before reading old metadata. Independent AICI TLS fixtures verify a real Range request and reject wrong status, wrong offset/total, missing/duplicate Content-Range, encoding and truncated bytes. This remains a candidate acquisition program, not a released `icu get-range` command.

Actual Common Crawl query returned four Reed homepage rows; the first compressed range is 9,367 bytes at offset 843666201, response 206, Content-Range `bytes 843666201-843675567/952222438`. SHA-256 `f9652a22cff1d6b5649b6bf9420b9c7c6c7058019348e8d201cb03ed42cf5d58`. The recovered 49,214 bytes match independent warcio 1.7.5 and jwarc 0.37.0 outputs. It is an unrelated homepage, not a syllabus. FSU PDF requests returned current HTTP 404. These are separate source outcomes.

The bounded provider now decodes simple chunked responses and preserves the explicit request version across redirects. Chunk extensions and trailers remain unsupported and fail closed. Native policy tests and independent capture/proxy/range acceptance are separate evidence. Existing native C is retained as the established temporary socket/OpenSSL provider and compiled with the actual pinned ICK; no Icky-source-profile acceptance or generic compiler substitution is claimed.
