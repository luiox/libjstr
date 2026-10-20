# Security Policy

## Supported Versions

Security fixes land only in the newest published package version — the commit
pinned by the latest `libjstr` entry in
[luiox-repo](https://github.com/luiox/luiox-repo). Older versions are not
backported. The repository has no GitHub release tags; the package entry is the
version of record.

## Reporting a Vulnerability

Please report security issues privately rather than opening a public issue.

Use GitHub's private vulnerability reporting for this repository
(**Security → Report a vulnerability**), or contact the maintainer directly.

Please include:

- affected version (luiox-repo package version or commit)
- platform and toolchain (e.g. Windows/MSVC 2022, xmake version)
- a minimal reproducer if possible
- the impact you observed (crash, out-of-bounds read, unbounded allocation, infinite loop)

## Scope

libjstr processes byte sequences taken from untrusted `.class` files: MUTF-8
decoding, standard UTF-8 decoding, validation, and lossy replacement. Reports in
scope include:

- out-of-bounds reads at the end of a truncated lead or continuation sequence
- loops that fail to advance or advance past the input length
- unbounded allocation driven by input contents
- `validate_*` accepting an ill-formed sequence, or rejecting a well-formed one

Out of scope:

- callers letting an `MUtf8StringRef` outlive the bytes it borrows (documented
  view contract in `src/jstr/mutf8_string.hpp`)
- allocation failure (`std::bad_alloc` / `std::length_error`), which follows
  normal C++ container semantics
- business meaning carried by the input; this library validates the encoding
  layer only

These boundaries are described in `doc/design.md`; behaviour matching them is a
documented design decision, not a vulnerability.
