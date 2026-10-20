/*==================================================================================================
 *  Services/pdur_cfg.h —— PduR 配置类型
 *
 *  【AUTOSAR 的路由模型】
 *
 *  标准里 PduR 的配置核心是 **PduRRoutingPath（路由路径）**：
 *
 *      PduRRoutingPath
 *        ├── PduRSrcPdu          （1 个源）
 *        └── PduRDestPdu         （1..n 个目标）  ← 这就是 1:N 扇出
 *
 *  【我们为什么拆成 TX / RX 两张表】
 *
 *  AUTOSAR 用**一张**路由表，靠"源 PDU 属于哪个模块"来判断方向。
 *  我们拆成两张，原因很实际：
 *    - 方向不同 → 调用方式不同
 *        下行（TX）：直接调 CanIf_Transmit()          —— 因为 CanIf 存在
 *        上行（RX）：调配置里的函数指针（COM/Dcm）     —— 因为 COM 还不存在
 *    - 拆开之后两边的结构体字段不一样，能各自更精确
 *
 *  语义上与标准的一张表等价，只是把"方向"从隐式（看源模块类型）
 *  变成了显式（两张表）。
 *================================================================================================*/
#ifndef PDUR_CFG_H
#define PDUR_CFG_H

/*==================================================================================================
 *                                        INCLUDES
 *================================================================================================*/
#include "pdur.h"
#include "canif.h"      /* 需要 Can_HwHandleType？不需要；但 TX 目标要用 CanIf 的 PduIdType */

/*--------------------------------------------------------------------------------------------------
 * 路由路径数量
 * pdur_cfg.c 末尾有编译期断言，保证与实际的表项数一致。
 *------------------------------------------------------------------------------------------------*/
#define PDUR_TX_PATH_COUNT          (3U)
#define PDUR_RX_PATH_COUNT          (4U)

/*==================================================================================================
 *                                 STRUCTURES AND OTHER TYPEDEFS
 *================================================================================================*/

/*--------------------------------------------------------------------------------------------------
 *                                  下行（TX）路由：上层 → CanIf
 *------------------------------------------------------------------------------------------------*/

/**
 * @brief TX 路径的一个目标
 * @details 目标模块是 CanIf，所以只需要它的 TxPduId。
 */
typedef struct
{
    PduIdType DestPduId;    /**< CanIf 的 TxPduId（CanIf 的编号空间） */
} PduR_TxDestConfigType;

/**
 * @brief 一条 TX 路由路径
 */
typedef struct
{
    PduIdType                    SrcPduId;         /**< 源：COM 的 TxPduId */
    PduR_TxConfirmationFctType   SrcTxConfirmation;/**< 发送确认回调（回到 COM） */
    uint8                        DestCount;        /**< 目标个数（1..n） */
    const PduR_TxDestConfigType* Dests;            /**< 目标数组 */
} PduR_TxPathConfigType;

/*--------------------------------------------------------------------------------------------------
 *                                  上行（RX）路由：CanIf → 上层
 *------------------------------------------------------------------------------------------------*/

/**
 * @brief RX 路径的一个目标
 * @details 目标模块是 COM / Dcm 等。因为它们的 RxIndication 签名相同，
 *          所以可以用同一个函数指针类型，逐个目标存一份。
 */
typedef struct
{
    PduIdType                  DestPduId;      /**< 目标模块的 RxPduId（如 COM 的编号） */
    PduR_RxIndicationFctType   RxIndication;   /**< 目标模块的上报函数（如 Com_RxIndication） */
} PduR_RxDestConfigType;

/**
 * @brief 一条 RX 路由路径
 */
typedef struct
{
    PduIdType                    SrcPduId;     /**< 源：CanIf 的 RxPduId */
    uint8                        DestCount;    /**< 目标个数（1..n） */
    const PduR_RxDestConfigType* Dests;        /**< 目标数组 */
} PduR_RxPathConfigType;

/*--------------------------------------------------------------------------------------------------
 *                                      顶层配置
 *------------------------------------------------------------------------------------------------*/

struct PduR_ConfigType
{
    const PduR_TxPathConfigType* TxPaths;
    uint16                       TxPathCount;

    const PduR_RxPathConfigType* RxPaths;
    uint16                       RxPathCount;
};

/*==================================================================================================
 *                                 GLOBAL VARIABLE DECLARATIONS
 *================================================================================================*/

/** @brief 由 pdur_cfg.c 提供的配置实例，传给 PduR_Init() */
extern const PduR_ConfigType PduR_Config;

#endif /* PDUR_CFG_H */
