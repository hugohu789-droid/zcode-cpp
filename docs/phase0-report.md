# Phase 0 调查报告：east-mod/ime 字形拼接引擎

调查对象：`ime-java` 检出，HEAD `cad6a24`（2022-06-09，作者 zorigt），远端 `https://github.com/east-mod/ime.git`，许可证 GPL-2.0（`LICENSE` 为 GPLv2 全文）。
本报告全部结论来自阅读源码与对 `ime.sql` 的流式统计；**尚未运行任何 Java 代码**，凡是依赖运行时行为的结论都单独标注。

---

## 0. 三个与任务前提不符的事实（需要你先决定）

| # | 任务书的前提 | 实际情况 |
|---|---|---|
| A | ZCode 位于 Unicode 私用区（PUA） | **不是。** ZCode 用的是 Unicode 蒙古文区块 U+1865–U+18AA 里**已分配**的码位（Sibe / Manchu / Ali Gali 字母），见 §1。 |
| B | 仓库自带一个自由许可的形码字体 | （已更新，见文末附录）**原仓库里没有字体。** `ime.tar.gz` 只有一个文件 `ime.sql`；整个 git 历史里也从未出现过任何 `.ttf/.otf/.woff`。见 §2。 |
| C | Phase 1 的 harness 建在 `ime-java` 里 | 和"`ime-java` 只读"冲突。另外本机 `~/.m2` 是空的，编译原版需要先从 Maven Central 下载 lombok / guava / commons-collections / slf4j。见 §7。 |

---

## 1. ZCode

### 1.1 码位与存储类型
- 定义在 `letter/ZCode.java`：52 个 `public static final char` 常量，Java 存储类型是 **`char`（UTF-16 码元）**。字形以 `String` 存储（`LetterShape.shape`），由 1–3 个这样的 `char` 拼成（`Strings.valueOf(char...)`）。
- 码位全部在 BMP，范围 **U+1865–U+18AA**，分两段：
  - U+1865–U+1878（缺 U+1869）
  - U+1887–U+18AA（缺 U+1892、U+1895、U+18A9）
- 作者跳过了 U+1879–U+187F（未分配）、U+1880–U+1886（其中 U+1885/1886 是 Mn 组合符）和 U+18A9（Mn），说明是有意避开组合字符，**但其余码位都是已分配的 `Lo` 字母**。例如（Unicode 15.1 名称）：
  - `titem` U+1865 = MONGOLIAN LETTER SIBE HA
  - `achvg` U+186A = MONGOLIAN LETTER SIBE JA
  - `agsilga` U+1887 = MONGOLIAN LETTER ALI GALI A
  - `galig_r_malgai` U+18AA = MONGOLIAN LETTER MANCHU ALI GALI LHA
- 词表实测：`word` 表 499,925 行里恰好出现 **51 个不同码位**，全部是 ZCode 常量；第 52 个常量 `silvvn` U+186E 没有任何字母用到，词表里也没有。

**后果**：任何遵循 Unicode 的软件（HarfBuzz/Uniscribe/CoreText 的蒙古文整形、规范化、断行、排序、输入框）都会把这些码位当作锡伯/满文/阿里嘎里字母处理。下游如果真是"PUA 原生"，ZCode 不能原样交给它。可选方案（**由你决定**）：
1. 原样输出（Mongolian-block ZCode），下游自己把它当不透明的形码处理；
2. 在库的输出边界做一个**双射重映射**到 PUA（例如 `U+E000 + 序号`，52 个码位一一对应，无损、可逆）；
3. 两种都提供，由 API 参数选择。

### 1.2 一个 ZCode 值代表什么
既不是抽象字母，也不是完整的呈现形（presentation form），而是**笔画部件（字形组件）**：`titem` 字冠、`achvg`、`silbi` 牙、`gedes` 肚、`orhich` 尾、`suul1` 尾、`b_banbvr_*`、`p_chihi_*` 等。一个字母的一个位置变体（`LetterShape`）由 1–3 个部件组成，例如：
- `a` 词首 = `titem achvg`
- `u` 词首 = `titem gedes silbi`
- `i` 词尾 = `silbi suul1`

一个词的 ZCode 串就是各字母部件的直接拼接。

### 1.3 ZCode → Unicode 映射
**代码、数据库、仓库里都没有**任何 ZCode→Unicode 表（也没有字体，所以不存在 cmap）。我不推测这张表。

唯一能做的是**反推到拉丁键**：`(latinKey, zcode, location)` 三元组表就在字母类里。但这个方向本质上是**多对一、有歧义**的：
- `achvg` 同时是 `a`/`e`/`n` 的词中形；
- `gedes` 是 `o`/`u` 的词中形；
- `t_senj` 是 `t` 词首形，也是 `d` 词首形；
- `b_banbvr_baga` 是 `b` 的形，也是 `o`/`u` 的词尾形。

另外，部件之间没有分隔符，一个串怎样切分成字母本身也可能有多种。

---

## 2. `ime.tar.gz` 清单

| 文件 | 大小 | 说明 |
|---|---|---|
| `ime.tar.gz` | 6,062,625 B | sha256 `531b43ff…5e5b`。历史上两次提交，内容相同（都是同一个 `ime.sql`） |
| └ `ime.sql` | 39,198,388 B | sha256 `064a564b…cdb6`，2021-07-02，Navicat 导出的 MySQL 5.7 dump，UTF-8 |

- **字体：不存在。** 名称和许可证无从确认，`NOTICE.md` 里无法记录。如果你手上另有字体，请告诉我它的来源。
- **数据来源待确认（按约束 4，在你批准前不提交）**：`ime.sql` 词表随 GPL-2.0 仓库发布，但语料出处和收集方式在仓库里没有任何说明。

### 2.1 表结构（只看结构 + 流式统计，未整表载入）

```sql
CREATE TABLE `word` (
  `id` int unsigned AUTO_INCREMENT PRIMARY KEY,
  `str` varchar(255) NOT NULL,          -- ZCode 串
  `length` int unsigned NOT NULL,       -- = str 的 UTF-16 长度（实测 499,925 行全部一致）
  `frequency` int unsigned NOT NULL,
  KEY (`length`,`frequency`), KEY (`frequency`)
);
CREATE TABLE `black_sequence` (
  `id` int unsigned AUTO_INCREMENT PRIMARY KEY,
  `str` varchar(100) NOT NULL
);
```

**`word` 表**
- 499,925 行，`str` 没有重复，没有引号或转义；
- 长度 1–72；
- 频率最小 1、中位数 2、最大 2,687,038；
- 服务端只加载 `frequency > 44` 的 **38,743 行**（`FREQUENCY_BOUND`），按长度分桶建 BK 树。

**`black_sequence` 表**
- dump 里是**空表**（只有结构）。
- 它是运行时缓存：模糊拼接产生、但在 BK 树里查不到的 ZCode 串，每 5 秒写回数据库。属于派生数据，不是源数据。

### 2.2 建议的纯文本格式
- `data/words.tsv`：UTF-8、LF；以 `#` 开头的注释行写来源与许可；每行 `zcode<TAB>frequency`。
  - `id` 和 `length` 都可以推导，丢弃。
  - 按 frequency 降序，再按码位序排，保证输出确定。
  - ZCode 列原样写字符（不写转义），便于 grep/diff。
  - 估计约 20 MB 原文 / 约 6 MB gzip。可以只提交 `>44` 的子集（约 2 MB），或放进 git LFS，或干脆不放进本库。本库不做词典匹配（模糊搜索在下游），词表只是给下游的参考数据。
- `black_sequence`：**不建议移植成文件**。它是派生缓存，dump 里也是空的。

---

## 3. 拼接模型

### 3.1 各类型如何组合
- **`LetterShape`** = `(latinKey, shape:String, location:byte)` 加上 4 个规则引用：`fuzzyFront`、`fuzzyBehind`、`severeFront`、`severeBehind`，可以为 `null`，`null` 表示"不限制"。
  - `equals`/`hashCode` **只比较 (latinKey, shape, location)**，不比较规则。
- **`LetterLocation`**：`HEAD=0`、`MIDDLE=1`、`TAIL=2`，即词首、词中、词尾。没有"独立形"。
- **`Nature`**：`CHAGH`（阳性）、`HUNDII`（阴性）、`SAARMAG`（中性）。
  - `NatureUtil`：`a`/`o` → CHAGH，`e`/`u` → HUNDII，其余（包括 `i`）→ SAARMAG。
- **`LetterShapeSequence`**：`List<LetterShape>` 加一个 `nature`。初始是 SAARMAG；每 append 一个形，如果它的 nature 不是 SAARMAG，就覆盖。因此 **nature = 最后一个非中性元音的阴阳性**，用于元音和谐。
- **`Letter` 子类**（A..Z、Galig*、NG）是**纯数据**：每个字段一个 `LetterShape`，构造后包成 `UnmodifiableLetterShape`。
- **`KeyboardLayout.get(key, loc)`**：用**反射** `getDeclaredFields()` 列出该字母的全部 `LetterShape` 字段，筛出指定位置的形。
  - **输出顺序依赖字段声明顺序。** JLS 不保证这个顺序；HotSpot 实际返回源码声明顺序。
  - 例子：`I` 的声明顺序是 `iHead, iiHead, iMiddle, iiMiddle, iTail, iTailSilbiSuul1`，和构造顺序不同。golden harness 必须记录实测顺序。

字形数量：可达的形共 127 个，加上 NG 的 2 个一共 129 个；按 `equals` 去重后可达的是 **126 类**（见 §3.4 第 1 条）。

### 3.2 `fuzzy()` 与 `severe()`
两者是同一个逐层笛卡尔扩展循环，区别只有两点：

| | 位置 0 | 位置 1..n-2 | 最后位置 n-1 | 使用的规则 |
|---|---|---|---|---|
| `fuzzy` | HEAD | MIDDLE | **MIDDLE + TAIL**（先 MIDDLE 后 TAIL） | `fuzzy*` 规则（很宽松） |
| `severe` | HEAD | MIDDLE | **TAIL** | `severe*` 规则（完整正字法） |

- 只有一个键时，`i==0` 分支先命中，**只取 HEAD**，两种模式都一样。
- `fuzzy` 让最后一个字母也可以是词中形，为的是把"正在输入的前缀"拿去 BK 树里模糊匹配。它的规则基本只有：元音 `FRONT_EMPTY`/`FRONT_NOT_EMPTY`，个别辅音 `BEHIND_IS_AE_CHACHILGA`。所以候选数量会成倍增长。
- 每层的循环是**外层遍历新形、内层遍历已有序列**，输出顺序由此确定，移植必须保持。
- 某个键在某位置没有任何形时，`continue` **跳过这个键**（计数 `i` 照样递增）。

### 3.3 `Verifier`

```
canXSplicing(lss, ls):
  if ls.XFront != null && !ls.XFront.match(lss)    → false   // 新形对已有序列的要求
  if lss 非空 && last.XBehind != null              → return last.XBehind.match(ls)  // 末形对新形的要求
  → true
```

- **behind 规则只在后面还有字母时才被调用，而且参数从来不是 `null`。**
  - `BEHIND_EMPTY`（`ls == null`）实际上等于"后面不许再接任何形"。
  - `BEHIND_NOT_EMPTY`（`ls != null`）**永远为真**。
  - 结果：词尾时，"末形要求后面必须还有字母"这条约束**从来不会被检查**。
- 前规则只依赖序列的**末形**和 **nature**；后规则只依赖**末形**和**新形**。（这一点是 §6 优化的基础。）

### 3.4 看起来像 bug 或可疑的地方（不会私自"修"，请你裁定）
1. **G 的两个形相等**：`gMiddleDoubleAchvg` 和 `gMiddleDoubleAchvgDagbvr` 都是 `("g", achvg achvg, MIDDLE)`，按 `equals` 相等，但规则不同。
   - 后果一：凡是集合规则里列了其中一个（如 `FrontLikeMiddleE`/`I`/`GedesU`/`BanbvrBagaU` 的排除集），另一个也一起被命中。
   - 后果二：两者都满足时，会输出**重复的 ZCode 串**。
   - 移植会忠实保留值相等语义。
2. **单个键只出 HEAD 形**：`severe("a")` 返回的是词首形 `titem achvg`，不是独立形或词尾形。加上 §3.3 里末端不检查 behind 的问题，单字母词的形可能不对。
3. **加利克字母 R/L/Z/C 只有 HEAD 形，H 没有 TAIL 形**。它们出现在非词首位置时被 `continue` **静默丢弃**，例如 `"aR"` 的 severe 结果就只剩 `a` 的词首形。
4. **`FrontLikeTailOrhichE` 里写的是 `mMiddleHurbelge`，A 版本里是 `mMiddleAchvg`。** 而 `mMiddleHurbelge` 的 severe behind 规则是 `BEHIND_IS_AE_CHACHILGA`，只允许后接 a/e 的 chachilga 尾，所以 `m + e(orhich)` 在 severe 下**永远生成不出来**。很像笔误。
5. `FrontLikeTailOrhichE` 没有 `tMiddleTSenj`，A 版本有。可能是有意的，请确认。
6. `oMiddleBanbvrBaga` 的 `fuzzyFront` 是 `null`，其他词中元音都是 `FRONT_NOT_EMPTY`。经过 Decomposer 的正常输入下不影响结果，但直接调用 splicer 时会有差别。
7. `NG` 已 `@Deprecated`，而且不在 `KEYS` 里，**不可达**。建议：作为数据保留，但不挂到任何键上；或者干脆不移植。
8. 大写键只有 `H R L Z C`（加利克字母），其余大写字母被当作未知字符丢掉。下游**不能**把输入统一转小写。
9. Java 端规则类的 `init()` 是懒加载、没有同步。这是为了绕开静态初始化环（Letters → 规则 → Letters）。在 Java 里有线程安全问题；C++ 里用不可变表，这个问题自然消失。

---

## 4. 规则类：行为还是数据？

**结论：全部是数据。** 26 个 front 和 9 个 behind 类（加两个 Holder）可以完全归约成下面几种谓词。下表中"集合成员"一律按 §3.1 的值相等判断。

| 种类 | 语义（`last` = 序列末形，`next` = 新形） | 用到它的类 |
|---|---|---|
| `front_empty` | 序列为空 | FrontEmpty |
| `front_not_empty` | 序列非空 | FrontNotEmpty |
| `last_in S` | 非空 且 last ∈ S | FrontLikeTail{OrhichA,OrhichE,ChachilgaA,ChachilgaE,VrvgsilgaA,VrvgsilgaE,GedesO,GedesU,SilbiSuul1I}、FrontLikeMiddleII、FrontIsMiddleSD |
| `last_not_in S` | 非空 且 last ∉ S | FrontLikeMiddle{A,E,I,O}、FrontLikeTailBanbvrBaga(U)、FrontIsNotMiddleSD、**FrontLikeTailHvmsI**（= `last_not_in` SilbiSuul1I 的集合） |
| `last_at L, not_in S` | 非空 且 last.location==L 且 last ∉ S | FrontLikeMiddleGedesSilbiU（L=HEAD）、FrontLikeMiddleGedesU（L=MIDDLE）、FrontTailGedesSuul1U（L=HEAD） |
| `nature_is N` / `nature_is_not N` | 非空 且 nature ==/!= N | FrontIsChigaSequence / FrontIsNotChigaSequence（N=CHAGH） |
| `behind_empty` | next 为空（实际永远假） | BehindEmpty |
| `behind_not_empty` | next 非空（实际永远真） | BehindNotEmpty |
| `next_in S` | next ∈ S | BehindIsAChachilga、BehindIsAEChachilga、BehindLikeMiddleBanbvrBagaO、BehindLikeHeadBanbvrIhB |
| `next_key_in K` / `next_key_not_in K` | next.latinKey ∈/∉ K | BehindIsChingaEhshig（{a,o}）、BehindIsEhshig（{a,e,i,o,u}）/ BehindIsNotEhshig |

### 建议的数据表（`data/` 下的纯文本，构建时嵌入）

`data/shapes.tsv`：每行一个形。**行的顺序就是 KeyboardLayout 的输出顺序**，由 harness 实测后固定下来。

```
# id                 key  loc     zcode                  fuzzy_front      fuzzy_behind            severe_front                severe_behind
a.head               a    HEAD    titem achvg            FRONT_EMPTY      -                       FRONT_EMPTY                 BEHIND_NOT_EMPTY
a.middle             a    MIDDLE  achvg                  FRONT_NOT_EMPTY  -                       FRONT_LIKE_MIDDLE_A         BEHIND_NOT_EMPTY
a.tail.orhich        a    TAIL    orhich                 FRONT_NOT_EMPTY  BEHIND_EMPTY            FRONT_LIKE_TAIL_ORHICH_A    BEHIND_EMPTY
g.middle.dvsaltai    g    MIDDLE  dvsaltai_agsilga       -                BEHIND_IS_AE_CHACHILGA  FRONT_IS_NOT_MIDDLE_SD      BEHIND_IS_A_CHACHILGA
o.middle.banbvr_baga o    MIDDLE  b_banbvr_baga          -                BEHIND_LIKE_MIDDLE_BANBVR_BAGA_O  FRONT_LIKE_TAIL_BANBVR_BAGA  BEHIND_LIKE_MIDDLE_BANBVR_BAGA_O
```

`data/rules.tsv`：

```
# name                          kind               args
FRONT_EMPTY                     front_empty
FRONT_LIKE_MIDDLE_A             last_not_in        b.head.banbvr_ih b.middle.banbvr_ih p.head.chihi_ih p.middle.chihi_ih h.head.hunhur ...
FRONT_LIKE_MIDDLE_GEDES_SILBI_U last_at_not_in     HEAD  b.head.banbvr_baga p.head.chihi_baga h.head.hanhar ...
FRONT_IS_CHIGA_SEQUENCE         nature_is          CHAGH
FRONT_LIKE_TAIL_HVMS_I          last_not_in        @FRONT_LIKE_TAIL_SILBI_SUUL_1_I   # 引用另一条规则的集合
BEHIND_IS_EHSHIG                next_key_in        a e i o u
BEHIND_IS_NOT_EHSHIG            next_key_not_in    a e i o u
```

`data/zcode.tsv`：部件名和码位，例如 `titem<TAB>U+1865`。

`data/keys.tsv`：键和字母的对应，例如 `H<TAB>galig_h`。

加载后：每个形得到一个稠密 `ShapeId`（uint8），每个相等类得到一个 `ShapeClassId`，集合变成 128 位 bitset。

---

## 5. 依赖图与移植顺序

```
ZCode   LetterLocation   Nature   Strings          (叶子)
   \          |            |         |
    \    LetterShape <───> SplicingAt{Front,Behind}Rule   (Java 里是环)
     \        |                    ^
      \  UnmodifiableLetterShape    |
       \      |                     |
        base/* (A..Z, Galig*, NG) ──> {Front,Behind}RuleHolder ──> front/*, behind/* ──> Letters (懒加载，又一个环)
              |                                                        |
           Letters <───────────────────────────────────────────────────┘
              |
   NatureUtil (→ Letters.A/E/O/U.latinKey)    LetterShapeSequence (→ NatureUtil)
              |
        KeyboardLayout ──> Decomposer
              |
          Verifier ──> LetterSplicer
   filter/* (→ ZCode，独立)
```

Java 里有两个环：形 ↔ 规则接口、Letters ↔ 规则。C++ 里规则变成"表里的 id 引用"，两个环都消失。

建议的移植顺序（每一步都能单测）：
1. `zcode`、`location`、`nature` 基本类型
2. 形表与加载器（`LetterShape` + `base/*` + `Letters`）
3. 规则表与求值器（`front/*`、`behind/*`、两个 Holder）
4. `LetterShapeSequence` + `NatureUtil`
5. `Verifier`
6. `KeyboardLayout`（键 → 有序形列表）
7. `LetterSplicer::fuzzy` / `LetterSplicer::severe`
8. `Decomposer`
9. `KeyFilter` / `BTailKeyFilter`
10. 公共 API 与 UTF-8 边界

**`common/Strings`**：只用到 `isBlank`（`trim` 语义，即去掉 ≤U+0020 的字符）和 `valueOf(char...)`，以内联函数形式移植。

**`common/State`**：完全不需要（只有 HTTP 错误码）。

**`BTailKeyFilter`**：作用对象是**词典候选词**，不是拼接结果。仅在 fuzzy 路径、且最后一个键是 `b` 时使用：拒绝"恰好只有最后一个字符是 `b_banbvr_baga`/`b_banbvr_ih`"的候选。本库不做词典匹配，所以把它移植成纯函数 `accept(lastKey, zcode)`，交给下游调用。

---

## 6. 优化机会（Phase 3，每项单独提交）

> 所有收益数字都要在 Phase 3 实测；下面只给预期，不是测量结果。

| # | 内容 | 为什么安全 | 预期收益 | 风险 |
|---|---|---|---|---|
| 1 | **用父指针 arena 取代"每次扩展都整串复制"**：Java 每生成一个候选就 `new LetterShapeSequence()` 再逐个 re-append 整个前缀（同时重算 nature）。改成每层一个扁平数组，节点存 `{parent, shapeId, nature}`。 | 规则只看末形和 nature（§3.3），前缀内容永远不再被读取 | 每次扩展从 O(n) 降到 O(1)，总量从 O(n·N) 降到 O(N)；分配次数从 N 次降到每层 1 次 | 低：输出顺序由同样的循环决定 |
| 2 | **把 Verifier 预计算成转移表**：`front_ok[nextShape][lastShape or EMPTY][nature]`、`behind_ok[lastShape][nextShape]`，fuzzy/severe 各一份，约 127×128×3 位 + 127×127 位，几 KB | 规则是纯函数，输入就是这几个变量 | 每次检查变成 1~2 次数组查表；去掉 Java `Objects.hash` 每次分配的可变参数数组和 HashSet 探测 | 低：可以用 golden 数据和"表 vs 解释器"穷举对比来验证 |
| 3 | **KeyboardLayout 预计算**：Java 每个键、每个位置都调用一次反射 `getDeclaredFields()` 并新建 List。改成 `key → loc → span<ShapeId>` 静态表 | 数据不可变 | 去掉每键每位置的反射和分配 | 无（顺序以实测为准） |
| 4 | **去掉逐次拒绝时的日志**：`logger.info("ignored:{},{}", clss.toString(), …)` 里的 `toString()` 在判断日志级别之前就已求值，每次拒绝都要构建整串 | 不影响输出 | 拒绝路径上的字符串构建全部消失（在 Java 里是主要开销之一） | 无 |
| 5 | **Decomposer**：当前所有键都是单字符，trie 退化成"保留在键集里的字符、丢弃其余"，而且**永远只产生 1 种分解**。可以改成 128 项查表，同时保留通用 trie 以支持多字符键 | 键集不变 | 去掉 `Map<Character,Node>` 装箱、递归里的 `substring` 和 `LinkedList` 复制 | 低：如果将来启用 NG 这类多字符键，需要保留 trie 路径 |
| 6 | **只在输出边界生成串**：内部始终用 ShapeId；最后一次性把 arena 路径展开成 `std::u32string` 或 UTF-8，并预留好长度 | 纯表示层变换 | 消除中间的 `StringBuilder` 和 `toString` | 无 |
| 7 | **逐键增量**（对 IME 最关键）：位置 0..n-2 的候选层只依赖前缀（位置 0 用 HEAD、中间用 MIDDLE），只有最后一层取决于"它是不是最后一个"。会话对象缓存"截至 n-1 的 MIDDLE 前沿"，每输入一个键只需 (a) 把前沿推进一层 MIDDLE，(b) 另算一层 TAIL（或 MIDDLE+TAIL）作为输出 | 和整串重算等价：长度≥2 时各位置使用的形集合完全一样；长度 1 时单独处理 | 每键代价从 O(总候选) 降到 O(一层) | 中：需要一个显式会话对象（不是全局变量）；退格要能弹出一层 |
| 8 | **候选计数与上限（行为变化，需你同意）**：fuzzy 的候选数在最坏情况下等于每个位置可选形数的乘积（例如 G 的词中形有 7 个），会指数增长。用转移表可以先 DP 算出总数而不必真正展开，再提供可选的 `max_candidates` 或惰性迭代器 | 默认关闭时行为不变 | 防止长输入在内存和时间上爆炸 | 打开上限后与 Java 输出不一致，必须明确标注 |
| 9 | **去重（行为变化，需你同意）**：§3.4 第 1 条的 G 相等对，以及不同字母产生相同 ZCode 串（如 `t`/`d` 词首都是 `t_senj`），都会导致输出里有重复串 | 默认关闭 | 下游少处理一些重复 | 与 Java 输出不一致 |

Java 写法在 C++ 里的对应处理：装箱的 `Character` 和单字符 `String` 键改用 `char`/`uint8_t`；`LinkedList` 改用 `std::vector`；`newUnmodifiable()` 防御性复制改用 `const` 表；`Objects.hash` 的可变参数分配改为预先算好的 id。

---

## 7. Phase 1（golden 数据）的可行性与需要你决定的事项

- 本机有 JDK 1.8.0_501（HotSpot）和 Maven 3.10.0。
- `~/.m2` **是空的**。原版依赖 lombok（`Decomposer.Node` 用了 `@Getter/@Setter`）、guava、commons-collections、slf4j，需要从 Maven Central 下载。这是外部网络操作，**需要你同意**。
- 不需要 Spring、MyBatis、MySQL：`LetterSplicer`、`Decomposer`、`KeyboardLayout` 只依赖上面 4 个库。
- **harness 位置**：建议放在本库的 `tools/golden-java/`。用 `javac -sourcepath ime-java/src/main/java` 只编译需要的类，产物放进本库的 `build/`，`ime-java` 一个字节都不改。
- harness 会额外导出两样东西：
  - 每个字母 `getDeclaredFields()` 的实测顺序；
  - 全部形的 `(key, zcode, location, 4 条规则名)` 清单。

  这样 `data/shapes.tsv` 也是**跑出来的**，不是我手抄的；手抄版只用来交叉校验。
- 输入集建议：
  - 全部长度 ≤3 的键串，共 30+30²+30³ ≈ 2.8 万条；
  - 从词典挑出的真实拼写，和一些长输入；
  - 包含未知字符、空白、大写的边界用例；
  - fuzzy 候选太多的输入，记录数量和 sha256，而不是展开全部。

---

## 附录：审阅后的决定与 Phase 1 实测（2026-10-20）

**决定**
- ZCode 原样输出，不做 PUA 映射。
- golden harness 放在 `tools/golden-java/`，依赖从 Maven Central 下载。
- NG 作为数据移植，但和原版一样不挂到任何键上。
- 候选上限、去重：都做成选项，默认关闭。

**字体**
- 字体来自 <https://github.com/zmongol/mongol_editor_package-main>，固定在 commit `618edf0`。参考字体选定 **Z52 Hara Tig**（`z52haratig.ttf`，v1.02）。
- 52 个 ZCode 码位全覆盖，不用 PUA；带 `mong` 文字的 GSUB（init/fina/isol/rclt），说明它是按 Unicode 整形引擎的工作方式设计的。
- 字体内**没有许可证字段**，版权字符串只有 "Z Code"。该仓库的 MIT 许可声明（2025 年，"Zmongol Editor Contributors"）看不出覆盖这些字体，所以**未提交**字体文件，只记录在 `NOTICE.md`，由 `tools/fetch_font.py` 下载并校验 sha256。
- 该仓库还附带微软的 `segoeui*.ttf`（专有字体），完全不使用。
- 该仓库里的 `z52brahmisanstig.ttf` 不是字体，而是一个 macOS 书签文件。

**运行 Java 原版确认的事实**（数据见 `testdata/`）
1. `getDeclaredFields()` 在 HotSpot 1.8.0_501 上返回声明顺序，`java_keyboard.tsv` 记录了实测顺序。
2. 129 个形中，只有 `G.gMiddleDoubleAchvgDagbvr` 和 `G.gMiddleDoubleAchvg` 按 `equals` 相等。
3. §3.4 第 4 条已证实：
   - severe 模式下 `e m e` 只输出 `1865.188D.1868`（m 用 hurbelge 形 + e 的 chachilga 尾）；对照 `e l e` 会输出两个候选。
   - 把 `mMiddleHurbelge` 从 `FRONT_LIKE_TAIL_ORHICH_E` 里删掉，golden 输出完全不变，只有真值表测试失败。说明这是一个"死"条目。
4. **新发现的怪行为：拼接"重新开始"。**
   - 某个位置的候选被全部拒绝后，下一个键会对着空序列重新开始拼接。而辅音的词中形没有前置规则，于是可以成为新的"第一个形"。
   - 结果是输出一个只含后半段的残片。例如 severe `[j k R u i t C]` 只输出 `1898`（t 的一个词中形），共 9 个 golden 输入出现这种情况。
   - C++ 忠实复现，**请裁定是否算 bug**。
5. 同一份输入连续两次运行 Java，输出逐字节相同。
