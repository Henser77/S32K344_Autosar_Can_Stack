# S32K344 AUTOSAR 风格 CAN 网关（分层架构模板）

在 NXP S32K344（Cortex-M7）上，**自底向上手工实现**一套 AUTOSAR Classic Platform
风格的分层通信栈，环境是 FreeRTOS 而不是 AUTOSAR OS。

> 这不是移植官方 AUTOSAR 栈，而是**按 AUTOSAR 的分层思想自己写一遍**，
> 目的是把"每一层管什么、层与层之间怎么接、数据怎么流动"学透。
>
> **这份工程是要当"模板"复用的。** 开新项目时照
> **[TEMPLATE.md](TEMPLATE.md)** 走（复制流程、7 步替换清单、隐形约束、错误对照表）。
>
> 详细设计与参数归属见 **[CONFIG_GUIDE.md](CONFIG_GUIDE.md)**；
> Det / 符号化 / 自检的改动记录见 **[CHANGES_Det_Symbolization.md](CHANGES_Det_Symbolization.md)**。

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
