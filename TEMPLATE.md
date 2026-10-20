# TEMPLATE.md —— 从这个模板开新项目的完整流程

> **这份文档解决一个问题**：你三个月后（或别人）复制了这个模板，**知道该改哪里、按什么顺序改、改完怎么验证。**
>
> 模板本身的设计说明在 `CONFIG_GUIDE.md`；这次的改动记录在 `CHANGES_Det_Symbolization.md`。

---

## 〇、这个模板是什么 / 不是什么

### 是什么

一套**自底向上手工实现**的 AUTOSAR 风格分层通信栈骨架：

```
Application → Rte → Services(Com/PduR/Dcm/EcuM/Det/BswCheck) → CanIf → Mcal(Can) → RTD → 硬件
```

自带**三道配置一致性防线**（这是模板最值钱的部分）：

| 防线 | 手段 | 数量 |
|---|---|---|
| 第一道 | 符号化引用（引用不存在的对象 = 编译错误） | 23 个符号 |
| 第二道 | 编译期断言（表项数与数量宏脱节 = 编译错误） | 15 个断言 |
| 第三道 | 启动配置自检（位布局越界/重叠等） | `bswcheck.c` |

### 不是什么

**不是完整的 AUTOSAR 栈**。缺：

| 缺什么 | 后果 |
|---|---|
| CanTp | 无法收发多帧（刷写、长 DID 读不出来） |
| Dem | 没有故障码，4S 店读不出故障 |
| NvM | 故障码/配置无法持久化 |
| CanSM | **Bus-off 只计数，不自动恢复** |
| BswM / WdgM | 无模式仲裁、无看门狗监督 |
| Dcm 完整实现 | 只做收帧记录，无 UDS 服务 |

**新项目如果要做真 ECU，这些要按需补。** 补的时候照着现有层的写法加即可。

---

## 一、复制模板（约 30 分钟）

### 步骤

```
1. 复制整个目录
   D:\S32DS\WorkSpace\S32K344_Autosar_CanGw
   → D:\S32DS\WorkSpace\<新项目名>

2. 改工程名（4 处）
   □ 文件夹名
   □ .project          里的 <name>
   □ .cproject         里的 <name>（共 2 处，configuration 里也有）
   □ <旧工程名>.mex  重命名为 <新项目名>.mex，并改里面第一行的 name

3. S32DS 里 File → Import → General → Existing Projects into Workspace
   选新目录 → Finish

4. 补回 RTD/ 和 FreeRTOS/（本仓库不含，见 README.md「环境准备」）

5. 配置 include path 与 source folders（README.md 有完整清单）

6. F5 刷新 → Clean → Build
```

### 复制后立刻做的空跑验证

**在改任何配置之前**，先烧一次，确认模板本身是好的：

```
调试器 Watch:
  g_BswCheckStatus.ErrorCount   → 必须是 0
  Det_numEventErrors[0]         → 必须是 0
```

**这两个都是 0，说明模板骨架健康，可以开始改配置了。**

如果 `ErrorCount` 不为 0 → 先排查（可能是 include path 指到了旧目录、或 RTD 版本不一致）。

---

## 二、核心：按顺序替换"示例区"

### 为什么必须按顺序

配置是**自底向上依赖**的：

```
MCAL 的句柄符号  ←  CanIf 引用
CanIf 的 PDU 符号 ←  PduR 引用
COM 的 PDU 符号  ←  PduR 引用
```

**从下往上改，每改完一层就编译一次** —— 编译期断言会立刻告诉你"上一层还没跟上"。
从下往上改，每一步的报错都指向"你刚改的东西"；从上往下改，报错会指向"你还没改的东西"，很乱。

### 七步替换清单

| 步 | 改哪个文件 | 改什么 | 改完编译应该发生什么 |
|---|---|---|---|
| **1** | `Mcal/can_cfg.h` | `CAN_CONTROLLER_COUNT`、`CAN_TX_OBJECT_COUNT`、`CAN_RX_OBJECT_COUNT`、`CanConf_*` 符号名 | 编译 **失败**（`can_cfg.c` 断言报错）← 正常 |
| **2** | `Mcal/can_cfg.c` | 三张表：控制器表、发送对象表、接收对象表 | 编译 **通过** |
| **3** | `EcuAbstraction/canif_cfg.h` | `CANIF_TX_PDU_COUNT`、`CANIF_RX_PDU_COUNT`、`CanIfConf_*` 符号名 | `canif_cfg.c` 断言报错 ← 正常 |
| **4** | `EcuAbstraction/canif_cfg.c` | 两张表（Hth/Hrh 列用 MCAL 的符号名） | 编译 **通过** |
| **5** | `Services/Communication/com_cfg.h` | 信号枚举 + `COM_SIG_COUNT` + `ComConf_ComSignal_*` + `COM_TX/RX_PDU_COUNT` + `ComConf_ComTxPdu_*` / `ComConf_ComRxPdu_*` | `com_cfg.c` 断言报错 ← 正常 |
| **6** | `Services/Communication/com_cfg.c` | 信号表 + 发送 PDU 表 + 接收 PDU 表 | 编译 **通过** |
| **7** | `Services/Communication/pdur_cfg.c` + `pdur_cfg.h` | 路由表 + `PDUR_TX_PATH_COUNT` / `PDUR_RX_PATH_COUNT` | 编译 **通过** |

**第 8 步（可选，取决于新项目）**：

| 步 | 改哪个文件 | 说明 |
|---|---|---|
| 8 | `Services/System/ecum_cfg.c` | 任务划分（有几个应用任务、优先级、栈深度） |
| 9 | `Rte/rte_*.c` / `.h` | 端口函数（应用要访问新信号时） |
| 10 | `Application/` | 换成真正的业务逻辑 |

### 找到所有示例区

示例区用醒目标记包起来了，直接搜：

```powershell
Select-String -Path .\Mcal\*.h,.\Mcal\*.c,.\EcuAbstraction\*.h,.\EcuAbstraction\*.c,`
  .\Services\Communication\*.h,.\Services\Communication\*.c,.\Services\System\*.c `
  -Pattern "示例区"
```

或者直接看这两个符号：`▼▼▼ 示例区开始` / `▲▲▲ 示例区结束`

### 示例区清单

| 文件 | 示例区包含 | 新项目要换成 |
|---|---|---|
| `Mcal/can_cfg.h` | 控制器数（2）、收发对象数（4/4）、8 个句柄符号 | 自己的控制器数、邮箱数、Hth/Hrh 编号 |
| `Mcal/can_cfg.c` | 控制器表、发送对象表、接收对象表 | 自己的 RFCAN 配置：控制器、邮箱号、CAN ID、掩码 |
| `EcuAbstraction/canif_cfg.h` | PDU 数（4/4）、8 个 PDU 符号 | 自己的 PDU 数量与命名 |
| `EcuAbstraction/canif_cfg.c` | 发送 PDU 表、接收 PDU 表 | 自己的 PDU ↔ 句柄 ↔ CAN ID 映射 |
| `Services/Communication/com_cfg.h` | 信号枚举（17 个）、信号 ID 宏、PDU 符号 | 自己的信号列表（按 PDU 分块连续编号） |
| `Services/Communication/com_cfg.c` | 信号表、发送 PDU 表、接收 PDU 表 | 自己的信号位布局、PDU 长度、周期 |
| `Services/Communication/pdur_cfg.c` | TX/RX 路由表 | 自己的 COM ↔ CanIf 路由（含扇出） |
| `Services/System/ecum_cfg.c` | 任务表（2 个任务） | 自己的任务划分 |

---

## 三、改配置时必须同步的"隐形约束"

这些约束**不会在编译器里报错**（或者报错信息很难懂），必须人工记住：

### 3.1 编号连续性

| 约束 | 违反后果 |
|---|---|
| **同一 PDU 的信号在信号表里必须连续** | `SignalCount` 统计错，部分信号不更新 |
| **信号枚举的编号必须等于它在信号表里的下标** | 所有信号全部错位，数据乱掉 |
| **`COM_SIG_COUNT` 必须等于信号表项数** | ✅ 编译期断言会抓（`Com_SignalCountAssert`） |

**第 1、2 条是手动约束**。改信号表时，**枚举和表必须一起改，且顺序一致**。

### 3.2 句柄编号约定

```
Hrh（接收句柄）：0 .. CAN_HTH_FIRST_INDEX - 1
Hth（发送句柄）：CAN_HTH_FIRST_INDEX ..
```

当前 `CAN_HTH_FIRST_INDEX = 4`，所以 Hrh 是 0~3、Hth 是 4 起。

**改了接收对象数量、把 Hrh 用超过 3 了，就必须调大 `CAN_HTH_FIRST_INDEX`** —— 否则 Hrh 和 Hth 编号撞车，收发串台。

### 3.3 邮箱号范围

| 约束 | 来源 |
|---|---|
| `MbIdx < max_num_mb`（CAN0 = 96，CAN1 = 64） | RTD 的 FlexCAN 配置 |
| `MbIdx < CAN_MB_BUF_COUNT`（`Mcal/can.c`） | 静态接收缓冲数量 |

**两个都要满足**。改邮箱号时要一起看。

### 3.4 接收掩码不是可选项

```c
{ CanConf_RxObject_Hrh0, 0U, 5U, 0x100U, CAN_RX_MASK_EXACT_STD, FALSE },
                                        ↑ 这一列不能省
```

**S32K344 的 RXIMR 寄存器复位值 = 0 = 接收所有 ID**。不填掩码，配置里的 `CanId` 完全无效 —— 这个邮箱会收下总线上的**每一帧**，而且不报错。

### 3.5 扇出时 DestCount 要跟着改

```c
static const PduR_TxDestConfigType PduR_TxDests_Com1[] = { { CanIfConf_TxPdu_1 },
                                                           { CanIfConf_TxPdu_2 } };
{  ComConf_ComTxPdu_1, Com_TxConfirmation, 2U, PduR_TxDests_Com1 },
                                          ↑ 加了一路就要从 1 改成 2
```

✅ 这个有编译期断言（`PduR_TxDests*CountAssert`），但**断言里写死的数字也要你自己改**。

### 3.6 信号位布局不能重叠

```c
{ COM_SIGNAL_TYPE_UINT8, 1U, 7U, ... },   /* [1, 8)  */
{ COM_SIGNAL_TYPE_UINT8, 8U, 2U, ... },   /* [8, 10) */
```

✅ 这个有**运行期**自检（`BswCheck_OnePdu`），编译期查不出来。上电后看 `g_BswCheckStatus.ErrorCount`。

---

## 四、每改完一轮怎么验证

三道验证，从快到慢：

### 4.1 编译（秒级）

```powershell
# IDE 里直接 Build
```

**编译通过 = 第一道（符号化）+ 第二道（断言）都过了。**

如果没通过，看报错：

| 报错 | 含义 |
|---|---|
| `'CanConf_TxObject_Hth7' undeclared` | MCAL 删了这个句柄，但 CanIf 还在引用 |
| `size of array 'Can_TxObjectCountAssert' is negative` | `CAN_TX_OBJECT_COUNT` 与表项数不一致 |
| `size of array 'Com_SignalCountAssert' is negative` | `COM_SIG_COUNT` 与信号表项数不一致 |

### 4.2 上电自检（烧录一次）

```
调试器 Watch:
  g_BswCheckStatus.ErrorCount    → 0 才算通过
  g_BswCheckStatus.LastErrorId   → 出错时看这个定位类型
  g_BswCheckStatus.LastInstance  → 出错时看这个是第几行配置
```

**ErrorId 对照**：

| ErrorId | 含义 | 去看哪个文件 |
|---|---|---|
| `0x20` | CanIf 的 Length > MCAL 的 PayloadSize | `canif_cfg.c` 的 Length 列 / `can_cfg.c` 的 PayloadSize 列 |
| `0x21` | CanIf 的 Hth 在 MCAL 里不存在 | `canif_cfg.c` 的 Hth 列 |
| `0x22` | CanIf 的 Hrh 在 MCAL 里不存在 | `canif_cfg.c` 的 Hrh 列 |
| `0x23` | COM 两个信号位重叠 | `com_cfg.c` 信号表，按 `InstanceId` 找到信号 |
| `0x24` | COM 信号超出 PDU 位宽 | 该信号的 BitPosition + BitSize 超过 PDU Length×8 |
| `0x25` | COM 的 FirstSignalIdx + SignalCount 越尾 | `com_cfg.c` PDU 表的这两个字段 |

### 4.3 总线验证（最后一步）

```
CAN 分析仪接上：
  □ 发送：能看到你配置的 ID 和周期
  □ 数据：字节内容符合信号位布局
  □ 接收：分析仪发一帧，应用能收到（Watch 里能看到信号值变化）
```

**到这一步才算真的通了。**

---

## 五、模板自带的两个验证工具

### 5.1 Det 接线自检

`Services/System/ecum.c` 里：

```c
#define ECU_SELFTEST_DET    (0)     /* 改成 1 打开 */
```

改成 1 后烧录，看 `Det_numEventErrors[0]`：

```
期待 = 3，且三组 (ModuleId, ApiId, ErrorId) 是：
    (50, 2, 3)   COM  / SendSignal / SignalId 越界
    (50, 2, 5)   COM  / SendSignal / 往接收信号写值
    (60, 1, 3)   CanIf/ Transmit   / PduId 不存在
```

**用途**：如果 Det 没接好（比如 `BSW_DEV_ERROR_DETECT` 被关了），这里立刻能发现，而不是等到真出 bug 时才发现"Det 没工作"。

### 5.2 严格模式

`Services/System/bswcheck.h` 里：

```c
#define BSWCHECK_STRICT   (0U)   /* 0 = 只报 Det 继续跑；1 = 报完停在死循环 */
```

开发期建议改 **1**：配置一错就不往下跑，绝不会出现"看起来在跑但数据不对"。
量产保持 **0**。

### 5.3 怎么验证防线真的有效（破坏测试）

想知道"如果我改错了，它到底会不会报"，就故意改坏一次：

| 想验证 | 怎么改坏 | 期待结果 |
|---|---|---|
| 符号化 | 注释掉 `Mcal/can_cfg.h` 里的 `CanConf_TxObject_Hth4` | 编译报 `undeclared` |
| 编译期断言 | 把 `CANIF_TX_PDU_COUNT` 从 4 改成 5 | 编译报 `size of array ... is negative` |
| 运行期自检 | 把某个信号的 `BitPosition` 改成和另一个重叠 | 上电后 `ErrorCount ≥ 1`、`LastErrorId = 0x23` |

**测完一定要恢复**：

```powershell
git checkout -- <改动的文件>
```

---

## 六、不该改的文件（骨架）

这些是"分层骨架"，除非你要改架构，否则不动：

| 文件 | 为什么不该动 |
|---|---|
| `Mcal/can.c` | 纯逻辑实现，配置全在 `can_cfg.c` |
| `Mcal/can.h` | 接口 + Det 声明，改接口会影响上层 |
| `EcuAbstraction/canif.c` | 同上 |
| `Services/Communication/com.c` | 同上 |
| `Services/Communication/pdur.c` | 同上 |
| `Services/System/ecum.c` | 启动时序是硬约束（Can → CanIf → PduR → Com → Rte → Det_Start → BswCheck → StartOS） |
| `Services/System/bswcheck.c` | 检查逻辑通用 |
| `Services/Diagnostic/dcm.c` | 目前是替身，真要诊断就整个替换 |
| `Rte/rte.c` | 空实现（RTE 目前只是"层次完整性"的占位） |

**判断标准**：文件里有没有 `▼▼▼ 示例区` 标记？
- 有 → 这一段是示例，换
- 没有 → 是骨架，不换

---

## 七、常见错误对照表

| 现象 | 可能原因 | 排查 |
|---|---|---|
| 编译报 `'Xxx' undeclared` | 上层引用了下层已删/改名的符号 | 看报错行引用了哪个符号，去下层头文件找 |
| 编译报 `size of array ... is negative` | 数量宏 ≠ 表项数 | 数一下表里有几行，改数量宏 |
| 上电 `ErrorCount = 0` 但数据不对 | 位布局错但没重叠，或 CAN ID 错 | 用分析仪看原始字节，对照 `com_cfg.c` 的位布局手算一遍 |
| 上电 `ErrorCount > 0` | 按 `LastErrorId` 查 §4.2 的表 | |
| 发了但总线上没东西 | Hth 引用了 MCAL 里不存在的行 / 通道模式是 TX_OFFLINE | 先看 Det（`Det_ErrorId`），再看 `CanIf_IsTxAllowed` |
| 收了但应用收不到 | Hrh 错 / PduR 路由断 / COM 的 RxPduId 错 | 在 `Com_RxIndication` 打断点，看有没有进来 |
| 收了所有 ID 的帧 | 掩码没配（复位值 0 = 全收） | 检查 `Can_RxObjects[]` 的 `CanIdMask` 列 |
| 板子卡死不上电 | `BSWCHECK_STRICT = 1` 且配置有错 | 看 `g_BswCheckStatus.ErrorCount` 和 `LastErrorId` |
| 任务创建失败 | FreeRTOS 堆不够 | Det 报 `ECUM_E_TASK_CREATE_FAILED`，调大 `configTOTAL_HEAP_SIZE` |

---

## 八、最小可跑配置（从 1 条报文开始）

新项目如果不确定从哪下手，**先只配 1 条发送报文**，跑通了再加。

| 位置 | 最小内容 |
|---|---|
| `can_cfg.h` | `CAN_TX_OBJECT_COUNT = 1`、`CAN_RX_OBJECT_COUNT = 0`、只留 `CanConf_TxObject_Hth4` |
| `can_cfg.c` | 发送对象表 1 行；`Can_RxObjects[]` 留空数组（`{}` 在某些编译器下非法，用一个空表加注释） |
| `canif_cfg.h` | `CANIF_TX_PDU_COUNT = 1`、`CANIF_RX_PDU_COUNT = 0`、只留 `CanIfConf_TxPdu_0` |
| `canif_cfg.c` | 发送表 1 行（`CanIfConf_TxPdu_0` + `CanConf_TxObject_Hth4`） |
| `com_cfg.h` | 枚举只有 1 个信号 + `COM_SIG_COUNT = 1`；`COM_TX_PDU_COUNT = 1` |
| `com_cfg.c` | 信号表 1 行；发送 PDU 表 1 行（`SignalCount = 1`） |
| `pdur_cfg.c` | TX 路径 1 条，1 个目标；`PDUR_TX_PATH_COUNT = 1` |
| `ecum_cfg.c` | 只留 `Task_Com` |

**注意**：`CAN_RX_OBJECT_COUNT = 0` 时，`Can_RxObjects[]` 不能是空数组（C99 不允许零长数组）。做法：

```c
/* 新项目暂时不接收，用 1 个占位对象（Hrh 0，掩码设为"收不到任何 ID"） */
static const Can_RxHwObjectConfigType Can_RxObjects[] =
{
    {  CanConf_RxObject_Hrh0,  0U,  0U,  0x7FFU,  CAN_RX_MASK_EXACT_STD,  FALSE },
};
#define CAN_RX_OBJECT_COUNT  (1U)
```

或者干脆保留模板的 4 个接收对象，先不用它们。

**验证**：分析仪能看到那条报文，周期正确，数据符合 1 个信号的位布局 → 再往上加。

---

## 九、给后来者的三条经验

### 1. 改配置永远"自底向上 + 每层编译一次"

从 MCAL 往 COM 改。**不要一次改 5 个文件然后编译** —— 那样报错会一起来，分不清是谁的问题。

### 2. 所有"我记住了"的地方都要写下来

这个模板的手工约束（§三）有 6 条，**编译器只抓其中 4 条**。剩下 2 条（信号枚举与表同步、句柄编号约定）靠人记 —— 所以写在这里。

### 3. 出现"编译过、运行不报错、数据不对"，先看 Det

```
Det_numEventErrors[0]  → 0 说明 Det 也没抓到，问题在"数值语义"（ID 错、位布局错）
                       → 非 0 说明有 Det 记录，去看 Det_ModuleId / ApiId / ErrorId
```

**Det 抓不到的那一类，只能靠分析仪看总线 + 手算位布局。**
