/*==================================================================================================
 *  Services/dcm.h —— Dcm（Diagnostic Communication Manager）接口
 *
 *  【本工程里 Dcm 是"半成品替身"】
 *
 *  AUTOSAR 的 Dcm 负责诊断通信：
 *      - 接收 UDS 请求（0x10 会话控制、0x22 读 DID、0x2E 写 DID、0x27 安全访问…）
 *      - 通过 PduR 收发诊断 PDU
 *      - 与 Dem（故障管理）、NvM 交互
 *
 *  完整实现要几千行，本工程只做**最小的接收记录**，
 *  目的是让 PduR 的 1:N 扇出有一个真实的第二个目标 ——
 *  「同一条收到的报文，既给 COM 解信号，也给 Dcm 解诊断」。
 *
 *  这正好体现了 PduR 存在的意义：**只有它知道一条报文要发给几个模块。**
 *================================================================================================*/
#ifndef DCM_H
#define DCM_H

#include "Std_Types.h"
#include "ComStack_Types.h"

/*==================================================================================================
 *                    模块标识与开发错误检测（Det）配置
 *================================================================================================*/

/** @brief Dcm 的标准 AUTOSAR 模块号（0x37 = 55） */
#define DCM_MODULE_ID                   (0x37U)

/** @brief 单实例模块，实例号固定 0 */
#define DCM_INSTANCE_ID                 (0U)

/**
 * @brief 开发错误检测开关
 * @details 优先级：本模块单独定义 > 全局 BSW_DEV_ERROR_DETECT > 默认 STD_ON
 */
#ifndef DCM_DEV_ERROR_DETECT
    #ifdef BSW_DEV_ERROR_DETECT
        #define DCM_DEV_ERROR_DETECT    (BSW_DEV_ERROR_DETECT)
    #else
        #define DCM_DEV_ERROR_DETECT    (STD_ON)
    #endif
#endif

/** @brief Dcm 的 API 编号 */
typedef enum
{
    DCM_API_INIT                        = 0U,
    DCM_API_RX_INDICATION               = 1U,
    DCM_API_TX_CONFIRMATION             = 2U,
    DCM_API_GET_VERSION_INFO            = 3U
} Dcm_ApiIdType;

/** @brief Dcm 的错误编号 */
typedef enum
{
    DCM_E_PARAM_POINTER         = 0x01U,    /**< 传了 NULL 指针 */
    DCM_E_UNINIT                = 0x02U,    /**< 未初始化就调用 API */
    DCM_E_PARAM_PDU_ID          = 0x03U     /**< Dcm RxPduId 不在配置表里 */
} Dcm_ErrorIdType;

/*==================================================================================================
 *                                 调试可见的状态
 *================================================================================================*/
typedef struct
{
    uint32 RxCount;             /**< 收到的诊断相关 PDU 总数 */
    uint8  LastRxPduId;         /**< 最近一次的 Dcm RxPduId */
    uint8  LastRequestLen;      /**< 最近一次请求的字节数 */
    uint8  LastRequestSid;      /**< 最近一次请求的服务 ID（UDS 第一个字节） */
} Dcm_StatusType;

/** @brief 运行状态（调试器 Watch 窗口看这个） */
extern volatile Dcm_StatusType g_DcmStatus;

/*==================================================================================================
 *                         PduR 调用本模块的接口（AUTOSAR 标准名）
 *================================================================================================*/

/**
 * @brief 收到一帧诊断相关 PDU（被 PduR 调用）
 *
 * @param[in] RxPduId     Dcm 自己的 RxPduId（PduR 已把 CanIf 的编号翻译过来）
 * @param[in] PduInfoPtr  数据（零拷贝指针，指向 MCAL 接收缓冲）
 *
 * 【调用上下文：中断】
 *  所以这里只做记录，不做真正的诊断处理。
 *  真实 Dcm 会把请求拷进自己的缓冲，然后交给任务上下文处理。
 */
void Dcm_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr);

/**
 * @brief 一帧诊断响应发送完成（被 PduR 调用）
 * @details 本工程没有诊断响应发送，保留接口占位。
 */
void Dcm_TxConfirmation(PduIdType TxPduId, Std_ReturnType result);

#endif /* DCM_H */
