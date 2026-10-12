# libjstr

为 JVM class 文件的 **Modified UTF-8（MUTF-8）** 字符串提供类型表达，以及
MUTF-8 与标准 UTF-8 的互转与校验。是 class 文件字节世界与程序内标准 UTF-8
世界之间的编码边界。

设计、数据流与各模块职责见 [doc/design.md](doc/design.md)。

## 目录结构

- `src/jstr/mutf8_string.hpp/.cpp` —— MUTF-8 字节容器
  （`MUtf8String` 拥有 / `MUtf8StringRef` 视图）。
- `src/jstr/conversion.hpp/.cpp` —— UTF-8 ⇄ MUTF-8 转换 + 校验。
- `unittest/` —— 单元测试。

命名空间 `jstr`，依赖 libca（core/str 模块）。对外公开 `src`：

```cpp
#include "jstr/mutf8_string.hpp"
#include "jstr/conversion.hpp"
```

## 构建

xmake ≥ 3.0。libca 经 [luiox-repo](https://github.com/luiox/luiox-repo) git
直连包解析，gtest 走 xrepo。首次构建先注册包仓库：

```sh
xmake repo --add luiox-repo https://github.com/luiox/luiox-repo.git
xmake f --with_tests=y -y     # 配置（gtest 仅测试需要）
xmake build libjstr           # 库本体
xmake build libjstr_unittest  # 单元测试
xmake run libjstr_unittest    # 跑测试
```

Windows + MSVC 下已配置 `/utf-8` 编译选项。

## 去哪看技术细节

MUTF-8 边界、`Result` 错误模型、strict/lenient 策略、视图生命周期等关键约定
都在 [doc/design.md](doc/design.md)。

## License

MIT
