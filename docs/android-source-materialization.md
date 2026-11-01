# Materialize the pinned TLS source independently

The Android build now exports exactly the tracked OpenSSL tree at
`f4dc4d58b48d346a8270183f89acf826d459b0ca` into each new ABI build directory.
It does not copy a configured working directory or a source-directory symlink.

The previous `cp -a` recipe preserved a symlink supplied as OPENSSL_SOURCE.
Configure and recursive make then wrote through that link into the original
source directory. A subsequent ARM32 link correctly rejected a retained x86_64
`libdefault-lib-pbkdf2_fips.o`. Tracked-only cleanliness did not detect that
ignored object. The failed archive and NDK linker diagnostic were retained.

`git archive` also excludes ignored objects and configuration from a real
directory. Every ABI receives its own directory and configuration while the
existing exact-ref/clean-tracked-source checks remain. Native sources, TLS
validation and link checks are unchanged. A fresh three-ABI build is required;
the failed mixed-architecture build is not a qualified candidate.
