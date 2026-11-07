# zcode-cpp 测试报告

- 日期：2026-11-08（初版 2026-10-21，当时只有拼接引擎）
- 被测版本：`b2bcab4`（`master`）
- 对照基准：east-mod/ime Java 原版 `cad6a24`，加上两处已裁定的 bug 修复（见 §4）；
  词典部分另外对照**运行中的** Java 服务（见 §9）

## 1. 结论

- **移植已完成，范围比初版大。** 除拼接引擎外，词典（近似匹配）一半也已移植，
  原先列在"范围外"的 `approximate/matching/*` 现在有 C++ 对应（见 §2）。
- **输出逐字节一致。** 全部 golden 比对与 Java 原版（含两处修复）的输出完全相同，顺序也一致。
- **与运行中的 Java 服务逐词表比对**：四个 HTTP 端点全部覆盖，610 个输入，
  没有一项差异解释不了 —— 要么是两处已裁定的修复，要么是同分候选的次序（见 §9）。
- **构建干净。** Release 构建 83/83 通过、0 编译警告。
- **golden 数据可复现。** 用当前 harness 重新运行 Java，生成的文件和已提交的逐字节相同。

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

### 2.1 词典一半（初版列为"范围外"，现已移植）

| Java（`com.zorigt.ime`） | C++ / 数据 | 验证方式 |
|---|---|---|
| `approximate/matching/SuggestWord` | `include/zcode/dictionary.h` | `toString()` 逐字符对齐，含 Java 的 float 格式 |
| `approximate/matching/ScoreMarker` | `src/dict/score_marker.h` | 六项指标与服务返回值逐项比对 |
| `approximate/matching/JaroWinklerDistance`（原版已大改） | `src/dict/distance.h` | 1,215 条返回值上验证"首码点必须相同"这一等价式 |
| `approximate/matching/Levenshtein` | `src/dict/distance.h` | 同上 |
| `approximate/matching/BurkhardKellerTree` | `src/dict/bk_tree.h` | 作为测试基准保留；trie 与它结果完全一致 |
| `InputMethodService.{severeMakeWord,fuzzyMakeWord,makeWord}` | `src/dict/ime.cpp` | 对照运行中的服务（§9） |
| `word` 表（`ime.sql`，`frequency > 44` 的行） | `data/dictionary.tsv`（38,743 词） | 与 MySQL 导出逐行比对，0 差异 |

### 2.2 原版没有的增补

这些在 Java 里没有对应物，所以没有"是否一致"可言，只有"是否改变了答案"：

| 增补 | 为什么 | 对答案的影响 |
|---|---|---|
| `Session`（增量拼接） | 每击一键只算一层 | 无：与无状态拼接逐次比对 |
| `dict/trie`（取代 BK 树） | BK 树在这个容差下退化成全树遍历 | 无：与 BK 树基准结果一致 |
| `service/`（stdio 行协议） | 让宿主跨进程驱动，避免 GPL 进入宿主链接 | 无 |
| 扩展词库（46 万词，可选） | 原版从不加载的那些行 | 仅在主词典为空时补充 |
| `UserWords`（用户词 + 时间衰减习惯） | 原版没有这个概念 | 用户库为空时无影响（有测试钉住） |
| 拼接序列去重 | 同一序列被查多次纯属浪费 | 无：重复查询产生的结果和第一次相同 |

**说明**
- **NG**：已作为数据移植。和原版一样，它不在 `KEYS` 里，因此不可达。
- **`common/State`**：只是 HTTP 错误码，按计划不移植。
- **仍然范围外**：Spring 配置、MyBatis/MySQL 访问层、HTTP 控制器、线程池、`IMEApplication`。

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
| `golden_dict.tsv` | 4,545 | 1,515 个输入 × 3 个词典端点，由**运行中的服务**经 HTTP 抓取（`tools/golden_dict.py`）——词典在 MySQL 里，没法像其余数据那样离线导出 |

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
| **小计（拼接引擎）** | | **17/17** | **17/17** |

词典一半与增补部分的测试（Release，0 编译警告）：

| 测试套 | 个数 | 检查内容 |
|---|---:|---|
| `Dictionary` | 8 | 嵌入词表加载；trie 与 BK 树基准结果一致；severe 等于拼接器；评分与原版一致；`makeWord` 的合并；Java float 格式；空白与未知输入；负缓存不改变结果 |
| `ProtocolTest` | 24 | 行协议：会话、`TYPE`/`UNTYPE`/`CAND`、错误请求不改变状态、控制字符拒绝、候选不含 TAB/换行 |
| `ProtocolDictionaryTest` | 17 | 五种模式；`full` 是"永不为空的 `score`"；`max` 截断留下最好的几条；无词典时拒绝且不动会话；`USER` 全部子命令；设置的读写与校验 |
| `UserWordsTest` | 17 | 习惯模型（饱和、衰减、时钟倒退）；匹配规则与词典一致；**空用户库逐分不改变答案**；学会的词上升而其余分数不变；忘记后复原 |
| **合计** | **83** | Release 83/83，7.8 s |

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

### 词典部分

索引换掉 BK 树之后，同样 60 次查询：**0.29 ms 对 237.57 ms，819×**，结果完全一致。

逐键输入整个词表（1,456 个输入，`TYPE ... full 27`，宿主超时预算 200 ms）：

| | 最慢的一次击键 |
|---|---:|
| 中位数 | **0.3 ms** |
| 真词里最慢（`jirguga`） | **16.2 ms** |
| 超过 20 ms 的输入 | 7 个（0.48%），**全部是一两个字母的重复串** |
| 全场最慢（`gggggg`，6 个 g） | 174 ms |

量这个数要当心：早先一版脚本用 `bufsize=0` 读管道，`readline()` 会逐字节读，
22 KB 的回包光在 Python 里就要 110 ms，当时把 `aga` 误报成 133 ms（实为 4.9 ms）。
`bench/dict_profile` 可以把一次击键拆成拼接、检索、评分三段，不必再写会骗人的脚本。

## 8. 与运行中的 Java 服务的对比

golden 文件来自直接运行 Java 类；这一节是另一回事——把**真的跑着的 Spring 服务**
（MySQL + `ime.jar`）和 C++ 放在一起，同一个输入各问一遍。这是 golden 测试覆盖不到的
部分：测试比的是 C++ 与 golden 文件，没有人比过服务与 C++。

`tools/web-compare` 是一个网页,同源代理掉服务的四个端点，逐候选对照：

| Java 端点 | 对照 | 一致 | 不一致 |
|---|---|---:|---:|
| `/ime/severe/{key}` | `SHAPE severe` | 604 | 1 |
| `/ime/fuzzy/{key}` | `CAND … word` | 532 | 73 |
| `/ime/fuzzy/score/{key}` | `CAND … score` | 532 | 73 |
| `/ime/{key}` | `CAND … make` | 535 | 70 |

**没有一项解释不了：**

- `severe` 那 1 项是 `eme`，两处已裁定修复之一（golden 对照是一致的，所以页面能自己
  判定出"只差在已知的原版 bug 上"，不依赖写死的输入清单）；
- 其余全部是**同分候选的次序**。原版把同分的词留在 HashMap 的桶序里；本移植按频率、
  再按码位排序，理由见 `README.md`。页面不是假定而是**证明**这一点：它向两边各要一份
  带评分的列表，比较词集合与分数序列，两者都相同才判为"只差次序"。

这组数字在后续每次改动后都重跑过（扩展词库、会话词典模式、`full` 模式、序列去重、
用户词），一个数没变过。用户词那部分还另有一条单元测试钉住"空用户库逐分不改变答案"，
因为这正是上面这张表成立的前提。

## 9. 未覆盖或尚未验证的部分

- **平台**：Windows + MSVC 上构建并全部测试通过。Linux（Ubuntu 24.04 / GCC）上
  **库和 `zcode_stdio` 可以构建并通过功能冒烟**（`HELLO`、`SHAPE`、`LOOKUP`、
  `TYPE ... full`、`USER`、`DICT` 都正常），但**测试套在 Linux 上编译不过**：
  固定版本的 GoogleTest 1.15.2 缺少 GCC 13+ 需要的 `<strings.h>`，上游在该版本之后才修。
  装一个系统 GoogleTest（构建会优先用 `find_package(GTest CONFIG)`）或抬高固定版本即可。
  macOS 未验证。
- **Java 字段顺序**：依赖 HotSpot 的 `getDeclaredFields()` 返回声明顺序（JLS 并不保证）。数据已经按 HotSpot 的实测顺序固化在 `shapes.tsv` 中。
- **输入长度**：长度 ≤3 是穷举的；4–8 是抽样；更长的输入只覆盖了个别精选词和长重复串。
- **哈希比较**：3 行候选特别多的 fuzzy 数据只比较了数量和哈希，没有逐个比较候选。
- **未做的测试**：没有测量代码行覆盖率（本机没有覆盖率工具），也没有做模糊测试。
- **ASan**：只对拼接引擎那 17 个测试跑过；词典与用户词部分新增的测试还没有在 ASan 下跑过一轮。
- **合并遍历**：一次击键最多要跑数千次独立 trie 查询（去重后）。已量过、也已决定暂不做——
  真词最慢 16.2 ms，离 200 ms 预算有 12 倍余量，只有重复键压力串吃得到这个优化。
- **字体**：没有渲染测试，只核对了字体的 cmap 覆盖全部 52 个码位。
- **许可**：字体 Z52 Hara Tig 已按要求提交进仓库，但它的许可证仍未确认（见 `NOTICE.md`）。
- **保留的原版行为**（已裁定保留）：单键输入只给词首形；非词首的加利克字母 R/L/Z/C 会被跳过。
