# Contributing

Thanks for considering a contribution.

## Scope of this library

This is a **C++17 MUTF-8 boundary library** for JVM class files: typed byte
containers plus strict/lenient transcoding and validation between Modified UTF-8
and standard UTF-8. It is deliberately narrow — no string pool, custom allocator,
UTF-16 container, or general-purpose Unicode library.

If your change would require copying code from another project, it almost
certainly does not belong here.

## Before you start

- Open an issue first for anything non-trivial (API shape, new encoding mode,
  new auxiliary carrier). It is much cheaper to agree on a shape than to rework a
  finished implementation.
- Small bug fixes and documentation fixes can go straight to a PR.

## Build and test

Register the package repository once:

```sh
xmake repo --add luiox-repo https://github.com/luiox/luiox-repo.git
```

Then:

```sh
xmake f --with_tests=y
xmake build libjstr
xmake run libjstr_unittest
```

CI runs the same build and test target on Windows and Linux.

## Ground rules

- **Malformed input is the normal case.** Every byte sequence read from a class
  file is untrusted. Decoders must classify malformed input precisely (error code
  + byte offset) and must never read past the input. A new entry point needs
  tests for truncation, overlong forms, surrogate edge cases, and
  resynchronization after an error.
- **Keep the two encodings apart.** Public APIs take either `MUtf8StringRef` /
  `MUtf8String` or standard UTF-8 `std::string`; do not add a bare string
  parameter whose encoding is ambiguous.
- **Document behaviour at the header.** API detail belongs in Doxygen comments
  on the declaration; `doc/design.md` records design trade-offs, not API
  listings.
- **Add tests that fail without your change.** A test that passes before and
  after proves nothing.
- **Do not weaken an assertion to make a test pass.** If the assertion is wrong,
  fix it deliberately and say why in the commit message.

## Style

- Follow the surrounding file. 4-space indent, K&R braces, 120-column limit.
- Comments explain *why*, not *what*. The code already says what.
- Chinese comments with English technical terms are the norm in this repository.

## Commit messages

One logical change per commit. Describe the observable behaviour change; the
diff already shows the mechanics.

## License

By contributing you agree that your contribution is licensed under the MIT
License that covers this repository.
