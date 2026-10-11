# libjstr 设计文档

## 这个库解决什么问题

JVM class 文件用 **Modified UTF-8（MUTF-8）** 存储所有字符串：
NUL（U+0000）编码为 `C0 80`，BMP 外的增补字符编码为「代理对」共 6 字节，
而非标准 UTF-8 的 4 字节。直接用 `std::string` 处理会在这两处与标准 UTF-8
混淆。libjstr 提供 MUTF-8 的**类型表达**与**与标准 UTF-8 的互转**，把
class 文件的字节世界和程序内其余部分（日志、文件系统、LLVM 交互）的标准
UTF-8 世界清晰隔开。

定位是**窄域库**：只做 MUTF-8 容器 + 转换校验，不含字符串池、分配器、UTF-16
等更大设想（早期草案有过，未落地，已从本库剔除）。

## 库的形态

静态库，命名空间 `jstr`，依赖 `libca_core`（`ca::usize`/`ca::u8` 等）
与 `libca_str`。两个模块：

- **`mutf8_string`** —— MUTF-8 字节容器：`MUtf8String`（拥有）/
  `MUtf8StringRef`（非拥有视图）。只持有字节，不校验不转码。
- **`conversion`** —— UTF-8 ⇄ MUTF-8 转换 + 校验，是两个编码世界的边界；另提供
  WTF-8（逐码元保留代理，成对代理不合并为 4 字节）与 Latin-1 辅助转换。

## 关键设计决策

### 错误处理用 Result，不抛异常

所有转换/校验入口返回 `ca::Result<T, EncodingError>`，即使输入畸形也是**返回
错误码而非抛异常**。`EncodingError` 带 `code`（区分截断/非法 UTF-8/非法 MUTF-8/
不支持码点等）和出错字节 `offset`。唯一可能抛的是 `MUtf8String` 构造时底层
`std::string` 的分配失败（已在头文件注明）。

### strict / lenient 两档策略

- **strict**（默认）：任何畸形序列都报精确错误，代理对不完整或非法组合一律拒绝。
- **lenient**：仅当 `allow_lossy_replacement=true` 时，把畸形序列替换为 `U+FFFD`
  后继续；该标志为 false 时 lenient 仍然拒绝畸形输入。strict 模式下该标志被忽略。
- 校验入口 `validate_utf8` / `validate_mutf8` 不做有损替换：两种模式都按 strict
  校验，`ConversionValidationMode` 参数仅为与转换入口 API 对称而保留。

每种策略既有「显式传 `ConversionOptions`」的入口，也有
`*_strict` / `*_lenient` 便捷封装。

### 拥有 vs 视图

`MUtf8StringRef` 是零拷贝视图（指针+长度），用于引用 class 文件缓冲区/内存映射
区域，**生命周期不得超过所指字节**。`MUtf8String` 拥有独立副本。两者都不做
校验——「这是不是合法 MUTF-8」是 conversion 模块的职责，容器只管存字节。

## 数据流（典型用法）

读 class：class 缓冲区字节 → `MUtf8StringRef` → `mutf8_to_utf8` → 程序内
标准 UTF-8。写 class：标准 UTF-8 → `utf8_to_mutf8` → `MUtf8String` → 写回缓冲区。
libjbytecode 的 `class_reader` / `byte_vector` 正是这样使用本库（常量池每条
UTF-8 项都走这条路）。

## API 去哪找

- MUTF-8 容器类型：`src/jstr/mutf8_string.hpp`
- 转换与校验、错误码、策略选项：`src/jstr/conversion.hpp`

每个接口的参数、返回、抛出与注意点见对应头文件的 Doxygen 注释。

## 测试

- `unittest/conversion_test.cpp`：核心转换（NUL→C0 80、增补字符代理对、
  strict/lenient、错误码、有损替换）。
- `unittest/validation_test.cpp`：`validate_utf8`/`validate_mutf8` 与
  `MUtf8String`/`MUtf8StringRef` 访问器。

核心转换另经 libjbytecode 全部测试在真实 class 文件读写中实战验证。
