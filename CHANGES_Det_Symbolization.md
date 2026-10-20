# 改动清单 —— Det 集成 + 符号化 + 启动自检

> **说明**：本工程**没有纳入 Git**，所以这不是机器 `git diff`，而是按实际编辑记录整理的
> 前后对照。每一处都标注了"改动前"和"改动后"的真实代码。
>
> **时间**：2026-10-20
> **改动前基线**：约 4740 行 → **改动后 5277 行**（净增 1029 行，其中约 80% 是注释与文档）

---

## 零、改动规模总览

| 文件 | 改动前 | 改动后 | 变化 |
|---|---|---|---|
| `Mcal/can.c` | 910 行 | 965 行 | +55 |
| `Mcal/can.h` | 137 行 | 188 行 | +51 |
| `Mcal/can_cfg.c` | 91 行 | 109 行 | +18 |
| `Mcal/can_cfg.h` | 105 行 | 133 行 | +28 |
| `EcuAbstraction/canif.c` | 408 行 | 456 行 | +48 |
| `EcuAbstraction/canif.h` | 142 行 | 203 行 | +61 |
| `EcuAbstraction/canif_cfg.c` | 71 行 | 83 行 | +12 |
| `EcuAbstraction/canif_cfg.h` | 93 行 | 119 行 | +26 |
| `Services/Communication/com.c` | 845 行 | 888 行 | +43 |
| `Services/Communication/com.h` | 223 行 | 264 行 | +41 |
| `Services/Communication/com_cfg.c` | 134 行 | 159 行 | +25 |
| `Services/Communication/com_cfg.h` | 205 行 | 227 行 | +22 |
| `Services/Communication/pdur.c` | 249 行 | 276 行 | +27 |
| `Services/Communication/pdur.h` | 143 行 | 176 行 | +33 |
| `Services/Communication/pdur_cfg.c` | 103 行 | 132 行 | +29 |
| `Services/Communication/pdur_cfg.h` | 93 行 | 99 行 | +6 |
| `Services/Diagnostic/dcm.c` | 49 行 | 66 行 | +17 |
| `Services/Diagnostic/dcm.h` | 45 行 | 82 行 | +37 |
| `Services/System/ecum.c` | 163 行 | 265 行 | +102 |
| `Services/System/ecum.h` | 39 行 | 85 行 | +46 |
| **`Services/System/bswcheck.c`** | — | 247 行 | **新增** |
| **`Services/System/bswcheck.h`** | — | 55 行 | **新增** |
| `CONFIG_GUIDE.md` | 1012 行 | 1183 行 | +171 |

**完全没有改动的文件**（确认过）：
`Rte/rte.c`、`Rte/rte.h`、`Rte/rte_doorstatus.c`、`Rte/rte_doorstatus.h`、
`Application/WindowCtrl/*`、`src/main.c`、`Services/System/ecum_cfg.c`、`Services/System/ecum_cfg.h`

---

## 一、Det 集成

### 1.1 模式：每个模块头文件都新增同一段（以 `can.h` 为例）

**改动前**（`Mcal/can.h` 第 34-53 行）：

```c
/*==================================================================================================
 *                                    DEFINES AND MACROS
 *================================================================================================*/

/**
 * @brief   Can_Write() 的叠加返回值（Overlayed return value）
 * @details ...
 */
#define CAN_BUSY_RET    ((Std_ReturnType)CAN_BUSY)
```

**改动后**（在 `CAN_BUSY_RET` 之前插入）：

```c
/*--------------------------------------------------------------------------------------------------
 *                    模块标识与开发错误检测（Det）配置
 *------------------------------------------------------------------------------------------------*/

/** @brief Can 的标准 AUTOSAR 模块号（0x50 = 80） */
#define CAN_MODULE_ID                       (0x50U)

/** @brief 单实例模块，实例号固定 0 */
#define CAN_INSTANCE_ID                     (0U)

/**
 * @brief 开发错误检测开关
 * @details 优先级：本模块单独定义 > 全局 BSW_DEV_ERROR_DETECT > 默认 STD_ON
 */
#ifndef CAN_DEV_ERROR_DETECT
    #ifdef BSW_DEV_ERROR_DETECT
        #define CAN_DEV_ERROR_DETECT        (BSW_DEV_ERROR_DETECT)
    #else
        #define CAN_DEV_ERROR_DETECT        (STD_ON)
    #endif
#endif

/** @brief Can 的 API 编号（Det 上报时用） */
typedef enum
{
    CAN_API_INIT                        = 0U,
    CAN_API_DEINIT                      = 1U,
    CAN_API_WRITE                       = 2U,
    ...
} Can_ApiIdType;

/** @brief Can 的错误编号（Det 上报时用） */
typedef enum
{
    CAN_E_PARAM_POINTER         = 0x01U,
    CAN_E_UNINIT                = 0x02U,
    CAN_E_PARAM_HANDLE          = 0x03U,
    ...
} Can_ErrorIdType;
```

**另外 5 个模块完全同构**（只换前缀和模块号）：

| 文件 | 新增内容 |
|---|---|
| `EcuAbstraction/canif.h` | `CANIF_MODULE_ID (0x3CU)` + 开关 + `CanIf_ApiIdType`（7 项）+ `CanIf_ErrorIdType`（8 项） |
| `Services/Communication/pdur.h` | `PDUR_INSTANCE_ID` + 开关 + `PduR_ApiIdType`（5 项）+ `PduR_ErrorIdType`（5 项）<br>（`PDUR_MODULE_ID` 原本就有） |
| `Services/Communication/com.h` | `COM_INSTANCE_ID` + 开关 + `Com_ApiIdType`（10 项）+ `Com_ErrorIdType`（7 项）<br>（`COM_MODULE_ID` 原本就有） |
| `Services/Diagnostic/dcm.h` | `DCM_MODULE_ID (0x37U)` + 开关 + `Dcm_ApiIdType`（4 项）+ `Dcm_ErrorIdType`（3 项） |
| `Services/System/ecum.h` | `ECUM_MODULE_ID (0x0AU)` + 开关 + `EcuM_ApiIdType`（3 项）+ `EcuM_ErrorIdType`（9 项） |

### 1.2 模式：每个模块 `.c` 新增"include + 宏 + 调用点"

**以 `canif.c` 为例，改动前：**

```c
#include "can.h"
#include "can_cfg.h"

#define CANIF_NO_PDU_INDEX          (0xFFFFU)
```

**改动后：**

```c
#include "can_cfg.h"

/* Det（开发错误追踪）。只有实现文件 include Det.h，canif.h 不依赖它。 */
#include "Det.h"

#define CANIF_NO_PDU_INDEX          (0xFFFFU)

/**
 * @brief Det 上报的便捷宏
 * @details ① 少打字 ② DEV_ERROR_DETECT = STD_OFF 时整个调用被预处理器删成 ((void)0)
 *          ③ 统一 InstanceId
 */
#if (CANIF_DEV_ERROR_DETECT == STD_ON)
    #define CANIF_DET_ERROR(api, err)                                   \
        ((void)Det_ReportError((uint16)CANIF_MODULE_ID,                 \
                               (uint8)CANIF_INSTANCE_ID,                \
                               (uint8)(api),                            \
                               (uint8)(err)))
#else
    #define CANIF_DET_ERROR(api, err)   ((void)0)
#endif
```

**调用点的改动（`CanIf_Transmit`，改动前）：**

```c
if (CanIf_Initialized == FALSE)
{
    return E_NOT_OK;
}
if ((PduInfoPtr == NULL_PTR) || (PduInfoPtr->SduDataPtr == NULL_PTR))
{
    return E_NOT_OK;
}
...
idx = CanIf_FindTxIdxByPduId(TxPduId);
if (idx == CANIF_NO_PDU_INDEX)
{
    return E_NOT_OK;
}
```

**改动后：**

```c
if (CanIf_Initialized == FALSE)
{
    CANIF_DET_ERROR(CANIF_API_TRANSMIT, CANIF_E_UNINIT);
    return E_NOT_OK;
}
if ((PduInfoPtr == NULL_PTR) || (PduInfoPtr->SduDataPtr == NULL_PTR))
{
    CANIF_DET_ERROR(CANIF_API_TRANSMIT, CANIF_E_PARAM_POINTER);
    return E_NOT_OK;
}
...
idx = CanIf_FindTxIdxByPduId(TxPduId);
if (idx == CANIF_NO_PDU_INDEX)
{
    CANIF_DET_ERROR(CANIF_API_TRANSMIT, CANIF_E_PARAM_PDU_ID);
    return E_NOT_OK;
}
```

**一处"刻意不报 Det"的例子**（`CanIf_Transmit` 的通道模式检查）：

```c
/* 【★ 这里刻意不报 Det】
 *  TX_OFFLINE 是**合法的运行状态**，不是程序员的错误：
 *  诊断会话切换时本来就要禁止应用报文发送。
 *  如果这里报 Det，Det 记录数组会被正常行为刷满，真正的错误反而被淹没。 */
if (CanIf_IsTxAllowed(txCfg->ControllerId) == FALSE)
{
    return E_NOT_OK;          /* ← 保持原样，没有 CANIF_DET_ERROR */
}
```

### 1.3 各模块 Det 调用分布

| 模块 | 调用数 | 主要检查点 |
|---|---|---|
| `can.c` | 17 | 未初始化、句柄不存在、控制器状态、长度超限、FD 配置不匹配 |
| `com.c` | 14 | 未初始化、SignalId 越界、**方向错误**、容量不足、RxPduId 未知 |
| `canif.c` | 9 | 未初始化、PduId 不存在、长度超限、Hrh 越界、中断上下文 |
| `pdur.c` | 6 | 未初始化、路由不存在、配置回调不匹配 |
| `dcm.c` | 1 | 收到 NULL 指针 |
| `ecum.c` | 1 | 任务创建失败（堆不足） |
| **合计** | **48** | |

### 1.4 `ecum.c` 新增 Det 生命周期

**改动前：**

```c
void EcuM_Init(void)
{
    uint8 i;

    Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);
    ...
    Can_Init(&Can_Config);
    CanIf_Init(&CanIf_Config);
    PduR_Init(&PduR_Config);
    Com_Init(&Com_Config);
    Rte_Init();
```

**改动后：**

```c
void EcuM_Init(void)
{
    uint8 i;

    /* 第零阶段：Det 初始化 —— 必须是第一个 */
    Det_Init();

    Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);
    ...
    Can_Init(&Can_Config);
    CanIf_Init(&CanIf_Config);
    PduR_Init(&PduR_Config);
    Com_Init(&Com_Config);
    Rte_Init();

    Det_Start();          /* ← 初始化期结束，之后才允许上报 */
```

### 1.5 `ecum.c` 新增的模板自检开关（默认关闭）

```c
#define ECU_SELFTEST_DET    (0)      /* 改成 1 可验证 Det 接线 */

#if (ECU_SELFTEST_DET == 1)
    {
        uint8 dummy = 0U;
        (void)Com_SendSignal(999U, &dummy);                      /* ① SignalId 越界 */
        (void)Com_SendSignal(COM_SIG_RX_DOOR_OPEN, &dummy);      /* ② 写接收信号 */
        { PduInfoType badPdu; badPdu.SduDataPtr = &dummy;
          badPdu.MetaDataPtr = NULL_PTR; badPdu.SduLength = 1U;
          (void)CanIf_Transmit(99U, &badPdu); }                  /* ③ PduId 不存在 */
    }
#endif
```

---

## 二、符号化 + 编译期断言

### 2.1 `can_cfg.h`：新增数量宏与句柄符号（改动前 → 后）

**改动前：**

```c
#define CAN_HTH_FIRST_INDEX         (4U)
#define CAN_CONTROLLER_COUNT        (2U)
```

**改动后（在同一位置后追加）：**

```c
#define CAN_CONTROLLER_COUNT        (2U)

#define CAN_TX_OBJECT_COUNT         (4U)
#define CAN_RX_OBJECT_COUNT         (4U)

/* 发送句柄：Hth 4/5/6/7 ↔ Can_TxObjects[] 的第 1/2/3/4 行 */
#define CanConf_TxObject_Hth4       (CAN_HTH_FIRST_INDEX + 0U)
#define CanConf_TxObject_Hth5       (CAN_HTH_FIRST_INDEX + 1U)
#define CanConf_TxObject_Hth6       (CAN_HTH_FIRST_INDEX + 2U)
#define CanConf_TxObject_Hth7       (CAN_HTH_FIRST_INDEX + 3U)

/* 接收句柄：Hrh 0/1/2/3 ↔ Can_RxObjects[] 的第 1/2/3/4 行 */
#define CanConf_RxObject_Hrh0       (0U)
#define CanConf_RxObject_Hrh1       (1U)
#define CanConf_RxObject_Hrh2       (2U)
#define CanConf_RxObject_Hrh3       (3U)
```

### 2.2 `can_cfg.c`：表改用符号名 + 新增断言

**改动前：**

```c
static const Can_TxHwObjectConfigType Can_TxObjects[] =
{
    /* Hth,                       ControllerId, MbIdx, IsExtended, PayloadSize */
    {  CAN_HTH_FIRST_INDEX + 0U,  0U,            1U,    FALSE,      8U },
    {  CAN_HTH_FIRST_INDEX + 1U,  0U,            2U,    FALSE,      8U },
    {  CAN_HTH_FIRST_INDEX + 2U,  1U,            1U,    FALSE,      8U },
    {  CAN_HTH_FIRST_INDEX + 3U,  1U,            2U,    FALSE,      8U },
};

static const Can_RxHwObjectConfigType Can_RxObjects[] =
{
    /* Hrh,  ControllerId, MbIdx, CanId,  CanIdMask,                IsExtended */
    {  0U,   0U,           5U,    0x100U, CAN_RX_MASK_EXACT_STD,    FALSE },
    {  1U,   0U,           6U,    0x200U, CAN_RX_MASK_EXACT_STD,    FALSE },
    {  2U,   1U,           5U,    0x300U, CAN_RX_MASK_EXACT_STD,    FALSE },
    {  3U,   1U,           6U,    0x400U, CAN_RX_MASK_EXACT_STD,    FALSE },
};
```

**改动后：**

```c
static const Can_TxHwObjectConfigType Can_TxObjects[] =
{
    {  CanConf_TxObject_Hth4,         0U,            1U,    FALSE,      8U },
    {  CanConf_TxObject_Hth5,         0U,            2U,    FALSE,      8U },
    {  CanConf_TxObject_Hth6,         1U,            1U,    FALSE,      8U },
    {  CanConf_TxObject_Hth7,         1U,            2U,    FALSE,      8U },
};

static const Can_RxHwObjectConfigType Can_RxObjects[] =
{
    {  CanConf_RxObject_Hrh0,  0U,         5U,    0x100U, CAN_RX_MASK_EXACT_STD,    FALSE },
    {  CanConf_RxObject_Hrh1,  0U,         6U,    0x200U, CAN_RX_MASK_EXACT_STD,    FALSE },
    {  CanConf_RxObject_Hrh2,  1U,         5U,    0x300U, CAN_RX_MASK_EXACT_STD,    FALSE },
    {  CanConf_RxObject_Hrh3,  1U,         6U,    0x400U, CAN_RX_MASK_EXACT_STD,    FALSE },
};
```

**文件末尾新增：**

```c
typedef char Can_TxObjectCountAssert[
    ((sizeof(Can_TxObjects) / sizeof(Can_TxObjects[0])) == (uint32)CAN_TX_OBJECT_COUNT) ? 1 : -1];

typedef char Can_RxObjectCountAssert[
    ((sizeof(Can_RxObjects) / sizeof(Can_RxObjects[0])) == (uint32)CAN_RX_OBJECT_COUNT) ? 1 : -1];

typedef char Can_ControllerCountAssert[
    ((sizeof(Can_Controllers) / sizeof(Can_Controllers[0])) == (uint32)CAN_CONTROLLER_COUNT) ? 1 : -1];
```

### 2.3 `canif_cfg.h`：新增 PDU 符号

```c
/* 改动前：文件里只有 CANIF_MAX_PDU_LEN */
#define CANIF_MAX_PDU_LEN           (8U)

/* 改动后：追加 */
#define CANIF_TX_PDU_COUNT          (4U)
#define CANIF_RX_PDU_COUNT          (4U)

#define CanIfConf_TxPdu_0           (0U)
#define CanIfConf_TxPdu_1           (1U)
#define CanIfConf_TxPdu_2           (2U)
#define CanIfConf_TxPdu_3           (3U)

#define CanIfConf_RxPdu_0           (0U)
#define CanIfConf_RxPdu_1           (1U)
#define CanIfConf_RxPdu_2           (2U)
#define CanIfConf_RxPdu_3           (3U)
```

### 2.4 `canif_cfg.c`：表改用符号名（这是**跨层引用**的关键改动）

**改动前：**

```c
{  0U,       CAN_HTH_FIRST_INDEX + 0U,  0U,            0x100U,   8U },
...
{  0U,       0U },      /* Hrh 0 → 0x100 */
```

**改动后：**

```c
{  CanIfConf_TxPdu_0,       CanConf_TxObject_Hth4,      0U,            0x100U,   8U },
...
{  CanIfConf_RxPdu_0,      CanConf_RxObject_Hrh0 },   /* → 0x100 */
```

**末尾新增 2 个断言：**

```c
typedef char CanIf_TxPduCountAssert[
    ((sizeof(CanIf_TxObjects) / sizeof(CanIf_TxObjects[0])) == (uint32)CANIF_TX_PDU_COUNT) ? 1 : -1];

typedef char CanIf_RxPduCountAssert[
    ((sizeof(CanIf_RxObjects) / sizeof(CanIf_RxObjects[0])) == (uint32)CANIF_RX_PDU_COUNT) ? 1 : -1];
```

### 2.5 `com_cfg.h`：新增 PDU 符号

```c
/* 改动后追加在 ComConf_ComSignal_* 之后 */
#define COM_TX_PDU_COUNT            (3U)
#define COM_RX_PDU_COUNT            (4U)

#define ComConf_ComTxPdu_0          (0U)
#define ComConf_ComTxPdu_1          (1U)
#define ComConf_ComTxPdu_2          (2U)

#define ComConf_ComRxPdu_0          (0U)
#define ComConf_ComRxPdu_1          (1U)
#define ComConf_ComRxPdu_2          (2U)
#define ComConf_ComRxPdu_3          (3U)
```

### 2.6 `com_cfg.c`：表改用符号名 + 4 个断言

**改动前：**

```c
{  0U,      0xFFFFU,  8U,     0U,             4U,          COM_TX_MODE_PERIODIC, 10U },
{  1U,      0xFFFFU,  4U,     4U,             2U,          COM_TX_MODE_DIRECT,    0U },
{  2U,      0xFFFFU,  4U,     6U,             2U,          COM_TX_MODE_PERIODIC, 50U },
...
{  0xFFFFU, 0U,      8U,     8U,             4U,          COM_TX_MODE_NONE, 0U },
```

**改动后：**

```c
{  ComConf_ComTxPdu_0,     0xFFFFU,  8U,     0U,             4U,          COM_TX_MODE_PERIODIC, 10U },
{  ComConf_ComTxPdu_1,     0xFFFFU,  4U,     4U,             2U,          COM_TX_MODE_DIRECT,    0U },
{  ComConf_ComTxPdu_2,     0xFFFFU,  4U,     6U,             2U,          COM_TX_MODE_PERIODIC, 50U },
...
{  0xFFFFU, ComConf_ComRxPdu_0, 8U,     8U,             4U,          COM_TX_MODE_NONE, 0U },
```

**末尾新增 4 个断言**（其中 `Com_SignalIdMaxAssert` 是针对"枚举失步"的）：

```c
typedef char Com_SignalCountAssert[
    ((sizeof(Com_Signals) / sizeof(Com_Signals[0])) == (uint32)COM_SIG_COUNT) ? 1 : -1];
typedef char Com_TxPduCountAssert[...];
typedef char Com_RxPduCountAssert[...];
typedef char Com_SignalIdMaxAssert[
    (((uint32)COM_SIG_RX_DCM_TEST + 1U) == (uint32)COM_SIG_COUNT) ? 1 : -1];
```

### 2.7 `pdur_cfg.c`：引用上下两层符号（改动最大的一处逻辑）

**改动前：**

```c
static const PduR_TxDestConfigType PduR_TxDests_Com0[] = { { 0U } };
static const PduR_TxDestConfigType PduR_TxDests_Com1[] = { { 1U }, { 2U } };
static const PduR_TxDestConfigType PduR_TxDests_Com2[] = { { 3U } };

static const PduR_TxPathConfigType PduR_TxPaths[] =
{
    /* SrcPduId,  SrcTxConfirmation,      DestCount,  Dests   */
    {  0U,        Com_TxConfirmation,     1U,         PduR_TxDests_Com0 },
    {  1U,        Com_TxConfirmation,     2U,         PduR_TxDests_Com1 },
    {  2U,        Com_TxConfirmation,     1U,         PduR_TxDests_Com2 },
};

static const PduR_RxDestConfigType PduR_RxDests_CanIf0[] = { { 0U, Com_RxIndication } };
...
static const PduR_RxPathConfigType PduR_RxPaths[] =
{
    /* SrcPduId,  DestCount,  Dests */
    {  0U,        1U,         PduR_RxDests_CanIf0 },
    {  3U,        2U,         PduR_RxDests_CanIf3 },
};
```

**改动后：**

```c
static const PduR_TxDestConfigType PduR_TxDests_Com0[] = { { CanIfConf_TxPdu_0 } };
static const PduR_TxDestConfigType PduR_TxDests_Com1[] = { { CanIfConf_TxPdu_1 },
                                                           { CanIfConf_TxPdu_2 } };
static const PduR_TxDestConfigType PduR_TxDests_Com2[] = { { CanIfConf_TxPdu_3 } };

static const PduR_TxPathConfigType PduR_TxPaths[] =
{
    /* SrcPduId（引用 COM）,          SrcTxConfirmation,      DestCount,  Dests   */
    {  ComConf_ComTxPdu_0,             Com_TxConfirmation,     1U,         PduR_TxDests_Com0 },
    {  ComConf_ComTxPdu_1,             Com_TxConfirmation,     2U,         PduR_TxDests_Com1 },
    {  ComConf_ComTxPdu_2,             Com_TxConfirmation,     1U,         PduR_TxDests_Com2 },
};

static const PduR_RxDestConfigType PduR_RxDests_CanIf0[] = { { ComConf_ComRxPdu_0, Com_RxIndication } };
...
static const PduR_RxPathConfigType PduR_RxPaths[] =
{
    /* SrcPduId（引用 CanIf）,         DestCount,  Dests */
    {  CanIfConf_RxPdu_0,               1U,         PduR_RxDests_CanIf0 },
    {  CanIfConf_RxPdu_3,               2U,         PduR_RxDests_CanIf3 },
};
```

**新增 include**（这是唯一"打破原有设计"的地方）：

```c
/* 改动前：只有 extern 声明，故意不 include COM 的头文件 */
extern void Com_RxIndication(...);
extern void Com_TxConfirmation(...);
extern void Dcm_RxIndication(...);

/* 改动后：追加两行（为了拿到符号名） */
#include "canif_cfg.h"
#include "com_cfg.h"
```

> **为什么这么做**：函数仍然用 `extern`（签名靠约定），但**符号名必须 include**，
> 否则没法做编译期检查。这也是真实 AUTOSAR 生成代码的做法
> ——`PduR_Cfg.c` 确实会 include `CanIf_Cfg.h` 和 `Com_Cfg.h`。

**末尾新增 6 个断言**（含 `DestCount` 与数组项数的对齐检查）：

```c
typedef char PduR_TxDestsCom1CountAssert[
    ((sizeof(PduR_TxDests_Com1) / sizeof(PduR_TxDests_Com1[0])) == 2U) ? 1 : -1];
...
typedef char PduR_TxPathCountAssert[
    ((sizeof(PduR_TxPaths) / sizeof(PduR_TxPaths[0])) == (uint32)PDUR_TX_PATH_COUNT) ? 1 : -1];
```

### 2.8 `pdur_cfg.h`：新增路径数量宏

```c
#define PDUR_TX_PATH_COUNT          (3U)
#define PDUR_RX_PATH_COUNT          (4U)
```

---

## 三、启动配置自检（全新模块）

### 3.1 新增 `Services/System/bswcheck.h`（55 行）

```c
#ifndef BSWCHECK_H
#define BSWCHECK_H
#include "Std_Types.h"

#define BSWCHECK_STRICT             (0U)   /* 0 = 只报 Det；1 = 报完停住 */

typedef struct
{
    uint16 ErrorCount;
    uint16 LastErrorId;
    uint16 LastInstance;
    uint16 CheckedCanIf;
    uint16 CheckedComSig;
} BswCheck_StatusType;

extern volatile BswCheck_StatusType g_BswCheckStatus;

Std_ReturnType BswCheck_Run(void);
#endif
```

### 3.2 新增 `Services/System/bswcheck.c`（247 行）

**结构：**

```c
static void BswCheck_Report(uint16 instance, uint16 errorId);        /* → Det */
static void BswCheck_CanIfVsMcal(void);                              /* ① ② */
static void BswCheck_SignalBitRange(const Com_SignalConfigType*, ...); /* 位区间换算 */
static void BswCheck_OnePdu(const Com_IpduConfigType*, ...);         /* ③ ④ ⑤ */
Std_ReturnType BswCheck_Run(void);
```

**核心逻辑（信号位区间重叠检查）：**

```c
/* 两个半开区间重叠的判据 */
if ((startA < endB) && (startB < endA))
{
    BswCheck_Report(sigIdxB, (uint16)ECUM_E_CHECK_SIGNAL_OVERLAP);
}
```

**注意一个容易错的地方 —— 两类信号的位语义不同：**

```c
if (sig->Type == COM_SIGNAL_TYPE_UINT8_N)
{
    /* 字节偏移 / 字节数 → 换成位 */
    *startBit = ((uint32)sig->BitPosition) * 8UL;
    *endBit   = (((uint32)sig->BitPosition) + ((uint32)sig->BitSize)) * 8UL;
}
else
{
    *startBit = (uint32)sig->BitPosition;
    *endBit   = ((uint32)sig->BitPosition) + ((uint32)sig->BitSize);
}
```

### 3.3 `ecum.h`：新增 ApiId 与 6 个检查错误码

```c
typedef enum
{
    ECUM_API_INIT                   = 0U,
    ECUM_API_START_OS               = 1U,
    ECUM_API_BSW_CHECK              = 2U     /* ← 新增 */
} EcuM_ApiIdType;

typedef enum
{
    ECUM_E_PARAM_POINTER        = 0x01U,
    ECUM_E_INVALID_CONFIG       = 0x02U,
    ECUM_E_TASK_CREATE_FAILED   = 0x03U,

    /* 新增：启动自检专用 */
    ECUM_E_CHECK_TX_LENGTH      = 0x20U,
    ECUM_E_CHECK_HTH_MISSING    = 0x21U,
    ECUM_E_CHECK_HRH_MISSING    = 0x22U,
    ECUM_E_CHECK_SIGNAL_OVERLAP = 0x23U,
    ECUM_E_CHECK_SIGNAL_BOUNDS  = 0x24U,
    ECUM_E_CHECK_SIGNAL_RANGE   = 0x25U
} EcuM_ErrorIdType;
```

### 3.4 `ecum.c`：调用自检

**在 `Det_Start()` 之后新增：**

```c
if (BswCheck_Run() != E_OK)
{
#if (BSWCHECK_STRICT == 1U)
    for (;;) { }        /* 严格模式：停住，等调试器 */
#endif
}
```

---

## 四、验证记录

### 4.1 编译

```
17 个文件，0 error 0 warning，链接无缺失符号
```

### 4.2 Det 上板实测（你已确认）

```
Det_numEventErrors[0] = 3
三组 (ModuleId, ApiId, ErrorId) = (50,2,3) (50,2,5) (60,1,3)
```

### 4.3 编译期防线破坏测试（6 条全部抓到）

| # | 破坏 | 编译器输出 |
|---|---|---|
| 1 | CanIf PDU 数 4→5 | `size of array 'CanIf_TxPduCountAssert' is negative` |
| 2 | `COM_SIG_COUNT` 17→18 | `size of array 'Com_SignalCountAssert' is negative` |
| 3 | 删 `CanIfConf_TxPdu_1` | `'CanIfConf_TxPdu_1' undeclared; did you mean 'CanIfConf_RxPdu_1'?` |
| 4 | `PDUR_TX_PATH_COUNT` 3→4 | `size of array 'PduR_TxPathCountAssert' is negative` |
| 5 | 删 `ComConf_ComTxPdu_1` | `'ComConf_ComTxPdu_1' undeclared` |
| 6 | 删 `CanConf_TxObject_Hth6` | `'CanConf_TxObject_Hth6' undeclared` |

### 4.4 Det 开关验证

```
CANIF_DEV_ERROR_DETECT = STD_ON   → .o 4480 字节，含 U Det_ReportError
CANIF_DEV_ERROR_DETECT = STD_OFF  → .o 4292 字节，Det 符号 0 个

全局 -DBSW_DEV_ERROR_DETECT=STD_OFF 后，6 个模块对 Det 的引用全部为 0
Det 本体占用：Flash 2,636 字节 + RAM 1,928 字节
```

### 4.5 ⏳ 尚未验证

**运行期自检（`bswcheck.c`）没有在板子上跑过**。验证方法见 `CONFIG_GUIDE.md` §6.15.3。

---

## 五、故障历史（供参考）

改动过程中发生过两次 IDE 与外部编辑冲突：

| 时间 | 事件 | 后果 | 修复 |
|---|---|---|---|
| 10-19 22:32 | IDE 在 `Application/` 根目录创建了一份 `app_windowcontrol.h` 副本 | 掩盖了 include path 未配置的问题 | 删除副本 |
| 10-20 16:09 | IDE 把 `Mcal/can_cfg.c` 还原成改动前版本 | 符号化和断言丢失 | 重新应用改动 |

**教训**：本工程没有版本管理，外部编辑与 IDE 缓冲会互相覆盖。
**建议**：把工程纳入 Git（`.gitignore` 排除 `Debug_FLASH/`、`RTD/`、`generate/`），之后所有改动可追溯、可回滚、可机器 diff。
