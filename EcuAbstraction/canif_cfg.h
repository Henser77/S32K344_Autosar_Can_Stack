/*==================================================================================================
 *  EcuAbstraction/canif_cfg.h —— CanIf 配置类型与句柄约定
 *
 *  【配置与实现分离】同 MCAL 层：改行为只改 canif_cfg.c，canif.c 不动。
 *
 *  【本层的配置在描述什么】
 *   MCAL 的配置描述"哪些邮箱收发什么"；
 *   CanIf 的配置描述"哪些 PDU 用哪些邮箱"，也就是给每个硬件资源起一个
 *   **逻辑名字（PduId）**。PduR / COM 只认 PduId，永远看不到 Hth / Hrh。
 *
 *   这正是"应用层不知道底层硬件"这条原则落到 CanIf 上的样子。
 *================================================================================================*/
#ifndef CANIF_CFG_H
#define CANIF_CFG_H

/*==================================================================================================
 *                                        INCLUDES
 *================================================================================================*/
#include "canif.h"

/* 【注意：本头文件**不** include MCAL 的 can_cfg.h】
 *
 * 句柄空间的常量（CAN_HTH_FIRST_INDEX / CAN_CONTROLLER_COUNT）只在
 * canif.c 与 canif_cfg.c 这两个**实现文件**里用得到，用来给索引数组定长、
 * 给配置表填句柄号。
 *
 * 把它们留在实现文件里，好处是：
 *   上层 include canif_cfg.h 时，**不会被动看到 MCAL 的任何东西**。
 *   这条隔离可以被验证 —— 只要上层能只凭 canif_cfg.h 编译通过，
 *   就说明 CanIf 确实把 MCAL 挡住了。
 *
 * （真实 AUTOSAR 里 CanIf_Cfg.h 也不 include Can_Cfg.h，
 *   HOH 编号在生成的配置里直接是字面数字。我们用宏是为了单一事实来源。） */

/*==================================================================================================
 *                                    DEFINES AND MACROS
 *================================================================================================*/

/**
 * @brief 本层支持的单个 PDU 最大字节数
 *
 * 【为什么现在就要有这个名字】
 * 现在值 8（经典 CAN）。将来启用 CAN FD 时改为 64 —— **只需要改这一个数字**。
 *
 * 如果不用这个宏，而是各处写死 8：
 *     uint8 buf[8];                        ← 每个缓冲
 *     if (len > 8) return E_NOT_OK;        ← 每处校验
 *     memcpy(dst, src, 8);                 ← 每处拷贝
 * 那么开 FD 时就要满工程找散落的 8，漏掉任何一个都是静默截断。
 *
 * ⚠️ 改这个值会**线性放大 RAM 占用**（每个 PDU 缓冲都变长），
 *    改之前先算一下预算。
 */
#define CANIF_MAX_PDU_LEN           (8U)

/*--------------------------------------------------------------------------------------------------
 * 本层的 PDU 数量
 * canif_cfg.c 末尾有编译期断言，保证与实际的表项数一致。
 *------------------------------------------------------------------------------------------------*/
#define CANIF_TX_PDU_COUNT          (4U)
#define CANIF_RX_PDU_COUNT          (4U)

/*==================================================================================================
 *      ★ PDU 的符号名 —— 上层（PduR）引用"名字"，不写"裸数字"
 *
 *  做法与 MCAL 的 CanConf_TxObject_Hth4 完全相同，目的也一样：
 *  让"引用了不存在的 PDU"从**运行时静默失败**变成**编译错误**。
 *
 *  【例子】
 *  PduR 的路由表里写：
 *        { ComConf_ComTxPdu_0,  Com_TxConfirmation,  1U,  { { CanIfConf_TxPdu_0 } } },
 *  如果 CanIf 端删掉了 PduId 0 这个对象（或者改了名），编译直接报错，
 *  而不是等运行时"发了但总线上没东西"。
 *================================================================================================*/

/* 发送 PDU（PduR 的 DestPduId 引用这些） */
#define CanIfConf_TxPdu_0           (0U)
#define CanIfConf_TxPdu_1           (1U)
#define CanIfConf_TxPdu_2           (2U)
#define CanIfConf_TxPdu_3           (3U)

/* 接收 PDU（PduR 的 SrcPduId 引用这些） */
#define CanIfConf_RxPdu_0           (0U)
#define CanIfConf_RxPdu_1           (1U)
#define CanIfConf_RxPdu_2           (2U)
#define CanIfConf_RxPdu_3           (3U)

/*==================================================================================================
 *                                 STRUCTURES AND OTHER TYPEDEFS
 *================================================================================================*/

/**
 * @brief 发送 PDU 配置
 * @details 把"逻辑 PDU 编号"绑定到"MCAL 的发送句柄 + CAN ID + 长度"。
 *
 *          【为什么必须有 ControllerId】
 *          PDU 通道模式（CanIf_SetPduMode）是按**控制器**生效的，而 Hth 只是
 *          一个编号，本身反推不出它属于哪个控制器（Hth 4 属于 CAN0 这件事
 *          只写在 MCAL 的配置表里）。所以这里显式记一份。
 *
 *          真实 AUTOSAR 是通过 CanIfTxPduHthRef → CanIfHthCfg → CanIfHthCanController
 *          这条引用链间接得到的；我们表小，直接写字段更直观。
 */
typedef struct
{
    PduIdType        TxPduId;      /**< 逻辑发送 PDU 号（必须等于本表下标，见 canif.c 的断言） */
    Can_HwHandleType Hth;          /**< 对应的 MCAL 发送句柄（>= CAN_HTH_FIRST_INDEX） */
    uint8            ControllerId; /**< 该句柄所属控制器（与 MCAL 的 Can_TxObjects[] 一致） */
    uint32           CanId;        /**< Can_IdType：裸 ID + bit31(扩展帧) + bit30(FD 帧) */
    uint8            Length;       /**< 该 PDU 的字节数（同时作为长度上限校验） */
} CanIf_TxPduConfigType;

/**
 * @brief 接收 PDU 配置
 * @details 把"逻辑 PDU 编号"绑定到"MCAL 的接收句柄"。
 *
 *          【为什么这里**没有** CanId / CanIdMask / IsExtended】
 *
 *          这是有意为之，不是漏写。判据是"接收 ID 的真相在哪一层"：
 *
 *              接收方向的 ID 过滤发生在**硬件**（RXIMR 掩码寄存器），
 *              那份配置写在 MCAL 的 Can_RxObjects[] 里。
 *              CanIf 拿到的已经是"硬件筛完之后的帧"，只知道是哪个 Hrh。
 *              再存一份 ID 就是**第二份事实** —— 改了 MCAL 忘了改 CanIf
 *              也不会报任何错。
 *
 *          【与官方实现对照】
 *          NXP 官方 CanIf 的 CanIf_RxPduType（CanIf_Types.h:318-325）
 *          字段是：PduId / PduLengthCheck / PduLength / CanIfCtrlId /
 *          UserRxIndication —— 同样**没有** CanId 和 Mask。
 *          而它的 TX 结构体（:330-331）里有 CanId 和 CanFrameType。
 *
 *          也就是说：**发送方向 ID 归 CanIf，接收方向 ID 归 MCAL** ——
 *          这条规则在官方实现里也是一致的。
 *
 *          （AUTOSAR 的配置容器 CanIfRxPduCfg 里确实有 CanIfRxPduCanId /
 *            CanIfRxPduCanIdMask 参数，但它们的用途是"生成 Can 驱动的 HOH
 *            过滤器配置"——那是代码生成器干的事。我们手写工程里，
 *            这个信息已经存在于 MCAL 的配置表，无需在 CanIf 再抄一份。）
 */
typedef struct
{
    PduIdType        RxPduId;    /**< 逻辑接收 PDU 号 */
    Can_HwHandleType Hrh;        /**< 对应的 MCAL 接收句柄（< CAN_HTH_FIRST_INDEX） */
} CanIf_RxPduConfigType;

/**
 * @brief CanIf 顶层配置
 */
struct CanIf_ConfigType
{
    const CanIf_TxPduConfigType* TxPdus;       /**< 发送 PDU 表 */
    uint16                       TxPduCount;   /**< 表项数 */

    const CanIf_RxPduConfigType* RxPdus;       /**< 接收 PDU 表 */
    uint16                       RxPduCount;   /**< 表项数 */
};

/*==================================================================================================
 *                                 GLOBAL VARIABLE DECLARATIONS
 *================================================================================================*/

/** @brief 由 canif_cfg.c 提供的配置实例，传给 CanIf_Init() */
extern const CanIf_ConfigType CanIf_Config;

#endif /* CANIF_CFG_H */
