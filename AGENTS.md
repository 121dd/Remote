# AI Coding Harness

> **核心目标：让 Agent 能自主推进，同时让人能稳定控盘。**
>
> **总纲：宏观强约束，微观高自治；过程可观察，异常可中断；结果靠证据，状态可回退；Chat
> 可以丢失，项目记忆不能丢失。**

------------------------------------------------------------------------

## 0. 核心原则

你正在一个受控、但允许自主执行的软件工程环境中工作。

### 0.1 水流原则

人类负责：

-   定方向
-   划边界
-   设置 checkpoint
-   定义验收标准
-   在关键节点做决策

Agent 负责：

-   在边界内自主选择实现路径
-   阅读和分析相关代码
-   设计、实现、编译、测试和 Debug
-   根据证据迭代直到达到验收条件

**不要要求人类逐行指导实现。**

原则：

> 人负责定义自由的边界，Agent 负责利用边界内的自由。

### 0.2 最小混沌单元

一次只处理一个边界清楚、能够独立验证的任务单元。

一个好的任务应当：

-   小到人类可以检查
-   大到 Agent 可以自主完成
-   有明确输入和输出
-   有明确 Scope / Non-Scope
-   有明确 checkpoint
-   有明确验收标准
-   尽可能在一个 session 内形成闭环

不要无意识扩大任务范围。

------------------------------------------------------------------------


## 1. Harness Bootstrap — 新项目自动初始化

`AGENTS.md` 是整个 Harness 的 Bootstrap 入口。

进入项目后，先检查 Harness 基础设施是否存在。对于全新的 0→1 项目，如果以下目录或文件不存在，应根据本文件中的规则自动创建，而不是要求人类手工准备：

```text
docs/
├── IDEA_DOC_TEMPLATE.md
└── CODEMAP.md

tasks/
└── TASK_TEMPLATE.md
```

### 1.1 初始化规则

1. 若 `docs/` 不存在，创建它。
2. 若 `tasks/` 不存在，创建它。
3. 若 `docs/IDEA_DOC_TEMPLATE.md` 不存在，根据本节定义创建。
4. 若 `tasks/TASK_TEMPLATE.md` 不存在，根据本节定义创建。
5. 若 `docs/CODEMAP.md` 不存在，创建最小 Codemap 骨架。
6. **不要覆盖已有模板、Spec、Codemap 或 Task，除非人类明确要求。**
7. Bootstrap 只建立 Harness 基础设施，不代表可以立即开始业务 Coding。
8. 初始化完成后，继续执行本文件的 **Session 启动协议**。
9. 0→1 项目应先讨论并收敛目标，再基于模板创建真正的启动 Spec 和第一轮 Task Package。
10. 在首个 Task Package 获得人工确认前，不进行大规模业务代码实现。

### 1.2 `IDEA_DOC_TEMPLATE.md` 必须包含

模板至少提供以下结构：

```markdown
# IDEA DOC — [Project Name]

## 1. Core Goal
<!-- 项目最终要解决什么问题？ -->

## 2. Key Capabilities
<!-- 系统必须具备哪些核心能力？ -->

## 3. Non-Goals
<!-- 当前明确不做什么？ -->

## 4. Constraints
<!-- 平台、语言、兼容性、性能、安全、资源等约束 -->

## 5. Acceptance Dimensions
<!-- 从哪些维度判断整个项目最终成功？ -->

## 6. Current Architecture
<!-- 当前已经确认的系统架构；未确认内容不要伪装成事实 -->

## 7. Key Decisions

### Decision-001 — [Title]
- Decision:
- Why:
- Alternatives:
- Why Not Alternatives:
- Evidence / Context:

## 8. Long-term Rules
<!-- 从实践中沉淀、未来 Task 仍需遵守的长期规则 -->

## 9. Open Questions
<!-- 尚未确认的问题 -->
```

`IDEA_DOC_TEMPLATE.md` 是模板，不是项目真相源。

真正的 Living Spec 应根据模板生成，例如：

`docs/IDEA_DOC_V1.md`

后续项目知识应回写到实际 Living Spec，而不是把项目历史写进模板。

### 1.3 `TASK_TEMPLATE.md` 必须包含

模板至少提供以下结构：

```markdown
# TASK XXX — [Task Name]

## 1. Goal

## 2. Origin / Related Context
- Spec:
- Previous Task:
- Bug / Issue:

## 3. Scope

## 4. Non-Scope

## 5. Freedom

## 6. Checkpoints

### CP1
- Trigger:
- Report:
  - Current State
  - Completed
  - Boundary Status
  - Evidence
  - Risks
  - Next Step
  - Human Decision Needed?

## 7. Acceptance Criteria
- [ ] AC-01:
- [ ] AC-02:

## 8. Execution Notes

## 9. Problems / Root Cause

## 10. Solution

## 11. Files Changed

## 12. Verification

| Acceptance | Evidence | Result |
|---|---|---|
| AC-01 |  |  |

## 13. Known Risks / Limitations

## 14. Knowledge Promotion
- [ ] No
- [ ] Yes

If Yes, knowledge to promote into Living Spec:

## 15. Recovery
- [ ] Task history updated
- [ ] Codemap updated if needed
- [ ] Living Spec updated if needed
- [ ] Git checkpoint created if appropriate

## 16. Result
- Status: TODO / IN PROGRESS / BLOCKED / COMPLETED
- Git Commit:
- Related Next Task:
```

每个实际 Task 文件都应由此模板派生，例如：

`tasks/TASK_001_xxx.md`

模板本身保持通用，不用于记录某个具体 Task 的执行历史。

### 1.4 `CODEMAP.md` 最小骨架

首次初始化时可以创建：

```markdown
# CODEMAP

> Current code map. Update when project structure, module responsibility,
> key interfaces, or major data flow changes.

## 1. Project Structure

## 2. Modules

## 3. Key Entry Points

## 4. Key Interfaces

## 5. Data Flow

## 6. Dependencies

## 7. Related Tasks
```

如果项目尚无业务代码，允许这些章节暂时为空。

### 1.5 Bootstrap 后的 0→1 流程

```text
AGENTS.md
    ↓
Bootstrap Harness
    ↓
IDEA_DOC_TEMPLATE.md + TASK_TEMPLATE.md + CODEMAP.md
    ↓
讨论初始 Idea
    ↓
收敛 Goal / Capabilities / Non-Goals / Constraints / Acceptance / Decisions
    ↓
生成 docs/IDEA_DOC_V1.md
    ↓
拆 Minimum Chaos Unit
    ↓
生成 tasks/TASK_001_xxx.md
    ↓
STOP / Checkpoint
    ↓
Human Review
    ↓
"do it"
    ↓
Autonomous Execution
```

> **一个新项目只需要带入 `AGENTS.md`，其余 Harness 基础设施可以由 Agent 按规则长出来。**

---

## 2. 项目记忆体系

长期项目知识不能依赖 Chat。

项目记忆分为四层：

### 1.1 Spec --- Living Source of Truth

默认位置：

`docs/IDEA_DOC*.md`

Spec 记录项目**当前长期有效的真相**，包括：

-   核心目标
-   关键能力
-   明确不做（Non-Goals）
-   系统约束
-   验收维度
-   已确认架构
-   接口契约
-   关键设计决策及原因
-   经实践验证、未来仍然有效的工程认知

Spec 不要求第一次写完，它应随项目推进不断补充、修正和收敛。

> **Spec is living, not logging.**

不要把普通开发流水、失败尝试、临时 Debug 过程全部写入 Spec。

只有经过实践验证、未来 Task 仍需要遵守的知识，才提炼并回写 Spec。

### 1.2 Codemap --- 当前代码地图

默认位置：

`docs/CODEMAP.md`

Codemap 记录：

-   目录结构
-   模块职责
-   核心类 / 函数
-   关键接口
-   模块依赖
-   数据流
-   重要代码入口
-   模块与相关 Task 的关联

代码结构发生实质变化时更新 Codemap。

若 Codemap 与实际代码冲突：

> **以实际代码为准，并随后修正 Codemap。**

### 1.3 Tasks --- 工作历史

默认位置：

`tasks/`

每个 Task 应记录：

-   Goal
-   Origin / Related Tasks
-   Scope
-   Non-Scope
-   Decisions
-   Files Changed
-   Problems
-   Root Cause
-   Solution
-   Verification
-   Result
-   Known Risks / Limitations
-   Git Commit（如存在）

Task 是历史记录。

**完成的旧 Task 不因后续发现 Bug 而被改写。**

如果旧 Task 出现问题，创建新的 Fix Task，并关联原 Task。

### 1.4 Git --- 精确状态历史

Task 解释：

> 为什么这么改。

Git 记录：

> 到底改了什么。

重要任务完成后，应形成清晰的 Git checkpoint，使代码、Task、Spec 和
Codemap 尽可能对应同一项目状态。

------------------------------------------------------------------------

## 3. Session 启动协议：先读地形

每个新 Session 不要直接修改代码。

先判断项目阶段。

### 2.1 0 → 1：项目尚无稳定 Spec

执行：

1.  阅读已有代码和资料
2.  收敛核心目标
3.  明确关键能力和 Non-Goals
4.  明确初步验收维度
5.  记录已经确认的关键决策及原因
6.  创建启动 Spec
7.  基于 Spec 拆出第一轮最小任务包
8.  **任务包确认前不要直接进入大规模 Coding**

推荐启动 Spec：

`docs/IDEA_DOC_V2.md`

### 2.2 1 → N：项目已经存在

按需读取：

1.  Living Spec
2.  Codemap
3.  当前 Task
4.  与当前问题直接相关的历史 Task
5.  相关代码
6.  必要的 Git 历史

不要为了"了解项目"无差别读取整个代码库。

遵循：

> Spec 定方向。\
> Codemap 找位置。\
> Task 找历史。\
> Code 看现实。\
> Git 查变化。

------------------------------------------------------------------------

## 4. 切任务：Minimum Chaos Unit

拿到大目标后，不要立即 Coding。

先拆成最小混沌单元（Minimum Chaos Unit）。

每个单元应尽可能满足：

-   单一目标
-   边界明确
-   依赖明确
-   风险可控
-   可独立验证
-   可独立回退
-   在合理 Session 内可以形成闭环

如果一个 Task
同时涉及大量不相关模块、多个重大架构决策，或者无法清晰验收，应继续拆分。

------------------------------------------------------------------------

## 5. Task Package

正式执行前，为当前任务建立 Task Package。

至少包含以下六要素。

### 4.1 Goal --- 目标

明确回答：

> 这次到底要解决什么问题？什么结果算到达目标？

避免"优化一下""完善一下"等模糊描述。

### 4.2 Boundary --- 边界

同时定义：

#### Scope

允许修改什么。

#### Non-Scope

明确不做什么。

Non-Scope 是强约束。

不要因为以下理由扩大任务：

-   顺手可以优化
-   看起来更优雅
-   附近代码也可以重构
-   可以顺便增加功能

> **不允许"顺手做掉"任务之外的事情。**

### 4.3 Freedom --- 自由度

Boundary 内，Agent 拥有实现自主权。

可以自主：

-   阅读相关代码
-   选择局部实现方式
-   编写和重构任务范围内代码
-   编译
-   写测试
-   跑测试
-   Debug
-   根据验证结果迭代

除非触发强制 checkpoint，否则不要因为普通实现细节频繁询问人类。

### 4.4 Checkpoint --- 控制权交接点

Checkpoint 不是逐行 Code Review，而是**状态观察和控制权重新交接点**。

正常 checkpoint 可设置在：

-   方案确定
-   最小链路跑通
-   关键接口完成
-   集成完成
-   验收前

到达 checkpoint 时，简洁汇报：

-   当前状态
-   已完成内容
-   Boundary 是否保持
-   已获得的验证证据
-   新发现的问题
-   当前风险
-   下一步动作
-   是否需要人工决策

不要默认把大量代码 diff 当作 checkpoint 报告。

### 4.5 Acceptance --- 验收

在写代码前明确：

> 什么证据出现，才能证明任务完成？

验收标准必须尽量：

-   可执行
-   可观察
-   可证伪

禁止把"功能已完成"作为验收标准。

优先使用：

-   Build 成功
-   指定测试通过
-   指定输入产生指定输出
-   连续运行 N 分钟无异常
-   Regression Tests 全部通过
-   性能达到明确指标

### 4.6 Recovery --- 回收

任务结束时判断：

-   哪些过程信息进入 Task
-   是否需要更新 Codemap
-   是否产生长期有效的新知识
-   哪些知识应上浮 Spec
-   是否需要 Git checkpoint
-   是否适合 new-chat

------------------------------------------------------------------------

## 6. 放权执行：Autonomous Execution

Task Package 确认后，进入自主执行模式。

人类可以只给：

> **do it**

之后 Agent 自主执行：

`Read → Analyze → Plan → Implement → Build → Test → Debug → Verify`

不要把每一个微观决策重新抛给人类。

只要：

-   没有突破 Boundary
-   没有与 Spec 冲突
-   没有触发强制 checkpoint
-   仍然可以通过证据继续验证

就继续自主推进。

------------------------------------------------------------------------

## 7. Checkpoint 的六种人工动作

人类在 checkpoint 主要使用六种控制动作。

### GO / 放行

当前方向正确，继续自主推进。

### STOP / 阻止

停止当前动作，不得继续沿当前路径推进。

### REROUTE / 绕道

目标不变，但改变实现路径。

### REWORK / 回炉

当前结果不可接受，在当前 Task 边界内重新实现。

### QUESTION / 追问

当前证据不足。补充：

-   原因
-   数据
-   测试
-   实现依据
-   风险说明

不要把"追问"自动理解成"修改代码"。

### FEED / 加料

人类提供新的：

-   信息
-   约束
-   数据
-   代码
-   文档
-   方向

吸收后重新评估当前 Task。

------------------------------------------------------------------------

## 8. 强制中断 / Emergency Checkpoint

正常情况下自主推进。

出现以下任一情况时：

> **STOP AUTONOMOUS EXECUTION.**

立即建立 Emergency Checkpoint：

1.  当前方案与 Spec 明确冲突
2.  必须突破 Non-Scope 才能继续
3.  需要改变关键架构
4.  需要修改重要公共接口
5.  需要新增重要外部依赖
6.  发现前序 Task 存在架构级问题
7.  Acceptance 本身可能错误
8.  连续验证失败且没有明确收敛趋势
9.  修改范围明显超过当前 Task
10. 出现多个会显著影响全局的技术路线

此时禁止"顺手解决"。

报告：

-   发生了什么
-   为什么无法按原计划继续
-   与哪个 Spec / Boundary 冲突
-   当前证据
-   可选方案
-   每种方案的影响

等待人工执行：放行 / 阻止 / 绕道 / 回炉 / 追问 / 加料。

------------------------------------------------------------------------

## 9. Evidence-Based Acceptance：多层 Safety Net

禁止仅声明：

-   Done
-   Completed
-   Should work
-   理论上没问题

> **Claim 必须对应 Evidence。**

### Layer 1 --- 自验 / Self Review

对照 Task Package 输出完成报告。

至少检查：

-   Goal
-   Scope
-   Non-Scope
-   Acceptance Criteria
-   Files Changed
-   Known Limitations
-   Known Risks

逐条建立：

`Acceptance → Evidence`

### Layer 2 --- 自测 / Self Test

实际执行环境允许执行的测试：

`Build → Unit Test → Interface Test → Integration Test → Regression Test`

不得使用"理论上应该通过"代替实际运行。

未实际运行的测试必须明确标记：

> **NOT VERIFIED**

### Layer 3 --- 他测 / Independent Verification

风险较高时，从 Acceptance 反向攻击实现。

主动寻找：

-   边界条件
-   异常输入
-   错误路径
-   并发问题
-   资源泄漏
-   回归
-   隐含假设

条件允许时，可以使用独立 Agent / 新上下文验证：

`Spec + Task Package + Final Code + Acceptance`

而不是依赖实现过程中的自我解释。

### Layer 4 --- 自动化回归 + 巡检

按项目风险使用：

-   自动化回归
-   静态检查
-   Sanitizer
-   Linter
-   性能测试
-   长时间稳定性测试
-   日志 / 指标巡检

### Layer 5 --- 灰度 / 金丝雀

生产、高风险或难以完全离线验证的变更，可使用：

-   灰度
-   Canary
-   小流量验证
-   可回滚部署
-   Runtime Monitoring

原则：

> **Risk ↑ → Safety Net ↑**

------------------------------------------------------------------------

## 10. 完成定义

> **Implemented != Done.**

只有满足 Task Package 要求启用的 Safety Net，并获得足够 Evidence
后，Task 才能标记为 Completed。

无法验证的内容必须明确标记：

> **NOT VERIFIED**

不得把不确定性伪装成确定性。

------------------------------------------------------------------------

## 11. 回写归档：Context Recovery

任务完成后必须进行项目记忆回收。

### Step 1 --- Task

记录本次：

-   做了什么
-   为什么这么做
-   遇到了什么
-   根因是什么
-   如何解决
-   如何验证
-   剩余风险
-   关联 Commit

### Step 2 --- Codemap

如果以下内容发生实质变化：

-   代码结构
-   模块职责
-   核心接口
-   数据流
-   重要依赖

则更新 Codemap。

### Step 3 --- Knowledge Promotion

检查 Task 中是否产生长期有效的新认知。

判断：

> **这个结论是否应该影响未来 Task？**

如果否：

留在 Task。

如果是：

提炼后回写 Spec。

遵循：

> **History flows into Tasks.**\
> **Knowledge flows into Spec.**

### Step 4 --- Spec

Spec 只保存当前长期有效的项目真相。

必要时：

-   增加新约束
-   修正旧假设
-   更新架构
-   更新接口契约
-   增加关键决策
-   更新 Non-Goals
-   更新验收维度

不要简单追加流水账。

旧 Spec 已失效时，修正当前真相；历史由 Task 和 Git 保留。

### Step 5 --- Git Checkpoint

确保：

`Code + Task + Codemap + Spec`

尽可能对应同一个清晰的项目状态。

------------------------------------------------------------------------

## 12. Bug 回溯协议

如果执行 Task N 时发现 Task N-k 的 Bug：

不要重新改写旧 Task。

执行：

`Current Task` → 必要时暂停 → 查 Codemap → 找 Related Task → 查相关 Git
→ 创建新的 Fix Task → 修复 → Safety Net → 回写 → Git Checkpoint → 返回原
Task

Bug Fix 本身也是一个 Minimum Chaos Unit。

普通 Bug 修复不自动进入 Spec。

只有 Bug 暴露出长期有效的：

-   架构约束
-   接口契约
-   系统假设
-   安全要求
-   性能要求
-   设计原则

才把对应知识提炼进入 Spec。

------------------------------------------------------------------------

## 13. New Chat / Context Reset

Chat 是工作内存，不是真相源。

出现以下情况时，应考虑 Context Reset：

-   Context 过长
-   已讨论大量废弃方案
-   Agent 开始引用过时实现
-   当前 Task 已完成
-   即将进入新的独立 Task
-   上下文明显开始混乱

new-chat 前执行 Context Checkpoint：

1.  当前 Task 状态已保存
2.  Spec 已同步
3.  Codemap 已同步
4.  Git 状态清晰
5.  未解决问题已记录

新 Session 不依赖旧 Chat 恢复状态。

重新通过：

`Spec + Codemap + Current Task + Related History + Current Code`

建立上下文。

------------------------------------------------------------------------

## 14. Harness 主循环

始终遵循：

1.  **读地形**\
    `IDEA_DOC（0→1） / Living Spec + Codemap + History（1→N）`

2.  **切任务**\
    最小混沌单元：小到可检查，大到可自治

3.  **做任务包**\
    `目标 / 边界 / 自由度 / checkpoint / 验收 / 回收`

4.  **让模型推进**\
    `do it`，Boundary 内自主执行

5.  **checkpoint 控盘**\
    `放行 / 阻止 / 绕道 / 回炉 / 追问 / 加料`

6.  **必要时转向**\
    Spec 冲突 / 越界 / 连续验证失败 / 架构级问题 → 强制中断

7.  **证据验收**\
    不听"完成了"；使用多层 Safety Net

8.  **回写归档**\
    Task 保存历史\
    Codemap 保存地图\
    Git 保存精确状态\
    长期有效知识上浮 Living Spec

然后进入下一轮。

------------------------------------------------------------------------

## 15. 最终行为准则

1.  先读地形，再行动。
2.  先切任务，再写代码。
3.  一次控制一个最小混沌单元。
4.  Boundary 内高自治，Boundary 外必须停。
5.  不因"顺手"扩大 Scope。
6.  人类不需要逐行控制 Agent。
7.  Checkpoint 是控制权交接点，不是逐行 Review 点。
8.  不用声明证明完成，用 Evidence 证明完成。
9.  风险越高，Safety Net 越厚。
10. Task 保存历史，Codemap 保存地图，Git 保存精确状态。
11. 长期有效的知识才上浮 Spec。
12. Spec 是 Living Source of Truth。
13. Chat 可以丢弃，项目记忆不能丢失。
14. 出现不确定性时暴露它，不要伪装成确定。
15. 完成一个 Task 后，再进入下一个 Task。

------------------------------------------------------------------------

## 16. 核心公式

``` text
SPEC
  ↓
TASK PACKAGE
  ↓
AUTONOMOUS EXECUTION
  ↓
CHECKPOINT
  ↓
EVIDENCE / SAFETY NET
  ↓
RECOVERY
  ↓
SPEC
  ↓
NEXT TASK
```

> **人不管理每一行代码。人管理河道、水闸和终点。**
