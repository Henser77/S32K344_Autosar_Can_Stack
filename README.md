# S32K344 AUTOSAR 风格 CAN 通信栈

在 NXP S32K344（Cortex-M7）上，**不用任何 AUTOSAR 工具链**，自底向上手写一套
AUTOSAR Classic Platform 风格的分层通信栈，OS 用 FreeRTOS 替代 AUTOSAR OS。

> 手写代码 7,188 行（16 个 `.c` + 15 个 `.h`），7 个层次。已上板验证。

---

## 为什么做这个

**问题背景**：DaVinci / EB tresos 这类 AUTOSAR 工具链能一键生成整套通信栈，
但生成出来的代码是黑盒 —— 你知道怎么填表，不知道数据从 CAN 帧到应用变量
中间经过了什么、每一层为什么存在。

**做法**：不用生成器，按 AUTOSAR 的分层规范（SWS）自己实现一遍每一层，
包括每个函数的接口语义、每个 Det 错误码、每张配置表的结构。
代价是要手工维护 5 张跨层配置表，但换来的是**每一行代码都能解释为什么**。

**由此产生的一个真实工程问题**：没有生成器 → 5 张配置表靠人手工对齐 →
任何一处对不上都会**编译通过、运行不报错、但数据不对**。
这就是下面第 3 条亮点要解决的问题。

---

## 技术亮点

### 1. 7 层分层架构，依赖严格单向

```
Application → Rte → Services(Com/PduR/Dcm/EcuM/Det/BswCheck)
                        → EcuAbstraction(CanIf) → Mcal(Can) → RTD → 硬件
```

- 上层调用下层，下层**只通过函数指针**回调上层，无反向 include
- 可用 `arm-none-eabi-nm <模块>.o | grep ' U '` 验证：每层只引用下层的符号
- **配置驱动**：`Mcal/can.c` 完全由 `Can_Config` 结构体驱动，
  同一份驱动代码配了 2 路 CAN / 4 发 4 收，换配置不需要改驱动

### 2. Det 开发错误追踪：6 模块 / 48 处检查点

| 模块 | 检查点数 | 模块 | 检查点数 |
|---|---|---|---|
| `can.c` | 17 | `com.c` | 14 |
| `canif.c` | 9 | `pdur.c` | 6 |
| `dcm.c` | 1 | `ecum.c` | 1 |

- 全局开关 `BSW_DEV_ERROR_DETECT`，通过 `-D` 传入，**关掉后 6 个模块对
  `Det_ReportError` 的引用全部归零**（实测：`.o` 从 4,480 B 降到 4,292 B，
  Det 本体可整体裁掉，省 2.6 KB Flash + 1.9 KB RAM）
- **刻意区分"程序员的错"和"合法的运行状态"**：`CanIf_IsTxAllowed` 返回
  `TX_OFFLINE` 是诊断会话切换时的正常行为，上报 Det 会刷满 5 条记录、
  淹没真正的错误 —— 所以这里**刻意不报**
- Det 可能在 CAN 中断里被调用，实现只写数组、不打印、不阻塞

### 3. 跨层配置一致性：三道防线

没有生成器，5 张表是手工维护的五份事实。用三道防线把"静默失败"变成"立刻失败"：

| 防线 | 手段 | 数量 | 抓住什么 |
|---|---|---|---|
| **编译期** | 符号化引用 | 23 个符号 | 引用了不存在的对象（句柄 / PduId） |
| **编译期** | 静态断言 | 15 个断言 | 表项数与数量宏脱节、`DestCount` 与数组不符 |
| **启动时** | 配置自检 | 5 类检查 | 位区间越界/重叠、长度超容量 |

**实测证据**（故意改坏后编译，全部被抓住）：

```
删掉 CanConf_TxObject_Hth6   → error: 'CanConf_TxObject_Hth6' undeclared;
                                       did you mean 'CanConf_TxObject_Hth4'?
CANIF_TX_PDU_COUNT 4 改 5    → error: size of array 'CanIf_TxPduCountAssert' is negative
COM_SIG_COUNT 17 改 18       → error: size of array 'Com_SignalCountAssert' is negative
PduR 路由指向已删的 PDU      → error: 'CanIfConf_TxPdu_1' undeclared
```

**运行期自检实测**（把一个信号的起始位改成和另一个重叠，烧录后）：

```
配置正确  → g_BswCheckStatus.ErrorCount = 0,  Det 记录 0 条
位重叠    → g_BswCheckStatus.ErrorCount = 2,  Det 记录 2 条
            LastErrorId = 0x23 (SIGNAL_OVERLAP), LastInstance = 3
```

注意结果是 **2 条不是 1 条** —— 那个区间同时压住了两个信号，
检查把**所有重叠对**都找出来了，不是发现一处就停。

### 4. 应用层：带安全规则的车窗控制器

不是点灯的 demo，是一个有 6 条联锁规则的状态机：

- 门开 → 强制全开；上锁 → 禁止动作；车速 > 20 km/h → 禁止上升
- 梯形速度曲线（步长 6% → 3% → 1%）
- 4 秒超时 → 报故障；故障保持 2 秒复位

**用于验证整条链路**：应用写信号 → COM 打包 → PduR 路由 → CanIf → MCAL →
总线 → 反向解包。`0x200 / 0x300` 两条报文还演示了 **1:N 扇出**
（一份数据同时出现在 CAN0 和 CAN1 上）。

---

## 关于"偏离标准"的几个决定

写过程中有几处**故意不按 AUTOSAR SWS 做**，每处都有理由：

| 决定 | 标准做法 | 我们的做法 | 理由 |
|---|---|---|---|
| Det 编译开关粒度 | `STD_OFF` 时连 `if` 判断一起编译掉 | **保留 `if`，只编译掉 `Det_ReportError` 调用** | 多一条比较指令，换"永远不因 NULL 崩溃" |
| `Fls` 接口 | 异步（发起 + 轮询状态） | 同步（内部轮询完才返回） | Bootloader 环境下无需并行 |
| Bootloader 的 OS | 可用 AUTOSAR OS | **裸机大循环** | 最小化、最可靠：App 坏了它要能跑 |
| PduR 配置文件的耦合 | 生成器用符号名 include 上下层配置头 | 同样 include | **这一条和标准一致** —— 符号名必须 include 才能编译期检查 |

---

## 这个仓库的用途

**它同时是一份可复用的模板。** 开新项目时照 **[TEMPLATE.md](TEMPLATE.md)** 走
（复制流程、7 步替换清单、6 条隐形约束、错误对照表、最小可跑配置）。

| 文档 | 内容 |
|---|---|
| **[TEMPLATE.md](TEMPLATE.md)** | 从模板开新项目的完整流程（402 行） |
| **[CONFIG_GUIDE.md](CONFIG_GUIDE.md)** | 每个参数归属哪一层、遵循什么规则（1,432 行） |
| **[CHANGES_Det_Symbolization.md](CHANGES_Det_Symbolization.md)** | Det / 符号化 / 自检的改动前后对照 + 验证记录（734 行） |

---

## 分层结构

```
┌──────────────────────────────────────────────────────────────┐
│ Application/   车窗控制器 SWC（业务状态机 + 6 条安全规则）      │
├──────────────────────────────────────────────────────────────┤
│ Rte/           端口函数：端口名 ↔ COM 信号 ID                   │
├──────────────────────────────────────────────────────────────┤
│ Services/      Communication/  Com（信号↔字节）+ PduR（路由）   │
│                Diagnostic/     Dcm（诊断，替身）                │
│                System/         EcuM（启动）+ Det（错误追踪）    │
│                                BswCheck（配置自检）             │
├──────────────────────────────────────────────────────────────┤
│ EcuAbstraction/ CanIf（PduId ↔ 硬件句柄 + CAN ID）              │
├──────────────────────────────────────────────────────────────┤
│ Mcal/          Can（句柄 ↔ 控制器 + 邮箱，二次封装 RTD）        │
├──────────────────────────────────────────────────────────────┤
│ generate/ board/   CT 生成的硬件配置（时钟/引脚/位率/中断）      │
├──────────────────────────────────────────────────────────────┤
│ RTD/          NXP 官方驱动（不在本仓库，见环境准备）             │
└──────────────────────────────────────────────────────────────┘
```

**依赖方向严格单向**：上层可以调用下层，下层只通过**函数指针**回调上层。
可用 `arm-none-eabi-nm <模块>.o | grep ' U '` 验证——每层只引用下层的符号。

---

## 环境准备

本仓库**不含**厂商 SDK（`.gitignore` 已排除），clone 后需要补两个目录才能编译：

| 目录 | 来源 | 放法 |
|---|---|---|
| `RTD/` | NXP S32K3 Real-Time Drivers（安装 S32 Design Studio S32K3 时自带，或从 NXP 官网下载 `SW32K3_S32M27x_RTD_R23-11`） | 把 `RTD` 的 `include/` 与 `src/` 拷到工程根目录的 `RTD/` 下 |
| `FreeRTOS/` | FreeRTOS 内核（S32DS 的 FreeRTOS 组件会自动放入） | 在 S32 Design Studio 里给工程启用 FreeRTOS 组件，或手工拷 `Source/` |

**必需的 IDE 配置**：

1. `Project Properties → C/C++ Build → Settings → Tool Settings → Standard S32DS C Compiler → Includes`
   加入这些路径：
   ```
   ${ProjDirPath}/Mcal
   ${ProjDirPath}/EcuAbstraction
   ${ProjDirPath}/Services/Communication
   ${ProjDirPath}/Services/Diagnostic
   ${ProjDirPath}/Services/System
   ${ProjDirPath}/Rte
   ${ProjDirPath}/Application/WindowCtrl
   ${ProjDirPath}/src
   ${ProjDirPath}/generate/include
   ${ProjDirPath}/RTD/include
   ${ProjDirPath}/board
   ${ProjDirPath}/FreeRTOS/Source/include
   ${ProjDirPath}/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1
   ```
   以及 SDK 的四条（跟随你的 SDK 安装位置）：
   ```
   .../BaseNXP_TS_T40D34M70I1R0/header
   .../BaseNXP_TS_T40D34M70I1R0/include
   .../Platform_TS_T40D34M70I1R0/include
   .../Platform_TS_T40D34M70I1R0/startup/include
   ```

2. `Source Location` 里要有：`Mcal`、`EcuAbstraction`、`Services`、`Rte`、`Application`、`src`、`generate`、`board`、`RTD`、`FreeRTOS`、`Project_Settings`
   （Eclipse 的 source folder 是**递归**的，所以 `Services` 一条就覆盖了三个子目录）

**然后 F5 刷新 → Clean → Build。**

---

## 上板验证要点

| 报文 | 总线 | 周期 | 数据 |
|---|---|---|---|
| `0x100` | CAN0 | 100 ms | `XX C1 5D 00 00 00 00 00`（byte0 随车窗位置变化） |
| `0x400` | CAN1 | 500 ms | `12 34 AB 00`（大端验证） |

应用层实现的是电动车窗控制器：分析仪发 `0x100` 且 `WindowPos = 100`，
ECU 的实际位置会按 **6% → 3% → 1%** 的递减步长爬升到 100（梯形速度曲线）。

**调试观察点**：

```
g_BswCheckStatus        配置自检结果（ErrorCount 应为 0）
g_AppWindowStatus       车窗控制器状态机
g_DcmStatus             诊断收帧记录
g_u32ComTaskCount       COM 任务活性
Det_numEventErrors[0]   Det 记录的错误数（正常应为 0）
Det_ModuleId[0][0..4]   最近 5 条错误的模块号
```

---

## 代码规模

| 类别 | 行数 |
|---|---|
| 手写分层代码（Mcal / EcuAbstraction / Services / Rte / Application / src） | 约 4500 行 |
| 其中注释与文档 | 约 60% |

---

## 已知缺口

见 `CONFIG_GUIDE.md` §十「已知缺口清单」，主要包括：

- COM 的 `MIXED` 发送模式有代码未验证；接收超时监控未启用
- **Bus-off 只计数，不自动恢复**（真实 ECU 必须能恢复）
- 缺 CanTp（多帧传输 → 无法刷写）、Dem（故障码）、NvM、CanSM、BswM、WdgM
- 运行期配置自检（`bswcheck.c`）尚未上板验证

---

## 许可与第三方

- `RTD/`、`generate/`、`board/` 中的 NXP 代码遵循 **NXP 许可**（Confidential and Proprietary），未纳入本仓库
- `FreeRTOS/` 遵循 **MIT**，未纳入本仓库
- 本工程手写部分（`Mcal/` `EcuAbstraction/` `Services/` `Rte/` `Application/` `src/` 及根目录文档）用于学习与展示
