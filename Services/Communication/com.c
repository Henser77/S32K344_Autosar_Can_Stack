/*==================================================================================================
 *  Services/com.c —— COMPDU 打包/解包与发送时机控制
 *
 *  【本文件的三个核心算法】
 *
 *    1. 位打包/解包（Com_PackRaw / Com_UnpackRaw）
 *       —— 处理两种位编号体系（Intel 线性 / Motorola 锯齿）和跨字节
 *
 *    2. 物理值换算（Com_PhysicalToRaw / Com_RawToPhysical）
 *       —— 缩放 + 偏移 + 四舍五入 + 钳位
 *
 *    3. 发送时机（Com_MainFunctionTx 里的状态机）
 *       —— PERIODIC / DIRECT / MIXED，用 tick 计数
 *
 *  【并发说明（重要）】
 *
 *    Com_RxIndication() 在 **CAN 中断** 里跑（Can ISR → CanIf → PduR → 这里）。
 *    Com_SendSignal() / Com_MainFunctionTx() / Com_ReceiveSignal() 在 **任务** 里跑。
 *
 *    共享数据只有两个：
 *      a) 信号影子值 Com_SigShadowValue[]  —— 用 sint32 存储
 *         Cortex-M 上 32 位对齐的单次读/写是**原子**的，所以不会撕裂。
 *         代价：中断里改了值，任务可能读到"这一帧"或"上一帧"，但不会是半新半旧。
 *         对信号来说这个语义完全够用（信号本来就是周期性刷新的）。
 *
 *      b) TX 影子缓冲（任务写、任务读，同一个任务 → 无竞争）
 *         若将来有多个任务写同一条 PDU 的不同信号，需要加临界区。
 * =================================================================================================*/

/*==================================================================================================
 *                                        INCLUDES
 *================================================================================================*/
#include "com.h"
#include "com_cfg.h"
#include "pdur.h"       /* PduR_ComTransmit —— 本层向下唯一的出口 */

/* Det（开发错误追踪）—— 叶子模块 */
#include "Det.h"

/*==================================================================================================
 *                                    LOCAL DEFINES
 *================================================================================================*/

/** @brief "这个 PduId 字段未使用"的标记 */
#define COM_PDU_ID_UNUSED           (0xFFFFU)

/** @brief Det 上报的便捷宏（DEV_ERROR_DETECT 关闭时被删成 ((void)0)） */
#if (COM_DEV_ERROR_DETECT == STD_ON)
    #define COM_DET_ERROR(api, err)                                     \
        ((void)Det_ReportError((uint16)COM_MODULE_ID,                   \
                               (uint8)COM_INSTANCE_ID,                  \
                               (uint8)(api),                            \
                               (uint8)(err)))
#else
    #define COM_DET_ERROR(api, err)     ((void)0)
#endif

/*==================================================================================================
 *                                    LOCAL TYPES
 *================================================================================================*/

/** @brief 一条 TX PDU 的运行时状态 */
typedef struct
{
    uint8   shadow[COM_MAX_PDU_LEN];   /**< 影子缓冲（按位打包好的整包字节） */
    uint16  periodCounter;             /**< 周期计数（tick） */
    boolean txRequested;               /**< 事件触发的"要发"标志 */
    boolean txInProgress;              /**< 已交给 PduR，等 TxConfirmation */
} Com_TxPduStateType;

/** @brief 一条 RX PDU 的运行时状态 */
typedef struct
{
    uint8   buffer[COM_MAX_PDU_LEN];   /**< 最近收到的整包字节（解包时用，也便于调试观察） */
} Com_RxPduStateType;

/*==================================================================================================
 *                                    LOCAL VARIABLES
 *================================================================================================*/

/** @brief 当前配置 */
static const Com_ConfigType* Com_ConfigPtr = NULL_PTR;

/** @brief 初始化完成标志 */
static boolean Com_Initialized = FALSE;

/** @brief 各信号的当前物理值（影子值） */
static sint32 Com_SigShadowValue[COM_MAX_SIGNALS];

/** @brief 各信号上次打包的原始值（用于 TRIGGERED_ON_CHANGE 判断"值变了没"） */
static uint32 Com_SigLastRaw[COM_MAX_SIGNALS];

/** @brief 各信号距离上次更新的 tick 数（超时监控用） */
static uint16 Com_SigTimeoutCounter[COM_MAX_SIGNALS];

/** @brief 信号 → 所属 PDU 的全局下标（0..TxCount-1 是 TX，TxCount.. 是 RX） */
static uint16 Com_SigToIpdu[COM_MAX_SIGNALS];

/** @brief 信号在所属 PDU 内的局部序号（0..SignalCount-1） */
static uint8 Com_SigLocalIdx[COM_MAX_SIGNALS];

/** @brief TX / RX PDU 的运行时状态 */
static Com_TxPduStateType Com_TxState[COM_MAX_TX_PDUS];
static Com_RxPduStateType Com_RxState[COM_MAX_RX_PDUS];

/*==================================================================================================
 *                          字母级工具：位访问
 *================================================================================================*/

/**
 * @brief 按 AUTOSAR 位编号规则，把"位号"翻译成"字节下标 + 字节内位号"
 *
 * @details 这是整个 COM 里最需要小心的地方。
 *
 *      LITTLE（Intel）：线性编号
 *          bit 0 → 字节0 bit0（LSB）
 *          bit 7 → 字节0 bit7（MSB）
 *
 *      BIG（Motorola）：锯齿编号
 *          bit 0 → 字节0 bit7（MSB）   ← 注意：字节内是**反的**
 *          bit 7 → 字节0 bit0（LSB）
 *
 *      所以同一个 bit 号，在两种规则下落在字节内**相反**的位置。
 */
static boolean Com_GetBit(const uint8* buf, uint8 len, uint16 pos, Com_EndianType endian)
{
    uint16 byteIdx   = (uint16)(pos / 8U);
    uint8  bitInByte = (uint8)(pos % 8U);

    if (byteIdx >= (uint16)len)
    {
        return FALSE;       /* 配置越界：静默返回 0，不越界读 */
    }
    if (endian == COM_ENDIAN_BIG)
    {
        bitInByte = (uint8)(7U - bitInByte);
    }
    return (((buf[byteIdx] >> bitInByte) & 1U) != 0U) ? TRUE : FALSE;
}

static void Com_SetBit(uint8* buf, uint8 len, uint16 pos, boolean value, Com_EndianType endian)
{
    uint16 byteIdx   = (uint16)(pos / 8U);
    uint8  bitInByte = (uint8)(pos % 8U);

    if (byteIdx >= (uint16)len)
    {
        return;             /* 配置越界：静默忽略，不越界写 */
    }
    if (endian == COM_ENDIAN_BIG)
    {
        bitInByte = (uint8)(7U - bitInByte);
    }

    if (value != FALSE)
    {
        buf[byteIdx] |= (uint8)(1U << bitInByte);
    }
    else
    {
        buf[byteIdx] &= (uint8)(~(uint8)(1U << bitInByte));
    }
}

/**
 * @brief 把原始值按位打包进 PDU 缓冲
 *
 * 【两种字节序的位映射 —— 核心】
 *
 *    LITTLE：值的 bit i  →  位号 (BitPosition + i)
 *            LSB 在位号低端，向高位号延伸
 *
 *    BIG   ：值的 bit i  →  位号 (BitPosition + BitSize - 1 - i)
 *            **MSB 放在 BitPosition**，LSB 在位号高端
 *
 *    配合 Com_SetBit 里"BIG 时字节内位号取反"，合起来才是完整的 Motorola 语义。
 */
static void Com_PackRaw(uint8* buf, uint8 len, const Com_SignalConfigType* sig, uint32 raw)
{
    uint8 i;

    for (i = 0U; i < sig->BitSize; i++)
    {
        boolean bitVal = ((raw >> i) & 1UL) != 0UL ? TRUE : FALSE;
        uint16  pos;

        if (sig->Endian == COM_ENDIAN_BIG)
        {
            pos = (uint16)(sig->BitPosition + (uint16)(sig->BitSize - 1U - i));
        }
        else
        {
            pos = (uint16)(sig->BitPosition + i);
        }
        Com_SetBit(buf, len, pos, bitVal, sig->Endian);
    }
}

/**
 * @brief 从 PDU 缓冲按位解出原始值
 * @return 无符号的原始位模式（有符号信号由调用者再做符号扩展）
 */
static uint32 Com_UnpackRaw(const uint8* buf, uint8 len, const Com_SignalConfigType* sig)
{
    uint32 raw = 0UL;
    uint8  i;

    for (i = 0U; i < sig->BitSize; i++)
    {
        uint16 pos;

        if (sig->Endian == COM_ENDIAN_BIG)
        {
            pos = (uint16)(sig->BitPosition + (uint16)(sig->BitSize - 1U - i));
        }
        else
        {
            pos = (uint16)(sig->BitPosition + i);
        }
        if (Com_GetBit(buf, len, pos, sig->Endian) != FALSE)
        {
            raw |= (1UL << i);
        }
    }
    return raw;
}

/*==================================================================================================
 *                          数值工具：位宽、符号扩展、缩放
 *================================================================================================*/

/** @brief 按位宽算最大原始值 */
static uint32 Com_MaxRaw(uint8 bits)
{
    if (bits == 0U)   { return 0UL; }
    if (bits >= 32U)  { return 0xFFFFFFFFUL; }
    return (uint32)((1UL << bits) - 1UL);
}

/** @brief 把 bits 位的无符号原始值做符号扩展到 32 位 */
static sint32 Com_SignExtend(uint32 raw, uint8 bits)
{
    uint32 mask;

    if ((bits == 0U) || (bits >= 32U))
    {
        return (sint32)raw;
    }

    mask = (1UL << bits) - 1UL;
    raw &= mask;

    if ((raw & (1UL << (bits - 1U))) != 0UL)
    {
        raw |= ~mask;       /* 补上高位 1 */
    }
    return (sint32)raw;
}

/** @brief 该信号是否为有符号类型 */
static boolean Com_IsSigned(const Com_SignalConfigType* sig)
{
    return ((sig->Type == COM_SIGNAL_TYPE_SINT8) ||
            (sig->Type == COM_SIGNAL_TYPE_SINT16) ||
            (sig->Type == COM_SIGNAL_TYPE_SINT32)) ? TRUE : FALSE;
}

/**
 * @brief 物理值 → 原始值
 * @details
 *      physical = raw × factor + offset
 *   ⇒  raw = (physical - offset) / factor
 *
 *  处理：负数、四舍五入、按位宽钳位。
 *  注意 factor 理论上可以配负值（递减量纲），这里统一化成正数处理。
 */
static uint32 Com_PhysicalToRaw(const Com_SignalConfigType* sig, sint32 phys)
{
    sint32 factor = sig->ScalingFactor;
    sint32 diff   = phys - sig->ScalingOffset;
    sint32 raw;
    uint32 maxRaw = Com_MaxRaw(sig->BitSize);

    if (factor < 0)
    {
        factor = -factor;
        diff   = -diff;
    }
    if (factor == 0)
    {
        factor = 1;         /* 防御：配置错成 0 时当 1 处理，避免除零 */
    }

    /* 四舍五入（向远离 0 的方向） */
    if (diff >= 0)
    {
        raw = (diff + (factor / 2)) / factor;
    }
    else
    {
        raw = (diff - (factor / 2)) / factor;
    }

    /* 钳位到 [0, maxRaw]（原始值是无符号位模式） */
    if (raw < 0)
    {
        raw = 0;
    }
    if ((uint32)raw > maxRaw)
    {
        raw = (sint32)maxRaw;
    }
    return (uint32)raw;
}

/**
 * @brief 原始值 → 物理值
 * @details physical = raw × factor + offset
 *          raw 已按信号符号性做过扩展。
 */
static sint32 Com_RawToPhysical(const Com_SignalConfigType* sig, sint32 rawSigned)
{
    return (sint32)((rawSigned * sig->ScalingFactor) + sig->ScalingOffset);
}

/*==================================================================================================
 *                          物理值读写（按信号类型解引用 void*）
 *================================================================================================*/

static sint32 Com_LoadPhysical(const Com_SignalConfigType* sig, const void* ptr)
{
    switch (sig->Type)
    {
        case COM_SIGNAL_TYPE_BOOLEAN: return ((*(const uint8*)ptr) != 0U) ? 1 : 0;
        case COM_SIGNAL_TYPE_UINT8:   return (sint32)(*(const uint8*)ptr);
        case COM_SIGNAL_TYPE_UINT16:  return (sint32)(*(const uint16*)ptr);
        case COM_SIGNAL_TYPE_UINT32:  return (sint32)(*(const uint32*)ptr);
        case COM_SIGNAL_TYPE_SINT8:   return (sint32)(*(const sint8*)ptr);
        case COM_SIGNAL_TYPE_SINT16:  return (sint32)(*(const sint16*)ptr);
        case COM_SIGNAL_TYPE_SINT32:  return *(const sint32*)ptr;
        default:                      return 0;
    }
}

static void Com_StorePhysical(const Com_SignalConfigType* sig, void* ptr, sint32 phys)
{
    switch (sig->Type)
    {
        case COM_SIGNAL_TYPE_BOOLEAN: *(uint8*)ptr  = (phys != 0) ? 1U : 0U; break;
        case COM_SIGNAL_TYPE_UINT8:   *(uint8*)ptr  = (uint8)phys;  break;
        case COM_SIGNAL_TYPE_UINT16:  *(uint16*)ptr = (uint16)phys; break;
        case COM_SIGNAL_TYPE_UINT32:  *(uint32*)ptr = (uint32)phys; break;
        case COM_SIGNAL_TYPE_SINT8:   *(sint8*)ptr  = (sint8)phys;  break;
        case COM_SIGNAL_TYPE_SINT16:  *(sint16*)ptr = (sint16)phys; break;
        case COM_SIGNAL_TYPE_SINT32:  *(sint32*)ptr = phys;         break;
        default: break;
    }
}

/*==================================================================================================
 *                          查表工具
 *================================================================================================*/

static const Com_IpduConfigType* Com_FindTxPdu(PduIdType TxPduId, uint16* outIdx)
{
    uint16 i;

    for (i = 0U; i < Com_ConfigPtr->TxPduCount; i++)
    {
        if (Com_ConfigPtr->TxPdus[i].TxPduId == TxPduId)
        {
            if (outIdx != NULL_PTR) { *outIdx = i; }
            return &Com_ConfigPtr->TxPdus[i];
        }
    }
    return NULL_PTR;
}

static const Com_IpduConfigType* Com_FindRxPdu(PduIdType RxPduId, uint16* outIdx)
{
    uint16 i;

    for (i = 0U; i < Com_ConfigPtr->RxPduCount; i++)
    {
        if (Com_ConfigPtr->RxPdus[i].RxPduId == RxPduId)
        {
            if (outIdx != NULL_PTR) { *outIdx = i; }
            return &Com_ConfigPtr->RxPdus[i];
        }
    }
    return NULL_PTR;
}

/*==================================================================================================
 *                              INIT / DEINIT
 *================================================================================================*/
void Com_Init(const Com_ConfigType* Config)
{
    uint16 i;
    uint8  k;

    if (Config == NULL_PTR)
    {
        COM_DET_ERROR(COM_API_INIT, COM_E_PARAM_POINTER);
        return;
    }

    /*--------------------------------------------------------------------------
     * 0. 容量检查
     *
     * 宁可拒绝初始化，也不要静默越界写静态数组。
     *
     * 【这类错误最该报 Det】
     * 容量不够 = COM_MAX_* 宏与实际配置表规模不匹配，是**模板复制后最容易
     * 忘改的一处**。不报的话，现象是"信号写不进去/PDU 不发"，极难定位。
     *------------------------------------------------------------------------*/
    if ((Config->SignalCount > (uint16)COM_MAX_SIGNALS) ||
        (Config->TxPduCount > (uint16)COM_MAX_TX_PDUS) ||
        (Config->RxPduCount > (uint16)COM_MAX_RX_PDUS))
    {
        COM_DET_ERROR(COM_API_INIT, COM_E_INVALID_CONFIG);
        return;
    }

    Com_ConfigPtr = Config;

    /*--------------------------------------------------------------------------
     * 1. 清零所有运行时状态
     *------------------------------------------------------------------------*/
    for (i = 0U; i < (uint16)COM_MAX_SIGNALS; i++)
    {
        Com_SigShadowValue[i]     = 0;
        Com_SigLastRaw[i]         = 0UL;
        Com_SigTimeoutCounter[i]  = 0U;
        Com_SigToIpdu[i]          = 0U;
        Com_SigLocalIdx[i]        = 0U;
    }
    for (i = 0U; i < (uint16)COM_MAX_TX_PDUS; i++)
    {
        for (k = 0U; k < (uint8)COM_MAX_PDU_LEN; k++) { Com_TxState[i].shadow[k] = 0U; }
        Com_TxState[i].periodCounter = 0U;
        Com_TxState[i].txRequested   = FALSE;
        Com_TxState[i].txInProgress  = FALSE;
    }
    for (i = 0U; i < (uint16)COM_MAX_RX_PDUS; i++)
    {
        for (k = 0U; k < (uint8)COM_MAX_PDU_LEN; k++) { Com_RxState[i].buffer[k] = 0U; }
    }

    /*--------------------------------------------------------------------------
     * 2. 派生反向索引：信号 → 所属 PDU
     *------------------------------------------------------------------------*/
    for (i = 0U; i < Config->TxPduCount; i++)
    {
        const Com_IpduConfigType* pdu = &Config->TxPdus[i];

        for (k = 0U; k < pdu->SignalCount; k++)
        {
            uint16 sigId = (uint16)(pdu->FirstSignalIdx + k);
            if (sigId < Config->SignalCount)
            {
                Com_SigToIpdu[sigId]   = i;             /* TX 段：0..TxCount-1 */
                Com_SigLocalIdx[sigId] = k;
            }
        }
    }
    for (i = 0U; i < Config->RxPduCount; i++)
    {
        const Com_IpduConfigType* pdu = &Config->RxPdus[i];

        for (k = 0U; k < pdu->SignalCount; k++)
        {
            uint16 sigId = (uint16)(pdu->FirstSignalIdx + k);
            if (sigId < Config->SignalCount)
            {
                /* RX 段：TxCount + i —— 用偏移区分，避免与 TX 下标冲突 */
                Com_SigToIpdu[sigId]   = (uint16)(Config->TxPduCount + i);
                Com_SigLocalIdx[sigId] = k;
            }
        }
    }

    /*--------------------------------------------------------------------------
     * 3. 用 InitValue 初始化信号影子值，并把 TX PDU 的影子缓冲打包好
     *
     * 这样即使应用一上来不写任何信号，第一次周期发送也有确定的初值。
     *------------------------------------------------------------------------*/
    for (i = 0U; i < Config->SignalCount; i++)
    {
        const Com_SignalConfigType* sig = &Config->Signals[i];
        uint16 pduGlobalIdx = Com_SigToIpdu[i];
        uint32 raw;

        Com_SigShadowValue[i] = sig->InitValue;

        if (pduGlobalIdx >= Config->TxPduCount)
        {
            continue;           /* RX 信号：只初始化影子值，不打包 */
        }

        raw = Com_PhysicalToRaw(sig, sig->InitValue);
        Com_SigLastRaw[i] = raw;

        if (sig->Type == COM_SIGNAL_TYPE_UINT8_N)
        {
            /* 字节数组：InitValue 无法表达，留 0 */
            continue;
        }
        Com_PackRaw(Com_TxState[pduGlobalIdx].shadow,
                    Config->TxPdus[pduGlobalIdx].Length,
                    sig, raw);
    }

    /*--------------------------------------------------------------------------
     * 4. 周期计数预置为"立即到期"
     *
     * 否则周期发送要等一个完整周期才开始，调试时看起来像"死掉了"。
     *------------------------------------------------------------------------*/
    for (i = 0U; i < Config->TxPduCount; i++)
    {
        Com_TxState[i].periodCounter = Config->TxPdus[i].TxPeriodTicks;
    }

    Com_Initialized = TRUE;
}

void Com_DeInit(void)
{
    Com_ConfigPtr   = NULL_PTR;
    Com_Initialized = FALSE;
}

#if (COM_VERSION_INFO_API == STD_ON)
void Com_GetVersionInfo(Std_VersionInfoType* versioninfo)
{
    if (versioninfo == NULL_PTR) { return; }

    versioninfo->vendorID         = (uint16)COM_VENDOR_ID;
    versioninfo->moduleID         = (uint16)COM_MODULE_ID;
    versioninfo->sw_major_version = (uint8)COM_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = (uint8)COM_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = (uint8)COM_SW_PATCH_VERSION;
}
#endif

/*==================================================================================================
 *                              Com_SendSignal —— 写信号
 *================================================================================================*/
Std_ReturnType Com_SendSignal(Com_SignalIdType SignalId, const void* SignalDataPtr)
{
    const Com_SignalConfigType* sig;
    uint16 pduGlobalIdx;
    uint16 txIdx;
    sint32 phys;
    uint32 raw;

    if ((Com_Initialized == FALSE) || (SignalDataPtr == NULL_PTR))
    {
        COM_DET_ERROR(COM_API_SEND_SIGNAL,
                      (Com_Initialized == FALSE) ? COM_E_UNINIT : COM_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    if (SignalId >= Com_ConfigPtr->SignalCount)
    {
        COM_DET_ERROR(COM_API_SEND_SIGNAL, COM_E_PARAM_SIGNAL_ID);
        return E_NOT_OK;
    }

    sig          = &Com_ConfigPtr->Signals[SignalId];
    pduGlobalIdx = Com_SigToIpdu[SignalId];

    /* 只有 TX PDU 的信号才能写。
     * 对接收信号调 SendSignal 是**调用方搞错方向**，属于典型的使用错误。 */
    if (pduGlobalIdx >= Com_ConfigPtr->TxPduCount)
    {
        COM_DET_ERROR(COM_API_SEND_SIGNAL, COM_E_SIGNAL_DIRECTION);
        return E_NOT_OK;
    }
    txIdx = pduGlobalIdx;

    /*--------------------------------------------------------------------------
     * 1. UINT8_N（字节数组）：不做位打包，直接按字节偏移覆盖
     *------------------------------------------------------------------------*/
    if (sig->Type == COM_SIGNAL_TYPE_UINT8_N)
    {
        const uint8* src = (const uint8*)SignalDataPtr;
        uint16 byteOff   = sig->BitPosition;      /* 对 UINT8_N，BitPosition 是字节偏移 */
        uint8  byteCnt   = sig->BitSize;          /* 对 UINT8_N，BitSize 是字节个数 */
        uint8  n;

        if ((uint16)(byteOff + byteCnt) > (uint16)Com_ConfigPtr->TxPdus[txIdx].Length)
        {
            COM_DET_ERROR(COM_API_SEND_SIGNAL, COM_E_INVALID_CONFIG);
            return E_NOT_OK;
        }
        for (n = 0U; n < byteCnt; n++)
        {
            Com_TxState[txIdx].shadow[byteOff + n] = src[n];
        }
        /* 字节数组没有"数值变化"的概念，一律视为已更新 */
        Com_TxState[txIdx].txRequested = TRUE;
        return E_OK;
    }

    /*--------------------------------------------------------------------------
     * 2. 普通数值信号：物理值 → 原始值 → 打包
     *------------------------------------------------------------------------*/
    phys = Com_LoadPhysical(sig, SignalDataPtr);
    raw  = Com_PhysicalToRaw(sig, phys);

    Com_PackRaw(Com_TxState[txIdx].shadow,
                Com_ConfigPtr->TxPdus[txIdx].Length,
                sig, raw);

    Com_SigShadowValue[SignalId] = phys;

    /*--------------------------------------------------------------------------
     * 3. 按传输属性决定要不要触发发送
     *
     * 注意：这里只是**置标志**，真正发送在 Com_MainFunctionTx 里。
     * 原因：同一条 PDU 的多个信号可能在本周期内先后被写，
     *       必须攒齐了整包再发，否则会发出"半新半旧"的报文。
     *------------------------------------------------------------------------*/
    switch (sig->TransferProperty)
    {
        case COM_TRANSFER_TRIGGERED:
            Com_TxState[txIdx].txRequested = TRUE;
            break;

        case COM_TRANSFER_TRIGGERED_ON_CHANGE:
            if (raw != Com_SigLastRaw[SignalId])
            {
                Com_TxState[txIdx].txRequested = TRUE;
            }
            break;

        case COM_TRANSFER_PENDING:
        default:
            /* 不触发，等周期 */
            break;
    }

    Com_SigLastRaw[SignalId] = raw;
    return E_OK;
}

/*==================================================================================================
 *                              Com_ReceiveSignal —— 读信号
 *================================================================================================*/
Std_ReturnType Com_ReceiveSignal(Com_SignalIdType SignalId, void* SignalDataPtr)
{
    const Com_SignalConfigType* sig;

    if ((Com_Initialized == FALSE) || (SignalDataPtr == NULL_PTR))
    {
        COM_DET_ERROR(COM_API_RECEIVE_SIGNAL,
                      (Com_Initialized == FALSE) ? COM_E_UNINIT : COM_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    if (SignalId >= Com_ConfigPtr->SignalCount)
    {
        COM_DET_ERROR(COM_API_RECEIVE_SIGNAL, COM_E_PARAM_SIGNAL_ID);
        return E_NOT_OK;
    }

    sig = &Com_ConfigPtr->Signals[SignalId];

    if (sig->Type == COM_SIGNAL_TYPE_UINT8_N)
    {
        /* 字节数组暂不支持通过本接口读取（需要长度参数），见 com.h 的 TODO。
         * 这是**功能未实现**，不是调用方写错 —— 但它确实会让调用方拿到
         * 一个"看起来正常"的 E_NOT_OK，所以还是报出来，避免他查半天。 */
        COM_DET_ERROR(COM_API_RECEIVE_SIGNAL, COM_E_UNSUPPORTED_TYPE);
        return E_NOT_OK;
    }

    Com_StorePhysical(sig, SignalDataPtr, Com_SigShadowValue[SignalId]);
    return E_OK;
}

Std_ReturnType Com_InvalidateSignal(Com_SignalIdType SignalId)
{
    if (Com_Initialized == FALSE)
    {
        COM_DET_ERROR(COM_API_INVALIDATE_SIGNAL, COM_E_UNINIT);
        return E_NOT_OK;
    }
    if (SignalId >= Com_ConfigPtr->SignalCount)
    {
        COM_DET_ERROR(COM_API_INVALIDATE_SIGNAL, COM_E_PARAM_SIGNAL_ID);
        return E_NOT_OK;
    }

    /* 按 SWS，失效后读出的是替代值。本实现用 InitValue 代替。 */
    Com_SigShadowValue[SignalId] = Com_ConfigPtr->Signals[SignalId].InitValue;
    return E_OK;
}

/*==================================================================================================
 *                         Com_RxIndication —— 收到一帧（中断上下文！）
 *================================================================================================*/
void Com_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr)
{
    const Com_IpduConfigType* pdu;
    Com_RxPduStateType*       state;
    uint16                    rxIdx;
    uint16                    k;

    if ((Com_Initialized == FALSE) || (PduInfoPtr == NULL_PTR))
    {
        /* ★ 本函数跑在 CAN 接收中断里 —— Det 上报也在中断上下文。 */
        COM_DET_ERROR(COM_API_RX_INDICATION,
                      (Com_Initialized == FALSE) ? COM_E_UNINIT : COM_E_PARAM_POINTER);
        return;
    }

    pdu = Com_FindRxPdu(RxPduId, &rxIdx);
    if (pdu == NULL_PTR)
    {
        /* 这条 RxPduId 没有配 COM 信号 —— 正常（可能只给 Dcm 用）。
         * PduR 的 1:N 扇出会把同一条报文同时给我们和 Dcm，
         * 我们这边"没有对应 PDU"就直接忽略，不影响 Dcm。 */
        return;
    }

    /*--------------------------------------------------------------------------
     * 1. 长度校验
     *
     * 收到的长度 < 配置长度 → 拒绝（按 SWS，不足的部分无法解包）。
     * 收到的长度 > 配置长度 → 接受，只用前 Length 字节。
     *------------------------------------------------------------------------*/
    if (PduInfoPtr->SduLength < (PduLengthType)pdu->Length)
    {
        /* 收到的帧比配置的短 —— 不是编程错误，是**总线上的对端发错了**
         * 或者双方配置不一致。严格说这属于 Dem 的领域（真实的通信故障），
         * 但在开发期先用 Det 报出来，方便对齐两端的通信矩阵。 */
        COM_DET_ERROR(COM_API_RX_INDICATION, COM_E_INVALID_CONFIG);
        return;
    }
    if (PduInfoPtr->SduDataPtr == NULL_PTR)
    {
        return;
    }

    /*--------------------------------------------------------------------------
     * 2. 整包拷进 RX 影子缓冲
     *
     * 【为什么必须拷】
     * SduDataPtr 指向 MCAL 的静态接收缓冲，本函数返回后随时会被下一帧覆盖。
     * 虽然解包在本次调用内就完成了，但保留一份整包字节便于调试观察
     * （在 Watch 窗口里能直接看到原始报文）。
     *------------------------------------------------------------------------*/
    state = &Com_RxState[rxIdx];
    for (k = 0U; k < (uint16)pdu->Length; k++)
    {
        state->buffer[k] = PduInfoPtr->SduDataPtr[k];
    }

    /*--------------------------------------------------------------------------
     * 3. 逐信号解包：位提取 → 符号扩展 → 缩放 → 存影子值
     *------------------------------------------------------------------------*/
    for (k = 0U; k < (uint16)pdu->SignalCount; k++)
    {
        uint16 sigId = (uint16)(pdu->FirstSignalIdx + k);
        const Com_SignalConfigType* sig;
        uint32 raw;
        sint32 rawSigned;
        sint32 phys;

        if (sigId >= Com_ConfigPtr->SignalCount)
        {
            break;
        }
        sig = &Com_ConfigPtr->Signals[sigId];

        if (sig->Type == COM_SIGNAL_TYPE_UINT8_N)
        {
            continue;       /* 字节数组不走数值解包 */
        }

        raw = Com_UnpackRaw(state->buffer, pdu->Length, sig);

        rawSigned = (Com_IsSigned(sig) != FALSE)
                  ? Com_SignExtend(raw, sig->BitSize)
                  : (sint32)raw;

        phys = Com_RawToPhysical(sig, rawSigned);

        /* 单次 32 位对齐写入，任务侧读取不会撕裂 */
        Com_SigShadowValue[sigId] = phys;

        /* 重置该信号超时计数 */
        Com_SigTimeoutCounter[sigId] = 0U;

        Com_SigLastRaw[sigId] = raw;
    }
}

/*==================================================================================================
 *                         Com_TxConfirmation —— 发送完成（中断上下文）
 *================================================================================================*/
void Com_TxConfirmation(PduIdType TxPduId, Std_ReturnType result)
{
    uint16 txIdx;
    (void)result;

    if (Com_Initialized == FALSE)
    {
        return;
    }

    if (Com_FindTxPdu(TxPduId, &txIdx) == NULL_PTR)
    {
        /* PduR 只会把 "SrcTxConfirmation = Com_TxConfirmation" 的路径回调过来，
         * 所以走进来就说明 PduR 配置里挂了一个 COM 不认识的 PduId —— 配置不一致。 */
        COM_DET_ERROR(COM_API_TX_CONFIRMATION, COM_E_PARAM_PDU_ID);
        return;
    }

    /* 1:N 扇出时本函数会被调用多次（TxPduId 相同），
     * 这里直接清标志 —— 幂等，重复调用无害。 */
    Com_TxState[txIdx].txInProgress = FALSE;
}

/*==================================================================================================
 *                              Com_MainFunctionTx —— 发送时机状态机
 *================================================================================================*/
void Com_MainFunctionTx(void)
{
    uint16 i;

    if (Com_Initialized == FALSE)
    {
        return;
    }

    for (i = 0U; i < Com_ConfigPtr->TxPduCount; i++)
    {
        const Com_IpduConfigType* pdu   = &Com_ConfigPtr->TxPdus[i];
        Com_TxPduStateType*       state = &Com_TxState[i];
        boolean                   needSend = FALSE;

        /*----------------------------------------------------------------------
         * 按传输模式判断本轮要不要发
         *----------------------------------------------------------------------*/
        switch (pdu->TxMode)
        {
            case COM_TX_MODE_PERIODIC:
                state->periodCounter++;
                if ((pdu->TxPeriodTicks > 0U) &&
                    (state->periodCounter >= pdu->TxPeriodTicks))
                {
                    state->periodCounter = 0U;
                    needSend = TRUE;
                }
                break;

            case COM_TX_MODE_DIRECT:
                if (state->txRequested != FALSE)
                {
                    state->txRequested = FALSE;
                    needSend = TRUE;
                }
                break;

            case COM_TX_MODE_MIXED:
                state->periodCounter++;
                if ((pdu->TxPeriodTicks > 0U) &&
                    (state->periodCounter >= pdu->TxPeriodTicks))
                {
                    state->periodCounter = 0U;
                    needSend = TRUE;
                }
                if (state->txRequested != FALSE)
                {
                    state->txRequested = FALSE;
                    needSend = TRUE;
                }
                break;

            case COM_TX_MODE_NONE:
            default:
                break;
        }

        /*----------------------------------------------------------------------
         * 真正下发
         *
         * 【为什么要看 txInProgress】
         * 上一帧还没确认（邮箱还在忙），这轮就不必再交一次 ——
         * 否则 PduR/CanIf 会返回 CAN_BUSY，白白损失一次。
         * 跳过这一轮，下个周期再试。
         *
         * 【为什么只在 E_OK 时才置 txInProgress】
         * 若 PduR 返回 E_NOT_OK/CAN_BUSY，说明这帧根本没进硬件，
         * 必须允许下轮重试，不能把状态卡住。
         *----------------------------------------------------------------------*/
        if ((needSend != FALSE) && (state->txInProgress == FALSE))
        {
            PduInfoType info;

            info.SduDataPtr  = state->shadow;
            info.MetaDataPtr = NULL_PTR;
            info.SduLength   = (PduLengthType)pdu->Length;

            if (PduR_ComTransmit(pdu->TxPduId, &info) == E_OK)
            {
                state->txInProgress = TRUE;
            }
        }
    }
}

/*==================================================================================================
 *                              Com_MainFunctionRx —— 接收超时监控
 *================================================================================================*/
void Com_MainFunctionRx(void)
{
    uint16 i;

    if (Com_Initialized == FALSE)
    {
        return;
    }

    for (i = 0U; i < Com_ConfigPtr->SignalCount; i++)
    {
        const Com_SignalConfigType* sig = &Com_ConfigPtr->Signals[i];
        uint16 globalIdx = Com_SigToIpdu[i];

        /* 只监控 RX 信号，且只监控配了超时的 */
        if (globalIdx < Com_ConfigPtr->TxPduCount)
        {
            continue;
        }
        if (sig->TimeoutTicks == 0U)
        {
            continue;
        }

        if (Com_SigTimeoutCounter[i] < 0xFFFFU)
        {
            Com_SigTimeoutCounter[i]++;
        }

        if (Com_SigTimeoutCounter[i] > sig->TimeoutTicks)
        {
            /* 超时：用替代值（InitValue）覆盖，并复位计数避免反复覆盖 */
            Com_SigShadowValue[i]        = sig->InitValue;
            Com_SigTimeoutCounter[i]     = 0U;
        }
    }
}
