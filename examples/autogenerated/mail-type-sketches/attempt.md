# Idriç mail type sketches: attempt record

## Purpose
Test the type distinctions in the SDF → issh → mbox/MIME → migration/import → Gmail problem before another-language implementation influences them. This is a type census, not a mail or SSH implementation.

## Values and domains
The files distinguish remote endpoint and mailbox identity, remote paths and observations, byte positions and spans, mbox envelope/body framing, raw RFC bytes, parsed headers and MIME interpretation, provenance, destination identity, import outcomes, restart observations, archival verification, and later acceptance evidence.

## Intended inputs and outputs
- A remote source step observes and opens one named remote mailbox, then yields bounded byte chunks, EOF, or read failure.
- The mbox sketch frames exact source intervals and preserves malformed or ambiguous cases.
- RFC/MIME interpretation accepts a raw message value; it does not own mailbox framing.
- Provenance binds source mailbox, interval, raw digest evidence, parser observation, Message-ID observation, and optional imported destination identity.
- Destination import accepts one raw message and request metadata and returns a destination receipt or a classified failure.
- Restart state retains source observation and represents uncertain external success before local acknowledgement.
- Archival copy/verification remains independent of destination import.
- Physical acceptance records future observations; no real acceptance is asserted.

## Effects, laws, and invariants
The operations records expose signatures without executable bodies. Source byte counts and offsets are absolute within a named source. End offsets are exclusive. A Message-ID observation is evidence, not a unique key. A retry after an uncertain write requires reconciliation before another import attempt. Semantic decoding does not replace original source bytes.

## Required primitives and backend
The sketches use Idris2/Idriç core types and Data.Bits.Bits8. They require no socket, OAuth, HTTP, Gmail, Drive, cryptographic, database, or filesystem implementation.

## Intended design versus current language evidence
All declarations are deliberately small and independent. Streaming.idric tests a step-shaped candidate to expose the types that must compose; it does not establish an iterator, callback, coroutine, actor, lazy-list, or final API. Since idris2 is not installed in this workspace, source review alone cannot establish compiler support. The focused workflow checks each file independently with the revision already pinned by the mailbox-reader workflow.

## Non-Idriç sources
No D, Agda, Haskell, C, Icky C, SSH implementation, mail library, Google SDK, or other-language implementation was consulted for type design. Existing Idriç SSH sketch files and repository build/workflow metadata were inspected. The imported old/ code trees were not read.
