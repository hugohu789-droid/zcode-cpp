# zcode-cpp 测试报告

- 日期：2026-10-21
- 被测版本：`ab2e456`（本地 `master`）
- 对照基准：east-mod/ime Java 原版 `cad6a24`，加上两处已裁定的 bug 修复（见 §4）

## 1. 结论

- **移植已完成。** 移植范围内的 Java 类全部有 C++ 对应（见 §2）。
- **输出逐字节一致。** 全部 golden 比对与 Java 原版（含两处修复）的输出完全相同，顺序也一致。
- **构建干净。** Release 和 AddressSanitizer 两套构建都是 17/17 通过、0 编译警告，ASan 没有报告任何内存错误。
- **golden 数据可复现。** 用当前 harness 重新运行 Java，生成的 8 份文件和已提交的逐字节相同。

## 2. 移植完整性

逐一核对了移植范围内的每个 Java 类在 C++ 源码或数据表中的位置。

| Java（`com.zorigt.ime`） | C++ / 数据 | 验证方式 |
|---|---|---|
| `letter/ZCode` | `data/zcode.tsv` | 52 个码位，范围检查 |
| `letter/base/*`（A..Z、5 个 Galig、NG）、`Letters`、`Letter`、`LetterShape`、`UnmodifiableLetterShape` | `data/shapes.tsv`、`src/tables.*` | 与 Java 反射导出的 129 个形逐行比对 |
| `letter/splice/front/*`（26 个）、`behind/*`（9 个）、两个 Holder、两个规则接口 | `data/rules.tsv`、`src/rules.*` | 与 Java 规则对象的真值表逐项比对 |
| `LetterLocation`、`Nature` | `src/types.h` | 间接验证（经由形状、规则和 golden 测试） |
| `LetterShapeSequence`、`NatureUtil` | `src/letter_shape_sequence.*` | 规则真值表中的阴阳性列 |
| `letter/splice/Verifier` | `src/verifier.*`、`src/splice_table.*` | 查表与 Verifier 在全部状态上穷举比对 |
| `letter/splice/LetterSplicer` | `src/letter_splicer.*` | golden 拼接数据 |
| `keyboard/KeyboardLayout` | `data/keyboard.tsv`、`src/keyboard_layout.*` | 与 Java 导出的 30 个键 × 3 个位置比对 |
| `keyboard/Decomposer` | `src/decomposer.*` | golden 分解数据 |
| `filter/KeyFilter`、`KeyFilterFactory`、`BTailKeyFilter` | `src/key_filter.*` | golden 过滤数据 |
| `common/Strings` | `src/strings.h` | 间接验证（空白输入的判断） |

**说明**
- **NG**：已作为数据移植。和原版一样，它不在 `KEYS` 里，因此不可达。
- **`common/State`**：只是 HTTP 错误码，按计划不移植。
- **范围外**（按任务要求不移植）：近似匹配（`approximate/matching/*`）、`word/` 和 `black/sequence/` 的数据库层、Spring 配置、控制器、`IMEApplication`。

## 3. 测试环境

| 项目 | 值 |
|---|---|
| 系统 | Windows 11 Pro 10.0.26200 |
| 编译器 | MSVC 17.14（VS 2022 Build Tools），C++20，`/W4 /permissive- /utf-8` |
| 构建 | CMake 4.2 + Ninja 1.13；GoogleTest 1.15.2（固定版本并校验 SHA-256） |
| Java 对照 | JDK 1.8.0_501（HotSpot）、Maven 3.10.0 |

## 4. 对照基准（golden 数据）

- golden 数据由 `tools/golden-java` 直接运行 Java 原版生成，不经过 Spring 和数据库。期望值全部来自 Java，没有手写。
- 默认构建在原版基础上打了两个已裁定的 bug 补丁（`tools/golden-java/src/patches/`）：
  1. `LetterSplicer`：候选全部被拒绝后不再重新开始拼接；
  2. `FrontLikeTailOrhichE`：`mMiddleHurbelge` 改为 `mMiddleAchvg`。
- `-Poriginal` 可以生成未修复版本的数据。修复前的 C++ 版本（`b708905`…`870f609`）就是对照这份未修复数据通过测试的。

| 文件 | 行数 | 内容 |
|---|---:|---|
| `java_shapes.tsv` | 129 | 全部形：键、位置、ZCode、4 条规则、相等类 |
| `java_keyboard.tsv` | 90 | 30 个键 × 3 个位置的形列表与顺序 |
| `java_rules_front.tsv` | 8,944 | 26 条 front 规则 × 所有可达状态 |
| `java_rules_behind.tsv` | 1,161 | 9 条 behind 规则 × 129 个形 |
| `golden_splice.tsv` | 61,904 | 30,952 组键序列 × 2 种模式，共 288,960 个候选 |
| `golden_decompose.tsv` | 659 | 拉丁输入的键分解 |
| `golden_e2e.tsv` | 1,318 | 659 个输入 × 2 种模式，端到端 |
| `golden_btail.tsv` | 12,922 | `b-tail` 过滤器的判定 |

**键序列来源**
- 长度 1–3 的全部组合：27,930 组；
- 随机生成、长度 4–8 的序列：3,000 组（固定随机种子）；
- 边界用例：22 组。

**端到端输入**
- 常见词的转写和加利克字母词；
- 边界用例：空输入、空白、未知字符、大写字母、非 ASCII、emoji、长重复串；
- 随机串：500 个。

**比较方式**：按候选数量加完整 ZCode 码位序列逐字比较，顺序也必须一致。只有 3 行 fuzzy 端到端数据（候选数 2,048 / 16,875 / 6,075）超过 2,000 个，这 3 行比较的是精确数量加 FNV-1a-64 哈希，不逐个列出候选。

## 5. 测试用例与结果

| 测试 | 检查内容 | Release | ASan |
|---|---|---:|---:|
| `Tables.ZCodeHas52Components` | 52 个 ZCode 都在 U+1865..U+18AA | 通过 | 通过 |
| `Tables.ShapesMatchJava` | 129 个形的全部字段和相等类与 Java 一致 | 通过 | 通过 |
| `Tables.KeyboardMatchesJava` | 键→形列表及顺序与 Java 一致 | 通过 | 通过 |
| `Rules.FrontMatchesJava` | 8,944 项 front 真值 | 通过 | 通过 |
| `Rules.BehindMatchesJava` | 1,161 项 behind 真值 | 通过 | 通过 |
| `Rules.EveryRuleIsCovered` | 35 条规则都至少被比对过一次 | 通过 | 通过 |
| `SpliceTable.AgreesWithVerifierOnEveryState` | 预计算表与 Verifier 在 388 个状态 × 129 个形 × 2 种模式上完全一致 | 通过 | 通过 |
| `Golden.Splice` | 61,904 行拼接结果 | 通过 | 通过 |
| `Golden.Decompose` | 659 行分解结果 | 通过 | 通过 |
| `Golden.EndToEnd` | 1,318 行端到端结果 | 通过 | 通过 |
| `Golden.EndToEndUtf8MatchesCodePoints` | UTF-8 输出与码位输出一致 | 通过 | 通过 |
| `Golden.BTailKeyFilter` | 12,922 项过滤判定；未注册的键一律接受 | 通过 | 通过 |
| `Session.MatchesSpliceAfterEveryPushAndPop` | 每次 push、pop、clear 后都与无状态拼接一致（超过 20 万次比对） | 通过 | 通过 |
| `Session.MatchesGoldenFinalCandidates` | 逐键输入的最终结果等于 golden | 通过 | 通过 |
| `Options.CountEqualsNumberOfCandidates` | `count()` 等于全部 golden 行的候选数 | 通过 | 通过 |
| `Options.MaxCandidatesKeepsThePrefixInOriginalOrder` | 上限 1/2/7/100 时结果是完整输出的前缀 | 通过 | 通过 |
| `Options.DeduplicateKeepsFirstOccurrences` | 去重等于稳定去重；与上限组合；Session | 通过 | 通过 |
| **合计** | | **17/17**（2.3 s） | **17/17**（269 s） |

另外，`ctest` 显示 17/17 通过；Release 和 ASan 两套 clean rebuild 都是 0 编译警告。

## 6. 测试有效性检查

- **变异测试**：从 `rules.tsv` 里删掉一个集合成员后，`Rules.FrontMatchesJava` 失败；格式错误的数据文件会被加载器拒绝。
- **Java 输出是确定的**：同一输入连续运行，输出逐字节相同。harness 重构前后，原版数据也逐字节相同。
- **修复的影响范围可核对**：两处 bug 修复各自只改变了预期中的那些行（30 行 + 1 行，以及 9 行），其他数据一行未变。

## 7. 性能（摘要）

详见 `docs/phase3-report.md`。以 Phase 2 的忠实移植为基线：

| 场景 | 加速 |
|---|---:|
| 全部键序列，严格模式 | 3.5× |
| 全部键序列，fuzzy | 4.1× |
| 逐前缀调用 `shape()`，fuzzy | 4.2× |
| 候选特别多的输入 | 5.0× |
| 逐键输入，`Session` 相对无状态拼接，严格模式 | 2.1× |
| 逐键输入，`Session` 相对无状态拼接，fuzzy | 1.3× |

两处 bug 修复之后，各项耗时没有变化。

## 8. 未覆盖或尚未验证的部分

- **平台**：只在 Windows + MSVC 上构建和测试过，本机没有 GCC/Clang，Linux/macOS 未验证。
- **Java 字段顺序**：依赖 HotSpot 的 `getDeclaredFields()` 返回声明顺序（JLS 并不保证）。数据已经按 HotSpot 的实测顺序固化在 `shapes.tsv` 中。
- **输入长度**：长度 ≤3 是穷举的；4–8 是抽样；更长的输入只覆盖了个别精选词和长重复串。
- **哈希比较**：3 行候选特别多的 fuzzy 数据只比较了数量和哈希，没有逐个比较候选。
- **未做的测试**：没有测量代码行覆盖率（本机没有覆盖率工具），也没有做模糊测试。
- **字体**：没有渲染测试，只核对了字体的 cmap 覆盖全部 52 个码位。
- **许可**：字体 Z52 Hara Tig 已按要求提交进仓库，但它的许可证仍未确认（见 `NOTICE.md`）。
- **保留的原版行为**（已裁定保留）：单键输入只给词首形；非词首的加利克字母 R/L/Z/C 会被跳过。
