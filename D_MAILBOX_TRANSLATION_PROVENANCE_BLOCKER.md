# D mailbox translation: provenance blocker

Status: **BLOCKED before translation**.

This branch records the source audit requested for a whole-hog D translation of the original mailbox/message program used to split the SDF mbox stream into importable RFC messages. The audit did not locate that retained original program or a complete copied source corpus. No D implementation or translated tests were started.

## Repositories examined

### `dilapidated-shed/icu`

The mailbox-related project is `dilapidated-shed/icu`, branch `sdf-mailbox-reader`, head commit `d7363a58766e17f4677fae8d2e936e9121aa40c9`.

- [SDF_MAILBOX.md at the audited branch](https://github.com/dilapidated-shed/icu/blob/sdf-mailbox-reader/SDF_MAILBOX.md) calls itself a design stub and says it does not implement mail access.
- Its mbox section describes a future incremental parser, including separator and escaped-body handling, headers, MIME, and transfer encodings. It does not identify an existing parser source or provide parser code/tests.
- The same note says the active ICU implementation deliberately removed most of curl. The retained `old/` tree contains curl mail-protocol code such as `old/lib/imap.c`, `old/lib/pop3.c`, and `old/lib/mime.c`. This is curl's mail transport and MIME construction code, not an mbox framing or RFC message parser. It does not constitute the requested mailbox/message program.

The audited `icu` tree therefore cannot supply the original parser for translation.

### `one-room-schoolhouse/mailR`

This was examined as a possible source because it is the other mail-related repository identified in prior project context.

- Repository: [one-room-schoolhouse/mailR](https://github.com/one-room-schoolhouse/mailR), default branch `master`, head `d7dcdf3d65843bdcdfe0bbed969a60f858838061`.
- Its tree at that commit contains an R package whose implementation is `R/mailR.R`, documentation, `commons-email-1.3.3.jar`, and `javax.mail.jar`. The README describes sending email from R through SMTP.
- GitHub identifies its upstream as [rpremraj/mailR](https://github.com/rpremraj/mailR). The fork's audited master head is an upstream commit dated 2015-01-13. Comparing it with upstream master found the fork behind by 24 commits and no fork-only commits at that point.
- This is an email-sending wrapper with compiled dependency JARs. It has no retained mbox splitter source tree and does not provide the original standalone parser program requested here.

This candidate is unrelated to the required mbox-to-message implementation and is not used as a substitute.

## Provenance facts still missing

The current evidence does not establish:

1. the repository that contains the intended original mailbox/message program;
2. its authoritative upstream URL and exact upstream commit;
3. the exact copied source tree/commit and its complete file inventory;
4. local changes relative to that upstream source;
5. which features the user deliberately discarded from that program;
6. that the retained source corpus includes the implementation and original tests needed for a whole-hog translation.

The expected implementation itself is missing from the audited project trees. No exact original source commit or local diff can therefore be named, and no deliberate-discard list can be reconstructed from these candidates. This is a source-provenance blocker, not evidence that the original program never existed.

## Translation and acceptance state

- D translation: **NOT STARTED**.
- Ordinary DMD build: **NOT RUN**.
- Original tests / translated tests: **NOT AVAILABLE**.
- Synthetic byte-exact mbox fixtures: **NOT CREATED**.
- Real exported samples: **NOT TESTED**.
- Bounded SDF sample: **NOT TESTED**.
- Streaming acceptance receipt: **NOT PRODUCED**.
- SDF mailbox: **not accessed or changed**.

Do not begin translation from `mailR`, curl's mail protocol code, or a newly selected mail library. Resume only after the intended original source repository/tree and its discarded-feature history are located and the source corpus is verified complete.
