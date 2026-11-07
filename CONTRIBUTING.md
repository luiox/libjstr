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
  on the declaration; `doc/design.rst` records design trade-offs, not API
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

## 文档规范

三层按**权威来源**分工，扩展名即角色：

| 载体 | 层 | 写什么 | 谁能引用它 |
|---|---|---|---|
| `///` Doxygen | 微观 | 单个符号的契约：参数、返回、失败模式、不变量；单模块的「背景/定位」（`@defgroup` 组注） | 随代码移动 |
| `doc/*.rst` | 宏观 | 跨模块设计、数据模型、编码边界契约 | 代码/测试/构建脚本 |
| `*.md` | 人 + AI 方向 | README、CONTRIBUTING 等流程文档 | 只有人和 AI 读 |

理由：markdown 撑不住代码耦合的规格——无 label、无校验引用面，代码改动后引用静默腐烂。RST 白送可校验的引用面，规范才谈得上执行。

硬规则（`doc/check_refs.py` + Sphinx `-W` 在 CI 挡）：

1. 代码/测试/构建脚本只可引用 `doc/*.rst`，路径必须存在；出现 `.md` 设计引用即失败（流程文档白名单除外）。
2. 引用定位用 label，形式 `doc:<label>`；被引用的位置必须有 `.. _<label>:` 声明（不必每节都放），命名 `<doc-slug>[-<section-slug>]`。跨文档的 `§N` 一律换 `:ref:`，编号会随插入章节漂移。
3. RST 里「``X`` 头注」指向微观层：`X` 必须是某个 `@defgroup` 组名或头文件名。
4. RST 站点以 `sphinx-build -b html -E -W --keep-going doc doc/_build` 构建，警告即失败；本地验证先 `pip install "sphinx==9.1.1"`（与 CI 钉同版）。
5. 微观层以 `doxygen doc/Doxyfile` 构建，`WARN_AS_ERROR` 生效；`///` 注释是 API 契约的唯一来源，与代码同仓同改。

RST 写作约定（中文正文的坑）：表格一律 `.. list-table::`（禁 grid table，全角字形会破坏边框校验）；行内代码紧邻中文时在闭定界符后补转义空格（docutils 要求 end-string 后接空白或 ASCII 标点）；标题层级 `=` / `-` / `^`；跨仓出处带仓名标记（libjbytecode / libca），带标记视为 prose 不校验存在性。
