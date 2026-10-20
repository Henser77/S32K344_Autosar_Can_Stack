/*==================================================================================================
 *  Mcal/can_cfg.h —— CAN MCAL 配置类型与句柄约定
 *
 *  【为什么配置要和实现分开】
 *  AUTOSAR 的核心设计思想之一是"**配置驱动**"：改变行为要靠改配置，不是改代码。
 *  对应到 C 语言，就是把"哪些邮箱、收哪个 ID、DLC 上限多少"这些**数据**从
 *  can.c 的**逻辑**里抽出来，放进独立的表。
 *
 *  好处很实在：换一个 ECU 变体（多一路 CAN、改一批报文 ID）时，
 *  can.c 一行都不用动，只改 can_cfg.c。
 *
 *  【本文件只放"类型和约定"，具体数值在 can_cfg.c】
 *================================================================================================*/
#ifndef CAN_CFG_H
#define CAN_CFG_H

/*==================================================================================================
 *                                        INCLUDES
 *================================================================================================*/
#include "can.h"

/*==================================================================================================
 *                                 HANDLE NUMBERING CONVENTION
 *
 *   ┌──────────────────────┬──────────────────────┐
 *   │   HRH   0 .. 3       │   HTH   4 .. 7       │
 *   │   (接收硬件对象)      	│   (发送硬件对象)        │
 *   └──────────────────────┴──────────────────────┘
 *                            ↑
 *                     CAN_HTH_FIRST_INDEX = 4
 *
 * 【为什么两类句柄要分段，而不是各自从 0 开始】
 *
 * 假设接收和发送都从 0 编号，那么"句柄 0"就同时表示"接收对象 0"和"发送对象 0"。
 * 现在不会出错，因为接收查接收表、发送查发送表，两条路径恰好不交叉。
 * 但只要有任何模块**把句柄存下来待用** —— 上层的硬件对象映射表、发送确认表、
 * 诊断会话缓冲索引 —— 语义立刻混淆，而且不会报任何错。
 *
 * 分段编号从结构上排除这个问题：拿到一个句柄值，先看它落在哪一段，
 * 就知道它是接收句柄还是发送句柄。
 *
 * 这个约定与 NXP 官方 Can_43_FLEXCAN 的 Can_uHthFirstIndex 字段含义一致
 * （官方注释："The first Hth after Hrh consecutive."）。
 *================================================================================================*/
#define CAN_HTH_FIRST_INDEX         (4U)

/*--------------------------------------------------------------------------------------------------
 * 控制器数量：必须与 can_cfg.c 的 Can_Controllers[] 表项数一致。
 * can.c 用它做数组边界检查，写小了会漏掉控制器，写大了会越界读表。
 *------------------------------------------------------------------------------------------------*/
#define CAN_CONTROLLER_COUNT        (2U)

/*--------------------------------------------------------------------------------------------------
 * 发送 / 接收硬件对象的数量
 * can_cfg.c 末尾有编译期断言，保证这里的数字与实际的表项数一致。
 *------------------------------------------------------------------------------------------------*/
#define CAN_TX_OBJECT_COUNT         (4U)
#define CAN_RX_OBJECT_COUNT         (4U)

/*==================================================================================================
 *      ★ 句柄的符号名 —— 上层引用"名字"，不写"裸数字"
 *
 *  【为什么要有这一组宏：这是"编译期一致性检查"的实现方式】
 *
 *  改造前 —— CanIf 的配置表里直接写：
 *        { 0U,  CAN_HTH_FIRST_INDEX + 0U,  0U,  0x100U,  8U },
 *    如果 MCAL 端把这个发送对象删了，CanIf **照样编译通过**，
 *    运行时才发现"发不出去"，而且不知道原因。
 *
 *  改造后 —— CanIf 的配置表里写：
 *        { 0U,  CanConf_TxObject_Hth4,     0U,  0x100U,  8U },
 *    如果 MCAL 端没有这个对象（宏不存在），**编译直接报 undeclared**。
 *
 *  这就是真实 AUTOSAR 生成器的做法：生成的各层配置之间用**符号名**互相引用，
 *  而不是裸数字。缺一个引用目标，链接/编译就过不去。
 *  我们手工模拟这一点。
 *
 *  【维护约定】
 *  宏的值必须与 can_cfg.c 里 Can_TxObjects[] / Can_RxObjects[] 的**行顺序**一致。
 *  表的行数由 can_cfg.c 末尾的编译期断言强制对齐 —— 删行/加行而没改
 *  CAN_TX_OBJECT_COUNT / CAN_RX_OBJECT_COUNT，编译立刻失败。
 *================================================================================================*/

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

/*==================================================================================================
 *                                 STRUCTURES AND OTHER TYPEDEFS
 *================================================================================================*/

/**
 * @brief 控制器配置
 * @details 把"逻辑控制器编号"绑定到"RTD 的配置对象与状态对象"。
 *
 *          这里用 void* 而不是 RTD 的 Flexcan_Ip_ConfigType*，
 *          是为了让本头文件**不依赖任何 RTD 头文件** ——
 *          配置层与实现层都保持对 RTD 类型的最小暴露面。
 *          can.c 内部会做一次显式强转（那是唯一知道真实类型的地方）。
 */
typedef struct
{
    uint8        ControllerId;  /**< 逻辑控制器编号：0 = CAN0，1 = CAN1 */
    const void*  RtdConfig;     /**< → FlexCAN_Config0 / FlexCAN_Config1（生成代码提供） */
    void*        RtdState;      /**< → FlexCAN_State0  / FlexCAN_State1 （生成代码提供） */
} Can_ControllerConfigType;

/**
 * @brief 发送硬件对象配置（HTH）
 * @details 每个"逻辑发送句柄"在这里描述它对应哪个控制器的哪个物理邮箱、
 *          能用什么帧类型、最大能发多少字节。
 *
 *          can.c 的 Can_Write() 收到 Hth 后**只查表，不做任何推导** ——
 *          这是"配置驱动"的落地方式。
 */
typedef struct
{
    Can_HwHandleType Hth;           /**< 逻辑发送句柄，取值必须 >= CAN_HTH_FIRST_INDEX */
    uint8            ControllerId;  /**< 用哪个控制器发 */
    uint8            MbIdx;         /**< 用哪个物理邮箱发（FlexCAN 邮箱号） */
    boolean          IsExtended;    /**< FALSE = 标准帧，TRUE = 扩展帧 */
    uint8            PayloadSize;   /**< 该句柄允许的最大数据长度（DLC 上限） */
} Can_TxHwObjectConfigType;

/*--------------------------------------------------------------------------------------------------
 * 接收过滤掩码的常用取值
 *
 * 【掩码语义】FlexCAN 的接收掩码是"**必须匹配**"掩码：
 *     掩码位 = 1  →  接收帧的对应 ID 位必须与该邮箱配置的 ID 位相同
 *     掩码位 = 0  →  该位"不关心"，任何值都接受
 *
 * 因此：
 *     掩码全 1  =  精确匹配（只收这一个 ID）
 *     掩码全 0  =  接受所有 ID
 *
 * 【必须显式配置的原因 —— 这是本层踩过的一个坑】
 * S32K344 的 RXMGMASK / RXIMR 寄存器**复位值都是 0**，也就是"接受所有 ID"。
 * 而 RTD 的 FlexCAN_Ip_Init() 与 FlexCAN_Ip_ConfigRxMb() 都**不设置掩码**
 * （已核对 FlexCAN_Ip.c：Init 里没有 SetRxMaskType 调用，
 *   ConfigRxMb 只写 CS/ID 字，见 FlexCAN_Ip.c:2262-2307）。
 *
 * 所以我们若不显式设掩码，配置表里写的"MB5 收 0x100"根本不会生效 ——
 * MB5 会把 CAN0 上收到的**任何帧**都接下来，并当成 Hrh 0 上报。
 * 上层以为 Hrh 0 就是 0x100，实际拿到的是别的 ID，且不会报任何错。
 *------------------------------------------------------------------------------------------------*/
#define CAN_RX_MASK_EXACT_STD       (0x7FFUL)       /**< 标准帧 11 位全部参与比较 */
#define CAN_RX_MASK_EXACT_EXT       (0x1FFFFFFFUL)  /**< 扩展帧 29 位全部参与比较 */
#define CAN_RX_MASK_ACCEPT_ALL      (0x0UL)         /**< 全部不比较 = 收所有 ID */

/**
 * @brief 接收硬件对象配置（HRH）
 * @details 描述"哪个控制器的哪个邮箱收哪个 ID、用什么掩码"，以及它对应的逻辑接收句柄。
 *
 *          【为什么没有 PayloadSize 字段】
 *          发送方向需要 DLC 上限来校验上层给的长度是否越界；
 *          接收方向的长度由对端决定，本地只能接受硬件报上来的值。
 *          所以收发两个结构体**有意不对称**，不是漏写。
 *
 *          【为什么必须有 CanIdMask 字段】
 *          没有它，"收哪个 ID"这件事只写了一半：CanId 说"期望的 ID 是什么"，
 *          CanIdMask 说"哪些位要比对"。两者配合才是一个完整的接收过滤器。
 *          对应 AUTOSAR 的 CanHwFilterCode / CanHwFilterMask 两个配置参数。
 */
typedef struct
{
    Can_HwHandleType Hrh;           /**< 逻辑接收句柄，取值必须 < CAN_HTH_FIRST_INDEX */
    uint8            ControllerId;  /**< 从哪个控制器收 */
    uint8            MbIdx;         /**< 从哪个物理邮箱收 */
    uint32           CanId;         /**< 该邮箱接收的 CAN ID（裸 ID，不含标志位） */
    uint32           CanIdMask;     /**< 过滤掩码：1=该位必须匹配，0=不关心。
                                         取值用上面的 CAN_RX_MASK_* 宏，或自定义范围掩码 */
    boolean          IsExtended;    /**< FALSE = 标准帧，TRUE = 扩展帧 */
} Can_RxHwObjectConfigType;

/**
 * @brief CAN 驱动顶层配置
 * @details 这就是 Can_Init() 的入参。它把三张表（控制器、发送对象、接收对象）
 *          聚合成一个整体，由 can_cfg.c 提供一个 const 实例 Can_Config。
 */
struct Can_ConfigType
{
    const Can_ControllerConfigType* Controllers;      /**< 控制器表 */
    uint8                           ControllerCount;  /**< 控制器表项数 */
    const Can_TxHwObjectConfigType* TxObjects;        /**< 发送硬件对象表 */
    uint8                           TxObjectCount;    /**< 发送表项数 */
    const Can_RxHwObjectConfigType* RxObjects;        /**< 接收硬件对象表 */
    uint8                           RxObjectCount;    /**< 接收表项数 */
};

/*==================================================================================================
 *                                 GLOBAL VARIABLE DECLARATIONS
 *================================================================================================*/

/** @brief 由 can_cfg.c 提供的配置实例，传给 Can_Init() 使用 */
extern const Can_ConfigType Can_Config;

#endif /* CAN_CFG_H */
