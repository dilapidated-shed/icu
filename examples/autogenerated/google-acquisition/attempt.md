# Source-bound Google acquisition

The command receives a URL and a bounded set of public representation headers.
`CommandOptions` validates the command before IO; `Network.HTTP` constructs the
request and owns framing. `Transport` continues to invoke the existing native
HTTP/TLS provider. No additional client, TLS policy or JNI primitive is needed.

Types: `List Text → Either Text CommandOptions`; a command contains an
`http_version`, `request` and `List request_header`. Only User-Agent, Accept and
Accept-Language are command options. Names are unique ignoring ASCII case;
values are at most 1024 ASCII characters and the total is at most 4096.
Host comes from the checked URL, and identity encoding remains explicit.
Errors are command refusals, transport outcomes, HTTP outcomes, and search
semantic outcomes; none is evidence for another.

The matrix uses one public test query, the two genuine Google origins already
used by IB, HTTP/1.0 and HTTP/1.1, and three public representation headers. It
retains each original transfer-decoded body and ICU metadata in private files,
with request configuration and digests. It does not claim to capture TLS/wire
octets. There is no cookie jar, continuation guessing or challenge bypass.

The independent observer must identify titled public destination links in the
retained response. HTTP 200, a title or an arbitrary anchor does not suffice.
Live results, checked host execution, DEX/ART and physical execution remain
separate. Pending results are not passes.

The existing IB producer demonstrates checked ICU IO at compiler
153e7fc8053ba4072954a5e77d4efa3609d829e4 / frontend
740bbf2b1d9e811fd00c719c3473eede17ceea3b. The older IO-lowering failure in
docs/sun-http-android-evidence.md is historical and is not a reason to invent
another runtime. Changed command source still needs fresh DEX/ART acceptance.

The first hosted attempt (ai-ci run41569944402, artifact13202425072, archive
SHA256 2cf45c57a441919cb6bd5f5d4412496ddebbb7b398f100a57e671ad5c72b4412)
failed before acquisition. `make` selected the first `require-ick` target and
did not build ICU; the separate command checker also omitted `-p idric_net`.
The default build goal is now explicitly `all`, the independent caller requests
`make all`, and the checker declares its network package. These are build
repairs, not Google or IO-lowering results. Fresh qualification remains PENDING.
No fallback language or backend is selected.
