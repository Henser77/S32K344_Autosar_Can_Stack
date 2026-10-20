/*==================================================================================================
 *  Services/System/bswcheck.c —— BSW 配置一致性自检实现
 *================================================================================================*/

#include "bswcheck.h"
#include "ecum.h"               /* ECUM_MODULE_ID / ECUM_API_BSW_CHECK / 错误码 */

#include "can_cfg.h"            /* Can_Config —— MCAL 配置 */
#include "canif_cfg.h"          /* CanIf_Config —— CanIf 配置 */
#include "com_cfg.h"            /* Com_Config —— COM 配置 */

#include "Det.h"

/*==================================================================================================
 *                                  调试可见变量
 *================================================================================================*/
volatile BswCheck_StatusType g_BswCheckStatus;

/*==================================================================================================
 *                                  本地辅助
 *================================================================================================*/

/**
 * @brief 上报一条配置错误
 * @param instance 出问题的表项下标（调试时据此定位是哪一行配置）
 * @param errorId  错误类型（ECUM_E_CHECK_*）
 */
static void BswCheck_Report(uint16 instance, uint16 errorId)
{
    g_BswCheckStatus.ErrorCount++;
    g_BswCheckStatus.LastErrorId = errorId;
    g_BswCheckStatus.LastInstance = instance;

    (void)Det_ReportError((uint16)ECUM_MODULE_ID,
                          (uint8)instance,          /* ← 表项下标当作 InstanceId */
                          (uint8)ECUM_API_BSW_CHECK,
                          (uint8)errorId);
}

/*==================================================================================================
 *  检查 1 & 2：CanIf ↔ MCAL
 *
 *  【为什么这个只能在运行期查】
 *  CanIf 的 Length 和 MCAL 的 PayloadSize 分别在两个 .c 文件的 static 表里，
 *  同一个编译单元看不到对方 —— 编译期的 typedef 断言帮不上忙。
 *  但两者的配置对象都是 extern 的（CanIf_Config / Can_Config），
 *  所以运行期能取到。
 *
 *  【顺带检查句柄存在性】
 *  这类"引用对不上"本来靠符号化（CanConf_TxObject_Hth4）在编译期就该挡住。
 *  这里再查一遍是**纵深防御**：万一有人绕开符号名直接写了数字，这里还能兜住。
 *================================================================================================*/
static void BswCheck_CanIfVsMcal(void)
{
    uint16 i;
    uint16 j;

    /*--------------------------------------------------------------------------
     * 1a. 每个 CanIf 发送 PDU 的 Hth 必须在 MCAL 里存在，且 Length 不超 PayloadSize
     *------------------------------------------------------------------------*/
    for (i = 0U; i < CanIf_Config.TxPduCount; i++)
    {
        Can_HwHandleType hth = CanIf_Config.TxPdus[i].Hth;
        boolean          found = FALSE;

        g_BswCheckStatus.CheckedCanIf++;

        for (j = 0U; j < Can_Config.TxObjectCount; j++)
        {
            if (Can_Config.TxObjects[j].Hth == hth)
            {
                found = TRUE;

                /* ① CanIf 说的长度 不能超过 MCAL 给该对象的容量 */
                if (CanIf_Config.TxPdus[i].Length > Can_Config.TxObjects[j].PayloadSize)
                {
                    BswCheck_Report(i, (uint16)ECUM_E_CHECK_TX_LENGTH);
                }
                break;
            }
        }

        /* ② Hth 在 MCAL 里根本不存在 */
        if (found == FALSE)
        {
            BswCheck_Report(i, (uint16)ECUM_E_CHECK_HTH_MISSING);
        }
    }

    /*--------------------------------------------------------------------------
     * 1b. 每个 CanIf 接收 PDU 的 Hrh 必须在 MCAL 里存在
     *------------------------------------------------------------------------*/
    for (i = 0U; i < CanIf_Config.RxPduCount; i++)
    {
        Can_HwHandleType hrh = CanIf_Config.RxPdus[i].Hrh;
        boolean          found = FALSE;

        for (j = 0U; j < Can_Config.RxObjectCount; j++)
        {
            if (Can_Config.RxObjects[j].Hrh == hrh)
            {
                found = TRUE;
                break;
            }
        }

        if (found == FALSE)
        {
            BswCheck_Report(i, (uint16)ECUM_E_CHECK_HRH_MISSING);
        }
    }
}

/*==================================================================================================
 *  检查 3 & 4：COM 信号位布局
 *
 *  【这一段是自检真正独有的价值】
 *  位区间、位重叠这些东西全部要在运行时按 BitPosition/BitSize 算，
 *  编译期完全无能为力（C 没有 constexpr）。
 *
 *  【两类信号的位语义不同，必须分开算】
 *      普通数值信号：BitPosition 是"位号"，BitSize 是"位宽"
 *                    → 占用 [BitPosition, BitPosition + BitSize)
 *      UINT8_N     ：BitPosition 是"字节偏移"，BitSize 是"字节数"
 *                    → 占用 [BitPosition*8, (BitPosition + BitSize)*8)
 *  混在一起算会把字节数当位数，得出完全错误的结论。
 *================================================================================================*/

/**
 * @brief 算出一个信号占用的位区间
 * @param[out] startBit 起始位（含）
 * @param[out] endBit   结束位（不含）
 */
static void BswCheck_SignalBitRange(const Com_SignalConfigType* sig,
                                    uint32* startBit, uint32* endBit)
{
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
}

/**
 * @brief 检查一条 PDU 内的所有信号
 * @param pdu           PDU 配置
 * @param pduIndex      PDU 下标（用于上报定位）
 * @param pduLength     该 PDU 的字节长度
 * @param isTx          TRUE = 发送 PDU，FALSE = 接收 PDU
 */
static void BswCheck_OnePdu(const Com_IpduConfigType* pdu, uint16 pduIndex,
                            uint8 pduLength, boolean isTx)
{
    uint32 pduBits = ((uint32)pduLength) * 8UL;
    uint8  k;
    uint8  m;

    /*--------------------------------------------------------------------------
     * 3a. 信号区间不能越过信号表尾
     *------------------------------------------------------------------------*/
    if (((uint32)pdu->FirstSignalIdx + (uint32)pdu->SignalCount) >
        (uint32)Com_Config.SignalCount)
    {
        BswCheck_Report(pduIndex, (uint16)ECUM_E_CHECK_SIGNAL_RANGE);
        return;     /* 区间本身就错了，后面的逐信号检查没有意义 */
    }

    /*--------------------------------------------------------------------------
     * 3b. 每个信号必须完全落在 PDU 的位宽内
     *------------------------------------------------------------------------*/
    for (k = 0U; k < pdu->SignalCount; k++)
    {
        uint16                      sigIdx = (uint16)(pdu->FirstSignalIdx + k);
        const Com_SignalConfigType* sig    = &Com_Config.Signals[sigIdx];
        uint32                      startBit;
        uint32                      endBit;

        g_BswCheckStatus.CheckedComSig++;

        BswCheck_SignalBitRange(sig, &startBit, &endBit);

        if (endBit > pduBits)
        {
            /* 信号伸出 PDU 边界 —— 打包时会被 Com_SetBit 静默丢弃一部分位，
             * 表现为"这个信号的值总是上不去"或"低位对但高位永远是 0"。 */
            BswCheck_Report(sigIdx, (uint16)ECUM_E_CHECK_SIGNAL_BOUNDS);
        }
    }

    /*--------------------------------------------------------------------------
     * 3c. 同一 PDU 内任意两个信号的位区间不能重叠
     *
     *  O(n²)，但 n ≤ COM_MAX_SIGNALS_PER_PDU（8），代价可以忽略。
     *  只查一次，不是每个周期都查，所以完全没必要优化。
     *------------------------------------------------------------------------*/
    for (k = 0U; k < pdu->SignalCount; k++)
    {
        uint16 sigIdxA = (uint16)(pdu->FirstSignalIdx + k);
        uint32 startA;
        uint32 endA;

        BswCheck_SignalBitRange(&Com_Config.Signals[sigIdxA], &startA, &endA);

        for (m = (uint8)(k + 1U); m < pdu->SignalCount; m++)
        {
            uint16 sigIdxB = (uint16)(pdu->FirstSignalIdx + m);
            uint32 startB;
            uint32 endB;

            BswCheck_SignalBitRange(&Com_Config.Signals[sigIdxB], &startB, &endB);

            /* 两个半开区间重叠的判据：A.start < B.end 且 B.start < A.end */
            if ((startA < endB) && (startB < endA))
            {
                /* 报**后一个**信号的下标（便于往下查） */
                BswCheck_Report(sigIdxB, (uint16)ECUM_E_CHECK_SIGNAL_OVERLAP);
            }
        }
    }

    (void)isTx;
}

/*==================================================================================================
 *                                  BswCheck_Run
 *================================================================================================*/
Std_ReturnType BswCheck_Run(void)
{
    uint16 i;

    /* 清零统计（每次上电从干净状态开始） */
    g_BswCheckStatus.ErrorCount   = 0U;
    g_BswCheckStatus.LastErrorId  = 0U;
    g_BswCheckStatus.LastInstance = 0U;
    g_BswCheckStatus.CheckedCanIf = 0U;
    g_BswCheckStatus.CheckedComSig = 0U;

    /*--------------------------------------------------------------------------
     * 1. CanIf ↔ MCAL（句柄存在性、长度不超容量）
     *------------------------------------------------------------------------*/
    BswCheck_CanIfVsMcal();

    /*--------------------------------------------------------------------------
     * 2. COM 发送 PDU 内的信号布局
     *------------------------------------------------------------------------*/
    for (i = 0U; i < Com_Config.TxPduCount; i++)
    {
        BswCheck_OnePdu(&Com_Config.TxPdus[i], i,
                        Com_Config.TxPdus[i].Length, TRUE);
    }

    /*--------------------------------------------------------------------------
     * 3. COM 接收 PDU 内的信号布局
     *------------------------------------------------------------------------*/
    for (i = 0U; i < Com_Config.RxPduCount; i++)
    {
        BswCheck_OnePdu(&Com_Config.RxPdus[i], i,
                        Com_Config.RxPdus[i].Length, FALSE);
    }

    return (g_BswCheckStatus.ErrorCount == 0U) ? E_OK : E_NOT_OK;
}
