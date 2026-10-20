# 工程结构与参数归属指南

> **用途**：这份文档回答三个问题
> 1. 代码放在哪、为什么放那（§二）
> 2. 想改某个参数时该动哪个文件（§五）
> 3. 改完要联动检查什么（§六）
>
> **状态**：2026-10-19，六层分层 + EcuM 已完成并逐层上板验证。
> 所有数值都对照过本工程的实际文件。

---

## 一、分层全图

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  Application/（手写 SWC）                     app_windowcontrol.c             │
│    管：业务逻辑、状态机、安全规则                                             │
│    ★ 特征：只调用 Rte_*，不知道 COM/CAN/PduId 的存在                          │
├──────────────────────────────────────────────────────────────────────────────┤
│  Rte/（手写的"生成物"）                       rte.c  rte_doorstatus.c          │
│    管：端口名 ↔ COM 信号 ID 的映射                                            │
│    ★ 特征：每个函数只有一行转发，价值在"映射关系"本身                         │
├──────────────────────────────────────────────────────────────────────────────┤
│  Services/（手写 BSW）                                                        │
│    Communication/  Com  管：信号 ↔ 字节的打包/解包、缩放、发送时机           │
│                    PduR 管：路由表、1:1 和 1:N 扇出                           │
│    Diagnostic/     Dcm  管：诊断请求接收（当前只记录，不响应）                │
│    System/         EcuM 管：初始化顺序、任务创建、启动 OS、控制器状态          │
│    ★ 特征：描述"数据的含义与去向"                                             │
├──────────────────────────────────────────────────────────────────────────────┤
│  EcuAbstraction/（手写）                      canif.c  canif_cfg.c            │
│    管：PDU 编号、PDU 长度、发送用哪个硬件句柄、发送 CAN ID、                   │
│        PDU 通道模式、本层容量上限                                             │
│    ★ 特征：描述"逻辑身份"（硬件完全不感知）                                   │
├──────────────────────────────────────────────────────────────────────────────┤
│  Mcal/（手写）                                can.c  can_cfg.c                │
│    管：逻辑句柄编号、句柄↔邮箱映射、收发对象表、接收过滤掩码、                │
│        静态缓冲大小、向上 hook                                                │
│    ★ 特征：描述"硬件资源怎么分配"                                             │
├──────────────────────────────────────────────────────────────────────────────┤
│  RTD 配置代码（CT 生成）generate/ + board/                                    │
│    管：时钟、引脚、波特率、位时序、邮箱总数、FD 开关、特性开关、              │
│        中断向量与优先级、回调函数名                                           │
│    ★ 特征：描述"硬件事实"                                                     │
│    ⚠️ 重新 Generate 会覆盖，手改会丢                                          │
├──────────────────────────────────────────────────────────────────────────────┤
│  RTD 驱动源码（NXP 官方）RTD/                                                 │
│    管：寄存器操作、状态机、中断分派                                            │
│    ★ 不要改                                                                   │
└──────────────────────────────────────────────────────────────────────────────┘

旁挂：FreeRTOS/（内核，CT 生成 FreeRTOSConfig.h）    Project_Settings/（启动代码、链接脚本）
```

### 一句话判据

> **这个参数描述的是"硬件事实"、"硬件资源分配"、"逻辑身份"，还是"数据含义"？**
>
> | 参数性质 | 归哪层 | 改哪个文件 |
> |---|---|---|
> | 硬件事实（线跑多快、几个邮箱、哪个引脚） | RTD/CT | 在 CT 里点 |
> | 硬件资源分配（哪个邮箱收哪个 ID、句柄叫几号） | MCAL | `Mcal/can_cfg.c` |
> | 逻辑身份（第几号 PDU、多长、发时用什么 ID） | CanIf | `EcuAbstraction/canif_cfg.c` |
> | 数据的去向（这条 PDU 发给谁、扇出几路） | PduR | `Services/Communication/pdur_cfg.c` |
> | 数据的含义（这几位是什么信号、怎么缩放） | COM | `Services/Communication/com_cfg.c` |
> | 端口的名字（应用怎么称呼这些信号） | RTE | `Rte/rte_doorstatus.c` |
> | 业务规则（什么时候动、怎么动） | Application | `Application/WindowCtrl/*.c` |
> | 启动顺序、任务划分 | EcuM | `Services/System/ecum.c` + `ecum_cfg.c` |

### 补充判据：**看信息流的方向**

判断 CAN ID 该归谁，用这条更可靠：

```
发送方向：ID 从上层往下流
    CanIf ──CanId──> MCAL ──> 硬件
    ↑ CanIf 决定发什么 ID        ↑ MCAL 只执行，不持有 ID
    → 发送方向的 CanId 归 CanIf

接收方向：ID 由硬件过滤器决定
    硬件 ──邮箱掩码──> MCAL ──Hrh──> CanIf
            ↑ MCAL 配置过滤器      ↑ 只把 Hrh 翻译成 PduId
    → 接收方向的 ID 归 MCAL
```

**一句话：ID 是谁"决定"的，就归谁管。**

---

## 二、文件结构与"新模块放哪"

### 2.1 当前目录树

```
S32K344_Autosar_CanGw/
├── RTD/                        NXP 官方驱动（不要改）
│   ├── include/                49 个头文件（Clock_Ip 等）
│   └── src/                    驱动实现
├── generate/                   CT 生成（不要手改）
│   ├── include/                16 个头文件
│   └── src/                    5 个源文件
├── board/                      CT 生成（不要手改）
├── Project_Settings/           启动代码、链接脚本（一般不改）
├── FreeRTOS/                   FreeRTOS 内核源码
│
├── Mcal/                       ── MCAL 层 ──
│   ├── can.c                   驱动封装实现
│   ├── can.h                   AUTOSAR Can SWS 接口
│   ├── can_cfg.c               配置数据（对象表）
│   └── can_cfg.h               配置类型 + 常量
│
├── EcuAbstraction/             ── ECU 抽象层 ──
│   ├── canif.c
│   ├── canif.h
│   ├── canif_cfg.c
│   └── canif_cfg.h
│
├── Services/                   ── 服务层 ──
│   ├── Communication/          ── 通信服务 ──
│   │   ├── com.c  com.h
│   │   ├── com_cfg.c  com_cfg.h
│   │   ├── pdur.c  pdur.h
│   │   └── pdur_cfg.c  pdur_cfg.h
│   ├── Diagnostic/             ── 诊断服务 ──
│   │   └── dcm.c  dcm.h
│   └── System/                 ── 系统服务 ──
│       ├── ecum.c  ecum.h
│       └── ecum_cfg.c  ecum_cfg.h
│
├── Rte/                        ── RTE 层 ──
│   ├── rte.c  rte.h            RTE 公共接口
│   └── rte_doorstatus.c/.h     DoorStatus 端口的端口函数
│
├── Application/                ── 应用层 ──
│   └── WindowCtrl/             一 SWC 一目录
│       └── app_windowcontrol.c/.h
│
└── src/
    └── main.c                  入口，只剩两次调用
```

### 2.2 每个模块的文件命名约定

本工程每个 BSW 模块固定 **4 个文件**：

```
<模块>.h        接口（AUTOSAR SWS 的 API 声明）
<模块>.c        实现
<模块>_cfg.h    配置结构体定义 + 容量上限宏
<模块>_cfg.c    配置数据（表）
```

理由：
- 接口和实现分离 → 改实现不影响调用方
- 配置和实现分离 → 改行为只动 `_cfg.c`，实现代码零改动

### 2.3 新模块放哪（按 AUTOSAR 功能组）

| 你想加 | 放哪 | 说明 |
|---|---|---|
| `CanTp`（CAN 传输层） | `Services/Communication/` | 通信服务 |
| `IpduM`（PDU 复用） | `Services/Communication/` | 通信服务 |
| `Dem`（故障管理） | `Services/Diagnostic/` | 诊断服务 |
| `FiM`（功能抑制） | `Services/Diagnostic/` | 诊断服务 |
| `BswM`（模式管理） | `Services/System/` | 系统服务 |
| `WdgM`（看门狗） | `Services/System/` | 系统服务 |
| `Det`（开发错误追踪） | `Services/System/` | 系统服务 |
| `NvM`（非易失存储） | `Services/Memory/`（新建） | 存储服务 |
| `Port` / `Dio`（引脚驱动） | `Mcal/` | MCAL 永远一模块 4 个文件 |
| `IoHwAb`（IO 抽象） | `EcuAbstraction/` | ECU 抽象 |
| 第二个 SWC（车门锁） | `Application/DoorLock/`（新建） | 一 SWC 一目录 |

**AUTOSAR 官方的 Services Layer 功能分组**（我们照此划分）：

| 功能组 | 包含的模块 |
|---|---|
| **Communication** | Com, PduR, CanTp, IpduM, LdCom |
| **Diagnostic** | Dcm, Dem, FiM |
| **System** | EcuM, BswM, WdgM, Det, Dlt |
| **Memory** | NvM, MemIf, Fee, Ea |
| **Crypto** | Csm |

### 2.4 ⚠️ 加新目录后必须做的事

**新目录 = 新的 IDE include path。**

1. `Project Properties → C/C++ Build → Settings → Tool Settings → Standard S32DS C Compiler → Includes`
2. `Add...` → **`File system...`**（不要选 Workspace）
3. Source Location **不用动**（Eclipse 的 source folder 是递归的）

**判据**：只有"跨模块 include"才需要 include path；同模块内 `#include "com.h"` 会先找当前文件所在目录。

### 2.5 哪些文件是"生成物"（手改会丢）

判据：`.mex` 文件的 `<file path="...">` 清单里列出的，都是 CT 生成物。

| 目录 | 性质 | 能否手改 |
|---|---|---|
| `generate/**`（21 个文件：16 头 + 5 源） | CT 生成 | ❌ 会被覆盖 |
| `board/**`（4 个文件） | CT 生成 | ❌ 会被覆盖 |
| `Mcal/**` | 手写（MCAL） | ✅ 随便改 |
| `EcuAbstraction/**` | 手写（CanIf） | ✅ 随便改 |
| `Services/**` | 手写（BSW） | ✅ 随便改 |
| `Rte/**` | 手写（模拟生成物） | ✅ 随便改 |
| `Application/**` | 手写（SWC） | ✅ 随便改 |
| `src/**` | 手写（入口） | ✅ 随便改 |
| `FreeRTOSConfig.h` | **CT 生成**（在 `generate/include/`） | ❌ 会被覆盖 |
| `RTD/**` | NXP 官方驱动 | ⚠️ 不要改 |
| `Project_Settings/**` | 启动代码/链接脚本 | ⚠️ 一般不改 |

`.mex` 里登记的全部生成文件：

```
board/Siul2_Port_Ip_Cfg.c            generate/include/FlexCAN_Ip_Sa_PBcfg.h
board/Siul2_Port_Ip_Cfg.h            generate/include/FreeRTOSConfig.h
board/Tspc_Port_Ip_Cfg.c             generate/include/IntCtrl_Ip_Cfg.h
board/Tspc_Port_Ip_Cfg.h             generate/include/IntCtrl_Ip_CfgDefines.h
generate/include/Clock_Ip_Cfg.h      generate/include/modules.h
generate/include/Clock_Ip_Cfg_Defines.h   generate/include/OsIf_ArchCfg.h
generate/include/DeviceDefinition.h  generate/include/OsIf_Cfg.h
generate/include/FlexCAN_Ip_Cfg.h    generate/include/Siul2_Dio_Ip_Cfg.h
generate/include/FlexCAN_Ip_CfgDefines.h  generate/include/Siul2_Port_Ip_Defines.h
generate/src/Clock_Ip_Cfg.c          generate/include/Soc_Ips.h
generate/src/FlexCAN_Ip_Sa_PBcfg.c   generate/src/IntCtrl_Ip_Cfg.c
generate/src/OsIf_Cfg.c
```

> **本工程从未手改过任何生成文件** —— 所有改动都在 CT 里点。这是正确做法。

---

## 三、依赖方向：怎么验证分层没被破坏

### 3.1 唯一规则

> **依赖只能向下，不能向上。**
>
> 上层可以 `#include` 下层、调用下层；**下层绝不认识上层**。

**唯一的例外是"接缝"**：下层通过**函数指针**回调上层。这个指针由配置提供，所以下层代码里只看到"一个函数指针"，看不到上层的名字。

### 3.2 三道接缝的接法（各不相同）

| 接缝 | 接法 | 在哪 |
|---|---|---|
| CAN 驱动 → CanIf | **运行时注册**（hook） | `CanIf_Init` 里 `Can_RegisterRxHook(CanIf_RxIndication)` |
| CanIf → PduR | **运行时注册**（hook） | `PduR_Init` 里 `CanIf_RegisterRxIndicationHook(PduR_CanIfRxIndication)` |
| **PduR → COM / Dcm** | **编译期静态函数指针** | `pdur_cfg.c` 的路由表里直接写 `Com_RxIndication` |
| COM → 应用 | **没有回调（拉模式）** | 应用主动 `Com_ReceiveSignal` |

**为什么 PduR → COM 用静态函数指针而不是注册**：
因为 PduR 的路由表是 `const`，条目里必须直接写函数地址。这也是真实 AUTOSAR 的做法（`PduRDestPdus` 里存函数指针）。

### 3.3 怎么验证（`nm` 查未定义符号）

```powershell
# 1. 先编译出 .o
arm-none-eabi-gcc -c ... -o com.o com.c

# 2. 看它引用了哪些外部符号
arm-none-eabi-nm com.o | Select-String '^\s+U '
```

**判据**：某层的 `.o` 里，**不应该出现任何"上层模块前缀"的未定义符号**。

### 3.4 实测结果（2026-10-19）

| 模块 | 引用上层模块符号的数量 | 说明 |
|---|---|---|
| `can.o` | CanIf_/PduR_/Com_/Rte_ = **0 / 0 / 0 / 0** | MCAL 谁也不认识 ✅ |
| `canif.o` | Can_ = 7，PduR_/Com_/Rte_ = **0 / 0 / 0** | 只向下依赖 ✅ |
| `pdur.o` | CanIf_ = 3，Com_/Rte_ = **0 / 0** | 只向下依赖 ✅ |
| `pdur_cfg.o` | Com_ = **2** | 只有配置文件引用 COM（正是设计意图）✅ |
| `com.o` | PduR_ = 1，Rte_ = **0** | 只向下依赖 ✅ |
| `rte_doorstatus.o` | Com_SendSignal / Com_ReceiveSignal = 2 | 只向下依赖 ✅ |
| `app_windowcontrol.o` | **Com_/CanIf_/Can_/PduR_ 全为 0**，只有 `Rte_*` | 应用层对 BSW 零依赖 ✅ |
| `ecum.o` | 引用所有层 | **集成者，应该都认识** ✅ |

---

## 四、一帧数据怎么流过六层

### 4.1 下行（发送）：应用 → 总线

```
【应用任务 Task_WinCtrl（优先级 2，100ms）】
  Rte_Write_DoorStatus_WindowPos(50)
      ↓ Rte 把端口名换成信号 ID
  Com_SendSignal(ComConf_ComSignal_WindowPos, &50)
      ↓ ① 查信号表 get 位布局/缩放
      ↓ ② 物理值 50 → raw 50（factor=1）
      ↓ ③ 打包进 Com_TxState[0].shadow[1..7] 位
      ↓ ④ 按 TransferProperty 置 txRequested（不立即发）
      ⛔ 到这里 signalId 使命结束

【BSW 任务 Task_Com（优先级 3，10ms）】
  Com_MainFunctionTx()
      ↓ 遍历 TX PDU，PERIODIC 计数器到 10 → 该发
  PduR_ComTransmit(pdu->TxPduId = 0, &{shadow, 8})
      ↓ PduR 查 TX 路由表：SrcPduId 0 → Dests{0}

  CanIf_Transmit(0, &info)
      ↓ CanIf 查发送表：PduId 0 → Hth 4 / Ctrl 0 / ID 0x100 / Len 8

  Can_Write(Hth 4, &pdu)
      ↓ MCAL 查发送表：Hth 4 → (CAN0, 邮箱1, PayloadSize 8)
  FlexCAN_Ip_Send(0, 1, ...)
      ↓ RTD 把 8 字节写进邮箱 RAM
【硬件】CAN0 发出 ID 0x100，数据 65 C1 5D 00 00 00 00 00
```

**发送方向的关键点**：

| # | 事实 |
|---|---|
| 1 | `Com_SendSignal` **不发送**，只更新影子缓冲和标志 |
| 2 | 真正的发送由 `Com_MainFunctionTx` 在**任务上下文**决定 |
| 3 | TX 数据是**同步拷贝**进邮箱 RAM 的（`FlexCAN_Ip_HwAccess.c:1646-1659`），函数返回后安全 |

### 4.2 上行（接收）：总线 → 应用

```
【CAN 接收中断（全链路同步，中断不退出）】
  FlexCAN0_IRQHandler
    ↓ FlexCAN_IRQHandlerRxMB（拷数据、清标志）
    ↓ FlexCAN0_Callback  →  Can_IsrBridge
    ↓ Can_RxHook  =  CanIf_RxIndication
    ↓ CanIf_RxUpHook  =  PduR_CanIfRxIndication
    ↓ Dests[i].RxIndication  =  Com_RxIndication   ← 配置表里的静态指针
       ├─ 查 RX PDU 表（Length / FirstSignalIdx / SignalCount）
       ├─ 拷字节 → Com_RxState[0].buffer
       ├─ 逐信号解包：raw → phys（缩放/偏移/符号扩展）
       └─ Com_SigShadowValue[sigId] = phys      ★ 存完就返回，不通知任何人
    ↓ Dests[i].RxIndication  =  Dcm_RxIndication（1:N 扇出，仅 PDU3）
  ← 中断退出

【应用任务（稍后，任意时刻）】
  Rte_Read_DoorStatus_WindowPos(&pos)
    ↓
  Com_ReceiveSignal(ComConf_ComSignal_RxWindowPos, &pos)
    ↓ 只是把 Com_SigShadowValue[9] 拷出来 → pos = 50
```

**接收方向的关键点**：

| # | 事实 |
|---|---|
| 1 | 整条链在**一个中断**里跑完，`Com_RxIndication` 返回前中断不退出 |
| 2 | COM 在中断里只做位运算和内存写入，**不做业务逻辑**（避免拖长中断） |
| 3 | **COM 对上没有任何回调** —— 数据是"拉"模式 |
| 4 | 信号语义是**最新值**，不是队列；连续多帧只保留最后一帧 |
| 5 | `PduInfoType` 里**没有 CAN ID**，COM 只靠 `RxPduId` 识别是哪条 PDU |

### 4.3 应用层因此获得的能力

`app_windowcontrol.c` 里 **一次都没有出现** `Com_` / `Can_` / `PduR_` / `CanIf_` / PduId / CAN ID / 位偏移。

它只看到：

```c
Std_ReturnType Rte_Read_DoorStatus_WindowPos(uint8* data);    /* 端口.数据元 */
Std_ReturnType Rte_Write_DoorStatus_WindowPos(uint8 data);
```

**这就是分层的终点。**

---

## 五、参数归属总表

### 5.1 RTD / CT 层（在 S32DS 的 CT 里改，然后 Generate）

| 想改什么 | CT 里的位置 | 生成到 | 改完 Generate | 联动检查 |
|---|---|---|---|---|
| **波特率** | FlexCAN → FlexCAN_Configurations → 位时序 | `FlexCAN_Ip_Sa_PBcfg.c` | ✅ 必须 | 见 §6.1 算法 |
| **CAN 时钟频率** | Clock → FLEXCAN0_CLK / FLEXCAN1_CLK | `Clock_Ip_Cfg.c` | ✅ 必须 | **改了要重算波特率** |
| **邮箱总数 max_num_mb** | FlexCAN → Number Of Message Buffer | `FlexCAN_Ip_Sa_PBcfg.c` | ✅ 必须 | MCAL 的 `CAN_MB_BUF_COUNT` 要够；MbIdx 要 < 它 |
| **CAN FD 开关** | FlexCAN → fd_enable / BRS / FD 位时序 | `FlexCAN_Ip_Sa_PBcfg.c` | ✅ 必须 | MCAL 里长度上限要放开 |
| **引脚复用** | Port → PortContainer → PortPin | `board/Siul2_Port_Ip_Cfg.c` | ✅ 必须 | — |
| **中断使能 / 优先级** | IntCtrl_Ip → IntCtrlConfig → PlatformIsrConfig | `IntCtrl_Ip_Cfg.c` | ✅ 必须 | **优先级必须 ≥ 1**（FreeRTOS 要求） |
| **回调函数名** | FlexCAN → FlexCanCallback / FlexCanErrorCallback | `FlexCAN_Ip_Sa_PBcfg.c` 的 extern + 取地址 | ✅ 必须 | **必须与 `Mcal/can.c` 里的实现同名** |
| **FreeRTOS 堆/tick/优先级数** | FreeRTOS 组件 | `generate/include/FreeRTOSConfig.h` | ✅ 必须 | 堆要够放所有任务；`configMAX_PRIORITIES` 要 ≥ 最高任务优先级+1 |
| **时钟树（PLL 等）** | Clock 组件 | `Clock_Ip_Cfg.c` | ✅ 必须 | 影响所有外设 |

**当前实际值（供对照）**：

```
FlexCAN 时钟            80 MHz          ClockYaml.txt:68-69
CAN0 max_num_mb         96              FlexCAN_Ip_Sa_PBcfg.c:131
CAN1 max_num_mb         64              FlexCAN_Ip_Sa_PBcfg.c:222
位时序  PROPSEG=9 PSEG1=4 PSEG2=3 PRESDIV=7   → 20 TQ / 8 分频
波特率                  80MHz / 8 / 20 = 500 kbps，采样点 80%
fd_enable               FALSE
中断  FlexCAN0_1 (110)  优先级 6        → 邮箱 0-31 收发
中断  FlexCAN1_1 (114)  优先级 6
中断  FlexCAN0_0 (109)  优先级 7        → Bus-off / Error
中断  FlexCAN1_0 (113)  优先级 7
回调  FlexCAN0_Callback / FlexCAN0_ErrorCallback
      FlexCAN1_Callback / FlexCAN1_ErrorCallback
FreeRTOS  configTOTAL_HEAP_SIZE = 8192, configTICK_RATE_HZ = 1000,
          configMAX_PRIORITIES = 5, configCHECK_FOR_STACK_OVERFLOW = 0
```

### 5.2 MCAL 层（直接改文件，不需要 Generate）

| 想改什么 | 文件 | 具体位置 | 联动检查 |
|---|---|---|---|
| **句柄编号分段起点**（接收/发送的界） | `Mcal/can_cfg.h` | `CAN_HTH_FIRST_INDEX`（现 4） | 所有 `Hrh` 必须 < 它；所有 `Hth` 必须 ≥ 它 |
| **控制器数量** | `Mcal/can_cfg.h` | `CAN_CONTROLLER_COUNT`（现 2） | 必须等于 `Can_Controllers[]` 项数 |
| **接收掩码常用值** | `Mcal/can_cfg.h` | `CAN_RX_MASK_EXACT_STD / EXT / ACCEPT_ALL` | — |
| **逻辑控制器 → RTD 实例的绑定** | `Mcal/can_cfg.c` | `Can_Controllers[]` | 引用的 `FlexCAN_ConfigN` 必须存在 |
| **哪个发送句柄用哪个邮箱** | `Mcal/can_cfg.c` | `Can_TxObjects[]` 的 `ControllerId` / `MbIdx` | `MbIdx` < 对应控制器的 `max_num_mb` |
| **发送 DLC 上限** | `Mcal/can_cfg.c` | `Can_TxObjects[]` 的 `PayloadSize` | 非 FD 时 ≤ 8 |
| **接收哪个 ID + 掩码** | `Mcal/can_cfg.c` | `Can_RxObjects[]` 的 `CanId` / `CanIdMask` | **掩码必须显式填**，见 §6.4 |
| **接收用哪个邮箱** | `Mcal/can_cfg.c` | `Can_RxObjects[]` 的 `MbIdx` | 见 §6.3 的两个上限 |
| **接收缓冲深度** | `Mcal/can.c` | `CAN_MB_BUF_COUNT`（现 8） | 必须 ≥ 所有 `RxObjects[].MbIdx` + 1 |
| **使能哪些错误中断** | `Mcal/can.c` | `Can_EnableErrorInterrupts()` | 见 §6.6 |
| **向上通知 hook 的实现** | `Mcal/can.c` | `Can_IsrBridge` | 接 CanIf 后由 hook 转发 |

**当前实际值**：

```
CAN_HTH_FIRST_INDEX   = 4        → 接收占 0..3，发送占 4..7
CAN_CONTROLLER_COUNT  = 2
CAN_MB_BUF_COUNT      = 8        （Mcal/can.c，每控制器预留 8 个接收邮箱缓冲）

发送表：Hth 4 → (CAN0, 邮箱1, 标准, ≤8字节)
        Hth 5 → (CAN0, 邮箱2, 标准, ≤8字节)
        Hth 6 → (CAN1, 邮箱1, 标准, ≤8字节)
        Hth 7 → (CAN1, 邮箱2, 标准, ≤8字节)

接收表：Hrh 0 ← (CAN0, 邮箱5, ID 0x100, 精确匹配)
        Hrh 1 ← (CAN0, 邮箱6, ID 0x200, 精确匹配)
        Hrh 2 ← (CAN1, 邮箱5, ID 0x300, 精确匹配)
        Hrh 3 ← (CAN1, 邮箱6, ID 0x400, 精确匹配)
```

### 5.3 ECU 抽象层 CanIf（直接改文件，不需要 Generate）

| 想改什么 | 文件 | 具体位置 | 联动检查 |
|---|---|---|---|
| **PDU 编号** | `EcuAbstraction/canif_cfg.c` | `CanIf_TxObjects[]` / `CanIf_RxObjects[]` 的 `TxPduId` / `RxPduId` | 上层代码里用的号 |
| **发送用哪个硬件句柄** | `canif_cfg.c` | `CanIf_TxObjects[]` 的 `Hth` | **必须与 MCAL 的 `Can_TxObjects[].Hth` 一致** |
| **发送 CAN ID** | `canif_cfg.c` | `CanIf_TxObjects[]` 的 `CanId` | ⚠️ **MCAL 不校验这个值**，填错就发错 ID |
| **发送 PDU 长度** | `canif_cfg.c` | `CanIf_TxObjects[]` 的 `Length` | 必须 ≤ MCAL 的 `PayloadSize` |
| **PDU 属于哪个控制器** | `canif_cfg.c` | `CanIf_TxObjects[]` 的 `ControllerId` | 必须与 MCAL 的 `Can_TxObjects[].ControllerId` 一致 |
| **接收用哪个硬件句柄** | `canif_cfg.c` | `CanIf_RxObjects[]` 的 `Hrh` | **必须与 MCAL 的 `Can_RxObjects[].Hrh` 一致** |
| **本层单 PDU 容量上限** | `EcuAbstraction/canif_cfg.h` | `CANIF_MAX_PDU_LEN`（现 8） | 开 FD 时改 64 |
| **上行 hook 的实现** | `Services/Communication/pdur.c` | `PduR_Init` 里注册 | 见 §3.2 |

**当前实际值**：

```
CANIF_MAX_PDU_LEN = 8

发送表：PduId 0 → (Hth 4, Ctrl 0, ID 0x100, 8 字节)
        PduId 1 → (Hth 5, Ctrl 0, ID 0x200, 8 字节)
        PduId 2 → (Hth 6, Ctrl 1, ID 0x300, 8 字节)
        PduId 3 → (Hth 7, Ctrl 1, ID 0x400, 8 字节)

接收表：PduId 0 ← Hrh 0
        PduId 1 ← Hrh 1
        PduId 2 ← Hrh 2
        PduId 3 ← Hrh 3
```

**注意接收表里没有 CAN ID** —— 这是有意的。接收过滤发生在硬件（MCAL 的
`Can_RxObjects[]` 把 ID 和掩码写进 RXIMR 寄存器），CanIf 拿到的已经是筛完的帧。
想看 Hrh 与 ID 的对应关系，去 `Mcal/can_cfg.c`。

> 与官方实现对照：NXP 官方 CanIf 的 `CanIf_RxPduType`
> （`CanIf_Types.h:318-325`）字段是 PduId / PduLengthCheck / PduLength /
> CanIfCtrlId / UserRxIndication —— **同样没有 CanId 和 Mask**。
> 而它的 TX 结构体里有 CanId 和 CanFrameType。规则一致。

### 5.4 PduR 层（直接改文件）

| 想改什么 | 文件 | 具体位置 | 联动检查 |
|---|---|---|---|
| **COM 的 PDU 发给哪些 CanIf PDU** | `Services/Communication/pdur_cfg.c` | `PduR_TxPaths[]` + `PduR_TxDests_ComN[]` | DestPduId 必须存在于 `CanIf_TxObjects[]` |
| **CanIf 收的 PDU 发给哪些上层** | `pdur_cfg.c` | `PduR_RxPaths[]` + `PduR_RxDests_CanIfN[]` | SrcPduId 是 **CanIf** 的号；DestPduId 是 **COM/Dcm** 的号 |
| **扇出几路** | `pdur_cfg.c` | 各 `DestCount` | 数组项数必须 = DestCount |
| **发送确认回调给谁** | `pdur_cfg.c` | `PduR_TxPaths[].SrcTxConfirmation` | 必须与 SrcPduId 对应的模块函数签名一致 |

**当前实际值**：

```
【TX 路由】COM 的 TxPduId → CanIf 的 DestPduId
  路径 0: SrcPduId 0 → { 0 }                    1 对 1
  路径 1: SrcPduId 1 → { 1, 2 }                 1 对 2（CAN0 0x200 + CAN1 0x300）
  路径 2: SrcPduId 2 → { 3 }                    1 对 1
  三条路径的 TxConfirmation 都是 Com_TxConfirmation

【RX 路由】CanIf 的 SrcPduId → 上层 DestPduId
  路径 0: SrcPduId 0（CanIf） → { DestPduId 0 → Com_RxIndication }
  路径 1: SrcPduId 1（CanIf） → { DestPduId 1 → Com_RxIndication }
  路径 2: SrcPduId 2（CanIf） → { DestPduId 2 → Com_RxIndication }
  路径 3: SrcPduId 3（CanIf） → { DestPduId 3 → Com_RxIndication }
                              → { DestPduId 0 → Dcm_RxIndication }   ← 扇出
```

> ⚠️ **`SrcPduId` 的归属随方向翻转**，见 §6.9。

### 5.5 COM 层（直接改文件）

| 想改什么 | 文件 | 具体位置 | 联动检查 |
|---|---|---|---|
| **有哪些信号** | `Services/Communication/com_cfg.h` | `Com_SignalIdEnum` | **必须与 `Com_Signals[]` 表顺序一一对应**，见 §6.11 |
| **信号的位布局** | `Services/Communication/com_cfg.c` | `Com_Signals[]` 的 `BitPosition` / `BitSize` | 同一 PDU 内不能重叠 |
| **信号类型** | `com_cfg.c` | `Com_Signals[]` 的 `Type` | **调用方传的变量类型必须匹配**（`void*`，编译器不检查） |
| **字节序** | `com_cfg.c` | `Com_Signals[]` 的 `Endian` | Intel=小端线性；Motorola=大端锯齿 |
| **物理值换算** | `com_cfg.c` | `Com_Signals[]` 的 `ScalingFactor` / `ScalingOffset` | 应用写的是物理值，线上是 `(物理-offset)/factor` |
| **初值** | `com_cfg.c` | `Com_Signals[]` 的 `InitValue` | 也是超时后的替代值 |
| **什么时候发** | `com_cfg.c` | `Com_Signals[]` 的 `TransferProperty` + `Com_TxPdus[]` 的 `TxMode` | 见 §5.5.2 |
| **发送周期** | `com_cfg.c` | `Com_TxPdus[]` 的 `TxPeriodTicks` | 单位是 **COM 主函数周期**（10ms），不是毫秒 |
| **PDU 对外编号** | `com_cfg.c` | `Com_TxPdus[]` / `Com_RxPdus[]` 的 `TxPduId` / `RxPduId` | **必须与 PduR 的 Src/Dest 对应**，见 §6.9 |
| **信号属于哪条 PDU** | `com_cfg.c` | `Com_TxPdus[]` 的 `FirstSignalIdx` + `SignalCount` | `FirstSignalIdx + SignalCount` 不能越过下一条的起点 |
| **各表容量上限** | `com_cfg.h` | `COM_MAX_SIGNALS` / `COM_MAX_TX_PDUS` / `COM_MAX_RX_PDUS` / `COM_MAX_PDU_LEN` | 必须 ≥ 实际项数/长度 |
| **COM 主函数周期** | `com_cfg.h` | `COM_MAIN_FUNCTION_PERIOD_MS`（现 10） | 改了要同步 `EcuM_TaskCom` 的延时（自动引用此宏） |

#### 5.5.1 当前信号表（17 个）

| ID | 符号 | 类型 | 位置 | 位宽 | 字节序 | factor | offset | 初值 | 传输属性 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | `COM_SIG_DOOR_OPEN` | BOOLEAN | bit0 | 1 | — | 1 | 0 | 0 | PENDING |
| 1 | `COM_SIG_WINDOW_POS` | UINT8 | bit1 | 7 | LITTLE | 1 | 0 | 0 | TRIGGERED_ON_CHANGE |
| 2 | `COM_SIG_LOCK_STATE` | UINT8 | bit8 | 2 | LITTLE | 1 | 0 | 0 | PENDING |
| 3 | `COM_SIG_VEHICLE_SPEED` | UINT16 | bit10 | 16 | LITTLE | 1 | 0 | 0 | PENDING |
| 4 | `COM_SIG_BATTERY_VOLTAGE` | UINT16 | bit0 | 16 | LITTLE | **10** | 0 | 12000 | **TRIGGERED** |
| 5 | `COM_SIG_ENGINE_TEMP` | UINT8 | bit16 | 8 | LITTLE | 1 | **-40** | 90 | PENDING |
| 6 | `COM_SIG_BE_UINT16` | UINT16 | bit0 | 16 | **BIG** | 1 | 0 | 0x1234 | PENDING |
| 7 | `COM_SIG_BE_UINT8` | UINT8 | bit16 | 8 | **BIG** | 1 | 0 | 0xAB | PENDING |
| 8 | `COM_SIG_RX_DOOR_OPEN` | BOOLEAN | bit0 | 1 | — | 1 | 0 | 0 | PENDING |
| 9 | `COM_SIG_RX_WINDOW_POS` | UINT8 | bit1 | 7 | LITTLE | 1 | 0 | 0 | PENDING |
| 10 | `COM_SIG_RX_LOCK_STATE` | UINT8 | bit8 | 2 | LITTLE | 1 | 0 | 0 | PENDING |
| 11 | `COM_SIG_RX_VEHICLE_SPEED` | UINT16 | bit10 | 16 | LITTLE | 1 | 0 | 0 | PENDING |
| 12 | `COM_SIG_RX_BATTERY_VOLTAGE` | UINT16 | bit0 | 16 | LITTLE | 10 | 0 | 12000 | PENDING |
| 13 | `COM_SIG_RX_ENGINE_TEMP` | UINT8 | bit16 | 8 | LITTLE | 1 | -40 | 90 | PENDING |
| 14 | `COM_SIG_RX_BE_UINT16` | UINT16 | bit0 | 16 | BIG | 1 | 0 | 0x1234 | PENDING |
| 15 | `COM_SIG_RX_BE_UINT8` | UINT8 | bit16 | 8 | BIG | 1 | 0 | 0xAB | PENDING |
| 16 | `COM_SIG_RX_DCM_TEST` | UINT16 | bit0 | 16 | LITTLE | 1 | 0 | 0 | PENDING |

**为什么 ID 8..16 的传输属性都是 PENDING？**
因为它们是**接收**信号，`TransferProperty` 在接收方向不生效（那个字段只影响发送触发）。COM 靠 `Com_SigToIpdu[sigId] >= TxPduCount` 来拒绝写接收信号。

#### 5.5.2 当前 PDU 表

**TX PDU（3 条）**

| 内部下标 | `TxPduId` | 长度 | FirstSignalIdx | Count | TxMode | PeriodTicks | 实际周期 | 发到 |
|---|---|---|---|---|---|---|---|---|
| 0 | **0** | 8 | 0 | 4 | PERIODIC | 10 | **100 ms** | CAN0 0x100 |
| 1 | **1** | 4 | 4 | 2 | DIRECT | 0 | 事件触发 | CAN0 0x200 **+** CAN1 0x300 |
| 2 | **2** | 4 | 6 | 2 | PERIODIC | 50 | **500 ms** | CAN1 0x400 |

**RX PDU（4 条）**

| 内部下标 | `RxPduId` | 长度 | FirstSignalIdx | Count | 来自 |
|---|---|---|---|---|---|
| 0 | **0** | 8 | 8 | 4 | CAN0 0x100 |
| 1 | **1** | 4 | 12 | 2 | CAN0 0x200 |
| 2 | **2** | 4 | 14 | 2 | CAN1 0x300 |
| 3 | **3** | 2 | 16 | 1 | CAN1 0x400 |

#### 5.5.3 发送时机怎么决定（两个字段配合）

`Com_MainFunctionTx` 每 10ms 跑一次，对每条 TX PDU：

| `TxMode` | 行为 |
|---|---|
| `PERIODIC` | `periodCounter++`，到 `TxPeriodTicks` 就发，然后清零 |
| `DIRECT` | 只看 `txRequested`，有就发，发完清掉 |
| `MIXED` | 两者都看（周期到 **或** 被请求，都发） |
| `NONE` | 永不发（用于 RX PDU 占位） |

`txRequested` 由 `Com_SendSignal` 根据信号的 `TransferProperty` 置位：

| `TransferProperty` | 写入后 |
|---|---|
| `PENDING` | 只更新影子，**不触发**发送（等下个周期带出去） |
| `TRIGGERED` | 置 `txRequested = TRUE` |
| `TRIGGERED_ON_CHANGE` | 比较 raw 是否变化，**变了才**置 `txRequested` |

**当前配置的效果**：
- PDU 0 = PERIODIC → 不管有没有人写，每 100ms 发一次
- PDU 1 = DIRECT + BatteryVoltage 是 TRIGGERED → 应用一写电池电压就发
- PDU 2 = PERIODIC → 每 500ms 发一次

> ⚠️ **当前应用（车窗控制器）不写 BatteryVoltage / EngineTemp，所以 PDU 1 永远不会触发，0x200 / 0x300 不发。** 这是设计后果，不是 bug。见 §十。

### 5.6 RTE 层（直接改文件）

| 想改什么 | 文件 | 具体位置 | 联动检查 |
|---|---|---|---|
| **端口名 / 数据元名** | `Rte/rte_doorstatus.h` | 函数声明 | 应用层调用点 |
| **端口 → COM 信号 ID 的映射** | `Rte/rte_doorstatus.c` | 函数体里的 `ComConf_*` 宏 | 宏定义在 `com_cfg.h` |
| **增加一个新端口** | 新建 `Rte/rte_<port>.c/.h` | — | 要在 IDE 里加该目录的 include path（已在） |

**当前端口**：`DoorStatus`，8 个数据元（4 读 + 4 写）。

**RTE 是手写的"生成物"** —— 真实项目里这些函数由生成器根据 ARXML 产出，开发者一行不写。

### 5.7 EcuM 层（直接改文件）

| 想改什么 | 文件 | 具体位置 | 联动检查 |
|---|---|---|---|
| **BSW 初始化顺序** | `Services/System/ecum.c` | `EcuM_Init()` | 顺序是**硬约束**，见 §5.7.1 |
| **任务划分（几个任务/优先级/栈）** | `Services/System/ecum_cfg.c` | `EcuM_Tasks[]` | 栈总字节数 ≤ `configTOTAL_HEAP_SIZE`（8192） |
| **COM 主函数任务的周期** | `Services/System/ecum.c` | `EcuM_TaskCom` 里的 `vTaskDelayUntil` | 自动引用 `COM_MAIN_FUNCTION_PERIOD_MS` |
| **BusOff / ErrorState 的处理** | `Services/System/ecum.c` | `EcuM_CanIfBusOff` / `EcuM_CanIfErrorState` | 真实项目归 CanSM |

#### 5.7.1 初始化顺序为什么不能改

```
① 板级：Clock → Siul2_Port → IntCtrl        引脚不配好，外设就没信号线
② BSW ：Can → CanIf → PduR → Com → Rte      上层依赖下层，下层必须先好
③ Hook：注册 BusOff / ErrorState             必须在启动总线前接好
④ 总线：SetControllerMode(STARTED)           ★ 永远最后
```

第 ④ 步必须最后：控制器一启动就能收帧，上层回调没接全的话那一帧就丢了。

#### 5.7.2 任务表

| 任务名 | 入口 | 优先级 | 栈（字） | 周期 | 干什么 |
|---|---|---|---|---|---|
| `Task_Com` | `EcuM_TaskCom` | **3** | 256 | 10 ms | `Com_MainFunctionTx/Rx` |
| `Task_WinCtrl` | `App_WindowControl_Task` | **2** | 256 | 100 ms | 车窗控制业务逻辑 |

**堆预算**（`configTOTAL_HEAP_SIZE = 8192`）：

```
Task_Com      TCB 92 + 栈 1024  ≈ 1116
Task_WinCtrl  TCB 92 + 栈 1024  ≈ 1116
空闲任务      MINIMAL_STACK_SIZE 90 字 ≈ 452
定时器任务    180 字 ≈ 812
定时器队列    ≈ 240
────────────────────────────────
合计          ≈ 3736    → 富余约 4.4 KB
```

### 5.8 应用层（直接改文件）

| 想改什么 | 文件 | 具体位置 | 联动检查 |
|---|---|---|---|
| **安全规则阈值** | `Application/WindowCtrl/app_windowcontrol.h` | `APP_WIN_*` 宏 | — |
| **车速联锁阈值** | `app_windowcontrol.c` | `APP_WIN_SPEED_LOCK_RAWH`（20.00 km/h） | 物理单位是 0.01 km/h |
| **速度曲线** | `app_windowcontrol.c` | `App_Win_CalcStep()` | 步长单位 = %/周期 |
| **任务周期** | `app_windowcontrol.c` | `APP_WIN_TASK_PERIOD_MS`（100） | 改了要同步步长的物理含义 |
| **调试注入** | `app_windowcontrol.c` | `g_AppSimMotorStuck` | 调试器里置 TRUE 模拟堵转 |

**当前安全规则**：

| 规则 | 内容 | 状态 |
|---|---|---|
| R1 | 车门打开 → 强制全开（逃生通路） | `EffectiveTarget = 100` |
| R2 | 中控锁锁止 → 禁止一切动作 | `APP_WIN_BLOCKED_LOCKED` |
| R3 | 车速 > 20 km/h → 禁止上升，允许下降 | `APP_WIN_BLOCKED_SPEED` |
| R4 | 梯形速度曲线（距离 >30% 走 6、>10% 走 3、否则 1） | 平滑逼近 |
| R5 | 连续运动 4 秒未到位 → 判定堵转 | `APP_WIN_FAULT_OVERTIME` |
| R6 | 故障保持 2 秒后自动复位 | 回到 `IDLE` |

**信号语义（重要）**：

| 方向 | 信号 | 含义 |
|---|---|---|
| **RX** `WindowPos` | 总线发来 | **目标位置**（驾驶员要去的位子） |
| **TX** `WindowPos` | ECU 发出 | **实际位置**（电机按曲线积分出来的） |

### 5.9 全链对照：一帧 0x100 发送时，谁贡献了什么

| 层 | 贡献的参数 | 值 | 谁决定的 |
|---|---|---|---|
| 应用 | 物理量 | 窗位置 50 | `app_windowcontrol.c` |
| **RTE** | COM 信号 ID | 1 | `rte_doorstatus.c` |
| **COM** | 位偏移 / 位宽 / 字节序 | bit1 / 7 / LITTLE | `com_cfg.c` |
| | 打包后的字节 | `65 C1 5D …` | `com.c` |
| | 发送时机 | 每 10 个 tick | `com_cfg.c` 的 `TxMode` |
| | 对外 PduId | 0 | `com_cfg.c` 的 `TxPduId` |
| **PduR** | 目标 CanIf PduId | 0 | `pdur_cfg.c` |
| **CanIf** | `Hth` | 4 | `canif_cfg.c` |
| | `CanId` | 0x100 | `canif_cfg.c` |
| | `Length` | 8 | `canif_cfg.c` |
| **MCAL** | 控制器 | 0 (CAN0) | `can_cfg.c` |
| | 物理邮箱 | 1 | `can_cfg.c` |
| | `PayloadSize` | 8 | `can_cfg.c` |
| RTD/硬件 | 位时序、邮箱寄存器 | — | CT 配置 |

### 5.10 Det 层（开发错误追踪 —— 横向模块，不属于任何一层）

**Det 不在纵向分层里**：所有 BSW 模块都调用它，它不调用任何业务模块。它是"报错热线"，不是上下级。

| 想改什么 | 位置 | 具体位置 | 联动检查 |
|---|---|---|---|
| **全局开关** | 编译选项 | `-DBSW_DEV_ERROR_DETECT=STD_OFF` | Release 构建用；不加则默认全开 |
| **单个模块开关** | 各模块头文件 | `<MOD>_DEV_ERROR_DETECT` | 优先级高于全局，一般不用单独改 |
| **模块号** | 各模块头文件 | `<MOD>_MODULE_ID` | **必须用 AUTOSAR 标准值**，不能自编 |
| **API 编号** | 各模块头文件 | `<MOD>_ApiIdType` | 本模块内唯一即可 |
| **错误编号** | 各模块头文件 | `<MOD>_ErrorIdType` | 本模块自定义含义，取值 0x01..0xFF |
| **Det 本体** | `RTD/src/Det.c` | NXP 官方实现，**不要改** | 内置"最近 5 条错误"环形记录 |

**AUTOSAR 标准模块号（本工程已用的）**：

```
Det  = 0x0F (15)      EcuM = 0x0A (10)      Com  = 0x32 (50)
PduR = 0x35 (53)      Dcm  = 0x37 (55)      CanIf = 0x3C (60)
Can  = 0x50 (80)
```

**当前集成情况（6 个模块 / 48 处调用）**：

| 模块 | Det 调用 | 主要检查点 |
|---|---|---|
| `can.c` | 17 | 未初始化、句柄不存在、控制器状态、长度超限、FD 配置不匹配 |
| `com.c` | 14 | 未初始化、SignalId 越界、方向错误、容量不足、RxPduId 未知 |
| `canif.c` | 9 | 未初始化、PduId 不存在、长度超限、Hrh 越界 |
| `pdur.c` | 6 | 未初始化、路由不存在、配置回调不匹配 |
| `dcm.c` | 1 | 收到 NULL 指针 |
| `ecum.c` | 1 | 任务创建失败（堆不足） |

**Det 的运行时观察点**（调试器 Watch 窗口直接看）：

```c
Det_ModuleId[0][0..4]     /* 最近 5 条错误的模块号 */
Det_InstanceId[0][0..4]
Det_ApiId[0][0..4]        /* 哪个函数 */
Det_ErrorId[0][0..4]      /* 什么错 */
Det_numEventErrors[0]     /* 累计次数 */
Det_OverflowErrorFlag[0]  /* 是否超过 5 条（溢出过） */
```

---

## 六、容易踩的联动关系

### 6.1 波特率怎么算

FlexCAN 的位时间由 4 段组成，寄存器里的值是"实际值 − 1"：

```
TQ 总数 = 1(同步段) + (PROPSEG+1) + (PSEG1+1) + (PSEG2+1)
分频系数 = PRESDIV + 1
波特率   = fCANCLK / 分频系数 / TQ总数
采样点   = (1 + PROPSEG+1 + PSEG1+1) / TQ总数
```

**本工程实例**（fCANCLK = 80 MHz）：

```
PROPSEG=9 → 10，PSEG1=4 → 5，PSEG2=3 → 4
TQ总数 = 1 + 10 + 5 + 4 = 20
分频   = 7 + 1 = 8
波特率 = 80,000,000 / 8 / 20 = 500 kbps ✓
采样点 = (1 + 10 + 5) / 20 = 80% ✓
```

**改波特率时**：在 CT 的 FlexCAN 位时序里改，改完确认 "Baudrate" 显示的是你要的值。
若同时改了 CAN 时钟频率，**必须重算**，否则实际波特率会变。

### 6.2 句柄编号分段

`CAN_HTH_FIRST_INDEX = 4` 意味着：

- 接收句柄只能取 **0..3**
- 发送句柄只能取 **4..7**

**为什么要分段**：见 `can_cfg.h` 里的详细说明。简单说 —— 若两类各自从 0 开始，
"句柄 0"会同时表示"接收对象 0"和"发送对象 0"。现在不会出错（查不同的表），
但只要上层把句柄存下来待用就会语义混淆，且不报错。

**增加接收对象时**：如果 Hrh 要超过 3，就要把 `CAN_HTH_FIRST_INDEX` 调大
（比如加到 8），同时检查所有 `Hth` 是否还 ≥ 它。

### 6.3 邮箱编号的两个上限

`Can_RxObjects[].MbIdx` 和 `Can_TxObjects[].MbIdx` 都受两个约束：

| 上限 | 来源 | 现 CAN0 / CAN1 |
|---|---|---|
| `max_num_mb` | CT 的 FlexCAN 配置 | 96 / 64 |
| `CAN_MB_BUF_COUNT` | `Mcal/can.c`（仅接收方向） | 8 |

`MbIdx` 超了会怎样：

- 超 `max_num_mb` → RTD 的 `DevAssert`（`DEV_ERROR_DETECT = ON` 时会停住）
- 超 `CAN_MB_BUF_COUNT`（接收）→ `can.c` 的边界检查会**静默丢弃**该邮箱的帧

### 6.4 接收掩码不能省

`Can_RxObjects[]` 里 `CanIdMask` **必须显式填**。原因：

S32K344 的 `RXMGMASK` / `RXIMR` 复位值都是 **0**，而 FlexCAN 的匹配规则是
`(接收字段 XOR 邮箱字段) AND 掩码 == 0` —— 掩码为 0 时恒等成立，**任何 ID 都匹配**。

常用取值：

```c
CAN_RX_MASK_EXACT_STD   0x7FF       标准帧精确匹配
CAN_RX_MASK_EXACT_EXT   0x1FFFFFFF  扩展帧精确匹配
CAN_RX_MASK_ACCEPT_ALL  0x0         收所有 ID（调试时方便）
0x7F0                               收 0x100~0x10F（低 4 位不比对）
```

### 6.5 改了 CT 之后必须回头检查 MCAL

| 你改了 CT 的… | 必须回 MCAL 检查 |
|---|---|
| `max_num_mb` 调小 | 所有 `MbIdx` 是否还小于它 |
| 回调函数名 | `Mcal/can.c` 里是否有同名实现（否则链接报 `undefined reference`） |
| 中断优先级 | 是否仍 ≥ 1（FreeRTOS 要求） |
| 增加了 CAN 实例 | `CAN_CONTROLLER_COUNT` 和 `Can_Controllers[]` |
| FD 开关 | `Can_Write` 里的长度上限、`Can_PduType.length`（是 `uint8`） |
| **FreeRTOS 堆大小** | 任务表里的栈总和是否还放得下 |

### 6.6 错误中断使能的选择

`Mcal/can.c` 的 `Can_EnableErrorInterrupts()` 里目前使能两项：

```c
FlexCAN_Ip_SetErrorInt(Controller, FLEXCAN_IP_INT_BUSOFF, TRUE);   /* Bus-off */
FlexCAN_Ip_SetErrorInt(Controller, FLEXCAN_IP_INT_ERR,    TRUE);   /* 通用错误 */
```

注释里还留了两行被注释掉的 `RX_WARNING` / `TX_WARNING` —— 需要更细的错误追踪时打开。

⚠️ 这些调用**必须在 Freeze 模式窗口内**（紧跟 `FlexCAN_Ip_Init` 之后），
因为 RTD 会检查 `FlexCAN_IsFreezeMode()`（`DEV_ERROR_DETECT = ON` 时直接返回错误）。
调用过 `Can_SetControllerMode(STARTED)` 之后就不能再改了。

### 6.7 CanIf ↔ MCAL 的跨层联动（三处静默失败）

这三处**编译不报错、链接不报错、运行也不报错**，只是行为不对：

| # | 不一致的地方 | 后果 | 症状 |
|---|---|---|---|
| 1 | CanIf 的 `Hth` ≠ MCAL 对应的句柄 | 发到错误的邮箱/控制器 | 分析仪在**另一条总线**上收到帧，或收到错误的 ID |
| 2 | CanIf 的 `Hrh` ≠ MCAL 的 `Hrh` | `CanIf_RxIdxByHrh[]` 查不到 | 帧收到了但**不上报**（`CanIf_RxIndication` 在 `idx == CANIF_NO_PDU_INDEX` 处静默返回） |
| 3 | CanIf 的 TX `CanId` 填错 | MCAL 不校验 TX ID，直接发 | 发出去的 ID 是错的，**上层以为是"发送成功"** |

第 3 条最危险：**发送方向的 CAN ID 完全由 CanIf 决定，MCAL 一个字都不检查。**

**改参数时的联动表**：

| 你改了什么 | 必须同时检查 |
|---|---|
| MCAL `Can_TxObjects[].Hth` | CanIf `CanIf_TxObjects[].Hth` |
| MCAL `Can_TxObjects[].ControllerId` | CanIf `CanIf_TxObjects[].ControllerId` |
| MCAL `Can_RxObjects[].Hrh` | CanIf `CanIf_RxObjects[].Hrh` |
| MCAL `Can_RxObjects[].CanId` / `CanIdMask` | 无（只此一份） |
| MCAL `PayloadSize` | CanIf `Length` 必须 ≤ 它 |
| 新增一条接收报文 | **MCAL 的 `Can_RxObjects[]` 和 CanIf 的 `CanIf_RxObjects[]` 两边都要加** |
| 新增一条发送报文 | **MCAL 的 `Can_TxObjects[]` 和 CanIf 的 `CanIf_TxObjects[]` 两边都要加** |

### 6.8 三类参数的关系（别混淆）

| 参数对 | 关系 | 说明 |
|---|---|---|
| MCAL `Hth`/`Hrh` ↔ CanIf `Hth`/`Hrh` | **引用** | CanIf 指着 MCAL 的编号，不是重复定义 |
| MCAL `PayloadSize` ↔ CanIf `Length` | **两个层次** | 前者是"邮箱能装多少"（硬件能力），后者是"这条 PDU 多长"（协议约定） |
| MCAL `Can_TxObjects[].ControllerId` ↔ CanIf 的同名字段 | ⚠️ **真重复** | 两处都要改（CanIf 需要它做 PDU 通道模式检查，因为 Hth 反推不出控制器） |

### 6.9 ★ PduR ↔ COM 的 PduId 对应（**最容易搞错**）

| COM 的字段 | 对应 PduR 的哪个字段 | 方向 |
|---|---|---|
| `Com_TxPdus[].TxPduId` | **`PduR_TxPaths[].SrcPduId`** | 下行 |
| `Com_RxPdus[].RxPduId` | **`PduR_RxDests_*[].DestPduId`**（**不是 SrcPduId**） | 上行 |

**规律：`SrcPduId` = "谁发起" 的编号，`DestPduId` = "谁接收" 的编号。**

```
【TX 下行】COM 是发起方
  COM.TxPduId 0 ──> SrcPduId 0 ──> Dests{ DestPduId 0 } ──> CanIf.TxPduId 0
  COM.TxPduId 1 ──> SrcPduId 1 ──> Dests{ 1, 2 }        ──> CanIf.TxPduId 1, 2

【RX 上行】CanIf 是发起方，COM 是接收方
  CanIf.RxPduId 0 ──> SrcPduId 0 ──> Dests{ DestPduId 0 } ──> COM.RxPduId 0
  CanIf.RxPduId 3 ──> SrcPduId 3 ──> Dests{ 3, 0 }        ──> COM.RxPduId 3
                                                              Dcm.RxPduId 0
```

⚠️ **本工程里所有编号碰巧都是 0,1,2,3，肉眼完全区分不出来。** 只有字段名能区分。

### 6.10 COM 内部的三种"身份"

| 身份 | 谁给的 | 作用 | 典型位置 |
|---|---|---|---|
| `signalId` | **上层**（Rte） | 查"信号说明书" | `Com_Signals[SignalId]` |
| `pduIndex` | **COM 内部** | 数组下标（`Com_TxState[]` / `Com_TxPdus[]`） | `Com_SigToIpdu[SignalId]` 的结果 |
| `TxPduId` / `RxPduId` | **配置** | 和 PduR 打交道 | `PduR_ComTransmit(pdu->TxPduId, …)` |

**注意 `pduIndex` 和 `PduId` 是两回事**：现在都是 0,1,2，但 `pduIndex` 是数组下标（永远连续），`TxPduId` 是配置里填的编号（可以填 100/205）。

**两级翻译**：

```
signalId ──Com_SigToIpdu[]──> pduIndex ──TxPdus[].TxPduId──> TxPduId
```

**`signalId` 只在 `Com_SendSignal` / `Com_ReceiveSignal` 里出现；一旦打包完成，后续只认 `pduIndex` 和 `TxPduId`。**

### 6.11 COM 配置的自洽约束（改信号时必查）

`com_cfg.h` 的枚举和 `com_cfg.c` 的信号表是**两套手工维护的平行事实**，C 语言不检查它们一致。

| 约束 | 违反后果 |
|---|---|
| 枚举值 = 信号表的**数组下标** | 在表中间插信号 → 后续所有 ID 错位 → **静默数据错乱** |
| `COM_SIG_COUNT` = 信号表项数 | 表尾加信号忘了加枚举 → 新信号用不了 |
| 同一 PDU 内 `BitPosition + BitSize` 不重叠 | 两个信号抢同一位 → 后打包的覆盖前面的 |
| `FirstSignalIdx + SignalCount` 不越界到下一 PDU | 解包/打包读错信号 |
| `Length` ≥ 信号占用的最大位/8 | 越界的位被**静默丢弃**（`Com_SetBit` 里有边界检查，不越界写、也不报错） |

**改信号时，一次改完这三处**：

1. `com_cfg.h` 的 `Com_SignalIdEnum`（数值 + `COM_SIG_COUNT`）
2. `com_cfg.c` 的 `Com_Signals[]`（在表里插入/追加）
3. `com_cfg.c` 的 `Com_TxPdus[]` / `Com_RxPdus[]`（若跨 PDU 边界，调整 `FirstSignalIdx` / `SignalCount`）

### 6.12 信号类型必须与调用方变量类型一致

`Com_SendSignal` / `Com_ReceiveSignal` 的参数是 `void*`，**编译器不检查类型**。

```c
uint16 wrong = 50U;
Com_SendSignal(ComConf_ComSignal_WindowPos, &wrong);   /* 编译通过，但信号是 UINT8 */
                                                       /* COM 只读低字节 → 静默错 */
```

**判据**：`Com_Signals[].Type` 必须与调用点传的变量类型完全一致。

真实项目靠 RTE 生成来保证——生成的函数形参类型就是配置的类型：

```c
Std_ReturnType Rte_Write_DoorStatus_WindowPos(uint8 data);   /* ★ uint8 由 Type 决定 */
```

### 6.13 中断上下文里能做什么

上行整条链（CAN ISR → CanIf → PduR → COM）**全在一个中断里同步执行**：

| 规则 | 原因 |
|---|---|
| 只做位运算和内存写入 | 拖长中断会阻塞同级/低级中断，可能丢帧 |
| **不能**调用 `vTaskDelay` / 阻塞 API | 中断上下文 |
| **不能**做业务逻辑 | 会拖垮系统响应 |
| 需要延后处理的数据必须 **memcpy** | `PduInfoPtr` 指向 MCAL 的静态缓冲，中断一返回就失效 |

**COM 在中断里只写到 `Com_SigShadowValue[sigId]` 就返回**，应用在任务里自己来读——这样应用代码永远不会在中断上下文跑。

### 6.14 ★ Det 开关的两个层次（Debug / Release）

**机制**：每个模块的开关都写成下面的形式，所以**源码里永远是 STD_ON**，量产配置只存在于构建选项里。

```c
#ifndef CANIF_DEV_ERROR_DETECT
    #ifdef BSW_DEV_ERROR_DETECT
        #define CANIF_DEV_ERROR_DETECT  (BSW_DEV_ERROR_DETECT)
    #else
        #define CANIF_DEV_ERROR_DETECT  (STD_ON)
    #endif
#endif
```

| 构建 | 编译选项 | 效果 |
|---|---|---|
| **Debug**（开发/调试） | 不加任何 `-D` | 全部模块 `STD_ON`，Det 全功能 |
| **Release**（量产） | `-DBSW_DEV_ERROR_DETECT=STD_OFF` | 全部模块 `STD_OFF`，Det 调用全消失 |

**为什么用 `#ifndef` + 编译选项，而不是手改源码**

> 手改成 `STD_OFF` 之后，下次调 bug 时会发现所有错误报告都静默了，白白浪费半天；
> 更糟的是改回去时可能漏掉几个模块。
> **源码保持 STD_ON，量产配置放在构建配置里 —— 不可能忘。**

**两个层次的资源节省（实测）**：

| 层次 | 操作 | 省什么 |
|---|---|---|
| **第 1 层** | `-DBSW_DEV_ERROR_DETECT=STD_OFF` | 48 处调用点消失（每模块上百字节） |
| **第 2 层** | 把 `RTD/src/Det.c` 从构建排除 | **2,636 字节 Flash + 1,928 字节 RAM** |

**第 2 层的前提**：第 1 层做完后必须是"零引用"。验证方法：

```
编译全部模块时加 -DBSW_DEV_ERROR_DETECT=STD_OFF，
然后 nm 检查每个 .o 是否还有 Det_ReportError 的未定义符号。
有残留 → 说明某个模块的开关没跟着走 → 不能移除 Det.c。
```

**⚠️ 量产关 Det，但 Dem 绝对不能关**

| | Det | Dem |
|---|---|---|
| 抓什么 | 程序员的 bug | 车辆的真实故障（Bus-off、传感器开路） |
| 谁看 | 开发者（调试器） | 诊断仪（4S 店读 DTC） |
| 量产 | ❌ 关掉 | ✅ **必须开** |

**Det 关了，车出故障你不知道；Dem 关了，4S 店读不出故障码，车没法修。**

### 6.15 ★ 跨层一致性的三道防线

**根问题**：本模板没有 AUTOSAR 生成器，5 张配置表是**手工维护的五份事实**。任何一处对不上，表现都是"编译过、链接过、运行不报错、但数据不对"——静默失败。

**真实 AUTOSAR 怎么解决**：单一数据源（ARXML）+ 生成器 + 工具校验 + 符号化引用，问题在**生成期**就被消灭，所以它不需要运行期检查。

**我们的补偿：三道防线**

| 防线 | 手段 | 抓住什么 | 时机 | 成本 |
|---|---|---|---|---|
| **第一道** | **符号化引用** | 引用了不存在的对象（句柄 / PduId / 配置） | **编译期** | 零运行时 |
| **第二道** | **编译期断言** | 表项数与数量宏脱节、DestCount 与数组不符 | **编译期** | 零运行时 |
| **第三道** | **启动配置自检** | 编译期算不出来的：Length > PayloadSize、位区间越界/重叠 | **启动时** | ~几百字节 |

#### 6.15.1 第一道：符号化引用（谁定义、谁引用）

| 层 | 定义符号 | 文件 | 谁引用 |
|---|---|---|---|
| MCAL | `CanConf_TxObject_Hth4..7`<br>`CanConf_RxObject_Hrh0..3` | `Mcal/can_cfg.h` | CanIf |
| CanIf | `CanIfConf_TxPdu_0..3`<br>`CanIfConf_RxPdu_0..3` | `EcuAbstraction/canif_cfg.h` | PduR |
| COM | `ComConf_ComTxPdu_0..2`<br>`ComConf_ComRxPdu_0..3` | `Services/Communication/com_cfg.h` | PduR |

**效果**：删掉上游的一个对象，下游立刻报 `error: 'XxxConf_...' undeclared`。

#### 6.15.2 第二道：编译期断言（数量与表项数对齐）

每个 `_cfg.c` 末尾都有断言，形如：

```c
typedef char Can_TxObjectCountAssert[
    ((sizeof(Can_TxObjects) / sizeof(Can_TxObjects[0])) == (uint32)CAN_TX_OBJECT_COUNT) ? 1 : -1];
```

| 文件 | 断言 | 抓什么 |
|---|---|---|
| `Mcal/can_cfg.c` | `Can_TxObjectCountAssert` | 发送对象数 ≠ `CAN_TX_OBJECT_COUNT` |
| | `Can_RxObjectCountAssert` | 接收对象数 ≠ `CAN_RX_OBJECT_COUNT` |
| | `Can_ControllerCountAssert` | 控制器数 ≠ `CAN_CONTROLLER_COUNT` |
| `EcuAbstraction/canif_cfg.c` | `CanIf_TxPduCountAssert` | TX PDU 数 ≠ `CANIF_TX_PDU_COUNT` |
| | `CanIf_RxPduCountAssert` | RX PDU 数 ≠ `CANIF_RX_PDU_COUNT` |
| `Services/Communication/com_cfg.c` | `Com_SignalCountAssert` | 信号表项数 ≠ `COM_SIG_COUNT` |
| | `Com_TxPduCountAssert` / `Com_RxPduCountAssert` | PDU 数 ≠ 数量宏 |
| | `Com_SignalIdMaxAssert` | 枚举最后一个信号 +1 ≠ `COM_SIG_COUNT` |
| `Services/Communication/pdur_cfg.c` | `PduR_*Dests*CountAssert` | `DestCount` ≠ Dests 数组实际项数 |
| | `PduR_TxPathCountAssert` / `PduR_RxPathCountAssert` | 路径数 ≠ 数量宏 |

**原理**：`typedef char X[条件 ? 1 : -1]`，条件不成立 → 数组长度为负 → 编译报错。C99 没有 `static_assert`，这是标准惯用法。

#### 6.15.3 第三道：启动配置自检（`Services/System/bswcheck.c`）

**只查编译期算不出来的**：

| 检查 | 为什么编译期做不到 |
|---|---|
| ① CanIf 的 `Length` ≤ MCAL 的 `PayloadSize` | 两个数字在两个 `.c` 的 static 表里，同一编译单元看不到 |
| ② CanIf 的 `Hth` / `Hrh` 在 MCAL 存在 | 纵深防御（第一道已挡，防有人绕开符号名写裸数字） |
| ③ COM 信号位区间不越过 PDU 位宽 | `BitPosition + BitSize` 要运行时算 |
| ④ COM 同 PDU 内两信号位区间不重叠 | 需要两两比较，C 没有 constexpr |
| ⑤ COM 的 `FirstSignalIdx + SignalCount` 不越尾 | 同上 |

**上报方式**：复用 `ECUM_MODULE_ID`，`ApiId = ECUM_API_BSW_CHECK`，**`InstanceId` = 出问题的表项下标** → 调试器里一眼看出是第几行配置错了。

| ErrorId | 含义 |
|---|---|
| `0x20` | CanIf Length > MCAL PayloadSize |
| `0x21` | CanIf Hth 在 MCAL 不存在 |
| `0x22` | CanIf Hrh 在 MCAL 不存在 |
| `0x23` | COM 信号位区间重叠 |
| `0x24` | COM 信号超出 PDU 位宽 |
| `0x25` | COM 信号区间越过信号表尾 |

**严格模式开关**（`bswcheck.h`）：

```c
#define BSWCHECK_STRICT   (0U)   /* 0 = 只报 Det 继续启动；1 = 报完停在死循环 */
```

开发期建议改 `1`；量产保持 `0`。

**观察点**：`g_BswCheckStatus`

```c
g_BswCheckStatus.ErrorCount      /* 0 = 全部通过 */
g_BswCheckStatus.LastErrorId     /* 最后一个错误编号 */
g_BswCheckStatus.LastInstance    /* 最后一处出问题的表项下标 */
g_BswCheckStatus.CheckedCanIf    /* 检查了几条 CanIf PDU（发送 4 + 接收 4 = 8） */
g_BswCheckStatus.CheckedComSig   /* 检查了几个 COM 信号（发送 8 + 接收 9 = 17） */
```

#### 6.15.4 三道防线的边界（诚实说明）

| 能抓 | 抓不到 |
|---|---|
| 引用了不存在的对象 | CAN ID 填错（数值合法但语义错） |
| 表项数与数量宏脱节 | 邮箱号越界（要对照 CT 的 `max_num_mb`） |
| DestCount 与数组不符 | 位时序/波特率算错 |
| Length 超容量、位重叠/越界 | 中断优先级配错 |

**最后那一类（ID 语义、邮箱号、位时序）只能靠 CT 复核清单（§八）和实际总线验证。三道防线不解决它们。**

---

## 七、常见修改场景 step-by-step

### 场景 1：改波特率 500k → 250k

1. 打开 CT → FlexCAN → FlexCAN_Configurations → 位时序
2. 调整 PRESDIV 或段长度，直到界面显示的 Baudrate 是 250 kbps
3. **Generate** 代码
4. Build
5. 验证：看 `generate/src/FlexCAN_Ip_Sa_PBcfg.c` 里的数字变了

> MCAL 层**不用改**。

### 场景 2：新增一个接收报文（CAN0 收 ID 0x2A0）

**MCAL 层（硬件过滤配置）**

1. `Mcal/can_cfg.c` 的 `Can_RxObjects[]` 加一行：
   ```c
   {  4U,   0U,   7U,    0x2A0U, CAN_RX_MASK_EXACT_STD,    FALSE },
   ```
   （Hrh 用 4 —— 但注意 `CAN_HTH_FIRST_INDEX` 现在是 4，见下面第 3 步）
2. `Mcal/can_cfg.h` 把 `CAN_HTH_FIRST_INDEX` 改成 **8**（给接收腾出 0..7）
3. `Mcal/can_cfg.c` 的发送表里所有 `Hth` 改成从 8 开始（否则句柄空间重叠）
4. 确认邮箱 7 < `CAN_MB_BUF_COUNT`(8) ✓ 且 < `max_num_mb`(96) ✓

**CanIf 层（逻辑身份）**

5. `EcuAbstraction/canif_cfg.c` 的 `CanIf_RxObjects[]` 加一行：
   ```c
   {  4U,   4U },      /* RxPduId 4 ← Hrh 4 */
   ```
6. `EcuAbstraction/canif_cfg.c` 的 `CanIf_TxObjects[]` 里所有 `Hth` 跟着改成从 8 开始

**PduR 层（路由）**

7. `Services/Communication/pdur_cfg.c` 的 `PduR_RxPaths[]` 加一行：
   ```c
   {  4U,   1U,   PduR_RxDests_CanIf4 },     /* CanIf 的 RxPduId 4 */
   ```
   并定义 `PduR_RxDests_CanIf4[]`（目标可以是 COM 或 Dcm）

**COM 层（信号）**

8. 若这条报文要解信号 → 走「场景 6」

**验证**

9. Build → 烧录 → 从分析仪发 0x2A0 → 观察

> 如果嫌改句柄号麻烦，可以复用已有邮箱：把新 ID 改成**掩码范围**接收，
> 比如 `CanId=0x200, CanIdMask=0x7F0` 就能覆盖 0x200~0x20F。
> 但这样同一个 Hrh 会收到多个 ID，而我们的 CanIf 是 1:1 映射，
> 无法在软件层再分辨 —— 那时就需要给 CanIf 加回 ID+掩码过滤。

### 场景 3：新增一个发送句柄

1. `Mcal/can_cfg.c` 的 `Can_TxObjects[]` 加一行（例如用 CAN0 邮箱 3）：
   ```c
   {  CAN_HTH_FIRST_INDEX + 4U,  0U,  3U,  FALSE,  8U },
   ```
2. 确认邮箱 3 没被接收表占用（当前接收用的是 5、6）✓
3. `EcuAbstraction/canif_cfg.c` 的 `CanIf_TxObjects[]` 加一行：
   ```c
   {  4U,  CAN_HTH_FIRST_INDEX + 4U,  0U,  0x2A0U,  8U },
   ```
4. `Services/Communication/pdur_cfg.c` 里给上层的 PDU 加目标
5. Build

> 不需要改 CT，也不需要 Generate。

### 场景 4：增加第三路 CAN（CAN2）

1. **CT**：加 FlexCAN 实例 2，配好引脚、位率、中断（`FlexCAN2_1_IRQn` / `FlexCAN2_0_IRQn`）、回调名
2. **CT**：IntCtrl 里登记这两个中断
3. **CT**：Generate
4. **MCAL**：
   - `can_cfg.h`：`CAN_CONTROLLER_COUNT` 改成 3
   - `can_cfg.c`：`Can_Controllers[]` 加 `{2U, &FlexCAN_Config2, &FlexCAN_State2}`
   - `can.c`：`Can_EnableErrorInterrupts` 里对实例 2 生效（现在是按 ControllerId 传的，自动适用）
   - `can.c`：新增 `FlexCAN2_Callback` / `FlexCAN2_ErrorCallback` 两个实现
5. Build

> ⚠️ 回调名如果 CT 里填的是 `FlexCAN2_Callback`，我们必须在 `can.c` 里实现它，
> 否则链接报 `undefined reference`。

### 场景 5：启用 CAN FD

1. **CT**：FlexCAN → `fd_enable = TRUE`，配 FD 位时序、BRS
2. **CT**：Generate
3. **MCAL**：
   - `can.c` 的 `Can_ValidatePdu` / `Can_Write` 里长度上限从 8 放开
   - `can_configRxMailboxes` / TX 的 `txInfo.fd_enable` 改成 TRUE
   - RX 上报时补 `CAN_ID_FD_MASK`（bit31）
   - `Can_PduType.length` 是 `uint8`，最大 255，够用
4. **CanIf**：`CANIF_MAX_PDU_LEN` 改成 64
5. **COM**：`COM_MAX_PDU_LEN` 改成 64
6. Build

> 这是个较大的改动，建议单独做一轮。

### 场景 6：★ 新增一个信号（COM 层）

假设要给 PDU 0 加一个「车窗防夹标志」（1 位，放在 bit26）。

**① 信号表（`Services/Communication/com_cfg.c`）**

在 `Com_Signals[]` 的**第 3 项之后**插入（因为 PDU 0 的信号必须连续）：

```c
{ COM_SIGNAL_TYPE_BOOLEAN, 26U, 1U, COM_ENDIAN_LITTLE, 1, 0, 0, COM_TRANSFER_PENDING, 0U },
```

**② 枚举（`Services/Communication/com_cfg.h`）**

从插入位置开始，**后面所有 ID 全部 +1**，`COM_SIG_COUNT` 17 → 18：

```c
COM_SIG_DOOR_OPEN           = 0,
COM_SIG_WINDOW_POS          = 1,
COM_SIG_LOCK_STATE          = 2,
COM_SIG_WINDOW_ANTIPINCH    = 3,     /* ★ 新增 */
COM_SIG_VEHICLE_SPEED       = 4,     /* 3 → 4 */
...
COM_SIG_COUNT               = 18,    /* 17 → 18 */
```

**③ PDU 表（`com_cfg.c`）**

PDU 0 的 `SignalCount` 4 → 5，**后面两条 PDU 的 `FirstSignalIdx` 全部 +1**：

```c
{ 0U, 0xFFFFU, 8U, 0U, 5U, COM_TX_MODE_PERIODIC, 10U },   /* Count 4→5 */
{ 1U, 0xFFFFU, 4U, 5U, 2U, COM_TX_MODE_DIRECT,    0U },   /* First 4→5 */
{ 2U, 0xFFFFU, 4U, 7U, 2U, COM_TX_MODE_PERIODIC, 50U },   /* First 6→7 */
```

RX 段的 `FirstSignalIdx` 全部 +1（8→9, 12→13, 14→15, 16→17）。

**④ RTE（`Rte/rte_doorstatus.c/.h`）**

加 `Rte_Write_DoorStatus_WindowAntiPinch()`，并在 `com_cfg.h` 加 `ComConf_ComSignal_WindowAntiPinch` 宏。

**⑤ 应用（`Application/WindowCtrl/app_windowcontrol.c`）**

调用 `Rte_Write_DoorStatus_WindowAntiPinch(...)`。

**⑥ 检查**

- 新信号在 bit26，PDU 0 是 8 字节（64 位）→ 26+1=27 ≤ 64 ✓
- 不和现有位重叠（现有最高到 bit25）✓
- `COM_MAX_SIGNALS`（32）≥ 18 ✓

### 场景 7：★ 改发送周期

**只改一个数字**：`Services/Communication/com_cfg.c` 的 `Com_TxPdus[]` 的 `TxPeriodTicks`。

```
TxPeriodTicks = 10  → 100 ms
TxPeriodTicks = 20  → 200 ms
TxPeriodTicks = 50  → 500 ms
TxPeriodTicks = 5   →  50 ms
```

**单位是 COM 主函数周期**（`COM_MAIN_FUNCTION_PERIOD_MS` = 10 ms），不是毫秒。

**若想让周期更细**（比如 5ms）：
1. `com_cfg.h` 的 `COM_MAIN_FUNCTION_PERIOD_MS` 改成 5
2. 所有 `TxPeriodTicks` 翻倍（保持实际周期不变）
3. `EcuM_TaskCom` 自动跟随（它引用同一个宏）
4. 注意 COM 中断/任务负载增加

### 场景 8：★ 改路由（让某条 PDU 多发一路）

假设要让 COM 的 PDU 0 同时发到 CAN1 的 0x300。

1. **`Services/Communication/pdur_cfg.c`**：把 `PduR_TxDests_Com0[]` 从 `{ {0U} }` 改成 `{ {0U}, {2U} }`，`DestCount` 1 → 2
2. 确认 CanIf 的 `PduId 2` 存在且指向 CAN1（见 `canif_cfg.c`）
3. Build

> ⚠️ 两条总线上会出现**数据完全相同**的两帧（PDU 级网关的特征）。

**反过来，如果要在 COM 层做信号级网关**（两条总线用不同布局），那就是两条独立的 COM PDU + 应用层搬运，不是改路由能做的。

### 场景 9：★ 新增一个应用 SWC

假设加「车门锁控制」。

**① 新建目录** `Application/DoorLock/`

**② 写文件**

```
Application/DoorLock/app_doorlock.h
Application/DoorLock/app_doorlock.c     ← 只调用 Rte_*，不碰 COM/CAN
```

**③ 新建 RTE 端口（可选）**

若需要新的信号，先加 COM 信号（场景 6），再在 `Rte/` 加 `rte_doorlock.c/.h`。

**④ 登记任务（`Services/System/ecum_cfg.c`）**

```c
{  "Task_DoorLock",  App_DoorLock_Task,  NULL,  256U,  2U },
```

**⑤ 检查堆**

栈总字节数 + 1116 ≤ 8192。超了就去 CT 改 `configTOTAL_HEAP_SIZE`。

**⑥ IDE**

新目录 `Application/DoorLock` 要加 include path（见 §2.4）。

---

## 八、重新 Generate 后的复核清单

每次在 CT 里改了东西并 Generate 之后，检查这几项：

| # | 检查什么 | 怎么看 |
|---|---|---|
| 1 | 回调名还是那四个吗 | `FlexCAN_Ip_Sa_PBcfg.c` 的 extern 声明（现为 98-107 行） |
| 2 | 回调落位对吗 | 现为 `:216 &FlexCAN0_Callback` / `:218 &FlexCAN0_ErrorCallback`、`:307` / `:309`（CAN1） |
| 3 | 中断还是 4 项吗 | `IntCtrl_Ip_Cfg.c` 的 `aIrqConfiguration1[]`（现 71-74 行） |
| 4 | 优先级 ≥ 1 吗 | 同上，第三列（现为 6/6/7/7） |
| 5 | `max_num_mb` 够不够 | 现 CAN0=96（`:131`）、CAN1=64（`:222`）；对比各表 `MbIdx` |
| 6 | 位率还是 500k 吗 | `FlexCAN_Ip_Sa_PBcfg.c` 里 PROPSEG/PSEG1/PSEG2/PRESDIV |
| 7 | **FreeRTOS 堆还是 8192 吗** | `generate/include/FreeRTOSConfig.h` 的 `configTOTAL_HEAP_SIZE` |
| 8 | **`configMAX_PRIORITIES` 还够吗** | 同上，必须 > 最高任务优先级 |

第 2 项特别重要：CT 生成的是**位置初始化**，如果字段顺序变了，
两个回调会**静默互换**，编译器不会报错。

---

## 九、怎么判断一个新参数该放哪层

### 9.1 问三个问题

1. **它描述的是硬件还是软件？**
   - 硬件（频率、引脚、邮箱数量、特性）→ CT
   - 软件（句柄编号、表结构、缓冲大小）→ MCAL 或更高

2. **换一颗芯片/换一个控制器，这个参数还要不要？**
   - 不要了 → CT（硬件相关）
   - 还要 → 上层（逻辑相关）

3. **它描述的是"谁"，还是"什么"？**
   - "谁"（编号、句柄、路由）→ CanIf / PduR
   - "什么"（信号的含义、位布局、缩放）→ COM
   - "什么时候、怎么处理"（业务规则）→ Application

### 9.2 举例

| 参数 | 判断 | 归属 |
|---|---|---|
| 波特率 500 kbps | 硬件事实，换芯片就变 | CT |
| `CAN_HTH_FIRST_INDEX = 4` | 软件约定，换芯片不变 | MCAL |
| 邮箱 5 用于接收 0x100 | 硬件资源分配（哪个邮箱）但由软件决定 | MCAL（邮箱号本身受 CT 的 max_num_mb 约束） |
| 中断优先级 6 | 硬件（NVIC）+ 与 OS 协调 | CT |
| 接收缓冲深度 8 | 软件（RAM 分配） | MCAL |
| 发 0x100 时用的 CAN ID | 逻辑身份 | CanIf |
| **这条 PDU 发给几条总线** | 数据的去向 | PduR |
| **窗位置在 bit1、7 位、factor 1** | 数据的含义 | COM |
| **发出去叫 `Rte_Write_DoorStatus_WindowPos`** | 端口的名字 | RTE |
| **车速 > 20km/h 禁止上升** | 业务规则 | Application |
| **Task_Com 优先级 3** | 调度配置 | EcuM（`ecum_cfg.c`） |

---

## 十、已知缺口清单（诚实记录）

### 10.1 代码写了但未验证 / 未启用

| 项 | 位置 | 状态 |
|---|---|---|
| COM `MIXED` 发送模式 | `com.c` | 代码完整，从未配置过 |
| COM 接收超时监控 | `com.c` | 代码完整，`TimeoutTicks` 全填 0（未启用） |
| **Bus-off 自动恢复** | `can.c:1109` | ❌ 只计数，不恢复。真实 ECU 必须能自动恢复 |
| `CAN_CS_SLEEP` | `can.h:110` | 明确返回 `E_NOT_OK`（故意"不撒谎"） |

### 10.2 COM 相对完整实现的差距

| 功能 | 状态 |
|---|---|
| BOOLEAN / UINT8..32 / SINT8..32 / UINT8_N | ✅ 已实现 |
| Intel / Motorola / OPAQUE 字节序 | ✅ 已实现 |
| 整数缩放 + 偏移 + 四舍五入 + 钳位 | ✅ 已实现 |
| PENDING / TRIGGERED / TRIGGERED_ON_CHANGE | ✅ 已实现 |
| PERIODIC / DIRECT / NONE | ✅ 已验证 |
| **MIXED** | ⚠️ 有代码未验证 |
| **接收超时监控** | ⚠️ 有代码未启用 |
| **更新位（Update Bit）** | ❌ 未实现 |
| **浮点信号（FLOAT32/64）** | ❌ 未实现 |
| **发送重复次数** | ❌ 未实现 |
| **I-PDU 组（Group 开关）** | ❌ 未实现 |
| **信号过滤（阈值）** | ❌ 未实现 |
| **`UINT8_N` 的 `Com_ReceiveSignal`** | ❌ 未实现（需要长度参数） |

### 10.3 缺失的 BSW 模块

| 模块 | 作用 | 现状 |
|---|---|---|
| **CanTp** | 多帧传输（刷写必需） | ❌ 无 → 无法刷写 |
| **Dcm** | UDS 服务、DID、安全访问 | ⚠️ 只记录收帧，不响应 |
| **Dem** | 故障码（DTC）存储 | ❌ 无 |
| **CanSM** | 网络状态管理 | ❌ 无（EcuM 代管，BusOff 不恢复） |
| **NvM** | 掉电存储 | ❌ 无 |
| **Det** | 开发错误追踪 | ❌ 无 |
| **BswM** | 运行期模式仲裁 | ❌ 无 |
| **WdgM** | 看门狗管理 | ❌ 无 |

### 10.4 工程化缺口

| 项 | 说明 |
|---|---|
| ~~COM 枚举与信号表可能失步~~ | ✅ **已解决**：`Com_SignalCountAssert` + `Com_SignalIdMaxAssert` 编译期强制（见 §6.15.2） |
| `txInProgress` 无超时保护 | 发送确认一旦丢失，该 PDU 永久停发 |
| 无单元测试入库 | 主机侧测试是临时的，不在工程内 |
| RTE 是手写替身 | 无 runnable 事件触发、无多 SWC 通信、无模式管理、无 ExclusiveArea |
| ~~运行期自检未上板验证~~ | ✅ **已验证**：故意制造 WindowPos/LockState 位重叠 → `ErrorCount=2`、`LastErrorId=0x23`、`LastInstance=3`，Det 同步记录 2 条；改回后 `ErrorCount=0` |

### 10.5 两个已知设计问题（待修）

| 问题 | 影响 | 修法 |
|---|---|---|
| **RX 和 TX 都用 CAN ID 0x100** | 分析仪发 0x100 会和 ECU 撞，可能出错误帧 | 改 `can_cfg.c` 的 `Can_RxObjects[0].CanId`（一行） |
| **0x200 / 0x300 不再发送** | 应用不写电池信号 → DIRECT 模式永不触发 → PduR 的 1:N 扇出看不到效果 | 让应用也写电池信号，或把 PDU 1 改成 PERIODIC |
