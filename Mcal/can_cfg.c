/*==================================================================================================
 *  Mcal/can_cfg.c —— CAN MCAL 配置数据
 *
 *  【本文件是"配置"，can.c 是"逻辑"】
 *  改行为只改这里。can.c 一行都不用动。
 *
 *  【本文件是唯一知道 RTD 配置对象名的地方之一】
 *  FlexCAN_Config0 / FlexCAN_State0 这些符号由 CT 生成的
 *  generate/src/FlexCAN_Ip_Sa_PBcfg.c 提供。
 *  它们是 RTD 层的配置实例，我们在 MCAL 层把它们绑定到逻辑控制器编号上。
 *================================================================================================*/

#include "can_cfg.h"

/*==================================================================================================
 *                              CT 生成的 RTD 配置对象（外部声明）
 *
 * 来源：generate/src/FlexCAN_Ip_Sa_PBcfg.c
 * 注意这里声明的类型是 FlexCAN_Ip_ConfigType，但 can_cfg.h 里存的是 void*。
 * 为了让本文件编译，需要 RTD 的类型定义 ——
 * 这是**配置层唯一允许接触 RTD 类型的地方**（因为它的职责就是描述用哪个 RTD 实例）。
 *================================================================================================*/
#include "FlexCAN_Ip.h"

extern const Flexcan_Ip_ConfigType FlexCAN_Config0;   /**< CAN0 的 RTD 配置 */
extern const Flexcan_Ip_ConfigType FlexCAN_Config1;   /**< CAN1 的 RTD 配置 */
extern Flexcan_Ip_StateType        FlexCAN_State0;    /**< CAN0 的 RTD 运行状态 */
extern Flexcan_Ip_StateType        FlexCAN_State1;    /**< CAN1 的 RTD 运行状态 */

/*==================================================================================================
 *                                     控制器表
 *
 *   逻辑编号 0  →  RTD 的 FlexCAN_Config0 / State0  →  物理 CAN0
 *   逻辑编号 1  →  RTD 的 FlexCAN_Config1 / State1  →  物理 CAN1
 *
 * 上层只认逻辑编号。将来若把 CAN1 换成别的控制器，只改这张表。
 *================================================================================================*/
/*==================================================================================================
 *  ▼▼▼ 示例区开始：控制器表 + 发送对象表 + 接收对象表
 *      三张表来自"车窗 / 电池 / 大端测试"演示工程（2 路 CAN、4 发 4 收、ID 0x100~0x400），
 *      **新项目必须按自己的硬件与通信矩阵整体替换**。
 *
 *      替换时严格按依赖顺序，且以下 4 处必须同步：
 *        □ Mcal/can_cfg.h              CAN_*_COUNT 数量宏 + CanConf_* 句柄符号名
 *        □ Mcal/can_cfg.c              本文件三张表
 *        □ EcuAbstraction/canif_cfg.c  引用本文件的 Hth/Hrh 符号名
 *        □ Mcal/can.c                  CAN_MB_BUF_COUNT 必须 ≥ 任何 MbIdx + 1
 *
 *      ⚠️ 接收方向的 CanIdMask 不是可选项：S32K344 的掩码寄存器复位值为 0
 *         （= 接收所有 ID）。不填这一列，配置表里的 CanId 形同虚设。
 *      配套说明见 TEMPLATE.md §三。
 *================================================================================================*/
static const Can_ControllerConfigType Can_Controllers[] =
{
    /* ControllerId, RtdConfig,        RtdState        */
    {  0U,           &FlexCAN_Config0, &FlexCAN_State0  },
    {  1U,           &FlexCAN_Config1, &FlexCAN_State1  },
};

/*==================================================================================================
 *                                  发送硬件对象表（HTH）
 *
 * 句柄取值必须 >= CAN_HTH_FIRST_INDEX（= 4），见 can_cfg.h 的编号约定。
 *
 *   Hth 4  →  CAN0 邮箱 1   标准帧  最多 8 字节
 *   Hth 5  →  CAN0 邮箱 2   标准帧  最多 8 字节
 *   Hth 6  →  CAN1 邮箱 1   标准帧  最多 8 字节
 *   Hth 7  →  CAN1 邮箱 2   标准帧  最多 8 字节
 *
 * 【为什么用 CAN_HTH_FIRST_INDEX + n 而不是直接写 4、5、6、7】
 * 把"分段起点"这个约定集中在一个宏上。将来若增加接收对象、起点右移，
 * 只需改 can_cfg.h 的一个数字，这张表自动跟着变。
 * 直接写数字会在改约定时静默错位 —— 这类缺陷编译不报错、只在运行时表现为
 * "句柄莫名其妙"。
 *================================================================================================*/
static const Can_TxHwObjectConfigType Can_TxObjects[] =
{
    /* Hth（用符号名，见 can_cfg.h）,  ControllerId, MbIdx, IsExtended, PayloadSize */
    {  CanConf_TxObject_Hth4,         0U,            1U,    FALSE,      8U },
    {  CanConf_TxObject_Hth5,         0U,            2U,    FALSE,      8U },
    {  CanConf_TxObject_Hth6,         1U,            1U,    FALSE,      8U },
    {  CanConf_TxObject_Hth7,         1U,            2U,    FALSE,      8U },
};

/*==================================================================================================
 *                                  接收硬件对象表（HRH）
 *
 * 句柄取值必须 < CAN_HTH_FIRST_INDEX（= 4），见 can_cfg.h 的编号约定。
 *
 *   Hrh 0  ←  CAN0 邮箱 5   标准帧  ID 0x100  精确匹配
 *   Hrh 1  ←  CAN0 邮箱 6   标准帧  ID 0x200  精确匹配
 *   Hrh 2  ←  CAN1 邮箱 5   标准帧  ID 0x300  精确匹配
 *   Hrh 3  ←  CAN1 邮箱 6   标准帧  ID 0x400  精确匹配
 *
 * 【CanIdMask 怎么填】
 *   CAN_RX_MASK_EXACT_STD  标准帧精确匹配（11 位全比对）
 *   CAN_RX_MASK_EXACT_EXT  扩展帧精确匹配（29 位全比对）
 *   CAN_RX_MASK_ACCEPT_ALL 收所有 ID（调试时很方便：分析仪发什么都进这个邮箱）
 *   自定义范围掩码         想收 0x100~0x10F，就写 0x7F0（低 4 位不比对）
 *
 *   ⚠️ 掩码不是可选项。S32K344 的掩码寄存器复位值 = 0 = "接受所有 ID"，
 *   不填这个字段就等于"这个邮箱收总线上的一切"，配置里的 CanId 形同虚设。
 *
 * 【MbIdx 的取值范围约束】
 *   CAN0 的 RTD 配置 max_num_mb = 96  → MbIdx 必须 < 96
 *   CAN1 的 RTD 配置 max_num_mb = 64  → MbIdx 必须 < 64
 *   同时 MbIdx 必须 < can.c 里 CAN_MB_BUF_COUNT（静态接收缓冲数量），
 *   否则 can.c 的边界检查会拒收并丢弃该帧。
 *   这两个约束是**硬约束**，改这里时必须一起检查。
 *================================================================================================*/
static const Can_RxHwObjectConfigType Can_RxObjects[] =
{
    /* Hrh（用符号名）,  ControllerId, MbIdx, CanId,  CanIdMask,              IsExtended */
    {  CanConf_RxObject_Hrh0,  0U,         5U,    0x100U, CAN_RX_MASK_EXACT_STD,    FALSE },
    {  CanConf_RxObject_Hrh1,  0U,         6U,    0x200U, CAN_RX_MASK_EXACT_STD,    FALSE },
    {  CanConf_RxObject_Hrh2,  1U,         5U,    0x300U, CAN_RX_MASK_EXACT_STD,    FALSE },
    {  CanConf_RxObject_Hrh3,  1U,         6U,    0x400U, CAN_RX_MASK_EXACT_STD,    FALSE },
};

/*==================================================================================================
 *  ▲▲▲ 示例区结束
 *      以下为顶层配置对象与编译期断言，**不需要改**（它们是机械汇总）
 *================================================================================================*/

/*==================================================================================================
 *                                  顶层配置对象
 *
 * Can_Init(&Can_Config) 的唯一入参。
 *
 * 【表项数为什么不手写数字】
 * 用 sizeof 计算，避免"表里加了项但数量忘了改"这类静默不一致 ——
 * 那是"两套并行事实"问题的典型形态：表是事实，数字是另一份事实，
 * 两者脱节时不报错。
 *================================================================================================*/
const Can_ConfigType Can_Config =
{
    .Controllers     = Can_Controllers,
    .ControllerCount = (uint8)(sizeof(Can_Controllers) / sizeof(Can_Controllers[0])),
    .TxObjects       = Can_TxObjects,
    .TxObjectCount   = (uint8)(sizeof(Can_TxObjects) / sizeof(Can_TxObjects[0])),
    .RxObjects       = Can_RxObjects,
    .RxObjectCount   = (uint8)(sizeof(Can_RxObjects) / sizeof(Can_RxObjects[0])),
};

/*==================================================================================================
 *                     编译期一致性断言（Compile-Time Assertion）
 *
 * 【为什么需要】
 *  can_cfg.h 里的 CAN_TX_OBJECT_COUNT / CAN_RX_OBJECT_COUNT 是**手写的数字**，
 *  can_cfg.c 里的表是**另一份事实**。两者脱节时：
 *      - can.c 的边界检查会按错误的数量截断 → 静默丢对象
 *      - 上层的句柄引用可能指向不存在的行
 *
 * 【怎么实现】
 *  C99 没有 static_assert。惯用技巧是"用条件表达式当数组长度"：
 *      条件成立 → 长度 1 → 合法
 *      条件不成立 → 长度 -1 → 编译器报 "size of array is negative"
 *
 *  这个断言在**编译期**生效，不占任何运行时空间和指令。
 *
 * 【什么时候会撑破】
 *  在 Can_TxObjects[] 里加/删一行，但忘了改 CAN_TX_OBJECT_COUNT。
 *================================================================================================*/
typedef char Can_TxObjectCountAssert[
    ((sizeof(Can_TxObjects) / sizeof(Can_TxObjects[0])) == (uint32)CAN_TX_OBJECT_COUNT) ? 1 : -1];

typedef char Can_RxObjectCountAssert[
    ((sizeof(Can_RxObjects) / sizeof(Can_RxObjects[0])) == (uint32)CAN_RX_OBJECT_COUNT) ? 1 : -1];

typedef char Can_ControllerCountAssert[
    ((sizeof(Can_Controllers) / sizeof(Can_Controllers[0])) == (uint32)CAN_CONTROLLER_COUNT) ? 1 : -1];
