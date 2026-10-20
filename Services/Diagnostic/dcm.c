/*==================================================================================================
 *  Services/dcm.c —— Dcm 替身实现（最小接收记录）
 *================================================================================================*/

#include "dcm.h"
#include "Det.h"

/*==================================================================================================
 *                                    LOCAL DEFINES
 *================================================================================================*/

/** @brief Det 上报的便捷宏 */
#if (DCM_DEV_ERROR_DETECT == STD_ON)
    #define DCM_DET_ERROR(api, err)                                     \
        ((void)Det_ReportError((uint16)DCM_MODULE_ID,                   \
                               (uint8)DCM_INSTANCE_ID,                  \
                               (uint8)(api),                            \
                               (uint8)(err)))
#else
    #define DCM_DET_ERROR(api, err)     ((void)0)
#endif

/*==================================================================================================
 *                                  调试可见变量
 *================================================================================================*/
volatile Dcm_StatusType g_DcmStatus;

/*==================================================================================================
 *                                  Dcm_RxIndication
 *
 * 【调用上下文：CAN 接收中断】
 * 整条链是同步的：CAN ISR → CanIf → PduR → 本函数 → 返回 → 中断退出。
 * 所以这里必须"快进快出"，只做记录。
 *================================================================================================*/
void Dcm_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr)
{
    if (PduInfoPtr == NULL_PTR)
    {
        DCM_DET_ERROR(DCM_API_RX_INDICATION, DCM_E_PARAM_POINTER);
        return;
    }

    g_DcmStatus.RxCount++;
    g_DcmStatus.LastRxPduId = (uint8)RxPduId;

    if ((PduInfoPtr->SduDataPtr != NULL_PTR) && (PduInfoPtr->SduLength > 0U))
    {
        g_DcmStatus.LastRequestLen = (uint8)PduInfoPtr->SduLength;
        g_DcmStatus.LastRequestSid = PduInfoPtr->SduDataPtr[0];   /* UDS 服务 ID */

        /*--------------------------------------------------------------------------
         * 真实 Dcm 在这里会做的事：
         *   1. 把请求拷进自己的缓冲（不能引用 SduDataPtr，中断一返回就失效）
         *   2. 交给 Dcm 任务上下文解析（0x10/0x22/0x2E/0x27/0x3E…）
         *   3. 组织响应，经 PduR → CanIf → Can 发回去
         *
         * 本工程只记录，不响应。
         *------------------------------------------------------------------------*/
    }
}

/*==================================================================================================
 *                                  Dcm_TxConfirmation
 *================================================================================================*/
void Dcm_TxConfirmation(PduIdType TxPduId, Std_ReturnType result)
{
    (void)TxPduId;
    (void)result;

    /* 本工程没有诊断响应发送，保留接口占位 */
}
