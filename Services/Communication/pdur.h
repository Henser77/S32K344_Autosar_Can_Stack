/*==================================================================================================
 *  Services/pdur.h —— 服务层 PduR（PDU Router）对外接口
 *
 *  【本层在架构中的位置】
 *
 *      COM（信号打包）        Dcm（诊断）        CanTp（分段传输）
 *            │                    │                   │
 *            └────────┬───────────┴───────────────────┘
 *                     ▼   PduR_ComTransmit / PduR_DcmTransmit ...
 *              ┌──────────────┐
 *              │     PduR     │  ← 本层：唯一的"路由图持有者"
 *              └──────────────┘
 *                     │   CanIf_Transmit
 *                     ▼
 *                   CanIf  →  Can  →  硬件
 *
 *  【本层存在的根本理由】
 *  不是为了"少几条连线"，而是为了让 **COM 不必知道总线的存在**。
 *
 *  如果没有 PduR，COM 就得写成：
 *      if (走 CAN) { CanIf_Transmit(...); }
 *      else if (走 LIN) { LinIf_Transmit(...); }
 *  那样 COM 就知道了"世界上有哪几种总线"，分层就崩了。
 *
 *  PduR 把这个知识全部吸收进来：**只有它知道"哪条 PDU 走哪条总线"**。
 *
 *  【1:N 扇出 —— 本层最重要的能力】
 *
 *      上行（接收）：一条收到的报文 → 同时给 COM 和 Dcm
 *      下行（发送）：一条 COM 的 PDU   → 同时发到两路 CAN（网关行为）
 *
 *  这正是 AUTOSAR 把 PduId 设计成"每层独立编号"的原因 ——
 *  只有独立的编号空间，才能让一条 PDU 有多个"下游身份"。
 *
 *  【依赖方向（重要）】
 *
 *      PduR  ──依赖──>  CanIf        （直接调用 CanIf_Transmit，真依赖）
 *      CanIf ──不依赖─>  PduR        （CanIf 只调函数指针，零代码引用）
 *
 *  所以数据是双向流的，但**依赖是单向的**。这叫依赖倒置：
 *  下层不依赖上层的实现，只依赖一个函数指针类型。
 *================================================================================================*/
#ifndef PDUR_H
#define PDUR_H

/*==================================================================================================
 *                                        INCLUDES
 *================================================================================================*/
#include "Std_Types.h"
#include "ComStack_Types.h"     /* PduIdType / PduLengthType / PduInfoType */

/*==================================================================================================
 *                                    DEFINES AND MACROS
 *================================================================================================*/

/** @brief PduR 的 AUTOSAR 模块号（AUTOSAR 标准分配，见 SWS_PduR） */
#define PDUR_MODULE_ID              (0x35U)

/** @brief 单实例模块，实例号固定 0 */
#define PDUR_INSTANCE_ID            (0U)

/**
 * @brief 开发错误检测开关
 * @details 优先级：本模块单独定义 > 全局 BSW_DEV_ERROR_DETECT > 默认 STD_ON
 *          说明见 canif.h 同一段。
 */
#ifndef PDUR_DEV_ERROR_DETECT
    #ifdef BSW_DEV_ERROR_DETECT
        #define PDUR_DEV_ERROR_DETECT   (BSW_DEV_ERROR_DETECT)
    #else
        #define PDUR_DEV_ERROR_DETECT   (STD_ON)
    #endif
#endif

/** @brief PduR 的 API 编号（Det 上报时用） */
typedef enum
{
    PDUR_API_INIT                   = 0U,
    PDUR_API_COM_TRANSMIT           = 1U,
    PDUR_API_CANIF_RX_INDICATION    = 2U,
    PDUR_API_CANIF_TX_CONFIRMATION  = 3U,
    PDUR_API_GET_VERSION_INFO       = 4U
} PduR_ApiIdType;

/** @brief PduR 的错误编号（Det 上报时用） */
typedef enum
{
    PDUR_E_PARAM_POINTER        = 0x01U,    /**< 传了 NULL 指针 */
    PDUR_E_UNINIT               = 0x02U,    /**< 未初始化就调用 API */
    PDUR_E_PARAM_PDU_ID         = 0x03U,    /**< 该 PDU 没有配置路由 */
    PDUR_E_INVALID_CONFIG       = 0x04U     /**< 配置表内部自相矛盾（DestCount 与数组不符等） */
} PduR_ErrorIdType;

/**
 * @brief 是否提供 PduR_GetVersionInfo()
 * @details AUTOSAR 把这类"功能开关"放在 *_Cfg.h 里。本工程的 API 头不 include
 *          配置头（为了保持接口层干净），所以在这里给一个默认值，
 *          配置头可以在 include 本文件之前覆盖它。
 */
#ifndef PDUR_VERSION_INFO_API
#define PDUR_VERSION_INFO_API       (STD_ON)
#endif

/** @brief 版本号（AUTOSAR 惯例，供 PduR_GetVersionInfo 使用） */
#define PDUR_VENDOR_ID              (43U)       /* 本工程用 NXP 的 vendor id */
#define PDUR_SW_MAJOR_VERSION       (1U)
#define PDUR_SW_MINOR_VERSION       (0U)
#define PDUR_SW_PATCH_VERSION       (0U)
#define PDUR_AR_RELEASE_MAJOR_VERSION   (4U)
#define PDUR_AR_RELEASE_MINOR_VERSION   (4U)

/*==================================================================================================
 *                                 STRUCTURES AND OTHER TYPEDEFS
 *================================================================================================*/

/** @brief PduR 配置类型前向声明（完整定义在 pdur_cfg.h） */
struct PduR_ConfigType;
typedef struct PduR_ConfigType PduR_ConfigType;

/**
 * @brief 向上层模块上报接收数据的函数类型
 * @details 对应 AUTOSAR 里各上层模块的 RxIndication 接口：
 *              void Com_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr);
 *              void Dcm_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr);
 *
 *          它们的签名在 AUTOSAR 里是**完全一致**的，所以可以用同一个函数指针类型。
 *          （真实 AUTOSAR 里 PduR 直接调 Com_RxIndication，由生成器填；
 *            我们还没有 COM 模块，所以用配置里的函数指针，语义等价。）
 */
typedef void (*PduR_RxIndicationFctType)(PduIdType RxPduId, const PduInfoType* PduInfoPtr);

/**
 * @brief 向上层模块上报发送确认的函数类型
 * @details 对应 AUTOSAR 的 Com_TxConfirmation / Dcm_TxConfirmation：
 *              void Com_TxConfirmation(PduIdType TxPduId, Std_ReturnType result);
 */
typedef void (*PduR_TxConfirmationFctType)(PduIdType TxPduId, Std_ReturnType result);

/*==================================================================================================
 *                                 GLOBAL FUNCTION PROTOTYPES
 *================================================================================================*/

/**
 * @brief 初始化 PduR（对应 SWS_PduR 的 PduR_Init）
 *
 * @details 做两件事：
 *          1. 保存配置指针
 *          2. 把自己注册到 CanIf 的上报 hook 上
 *             → CanIf_RegisterRxIndicationHook(PduR_CanIfRxIndication)
 *             → CanIf_RegisterTxConfirmationHook(PduR_CanIfTxConfirmation)
 *
 * 【调用顺序】
 *      Can_Init()        →  CanIf_Init()  →  PduR_Init()  →  上层注册  →  SetControllerMode(STARTED)
 *
 *  PduR 必须在控制器启动**之前**完成注册，否则第一帧可能到达而 PduR 还没接上。
 *  （实际上 CanIf 的 hook 槽位在 Init 后不会重置，所以 PduR_Init 与 CanIf_Init
 *    的先后无所谓，但都在 SetControllerMode 之前是硬要求。）
 */
void PduR_Init(const PduR_ConfigType* ConfigPtr);

/**
 * @brief 读取版本信息（对应 SWS_PduR 的 PduR_GetVersionInfo）
 * @details 标准要求每个 BSW 模块都提供它，用于诊断和版本核对。
 *          这类"看起来没用"的接口，正是 AUTOSAR 模块的标准形态的一部分。
 */
#if (PDUR_VERSION_INFO_API == STD_ON)
void PduR_GetVersionInfo(Std_VersionInfoType* versioninfo);
#endif

/*--------------------------------------------------------------------------------------------------
 *                        下行接口 —— 被上层模块（COM / Dcm / CanTp）调用
 *------------------------------------------------------------------------------------------------*/

/**
 * @brief 发送请求（对应 SWS_PduR 的 PduR_ComTransmit）
 *
 * @param[in] TxPduId     源 PDU 号 —— 这是 **COM 自己的编号**，不是 CanIf 的
 * @param[in] PduInfoPtr  待发数据
 *
 * @return E_OK     所有目标都接受了发送请求
 * @return E_NOT_OK 有任一目标拒绝（或 PDU 未配置路由）
 *
 * 【为什么参数叫 TxPduId 而不是 ComTxPduId】
 * AUTOSAR 为每个上层模块生成一个同名接口：
 *      PduR_ComTransmit   —— COM 用
 *      PduR_DcmTransmit   —— Dcm 用
 *      PduR_CanTpTransmit —— CanTp 用
 * 它们的签名完全一样，只是"编号空间"不同。
 * 本工程目前只实现了 Com 这一个入口；加 Dcm 时再补一个同签名的薄封装即可。
 *
 * 【1:N 扇出的返回值语义】
 * 本实现按 SWS 取"**全部成功才算成功**"：任一目标返回 E_NOT_OK，整体返回 E_NOT_OK。
 * 注意这意味着"部分成功"会被报成失败 —— 上层需要知道某一帧可能已经发出去了。
 */
Std_ReturnType PduR_ComTransmit(PduIdType TxPduId, const PduInfoType* PduInfoPtr);

/*--------------------------------------------------------------------------------------------------
 *                        上行接口 —— 被下层模块（CanIf）调用
 *
 * 这两个函数名是 AUTOSAR 标准规定的（SWS_PduR 里的 PduR_CanIfRxIndication /
 * PduR_CanIfTxConfirmation），不是我们自创的。
 *------------------------------------------------------------------------------------------------*/

/**
 * @brief 收到一帧报文（被 CanIf 回调）
 *
 * @param[in] RxPduId     CanIf 的 RxPduId（CanIf 的编号空间）
 * @param[in] PduInfoPtr  数据（**零拷贝**指针，指向 MCAL 的接收缓冲）
 *
 * 【零拷贝约定】
 *  PduInfoPtr->SduDataPtr 指向 MCAL 的静态接收缓冲，本函数返回后随时可能被
 *  下一帧覆盖。所以所有收到它的上层模块必须**同步消费完**，不得保存指针。
 *
 * 【1:N 扇出】
 *  按路由表把同一份数据分发给所有目标（例如 COM + Dcm）。
 *  注意：这里传给各目的地的是**同一个 PduInfoPtr**，所以每个目的地都必须自己
 *  决定要不要拷贝 —— 若某个目的地想延后处理，必须自己 memcpy。
 */
void PduR_CanIfRxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr);

/**
 * @brief 一帧发送完成（被 CanIf 回调）
 *
 * @param[in] TxPduId   CanIf 的 TxPduId（**注意：不是 COM 的编号！**）
 * @param[in] result    发送结果
 *
 * 【这个函数做的核心工作：编号空间的反向翻译】
 *
 *       CanIf 说："你的 3 号 PDU 发出去了"
 *           ↓ PduR 反查路由表
 *       PduR 翻译成："COM，你的 7 号 PDU 发出去了"
 *           ↓
 *       Com_TxConfirmation(7, result)
 *
 * 这个反向查找必须扫整个 TX 路由表（目标字段），因为没有现成的反向索引。
 *
 * ⚠️ 配置约束：**同一个 CanIf TxPduId 不应出现在多条 PduR 路径里**
 *    否则一次确认会多播给多个源，语义就错了。
 */
void PduR_CanIfTxConfirmation(PduIdType TxPduId, Std_ReturnType result);

#endif /* PDUR_H */
