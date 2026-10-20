/*==================================================================================================
 *  Services/com_cfg.c —— COM 配置数据
 *
 *  【改信号布局 / 周期 / 缩放只改这里，com.c 一行不动】
 *
 *  【本文件的信号表必须与下面两张 PDU 表的 FirstSignalIdx 严格对应】
 *
 *      信号下标   0..3    → TX PDU 0（DoorStatus,   8 字节, 0x100）
 *               4..5    → TX PDU 1（BatteryInfo,  4 字节, 0x200）
 *               6..7    → TX PDU 2（MotorolaTest, 4 字节, 0x300）
 *               8..11   → RX PDU 0（DoorStatusRx, 8 字节, 0x100）
 *               12..13  → RX PDU 1（BatteryInfoRx,4 字节, 0x200）
 *               14..15  → RX PDU 2（MotorolaTestRx,4 字节,0x300）
 *               16      → RX PDU 3（DcmTestRx,    2 字节, 0x400）
 *
 *  com.c 的 Com_Init() 会校验这个对应关系（见那里的注释）。
 *================================================================================================*/

#include "com_cfg.h"

/*==================================================================================================
 *                                      信号表
 *
 * 字段顺序：
 *    Type, BitPosition, BitSize, Endian, ScalingFactor, ScalingOffset,
 *    InitValue, TransferProperty, TimeoutTicks
 *
 * 【位布局设计（TX PDU 0 / RX PDU 0 相同）—— Intel 小端】
 *
 *   字节 0:  bit0      = DoorOpen      (1 bit)
 *           bit1..7   = WindowPos     (7 bit)
 *   字节 1:  bit8..9   = LockState     (2 bit)
 *           bit10..15 = VehicleSpeed 低 6 位
 *   字节 2:  bit16..23 = VehicleSpeed 中 8 位
 *   字节 3:  bit24..25 = VehicleSpeed 高 2 位
 *   字节 4-7: 保留
 *
 *   举例（DoorOpen=1, WindowPos=50, LockState=1, VehicleSpeed=6000）：
 *       byte0 = (50 << 1) | 1        = 0x65
 *       byte1 = (48 << 2) | 1        = 0xC1   （48 = 6000 & 0x3F）
 *       byte2 = (6000 >> 6) & 0xFF   = 0x5D
 *       byte3 = (6000 >> 14) & 0x03  = 0x00
 *   → 65 C1 5D 00 00 00 00 00
 *================================================================================================*/
/*==================================================================================================
 *  ▼▼▼ 示例区开始：信号表 + 发送 PDU 表 + 接收 PDU 表
 *      三张表全部来自"车窗 / 电池 / 大端测试"演示工程，**新项目必须整体替换**。
 *
 *      替换时严格按依赖顺序，且以下 4 处必须同步（漏一处就静默错位）：
 *        □ Services/Communication/com_cfg.h  信号枚举 + ComConf 宏 + PDU 符号
 *        □ Services/Communication/com_cfg.c  本文件的 3 张表（尤其 COM_SIG_COUNT）
 *        □ Services/Communication/pdur_cfg.c 路由表引用的 PDU 符号
 *        □ Rte/rte_*.c/.h                    端口函数（如果用到了被改的信号）
 *
 *      改完编译：编译期断言会检查"表项数 = 数量宏"，不一致直接报错。
 *      配套说明见 TEMPLATE.md §三。
 *================================================================================================*/
static const Com_SignalConfigType Com_Signals[] =
{
    /* ───────────── TX PDU 0：DoorStatus（bit 0 起，Intel）───────────── */
    /* 0  DoorOpen：BOOLEAN，bit0，1 位。PENDING —— 门状态变化不单独触发发送，
     *   跟周期一起发（真实项目里门状态变化通常也走事件，这里演示 PENDING 用法）。 */
    { COM_SIGNAL_TYPE_BOOLEAN,  0U,   1U,  COM_ENDIAN_LITTLE,   1,    0,      0,  COM_TRANSFER_PENDING,            0U },

    /* 1  WindowPos：UINT8，7 位，0..100 %。TRIGGERED_ON_CHANGE —— 窗动了就发。 */
    { COM_SIGNAL_TYPE_UINT8,    1U,   7U,  COM_ENDIAN_LITTLE,   1,    0,      0,  COM_TRANSFER_TRIGGERED_ON_CHANGE, 0U },

    /* 2  LockState：UINT8，2 位，0..3。PENDING。 */
    { COM_SIGNAL_TYPE_UINT8,    8U,   2U,  COM_ENDIAN_LITTLE,   1,    0,      0,  COM_TRANSFER_PENDING,            0U },

    /* 3  VehicleSpeed：UINT16，16 位，物理单位 = 0.01 km/h（6000 = 60.00 km/h）。
     *   factor=1 表示"物理值直接就是 raw"。PENDING —— 跟周期发。 */
    { COM_SIGNAL_TYPE_UINT16,  10U,  16U,  COM_ENDIAN_LITTLE,   1,    0,      0,  COM_TRANSFER_PENDING,            0U },

    /* ───────────── TX PDU 1：BatteryInfo（bit 0 起，Intel）───────────── */
    /* 4  BatteryVoltage：UINT16，16 位。物理值单位 mV，raw 单位 10mV
     *   → factor = 10。物理 12000 mV → raw = 12000/10 = 1200。
     *   TRIGGERED —— 电压一更新就触发一次发送（DIRECT 模式的典型用法）。 */
    { COM_SIGNAL_TYPE_UINT16,   0U,  16U,  COM_ENDIAN_LITTLE,  10,    0,  12000,  COM_TRANSFER_TRIGGERED,           0U },

    /* 5  EngineTemp：UINT8，8 位。物理值 °C，raw 0..255 对应 -40..215 °C
     *   → factor = 1, offset = -40。物理 90 °C → raw = (90+40)/1 = 130。
     *   验证"负偏移"的换算。 */
    { COM_SIGNAL_TYPE_UINT8,   16U,   8U,  COM_ENDIAN_LITTLE,   1,  -40,     90,  COM_TRANSFER_PENDING,            0U },

    /* ───────────── TX PDU 2：MotorolaTest（bit 0 起，Motorola 大端）─────────────
     *
     * 【大端布局的关键】按 AUTOSAR 锯齿编号，bit0 = 字节0 的 MSB。
     *
     *   BeUint16 在 bit 0、16 位：
     *       MSB（bit15）→ 位号 0  → 字节0 bit7
     *       LSB（bit0） → 位号 15 → 字节1 bit0
     *       → 值 0x1234 在总线上是：12 34（高字节在前）
     *
     *   BeUint8 在 bit 16、8 位：
     *       MSB → 位号 16 → 字节2 bit7
     *       LSB → 位号 23 → 字节2 bit0
     *       → 值 0xAB 在字节2
     *
     *   所以初值 0x1234 / 0xAB 应该产生：12 34 AB 00
     *   —— 这一条最容易在分析仪上直接看出对错。 */
    { COM_SIGNAL_TYPE_UINT16,   0U,  16U,  COM_ENDIAN_BIG,      1,    0, 0x1234,  COM_TRANSFER_PENDING,            0U },
    { COM_SIGNAL_TYPE_UINT8,   16U,   8U,  COM_ENDIAN_BIG,      1,    0,   0xAB,  COM_TRANSFER_PENDING,            0U },

    /* ───────────── RX PDU 0：DoorStatusRx（与 TX PDU 0 布局相同）───────────── */
    /*  8 */ { COM_SIGNAL_TYPE_BOOLEAN,  0U,   1U, COM_ENDIAN_LITTLE, 1,  0, 0, COM_TRANSFER_PENDING, 0U },
    /*  9 */ { COM_SIGNAL_TYPE_UINT8,    1U,   7U, COM_ENDIAN_LITTLE, 1,  0, 0, COM_TRANSFER_PENDING, 0U },
    /* 10 */ { COM_SIGNAL_TYPE_UINT8,    8U,   2U, COM_ENDIAN_LITTLE, 1,  0, 0, COM_TRANSFER_PENDING, 0U },
    /* 11 */ { COM_SIGNAL_TYPE_UINT16,  10U,  16U, COM_ENDIAN_LITTLE, 1,  0, 0, COM_TRANSFER_PENDING, 0U },

    /* ───────────── RX PDU 1：BatteryInfoRx ───────────── */
    /* 12 */ { COM_SIGNAL_TYPE_UINT16,   0U,  16U, COM_ENDIAN_LITTLE, 10, 0, 12000, COM_TRANSFER_PENDING, 0U },
    /* 13 */ { COM_SIGNAL_TYPE_UINT8,   16U,   8U, COM_ENDIAN_LITTLE, 1, -40,   90,  COM_TRANSFER_PENDING, 0U },

    /* ───────────── RX PDU 2：MotorolaTestRx ───────────── */
    /* 14 */ { COM_SIGNAL_TYPE_UINT16,   0U,  16U, COM_ENDIAN_BIG,    1,  0, 0x1234, COM_TRANSFER_PENDING, 0U },
    /* 15 */ { COM_SIGNAL_TYPE_UINT8,   16U,   8U, COM_ENDIAN_BIG,    1,  0, 0xAB,   COM_TRANSFER_PENDING, 0U },

    /* ───────────── RX PDU 3：DcmTestRx（PduR 1:N 扇出到 COM + Dcm 的那条）───────────── */
    /* 16 */ { COM_SIGNAL_TYPE_UINT16,   0U,  16U, COM_ENDIAN_LITTLE, 1,  0, 0, COM_TRANSFER_PENDING, 0U },
};

/*==================================================================================================
 *                                  发送 PDU 表
 *
 * 【TxPduId 必须与 pdur_cfg.c 的 TX 路由表 SrcPduId 一致】
 *
 *      ComTxPduId 0  →  PduR TX 路径 0  →  CanIf 0    →  CAN0 0x100
 *      ComTxPduId 1  →  PduR TX 路径 1  →  CanIf 1, 2 →  CAN0 0x200 + CAN1 0x300（扇出）
 *      ComTxPduId 2  →  PduR TX 路径 2  →  CanIf 3    →  CAN1 0x400
 *
 * 【周期单位是 tick，不是毫秒】
 *      1 tick = 1 次 Com_MainFunctionTx() 调用 = COM_MAIN_FUNCTION_PERIOD_MS 毫秒
 *      本工程 1 tick = 10ms，所以 10 tick = 100ms。
 *================================================================================================*/
static const Com_IpduConfigType Com_TxPdus[] =
{
    /* TxPduId（符号名）,        RxPduId, Length, FirstSignalIdx, SignalCount, TxMode,             TxPeriodTicks */
    {  ComConf_ComTxPdu_0,     0xFFFFU,  8U,     0U,             4U,          COM_TX_MODE_PERIODIC, 10U },  /* 100ms */
    {  ComConf_ComTxPdu_1,     0xFFFFU,  4U,     4U,             2U,          COM_TX_MODE_DIRECT,    0U },  /* 事件驱动 */
    {  ComConf_ComTxPdu_2,     0xFFFFU,  4U,     6U,             2U,          COM_TX_MODE_PERIODIC, 50U },  /* 500ms */
};

/*==================================================================================================
 *                                  接收 PDU 表
 *
 * 【RxPduId 必须与 pdur_cfg.c 的 RX 路由表 DestPduId 一致】
 *
 *      PduR RX 路径 0 → COM RxPduId 0
 *      PduR RX 路径 1 → COM RxPduId 1
 *      PduR RX 路径 2 → COM RxPduId 2
 *      PduR RX 路径 3 → COM RxPduId 3 **+ Dcm**（1:N 扇出）
 *================================================================================================*/
static const Com_IpduConfigType Com_RxPdus[] =
{
    /* TxPduId, RxPduId（符号名）, Length, FirstSignalIdx, SignalCount, TxMode,          TxPeriodTicks */
    {  0xFFFFU, ComConf_ComRxPdu_0, 8U,     8U,             4U,          COM_TX_MODE_NONE, 0U },
    {  0xFFFFU, ComConf_ComRxPdu_1, 4U,     12U,            2U,          COM_TX_MODE_NONE, 0U },
    {  0xFFFFU, ComConf_ComRxPdu_2, 4U,     14U,            2U,          COM_TX_MODE_NONE, 0U },
    {  0xFFFFU, ComConf_ComRxPdu_3, 2U,     16U,            1U,          COM_TX_MODE_NONE, 0U },
};

/*==================================================================================================
 *  ▲▲▲ 示例区结束
 *      以下为顶层配置对象与编译期断言，**不需要改**（它们是机械汇总）
 *================================================================================================*/

/*==================================================================================================
 *                                  顶层配置对象
 *================================================================================================*/
const Com_ConfigType Com_Config =
{
    .Signals     = Com_Signals,
    .SignalCount = (uint16)(sizeof(Com_Signals) / sizeof(Com_Signals[0])),

    .TxPdus      = Com_TxPdus,
    .TxPduCount  = (uint16)(sizeof(Com_TxPdus) / sizeof(Com_TxPdus[0])),

    .RxPdus      = Com_RxPdus,
    .RxPduCount  = (uint16)(sizeof(Com_RxPdus) / sizeof(Com_RxPdus[0])),
};

/*==================================================================================================
 *                     编译期一致性断言（Compile-Time Assertion）
 *
 *  【这四个断言直接消灭了我们讨论过的四类"静默失步"】
 *
 *  ① Com_SignalCountAssert
 *     信号表项数 == COM_SIG_COUNT（枚举里的计数值）
 *     → 捕捉"表里加了信号但忘了在枚举里加"，或反之。
 *
 *  ② Com_TxPduCountAssert / ③ Com_RxPduCountAssert
 *     PDU 表项数 == COM_TX_PDU_COUNT / COM_RX_PDU_COUNT
 *
 *  ④ Com_SignalIdMaxAssert
 *     枚举里最后一个真实信号（COM_SIG_RX_DCM_TEST）的值 + 1 == COM_SIG_COUNT
 *     → 这一条抓的是"枚举被改了但 COUNT 没跟着改"。
 *
 *  原理：typedef char X[条件 ? 1 : -1]，条件不成立时数组长度为负 → 编译报错。
 *================================================================================================*/
typedef char Com_SignalCountAssert[
    ((sizeof(Com_Signals) / sizeof(Com_Signals[0])) == (uint32)COM_SIG_COUNT) ? 1 : -1];

typedef char Com_TxPduCountAssert[
    ((sizeof(Com_TxPdus) / sizeof(Com_TxPdus[0])) == (uint32)COM_TX_PDU_COUNT) ? 1 : -1];

typedef char Com_RxPduCountAssert[
    ((sizeof(Com_RxPdus) / sizeof(Com_RxPdus[0])) == (uint32)COM_RX_PDU_COUNT) ? 1 : -1];

typedef char Com_SignalIdMaxAssert[
    (((uint32)COM_SIG_RX_DCM_TEST + 1U) == (uint32)COM_SIG_COUNT) ? 1 : -1];
