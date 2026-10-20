/*==================================================================================================
 *  EcuAbstraction/canif_cfg.c —— CanIf 配置数据
 *
 *  【改行为只改这里，canif.c 不动】
 *
 *  【和 MCAL 配置表的关系】
 *
 *      Mcal/can_cfg.c           EcuAbstraction/canif_cfg.c
 *      ─────────────────────    ────────────────────────────
 *      Can_TxObjects[]          CanIf_TxObjects[]（引用 Hth）
 *      Can_RxObjects[]          CanIf_RxObjects[]（引用 Hrh）
 *
 *  两张表通过 **Hth / Hrh 编号** 对应起来。
 *  改了一边的邮箱分配，另一边一般不用动（因为句柄号没变）；
 *  但如果增删了句柄，两边都要改，而且句柄空间分段约定（CAN_HTH_FIRST_INDEX）
 *  要同时成立。
 *================================================================================================*/

#include "canif_cfg.h"

/* 句柄空间常量（CAN_HTH_FIRST_INDEX）与接收掩码宏（CAN_RX_MASK_*）来自 MCAL 配置头。
 * 只在实现文件里引入 —— 理由见 canif_cfg.h 顶部注释。 */
#include "can_cfg.h"

/*==================================================================================================
 *                                  发送 PDU 表
 *
 *  PduId 0..3 分别对应 MCAL 的 Hth 4..7：
 *
 *      PduId 0  →  Hth 4  →  CAN0 邮箱 1  →  ID 0x100
 *      PduId 1  →  Hth 5  →  CAN0 邮箱 2  →  ID 0x200
 *      PduId 2  →  Hth 6  →  CAN1 邮箱 1  →  ID 0x300
 *      PduId 3  →  Hth 7  →  CAN1 邮箱 2  →  ID 0x400
 *
 *  【CanId 字段怎么填】
 *  这是个 Can_IdType，高两位带标志：
 *      bit31 = 1  扩展帧
 *      bit30 = 1  CAN FD 帧
 *      bit29..0   实际 ID
 *  本表现在全是标准帧（不加 bit31），最大 8 字节（经典 CAN）。
 *
 *  【将来开 CAN FD 的改法】
 *  把要发 FD 帧的那几行的 CanId 加上 bit30（0x40000000U），并把 Length 改成实际长度
 *  （最大 64）。**canif.c 一行都不用改** —— 因为 canif.c 只是把 CanId 原样
 *  传给 Can_Write()，而 MCAL 已经会读 bit30 决定是否用 FD 格式。
 *================================================================================================*/
static const CanIf_TxPduConfigType CanIf_TxObjects[] =
{
    /* TxPduId（符号名）,       Hth（引用 MCAL 的符号名）,  ControllerId,  CanId,    Length */
    {  CanIfConf_TxPdu_0,       CanConf_TxObject_Hth4,      0U,            0x100U,   8U },
    {  CanIfConf_TxPdu_1,       CanConf_TxObject_Hth5,      0U,            0x200U,   8U },
    {  CanIfConf_TxPdu_2,       CanConf_TxObject_Hth6,      1U,            0x300U,   8U },
    {  CanIfConf_TxPdu_3,       CanConf_TxObject_Hth7,      1U,            0x400U,   8U },
};

/*==================================================================================================
 *                                  接收 PDU 表
 *
 *  PduId 0..3 分别对应 MCAL 的 Hrh 0..3：
 *
 *      Hrh 0  ←  CAN0 邮箱 5  ←  ID 0x100   →  PduId 0
 *      Hrh 1  ←  CAN0 邮箱 6  ←  ID 0x200   →  PduId 1
 *      Hrh 2  ←  CAN1 邮箱 5  ←  ID 0x300   →  PduId 2
 *      Hrh 3  ←  CAN1 邮箱 6  ←  ID 0x400   →  PduId 3
 *
 *  【为什么表里看不到 CAN ID】
 *  因为接收过滤发生在**硬件**（MCAL 的 Can_RxObjects[] 把 ID 和掩码写进
 *  RXIMR 寄存器）。CanIf 拿到的已经是筛完的帧，只知道"是哪个 Hrh"。
 *
 *  想看 Hrh 与 ID 的对应关系，去 Mcal/can_cfg.c 的 Can_RxObjects[]：
 *      {  0U,   0U,   5U,    0x100U, CAN_RX_MASK_EXACT_STD,    FALSE },  ← Hrh 0 = 0x100
 *
 *  左列注释里的 ID 只是给人看的提示，**不是可配置项**，改它不生效。
 *  真要改接收 ID，去 Mcal/can_cfg.c。
 *================================================================================================*/
static const CanIf_RxPduConfigType CanIf_RxObjects[] =
{
    /* RxPduId（符号名）,      Hrh（引用 MCAL 的符号名） */
    {  CanIfConf_RxPdu_0,      CanConf_RxObject_Hrh0 },   /* → 0x100（ID 在 Mcal/can_cfg.c 里配） */
    {  CanIfConf_RxPdu_1,      CanConf_RxObject_Hrh1 },   /* → 0x200 */
    {  CanIfConf_RxPdu_2,      CanConf_RxObject_Hrh2 },   /* → 0x300 */
    {  CanIfConf_RxPdu_3,      CanConf_RxObject_Hrh3 },   /* → 0x400 */
};

/*==================================================================================================
 *                                  顶层配置对象
 *================================================================================================*/
const CanIf_ConfigType CanIf_Config =
{
    .TxPdus     = CanIf_TxObjects,
    .TxPduCount = (uint16)(sizeof(CanIf_TxObjects) / sizeof(CanIf_TxObjects[0])),

    .RxPdus     = CanIf_RxObjects,
    .RxPduCount = (uint16)(sizeof(CanIf_RxObjects) / sizeof(CanIf_RxObjects[0])),
};

/*==================================================================================================
 *                     编译期一致性断言（Compile-Time Assertion）
 *
 *  保证 canif_cfg.h 里的 CANIF_TX_PDU_COUNT / CANIF_RX_PDU_COUNT
 *  与实际表项数一致。加/删 PDU 而忘了改计数 → 编译失败。
 *
 *  （原理见 Mcal/can_cfg.c 里同名断言的说明）
 *================================================================================================*/
typedef char CanIf_TxPduCountAssert[
    ((sizeof(CanIf_TxObjects) / sizeof(CanIf_TxObjects[0])) == (uint32)CANIF_TX_PDU_COUNT) ? 1 : -1];

typedef char CanIf_RxPduCountAssert[
    ((sizeof(CanIf_RxObjects) / sizeof(CanIf_RxObjects[0])) == (uint32)CANIF_RX_PDU_COUNT) ? 1 : -1];
