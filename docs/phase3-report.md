# Phase 3 报告：优化

测量环境：Windows 11、MSVC 17.14（VS 2022 Build Tools）、Release（/O2）、Ninja。
工具：`bench/zcode_bench.cpp`（`tools/bench.ps1` 一键完成构建、测试、测量）。每个工作负载取 9 次运行的最小值。
同一时段内先跑基线（commit `4fb3352`：Phase 2 的忠实移植 + 基准程序，在独立 worktree 中构建），紧接着跑最终版本（`870f609`）。
每一行的候选数与码位数校验和，在两个版本之间完全相同。

## 总体结果（基线 → 最终）

| 工作负载 | 内容 | 基线 | 最终 | 倍数 |
|---|---|---:|---:|---:|
| splice_all_severe | 全部 30,952 组 golden 键序列，严格模式 | 81.40 ms | 23.56 ms | 3.5× |
| splice_all_fuzzy | 同上，fuzzy 模式（243,882 个候选） | 201.89 ms | 49.02 ms | 4.1× |
| typing_severe | 659 个词，每个前缀调用一次 `shape()` | 9.47 ms | 3.41 ms | 2.8× |
| typing_fuzzy | 同上，fuzzy 模式 | 58.79 ms | 13.89 ms | 4.2× |
| heavy_fuzzy | 6 个候选特别多的输入（41,006 个候选） | 44.19 ms | 8.87 ms | 5.0× |

以下只有最终版本才有，同一时段测得：

| 工作负载 | 内容 | 结果 |
|---|---|---:|
| keys_stateless_severe → keys_session_severe | 逐键取候选：无状态 `splice` 对比 `Session` | 1.60 → 0.76 ms |
| keys_stateless_fuzzy → keys_session_fuzzy | 同上，fuzzy 模式 | 11.90 → 9.49 ms |
| heavy_fuzzy_cap100 | `max_candidates = 100` | 1.48 ms（600 个候选） |
| count_heavy_fuzzy | 用 `count()` 只计数，不展开 | 0.08 ms |
| splice_all_fuzzy_dedup | `deduplicate = true` | 90.49 ms（209,349 个候选，比不开启时少 14%） |

## 逐项结果（每项一个 commit，提交后 golden 测试全绿）

各 commit message 里有该项完整的前后对比数据。

| Commit | 优化（对应 Phase 0 报告 §6 的条目） | 实测效果 |
|---|---|---|
| `ead22b1` | KeyboardLayout 预计算（第 3 项） | 严格模式全量 82.5 → 44.7 ms；fuzzy 全量 200.9 → 149.8 ms |
| `51efa16` | Verifier 预计算成查表（第 2 项） | **提交时测不出差别**（耗时被复制开销淹没）。在第 4 项之后做 A/B（临时换回解释执行，未提交）：严格模式全量 25.4 → 21.9 ms（约 −14%），fuzzy 全量 50.4 → 46.5 ms（约 −8%），其余在噪声内 |
| `9da19d2` | 父指针 arena，不再复制前缀（第 1 项） | 严格模式全量 45.0 → 32.1 ms；fuzzy 全量 152.5 → 120.1 ms |
| `c95cd9a` | 输出串一次按精确长度分配（第 6 项） | fuzzy 全量 120.1 → 48.0 ms；heavy_fuzzy 35.3 → 14.8 ms |
| `275a8d2` | Decomposer：数组索引的 trie + 回溯式前缀（第 5 项） | typing_severe 4.46 → 3.71 ms；typing_fuzzy 20.2 → 18.9 ms |
| `ac46e26` | `Session` 逐键增量拼接（第 7 项） | 逐键严格模式 1.54 → 0.73 ms；逐键 fuzzy 11.40 → 8.93 ms |
| `13f8006` | `max_candidates`（默认关闭）+ `count()`（第 8 项） | 见上表；`shape()` 改为直接收集结果后，typing_fuzzy 19.4 → 13.4 ms |
| `870f609` | `deduplicate`（默认关闭，第 9 项） | 默认路径无变化。首版每次调用都构造一个空 `unordered_set`，默认路径慢了约 20%，已修正后提交 |

第 4 项（去掉逐次拒绝时的日志）不需要单独做：Phase 2 的移植里本来就没有移植日志。

## 审阅后的裁定（2026-10-21）

1. **拼接"重新开始"：按 bug 修复**（`f512288`）。全部候选被拒绝后结果保持为空。golden 数据中恰好是那 9 行发生变化，每行都变为 0 个候选。
2. **`FRONT_LIKE_TAIL_ORHICH_E` 笔误：按 bug 修复**（`6854b50`）。改为 `mMiddleAchvg`。golden 数据中恰好是 30 行严格模式 `<键> m e` 输入发生变化，例如 `e m e` 从 1 个候选变为 2 个，与 `e l e` 一致。
3. **单键只给词首形、非词首加利克字母被跳过：保留原行为。**

两处修复同时作用于 Java 原版：`tools/golden-java/src/patches/` 里是打过补丁的类副本，`ime-java` 本身不改。golden 数据由打过补丁的 Java 重新生成，`-Poriginal` 仍可生成未修复版本的数据。修复后各项耗时没有变化。

## 仍未解决

- 字体 Z52 Hara Tig 没有许可证声明，所以未提交，只记录在 `NOTICE.md`。
- fuzzy 模式最坏情况下的候选数随输入长度指数增长。现在可以先用 `count()` 判断、再用 `max_candidates` 限制，但默认行为不变。
