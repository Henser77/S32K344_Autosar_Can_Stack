/*==================================================================================================
 *  Services/pdur_cfg.c —— PduR 路由表
 *
 *  【改路由只改这里，pdur.c 不动】
 *
 *  【本文件里出现的两套编号 —— 这是最容易看错的地方】
 *
 *      TX 路由：SrcPduId 是【COM 的编号】，DestPduId 是【CanIf 的编号】
 *      RX 路由：SrcPduId 是【CanIf 的编号】，DestPduId 是【上层模块的编号】
 *
 *  也就是说 **SrcPduId 的含义随方向变化** —— 因为它永远指"这条路由的源头"，
 *  而源在哪一侧，取决于方向。
 *
 *  【与 CanIf 配置的对应关系】
 *
 *      CanIf 侧（EcuAbstraction/canif_cfg.c）：
 *          TxPduId 0 → Hth 4 → CAN0 邮箱1 → ID 0x100
 *          TxPduId 1 → Hth 5 → CAN0 邮箱2 → ID 0x200
 *          TxPduId 2 → Hth 6 → CAN1 邮箱1 → ID 0x300
 *          TxPduId 3 → Hth 7 → CAN1 邮箱2 → ID 0x400
 *
 *          RxPduId 0 ← Hrh 0 ← 0x100
 *          RxPduId 1 ← Hrh 1 ← 0x200
 *          RxPduId 2 ← Hrh 2 ← 0x300
 *          RxPduId 3 ← Hrh 3 ← 0x400
 *================================================================================================*/

#include "pdur_cfg.h"

/*==================================================================================================
 *                        上层模块 —— COM 已就位，Dcm 还是替身
 *
 * 【这一段的演变，正好体现了分层的价值】
 *
 *  上一版（COM 还没写）：由 main.c 提供两个临时替身
 *      App_ComRxIndication / App_ComTxConfirmation
 *
 *  本版（COM 已就位）：指向真正的 COM 模块
 *      Com_RxIndication / Com_TxConfirmation
 *
 *  **改动量：只改了函数名，pdur.c 一行没动。**
 *
 * 【为什么用 extern 而不 include com.h —— 但这不等于不 include 配置头】
 *  AUTOSAR 的生成器在 PduR_Cfg.c 里就是用 extern 声明引用上层模块**函数**的，
 *  这样 PduR 的配置文件不必依赖 COM 的接口头文件。
 *  类型一致性由两边的头文件各自保证 —— 签名必须等于：
 *      void Com_RxIndication(PduIdType, const PduInfoType*);
 *      void Com_TxConfirmation(PduIdType, Std_ReturnType);
 *  正好对应 PduR 的 PduR_RxIndicationFctType / PduR_TxConfirmationFctType。
 *
 * 【但符号名必须 include】
 *  路由表里要引用 CanIfConf_TxPdu_N 和 ComConf_ComTxPdu_N，
 *  这些宏定义在 canif_cfg.h / com_cfg.h 里，所以必须 include。
 *
 *  这恰恰是**真实 AUTOSAR 的做法**：生成的 PduR_Cfg.c 会 include
 *  CanIf_Cfg.h 和 Com_Cfg.h，因为它必须用这些符号名。
 *  "函数用 extern、符号用 include" 是这个文件的两个不同层次：
 *      函数  → extern（接口，签名靠约定）
 *      符号  → include（配置，靠编译器检查）
 *================================================================================================*/
extern void Com_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr);
extern void Com_TxConfirmation(PduIdType TxPduId, Std_ReturnType result);
extern void Dcm_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr);

/* 为了拿到 CanIfConf_TxPdu_N / CanIfConf_RxPdu_N / ComConf_ComTxPdu_N / ComConf_ComRxPdu_N */
#include "canif_cfg.h"
#include "com_cfg.h"

/*==================================================================================================
 *                                  下行（TX）路由表：COM → CanIf
 *
 *    COM 的 TxPduId 0  →  CanIf 的 TxPduId 0            （1 对 1）
 *    COM 的 TxPduId 1  →  CanIf 的 TxPduId 1, 2         （1 对 2 —— 扇出演示）
 *    COM 的 TxPduId 2  →  CanIf 的 TxPduId 3            （1 对 1）
 *
 * 【扇出的实际效果】
 *  COM 调一次 PduR_ComTransmit(1, ...)，会同时：
 *      CanIf_Transmit(1, ...)  → CAN0 邮箱2，ID 0x200
 *      CanIf_Transmit(2, ...)  → CAN1 邮箱1，ID 0x300
 *  也就是一份数据同时出现在两条 CAN 总线上（网关行为）。
 *
 * 【配置约束】
 *  同一个 CanIf TxPduId **不要**出现在两条不同的路径里。
 *  否则 CanIf 报一次发送确认，PduR 会反查出两条路径，给 COM 报两次确认，
 *  而 COM 只发了一次 —— 语义就错了。
 *================================================================================================*/
/*==================================================================================================
 *  ▼▼▼ 示例区开始：TX 路由表 + RX 路由表
 *      路由关系来自"车窗 / 电池 / 大端测试"演示工程，**新项目必须整体替换**。
 *
 *      这是全工程**唯一同时引用上下两层符号**的文件：
 *          SrcPduId   ← 发起方的 PDU 编号（TX 方向是 COM，RX 方向是 CanIf）
 *          DestPduId  ← 接收方的 PDU 编号（TX 方向是 CanIf，RX 方向是 COM）
 *      符号名写错 → 编译报 undeclared；数值写错 → 运行期静默丢弃。
 *
 *      替换时同步：
 *        □ Services/Communication/pdur_cfg.h  PDUR_TX_PATH_COUNT / PDUR_RX_PATH_COUNT
 *        □ Services/Communication/com_cfg.h   ComConf_ComTxPdu_N / ComConf_ComRxPdu_N
 *        □ EcuAbstraction/canif_cfg.h         CanIfConf_TxPdu_N / CanIfConf_RxPdu_N
 *        □ 扇出时 DestCount 必须与 Dests 数组项数一致（末尾断言会检查）
 *      配套说明见 TEMPLATE.md §三。
 *================================================================================================*/
static const PduR_TxDestConfigType PduR_TxDests_Com0[] = { { CanIfConf_TxPdu_0 } };
static const PduR_TxDestConfigType PduR_TxDests_Com1[] = { { CanIfConf_TxPdu_1 },
                                                           { CanIfConf_TxPdu_2 } };   /* ← 1:N 扇出 */
static const PduR_TxDestConfigType PduR_TxDests_Com2[] = { { CanIfConf_TxPdu_3 } };

static const PduR_TxPathConfigType PduR_TxPaths[] =
{
    /* SrcPduId（引用 COM 的符号名）,  SrcTxConfirmation,      DestCount,  Dests                    */
    {  ComConf_ComTxPdu_0,             Com_TxConfirmation,     1U,         PduR_TxDests_Com0 },
    {  ComConf_ComTxPdu_1,             Com_TxConfirmation,     2U,         PduR_TxDests_Com1 },     /* 扇出 */
    {  ComConf_ComTxPdu_2,             Com_TxConfirmation,     1U,         PduR_TxDests_Com2 },
};

/*==================================================================================================
 *                                  上行（RX）路由表：CanIf → 上层
 *
 *    CanIf 的 RxPduId 0  →  COM 的 RxPduId 0            （1 对 1）
 *    CanIf 的 RxPduId 1  →  COM 的 RxPduId 1            （1 对 1）
 *    CanIf 的 RxPduId 2  →  COM 的 RxPduId 2            （1 对 1）
 *    CanIf 的 RxPduId 3  →  COM 的 RxPduId 3             （1 对 2）
 *                        →  DCM 的 RxPduId 0             （扇出演示）
 *
 * 【扇出的实际效果】
 *  CAN1 收到 0x400 → CanIf 报 RxPduId 3 → PduR 同时调：
 *      Com_RxIndication(3, ...)        COM 解包成信号
 *      Dcm_RxIndication(0, ...)        诊断模块也看这条
 *
 *  这正是"同一条报文既给应用又给诊断"的实现方式。
 *  真实项目里刷写/读 DID 就依赖这个机制。
 *
 * 【零拷贝提醒】
 *  两个目标拿到的是**同一个 PduInfoPtr**，指向 MCAL 的接收缓冲。
 *  COM 在 Com_RxIndication 里会立刻拷进自己的 RX 影子缓冲，
 *  所以 COM 是安全的；Dcm 若想延后处理，必须自己 memcpy。
 *================================================================================================*/
static const PduR_RxDestConfigType PduR_RxDests_CanIf0[] = { { ComConf_ComRxPdu_0, Com_RxIndication } };
static const PduR_RxDestConfigType PduR_RxDests_CanIf1[] = { { ComConf_ComRxPdu_1, Com_RxIndication } };
static const PduR_RxDestConfigType PduR_RxDests_CanIf2[] = { { ComConf_ComRxPdu_2, Com_RxIndication } };
static const PduR_RxDestConfigType PduR_RxDests_CanIf3[] =
{
    { ComConf_ComRxPdu_3, Com_RxIndication },   /* COM 的 3 号 */
    { 0U,                 Dcm_RxIndication },   /* DCM 的 0 号 —— 1:N 扇出 */
};

static const PduR_RxPathConfigType PduR_RxPaths[] =
{
    /* SrcPduId（引用 CanIf 的符号名）,  DestCount,  Dests                     */
    {  CanIfConf_RxPdu_0,               1U,         PduR_RxDests_CanIf0 },
    {  CanIfConf_RxPdu_1,               1U,         PduR_RxDests_CanIf1 },
    {  CanIfConf_RxPdu_2,               1U,         PduR_RxDests_CanIf2 },
    {  CanIfConf_RxPdu_3,               2U,         PduR_RxDests_CanIf3 },     /* 扇出：COM + Dcm */
};

/*==================================================================================================
 *  ▲▲▲ 示例区结束
 *      以下为顶层配置对象与编译期断言，**不需要改**（它们是机械汇总）
 *================================================================================================*/

/*==================================================================================================
 *                                  顶层配置对象
 *================================================================================================*/
const PduR_ConfigType PduR_Config =
{
    .TxPaths     = PduR_TxPaths,
    .TxPathCount = (uint16)(sizeof(PduR_TxPaths) / sizeof(PduR_TxPaths[0])),

    .RxPaths     = PduR_RxPaths,
    .RxPathCount = (uint16)(sizeof(PduR_RxPaths) / sizeof(PduR_RxPaths[0])),
};

/*==================================================================================================
 *                     编译期一致性断言（Compile-Time Assertion）
 *
 *  ① DestCount 与 Dests 数组的实际项数一致
 *     —— 这是最容易漏的一处：在 Dests 数组里加了一路扇出，
 *        但忘了把 DestCount 从 1 改成 2，结果第二路永远不会被调用。
 *
 *  ② 路径数量与 pdur_cfg.h 的 PDUR_TX_PATH_COUNT / PDUR_RX_PATH_COUNT 一致
 *
 *  原理同其余模块：typedef char X[条件 ? 1 : -1]
 *================================================================================================*/
typedef char PduR_TxDestsCom0CountAssert[
    ((sizeof(PduR_TxDests_Com0) / sizeof(PduR_TxDests_Com0[0])) == 1U) ? 1 : -1];
typedef char PduR_TxDestsCom1CountAssert[
    ((sizeof(PduR_TxDests_Com1) / sizeof(PduR_TxDests_Com1[0])) == 2U) ? 1 : -1];
typedef char PduR_TxDestsCom2CountAssert[
    ((sizeof(PduR_TxDests_Com2) / sizeof(PduR_TxDests_Com2[0])) == 1U) ? 1 : -1];
typedef char PduR_RxDestsCanIf3CountAssert[
    ((sizeof(PduR_RxDests_CanIf3) / sizeof(PduR_RxDests_CanIf3[0])) == 2U) ? 1 : -1];

typedef char PduR_TxPathCountAssert[
    ((sizeof(PduR_TxPaths) / sizeof(PduR_TxPaths[0])) == (uint32)PDUR_TX_PATH_COUNT) ? 1 : -1];
typedef char PduR_RxPathCountAssert[
    ((sizeof(PduR_RxPaths) / sizeof(PduR_RxPaths[0])) == (uint32)PDUR_RX_PATH_COUNT) ? 1 : -1];
