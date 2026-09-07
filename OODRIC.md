# ICU on Oodriç

This branch is a real-program probe for the `isomorphisms/Idric` `Oodriç` compiler branch.

The rewrite rule is deliberately stronger than "make the old code compile": source should be arranged in the order that best explains the program. A reader should meet the useful action first and implementation mechanism later.

## First rewrite

`src/Main.idric` now reads:

1. `main`
2. `run`
3. command parsing
4. failure handling
5. usage text

`src/Transport.idric` now reads:

1. `send_request`
2. typed transport selection
3. the foreign declarations used to reach the inherited native transport

That ordering intentionally creates ordinary forward references. Compiling this branch is therefore an integration test of Oodriç's two-pass declaration/body experiment rather than another tiny synthetic example.

## Total-rewrite boundary

The target is a total ICU rewrite in Oodriç. The inherited `native/transport.c` is retained for now as an executable behavior oracle and bootstrap boundary; it is not considered part of the finished rewrite. The branch should progressively move socket, HTTP response framing, redirect handling, and TLS-facing behavior behind ordinary purpose-named Oodriç code, deleting native implementation only when equivalent acceptance is executable.

Do not weaken the existing behavioral boundaries while doing that:

- only HTTP and HTTPS
- only GET and POST
- binary-safe response bodies
- bounded response headers
- explicit redirect policy
- verified TLS for HTTPS
- no curl fallback

A green compile proves only that Oodriç can elaborate and compile this program shape. Network behavior still needs the existing ICU acceptance cases.

## Compiler pin

The first probe pins Oodriç at `b7ac1eea7adca7c44a68d465a24a55c8e0830c4c`, the head that added executable Oodriç probes on 2026-09-07.
