# Historical ICU network lab: PRs #3, #10, and #11

This directory preserves the exact diffs and design intent of three August 2026
network experiments.

They are archived side by side because the useful ideas have not been rejected,
but the architecture around them changed substantially before they could be
integrated cleanly.

## What each experiment was doing

### PR #3 — Translate pure transport logic into Idriç

This explored moving the pure parts of the then-current native HTTP transport
into an Idriç `TransportModel`: status/header parsing, redirect classification
and resolution, request-target validation, authority rendering, and redirected
GET rewriting.

The important idea was differential evidence: run the C implementation and the
Idriç model over the same fixtures and compare externally visible behavior
byte-for-byte.

### PR #10 — Run hostile ingestion corpus through the Idriç path

This connected ICU to the canonical hostile-ingestion corpus and AICI receipt
machinery. It added explicit response metadata, Content-Length mismatch refusal,
UTF-8 validation, stage receipts, independent oracle execution, exec tracing,
and first-failing-boundary accounting.

The important ideas were:

- never silently fall back to the oracle;
- preserve failure state instead of turning failure into empty bytes;
- compare the candidate and reference implementation stage by stage;
- retain exact implementation/corpus/oracle identities in receipts.

This work advances issue #9, "Cross-repo: typed network / bytes / decoding
boundary for ICU".

### PR #11 — Converge hostile ingestion with the pure transport model

This was an explicit laboratory convergence of #3 and an earlier #10 head. It
kept the live C/OpenSSL transport and the pure Idriç model side by side rather
than claiming that the model had become the live network owner.

It demonstrated that both kinds of evidence could coexist: hostile-ingestion
receipts for the live path and C-vs-Idriç equivalence fixtures for the pure
transport logic.

## Why these are archived instead of merged into the live implementation

These experiments were designed before the present ownership boundaries had
settled.

Current ICU now depends on `Idric-Net` for reusable URL, HTTP, transport, byte,
port, and result semantics. The current Android direction also adds direct DEX,
ART, JNI, NDK/OpenSSL packaging, and Cat Food delivery boundaries.

Relevant later work includes:

- #20 — caller-declared credential headers and redirect policy;
- #21 — purpose-level transport actions;
- #22 — current Idric-Net transport types;
- #24 — ARMv7 Android/Cat Food acceptance tracking;
- #25 — maintained Android JNI/native transport boundary;
- #26 — remaining direct-DEX/JNI/ART integration experiment;
- #27 — fail-closed Cat Food package boundary;
- #28 — direct-DEX export of the checked ICU command.

Because of that later work, copying the old modules into active `src/`,
`native/`, or the active workflows would confuse ownership and could regress
the current Idric-Net/Android architecture.

Archiving them here keeps the code and reasoning without making a premature
decision about how they should be reincorporated.

## Questions for a future reconstruction pass

A later high-reasoning audit should decide, from current repositories rather
than these old branches alone:

1. Which pure transport-model functions now belong in Idric-Net conformance
   tests rather than ICU?
2. Which hostile-ingestion states should be represented by current Idric-Net
   types and which remain ICU/application-level states?
3. How should AICI stage receipts cover the direct-DEX/JNI Android path without
   duplicating transport semantics?
4. Which response metadata and truncation/decoding checks remain missing from
   current ICU?
5. How should #20's credential-sensitive redirect policy compose with the
   current Idric-Net and Android path?
6. Which old C-vs-Idriç fixtures are still useful as independent oracles even
   after ownership moves?

## Preserved material

- `pr-3.diff` — exact patch for PR #3
- `pr-10.diff` — exact patch for PR #10 at its current head
- `pr-11.diff` — exact patch for PR #11 at its current head

These are historical evidence, not active source files.


## PR #20 — caller-declared credential headers

PR #20 was built on the historical #11 convergence line and also carried the
earlier response-capture/custom-header stack and an experimental OpenAI client.
Its exact patch is preserved as `pr-20.diff`.

The current-line reconstruction intentionally separates those concerns:

- checked caller-header and credential metadata belong in Idric-Net;
- ICU's current native transport enforces same-origin retention and cross-origin
  credential stripping;
- the ordinary five-argument ICU transport ABI remains intact for the current
  Android/JNI path;
- old response-capture, ingestion, and OpenAI-client code is preserved here but
  is not reactivated implicitly.

A later reconstruction pass can decide which preserved response-capture and
ingestion pieces belong in current Idric-Net/ICU without making the old #11
architecture authoritative again.
