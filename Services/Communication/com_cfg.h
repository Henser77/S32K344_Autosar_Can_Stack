/*==================================================================================================
 *  Services/com_cfg.h —— COM 配置类型
 *
 *  【AUTOSAR COM 的配置模型】
 *
 *      ComIPdu（一条 PDU）
 *        ├── ComIPduLength                     长度
 *        ├── ComIPduDirection                  SEND / RECEIVE
 *        ├── ComIPduSignalRef  ──┐             引用信号
 *        └── ComTxModeTrue/False ┘            发送模式
 *              └── ComTxModeMode               PERIODIC / DIRECT / MIXED / NONE
 *              └── ComTxModeTimePeriod         周期（以主函数 tick 计）
 *                  ↓
 *      ComSignal（一个信号）
 *        ├── ComBitPosition         起始位（AUTOSAR 位编号规则）
 *        ├── ComBitSize             位宽
 *        ├── ComSignalEndianess     LITTLE / BIG / OPAQUE
 *        ├── ComSignalType          BOOLEAN / UINT8.. / SINT8.. / UINT8_N
 *        ├── ComScalingFactor       缩放系数
 *        ├── ComScalingOffset       偏移
 *        ├── ComSignalInitValue     初值（物理值）
 *        ├── ComTransferProperty    PENDING / TRIGGERED / TRIGGERED_ON_CHANGE
 *        └── ComTimeout             超时 tick 数（0 = 不监控）
 *
 *  【本工程的简化：信号表按 PDU 分组连续存放】
 *
 *  AUTOSAR 里 Pdu 通过引用列表指向信号，信号可以任意排列。
 *  我们要求**同一个 PDU 的信号在信号表里连续**，PDU 只记录
 *  `FirstSignalIdx` + `SignalCount`。好处：
 *      - 配置表更短（不用为每个 PDU 生成一个引用数组）
 *      - 遍历某个 PDU 的信号是连续内存访问
 *  代价：
 *      - 信号表必须按 PDU 分块排列（配置时要注意顺序）
 *      - com.c 的 Com_Init 会做一致性校验并派生反向索引
 *================================================================================================*/
#ifndef COM_CFG_H
#define COM_CFG_H

/*==================================================================================================
 *                                        INCLUDES
 *================================================================================================*/
#include "com.h"

/*==================================================================================================
 *                                    DEFINES AND MACROS
 *================================================================================================*/

/**
 * @brief 本层支持的单条 I-PDU 最大字节数
 * @details 现在 8（经典 CAN）。将来开 CAN FD 时改成 64 —— 只改这一个数字。
 *          所有影子缓冲都按这个宽度分配（RAM 代价 = PDU 数量 × 此值 × 2）。
 */
#ifndef COM_MAX_PDU_LEN
#define COM_MAX_PDU_LEN             (8U)
#endif

/**
 * @brief 单条 PDU 最多包含的信号个数
 * @details 只用于初始化时的一致性检查，不影响运行。
 */
#define COM_MAX_SIGNALS_PER_PDU     (8U)

/*--------------------------------------------------------------------------------------------------
 * 运行时数组的容量上限
 *
 * com.c 里的影子值、超时计数、反向索引都是静态数组，必须有编译期长度。
 * Com_Init() 会检查配置表项数不超过这些上限，超了就直接拒绝初始化
 * （宁可起不来，也不要静默越界）。
 *
 * 【RAM 代价】
 *      信号影子值 : COM_MAX_SIGNALS    × 4 字节
 *      信号上次 raw: COM_MAX_SIGNALS    × 4 字节
 *      超时计数   : COM_MAX_SIGNALS    × 2 字节
 *      反向索引   : COM_MAX_SIGNALS    × 2 字节
 *      TX 影子缓冲 : COM_MAX_TX_PDUS   × COM_MAX_PDU_LEN
 *      RX 影子缓冲 : COM_MAX_RX_PDUS   × COM_MAX_PDU_LEN
 *  当前配置（17 信号 / 3 TX / 4 RX / 8 字节）实际占用约 250 字节。
 *------------------------------------------------------------------------------------------------*/
#ifndef COM_MAX_SIGNALS
#define COM_MAX_SIGNALS             (32U)
#endif

#ifndef COM_MAX_TX_PDUS
#define COM_MAX_TX_PDUS             (8U)
#endif

#ifndef COM_MAX_RX_PDUS
#define COM_MAX_RX_PDUS             (8U)
#endif

/*==================================================================================================
 *                                 STRUCTURES AND OTHER TYPEDEFS
 *================================================================================================*/

/**
 * @brief 信号 ID 枚举 —— 对应 AUTOSAR 生成器产出的 `ComConf_ComSignal_<名字>` 宏
 *
 * @details 应用层用这些名字写信号，永远不该出现裸数字：
 *              Com_SendSignal(COM_SIG_VEHICLE_SPEED, &speed);
 *
 * 【必须与下面 com_cfg.c 里的信号表顺序严格一致】
 *  因为 ID 就是表下标。表里插一条信号，这个枚举必须同步插入，
 *  否则后面所有信号全部错位 —— 而且不会报错。
 *
 * 【编号按 PDU 分块连续】
 *  这是 com_cfg.h 顶部说明的约束：同一 PDU 的信号在信号表里必须连续。
 */
/*==================================================================================================
 *  ▼▼▼ 示例区开始：信号枚举 + 信号 ID 宏 + PDU 符号
 *      以下全部来自"车窗 / 电池 / 大端测试"演示工程，**新项目必须整体替换**。
 *      配套改动清单见 TEMPLATE.md §三。
 *================================================================================================*/
typedef enum
{
    /* ── TX PDU 0：DoorStatus（8 字节，CAN0 / ID 0x100）── */
    COM_SIG_DOOR_OPEN           = 0,    /**< 1 bit  */
    COM_SIG_WINDOW_POS          = 1,    /**< 7 bit  */
    COM_SIG_LOCK_STATE          = 2,    /**< 2 bit  */
    COM_SIG_VEHICLE_SPEED       = 3,    /**< 16 bit */

    /* ── TX PDU 1：BatteryInfo（4 字节，CAN0 / ID 0x200）── */
    COM_SIG_BATTERY_VOLTAGE     = 4,    /**< 16 bit，缩放 ×10  */
    COM_SIG_ENGINE_TEMP         = 5,    /**< 8 bit，偏移 -40   */

    /* ── TX PDU 2：MotorolaTest（4 字节，CAN1 / ID 0x300）── */
    COM_SIG_BE_UINT16           = 6,    /**< 16 bit，大端 */
    COM_SIG_BE_UINT8            = 7,    /**< 8 bit，大端  */

    /* ── RX PDU 0：DoorStatusRx（8 字节，CAN0 / ID 0x100）── */
    COM_SIG_RX_DOOR_OPEN        = 8,
    COM_SIG_RX_WINDOW_POS       = 9,
    COM_SIG_RX_LOCK_STATE       = 10,
    COM_SIG_RX_VEHICLE_SPEED    = 11,

    /* ── RX PDU 1：BatteryInfoRx（4 字节，CAN0 / ID 0x200）── */
    COM_SIG_RX_BATTERY_VOLTAGE  = 12,
    COM_SIG_RX_ENGINE_TEMP      = 13,

    /* ── RX PDU 2：MotorolaTestRx（4 字节，CAN1 / ID 0x300）── */
    COM_SIG_RX_BE_UINT16        = 14,
    COM_SIG_RX_BE_UINT8         = 15,

    /* ── RX PDU 3：DcmTestRx（2 字节，CAN1 / ID 0x400）── */
    COM_SIG_RX_DCM_TEST         = 16,

    COM_SIG_COUNT               = 17    /**< 信号总数（= 表项数） */
} Com_SignalIdEnum;

/*--------------------------------------------------------------------------------------------------
 *                    AUTOSAR 风格的信号 ID 宏（由 RTE / 应用层使用）
 *
 * 真实 AUTOSAR 的 Com_Cfg.h 里只有这批宏，不开枚举：
 *     #define ComConf_ComSignal_WindowPos   ((Com_SignalIdType)1)
 *
 * 我们保留枚举作为"唯一定义处"，再用宏提供 API 侧的名字。
 * 这样 RTE 里写 ComConf_ComSignal_WindowPos，和真实项目一致；
 * 而改配置只需要改枚举一处。
 *
 * ⚠️ RTE / 应用层**只应使用 ComConf_* 宏**，不要直接写 COM_SIG_*。
 *    前者是"对外契约"，后者是"内部编号"——真实项目里应用看不到后者。
 *------------------------------------------------------------------------------------------------*/
#define ComConf_ComSignal_DoorOpen          ((Com_SignalIdType)COM_SIG_DOOR_OPEN)
#define ComConf_ComSignal_WindowPos         ((Com_SignalIdType)COM_SIG_WINDOW_POS)
#define ComConf_ComSignal_LockState         ((Com_SignalIdType)COM_SIG_LOCK_STATE)
#define ComConf_ComSignal_VehicleSpeed      ((Com_SignalIdType)COM_SIG_VEHICLE_SPEED)
#define ComConf_ComSignal_BatteryVoltage    ((Com_SignalIdType)COM_SIG_BATTERY_VOLTAGE)
#define ComConf_ComSignal_EngineTemp        ((Com_SignalIdType)COM_SIG_ENGINE_TEMP)
#define ComConf_ComSignal_BeUint16          ((Com_SignalIdType)COM_SIG_BE_UINT16)
#define ComConf_ComSignal_BeUint8           ((Com_SignalIdType)COM_SIG_BE_UINT8)

#define ComConf_ComSignal_RxDoorOpen        ((Com_SignalIdType)COM_SIG_RX_DOOR_OPEN)
#define ComConf_ComSignal_RxWindowPos       ((Com_SignalIdType)COM_SIG_RX_WINDOW_POS)
#define ComConf_ComSignal_RxLockState       ((Com_SignalIdType)COM_SIG_RX_LOCK_STATE)
#define ComConf_ComSignal_RxVehicleSpeed    ((Com_SignalIdType)COM_SIG_RX_VEHICLE_SPEED)
#define ComConf_ComSignal_RxBatteryVoltage  ((Com_SignalIdType)COM_SIG_RX_BATTERY_VOLTAGE)
#define ComConf_ComSignal_RxEngineTemp      ((Com_SignalIdType)COM_SIG_RX_ENGINE_TEMP)
#define ComConf_ComSignal_RxBeUint16        ((Com_SignalIdType)COM_SIG_RX_BE_UINT16)
#define ComConf_ComSignal_RxBeUint8         ((Com_SignalIdType)COM_SIG_RX_BE_UINT8)
#define ComConf_ComSignal_RxDcmTest         ((Com_SignalIdType)COM_SIG_RX_DCM_TEST)

/*==================================================================================================
 *      ★ PDU 的符号名与数量 —— 供 PduR 的配置引用，并做编译期一致性检查
 *
 *  【为什么要这样】
 *  PduR 的路由表里要写"COM 的几号 PDU"。改造前写裸数字：
 *        { 0U, Com_TxConfirmation, 1U, PduR_TxDests_Com0 },
 *  如果 COM 端删了这个 PDU，PduR 照样编译通过，运行时静默不发。
 *
 *  改造后 PduR 写 ComConf_ComTxPdu_0，COM 端一删就**编译报错**。
 *  这与 MCAL 的 CanConf_TxObject_Hth4、CanIf 的 CanIfConf_TxPdu_0 是同一套做法。
 *================================================================================================*/

/* 发送 PDU 数量（com_cfg.c 末尾有编译期断言保证与表项数一致） */
#define COM_TX_PDU_COUNT            (3U)
/* 接收 PDU 数量 */
#define COM_RX_PDU_COUNT            (4U)

/* 发送 PDU 符号名（PduR 的 TX 路由表 SrcPduId 引用） */
#define ComConf_ComTxPdu_0          (0U)
#define ComConf_ComTxPdu_1          (1U)
#define ComConf_ComTxPdu_2          (2U)

/* 接收 PDU 符号名（PduR 的 RX 路由表 DestPduId 引用） */
#define ComConf_ComRxPdu_0          (0U)
#define ComConf_ComRxPdu_1          (1U)
#define ComConf_ComRxPdu_2          (2U)
#define ComConf_ComRxPdu_3          (3U)

/*==================================================================================================
 *  ▲▲▲ 示例区结束 —— 以下为通用的配置结构体定义，**不需要改**
 *================================================================================================*/


/**
 * @brief 信号配置
 */
typedef struct
{
    Com_SignalTypeType       Type;             /**< 数据类型 */
    uint16                   BitPosition;      /**< 起始位（AUTOSAR 位编号，见 com.h 的说明） */
    uint8                    BitSize;          /**< 位宽（UINT8_N 时是字节数） */
    Com_EndianType           Endian;           /**< 字节序 */
    sint32                   ScalingFactor;    /**< 缩放：physical = raw × factor + offset */
    sint32                   ScalingOffset;    /**< 偏移（常用于温度：offset = -40） */
    sint32                   InitValue;        /**< 初始物理值（Com_Init 时换算成 raw 打包） */
    Com_TransferPropertyType TransferProperty; /**< 写这个信号会不会触发发送 */
    uint16                   TimeoutTicks;     /**< 超时 tick 数（0 = 不监控） */
} Com_SignalConfigType;

/**
 * @brief I-PDU 配置
 *
 * 【TxPduId 和 RxPduId 的区别 —— 注意方向】
 *      TX PDU：用 `TxPduId`，它必须与 **PduR 的 TX 路由表 SrcPduId** 一致
 *      RX PDU：用 `RxPduId`，它必须与 **PduR 的 RX 路由表目标 DestPduId** 一致
 *
 *  没用到的那个字段填 0xFFFF（不用）即可。
 */
typedef struct
{
    PduIdType        TxPduId;       /**< 发送时用：PduR_ComTransmit 的入参。RX PDU 填 0xFFFF */
    PduIdType        RxPduId;       /**< 接收时用：PduR 上报的 PduId。TX PDU 填 0xFFFF */
    uint8            Length;        /**< PDU 字节数 */
    uint8            FirstSignalIdx;/**< 该 PDU 的第一个信号在信号表里的下标 */
    uint8            SignalCount;   /**< 该 PDU 的信号个数 */
    Com_TxModeType   TxMode;        /**< 发送模式（RX PDU 填 COM_TX_MODE_NONE） */
    uint16           TxPeriodTicks; /**< 周期（以主函数 tick 计）。DIRECT 模式忽略 */
} Com_IpduConfigType;

/**
 * @brief COM 顶层配置
 *
 * 【为什么 TX 和 RX 分成两张表】
 *  一条 PDU 要么发要么收，方向是固定的。分开之后：
 *      - 各自的 PduId 字段含义清晰（TX 只用 TxPduId，RX 只用 RxPduId）
 *      - 代码里查表的目的明确（发送查 Tx 表，接收查 Rx 表）
 *  AUTOSAR 用一张表 + ComIPduDirection 字段区分，语义等价。
 */
struct Com_ConfigType
{
    const Com_SignalConfigType* Signals;
    uint16                      SignalCount;

    const Com_IpduConfigType*   TxPdus;
    uint16                      TxPduCount;

    const Com_IpduConfigType*   RxPdus;
    uint16                      RxPduCount;
};

/*==================================================================================================
 *                                 GLOBAL VARIABLE DECLARATIONS
 *================================================================================================*/
extern const Com_ConfigType Com_Config;

#endif /* COM_CFG_H */
