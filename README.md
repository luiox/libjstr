# libjstr

JVM class 文件的 **MUTF-8** 编码边界：类型化字符串容器，以及与标准 UTF-8 的互转/校验（C++17 / xmake）。

JVM 在 class 文件里用 **Modified UTF-8** 存储所有字符串：NUL 编码为 `C0 80`、增补字符编码为 6 字节代理对，
与标准 UTF-8 在这两处不同。libjstr 用类型把两个编码世界隔开——容器只管持有 MUTF-8 字节，
conversion 模块负责互转与校验，所有入口返回 `ca::Result`、不抛异常。

## 特性

- **类型化容器**：`MUtf8String`（拥有字节）/ `MUtf8StringRef`（零拷贝视图，适合引用 class 文件
  缓冲区）。只存字节，不校验不转码，杜绝裸 `std::string` 混用两种编码。
- **互转与校验**：`utf8_to_mutf8` / `mutf8_to_utf8` / `validate_utf8` / `validate_mutf8`；
  失败时返回带错误码与出错字节 `offset` 的 `EncodingError`，不抛异常。
- **strict / lenient 两档**：strict 对任何畸形序列报精确错误；lenient 仅在与
  `allow_lossy_replacement` 搭配时以 `U+FFFD` 有损续行，单独使用仍然拒绝；校验入口恒按 strict 执行。
- **边界语义完整**：NUL ↔ `C0 80`、增补字符 ↔ 6 字节代理对；overlong（UTF-8 与 MUTF-8 两侧）、
  裸 NUL、孤立/未配对代理一律拒绝；截断序列单独报 `kUnexpectedEnd`。
- **辅助载体**：WTF-8（逐码元保留代理，成对代理不合并为 4 字节）与 Latin-1（逐字节独立编码，
  永不失败），覆盖字节流密文等非 UTF-8 场景。

## 快速开始

消费方经 [luiox-repo](https://github.com/luiox/luiox-repo) 接入（xmake ≥ 3.0）：

```lua
add_repositories("luiox-repo https://github.com/luiox/luiox-repo.git")
add_requires("libjstr")

target("demo")
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages("libjstr")
```

最小用例（构造容器 + 双向转换，可直接编译运行）：

```cpp
#include <iostream>
#include <string>

#include "jstr/conversion.hpp"

int main()
{
    using namespace jstr;

    // 标准 UTF-8 → MUTF-8：strict 模式，任何畸形序列报精确错误，不抛异常
    auto encoded = utf8_to_mutf8("caf\xC3\xA9 \xF0\x9F\x98\x80");   // "café 😀"
    if (encoded.is_err()) return 1;
    const MUtf8String mutf8 = encoded.unwrap();   // 拥有 MUTF-8 字节的容器

    // MUTF-8 → 标准 UTF-8：view() 给出零拷贝视图，再解码回文本世界
    auto text = mutf8_to_utf8(mutf8.view());
    if (text.is_err()) return 1;
    std::cout << text.unwrap() << "\n";

    // 也可以直接持有 class 文件里读出的 MUTF-8 字节（容器只存字节，不校验）
    MUtf8String from_class(std::string("A\xC0\x80"
                                       "B",
                                       4));
    return validate_mutf8(from_class.view()).is_ok() ? 0 : 1;
}
```

接口的参数、返回与注意点以 `src/jstr/conversion.hpp` 的 Doxygen 注释为准。

## 构建与测试

本仓独立构建（libca 经 luiox-repo git 直连解析；gtest 走 xrepo，仅测试需要）：

```sh
xmake repo --add luiox-repo https://github.com/luiox/luiox-repo.git
xmake f --with_tests=y -y     # 配置
xmake build libjstr           # 库本体
xmake build libjstr_unittest  # 单元测试
xmake run libjstr_unittest    # 跑测试
```

Windows + MSVC 下已配置 `/utf-8` 编译选项。

## 模块速览

| 模块 | 职责 |
|------|------|
| `mutf8_string` | MUTF-8 字节容器：`MUtf8String`（拥有）/ `MUtf8StringRef`（视图），只存字节 |
| `conversion`   | UTF-8 ⇄ MUTF-8 转换 + 校验，WTF-8 / Latin-1 辅助载体，错误码与策略选项 |

源码在 `src/jstr/`，单元测试在 `unittest/`。

## 依赖

- [libca](https://github.com/luiox/libca)（luiox-repo git 直连包，版本锁 0.0.16）：用 `core`
  （`ca::Result`、`ca::usize` 等）与 `str`（字节级 UTF-8 工具）模块；升级须显式换版。
- gtest：仅 `--with_tests=y` 时拉取，不随库分发给消费方。

## 状态与 License

开发中，API 尚未定版，不承诺兼容性。以 [MIT](LICENSE) 协议发布。

## 深入阅读

MUTF-8 边界、`Result` 错误模型、strict/lenient 策略、视图生命周期等设计取舍见
[doc/design.md](doc/design.md)。
