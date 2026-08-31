# SDF mailbox reader sketch

This branch is only a design stub. It does not implement mail access yet.

## Motivation

Reading the mailbox by opening Termux and SSHing into SDF works, but it is inconvenient on a phone. The intended utility is a very small, read-only mailbox viewer that can cheaply answer: what mail is here, who sent it, when, what is the subject, and what does the beginning say?

The remote mailbox remains canonical. The phone should not accumulate complete copies of tens of thousands of messages.

## Local record

For each message, keep only:

- `To`
- `From`
- `Date`
- `Subject`
- first meaningful paragraph of body text

A small locator may also be necessary so the complete remote message can be reopened later. Prefer a stable server identifier when the transport provides one; for an mbox stream, a byte offset plus enough identity information to detect a changed mailbox may be useful.

Cap the locally retained first paragraph (for example around 1-2 KiB). With roughly 50,000 messages this should keep the local index in the tens of megabytes rather than mirroring the whole mailbox.

## Which protocol?

Do not decide this merely because the source currently looks like an mbox file over SSH.

If the mailbox being inspected through SSH is the normal SDF mail spool exposed through IMAP, IMAP should be tried first. It naturally fits a read-only phone index because the client can ask the server for message metadata and selected body data without downloading and storing every complete message.

POP3 is a possible smaller alternative, especially if restoring its curl implementation is substantially cheaper. It is less attractive for long-lived synchronization and selective access. It should be evaluated rather than assumed.

Neither IMAP nor POP3 is a generic remote-mbox-file protocol. If the desired file is just an arbitrary mbox somewhere in the SDF home directory and is not the mailbox served by SDF's mail service, IMAP/POP3 will not magically expose that file. In that case the transport and the mbox parser are separate problems: stream the file somehow, then parse it incrementally.

## mbox parser

An mbox parser is still useful independently of the network choice. It should work incrementally and should not require the entire mailbox in memory or local storage.

Minimum concerns:

- distinguish mbox `From ` separators from escaped body lines such as `>From `;
- unfold RFC mail headers;
- extract `To`, `From`, `Date`, and `Subject`;
- decode common encoded headers;
- find a useful `text/plain` body part in MIME messages;
- handle quoted-printable/base64 and common character sets as needed;
- take the first meaningful paragraph and then discard the rest of the body.

This is a good concrete parser target for Idriç/Edriç, but it should stay separable from ICU's transport code.

## ICU / curl

ICU deliberately deleted most of curl from its active implementation. The repository's `old/lib/` tree still contains curl's `imap.c` and `pop3.c`, so the removed implementations are available as reference/source material.

Do **not** restore all of curl to get mail support. First determine which transport is actually needed, then restore the smallest coherent set of protocol/authentication/TLS dependencies needed for that transport. Keep any restored mail protocol feature-gated so ICU can still build in its very small existing configuration.

Likely experiment order:

1. Confirm whether this SDF mailbox is reachable read-only over IMAP TLS.
2. Measure the code/dependency increase from restoring minimal IMAP support to ICU.
3. If IMAP is disproportionately expensive, measure minimal POP3 support before choosing.
4. Independently write/test a streaming mbox/MIME parser against saved fixtures.
5. Build the compact local index and only then add the phone UI.

## Compiler-learning purpose

Keep the streaming version visibly distinct from any whole-input/materializing reference implementation. The point is to connect language/compiler choices to a program that actually matters.

Things to inspect through Idriç -> IR -> ARM/Thumb include:

- whether bounded source buffers stay bounded after lowering;
- hidden copies and allocations;
- parser-state representation;
- buffer ownership and lifetime;
- register pressure and spills;
- stack-frame size;
- branch/loop structure;
- read/syscall granularity;
- behavior when network chunks split headers, UTF-8, base64, or MIME boundaries.

A useful invariant is that the streaming path's working memory is independent of total mailbox size.

## Cross-repository tracking

- ICU transport: https://github.com/dilapidated-shed/icu/issues/14
- phone utility: https://github.com/isomorphisms/utilities-android-phone-user/issues/30
- Idriç compiler workload: https://github.com/isomorphisms/Idric/issues/52
- ARM/Thumb lowering: https://github.com/isomorphisms/idric-arm-thumb/issues/40
- ai-ci acceptance contract: https://github.com/isomorphisms/ai-ci/issues/52
