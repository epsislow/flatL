# Bundled Lexbor subset

This directory contains the Lexbor v3.0.0 amalgamated source used by FlatL's optional
`dom` native module.

- `lexbor.c` is generated from upstream modules `html` and `selectors`, including
  their transitive module dependencies.
- The amalgamation was generated with upstream `single.pl --port=all html selectors`.
  Lexbor's generated source includes the POSIX and Windows platform branches.
- `LICENSE` is the upstream Apache-2.0 license.
- `version` identifies the upstream release.

The file is compiled by default (`dom_compile=1`). Pass `dom_compile=0` (or `dom=0`)
to leave it out of the FlatL build. To regenerate it, obtain the matching upstream
source release and run the command above from its root directory.
