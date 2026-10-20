/*==================================================================================================
 *  Services/System/bswcheck.h —— BSW 配置一致性自检
 *
 *  【它解决什么问题】
 *  本模板没有 AUTOSAR 生成器，5 张配置表是手工维护的 —— 也就是**真的有五份可以写错的事实**。
 *
 *  其中大部分"引用对不上"已经用**符号化 + 编译期断言**解决了（见 can_cfg.h /
 *  canif_cfg.h / com_cfg.h 里的 <MOD>Conf_* 宏和 *CountAssert）。
 *
 *  但还有几类问题**编译期算不出来**，只能在启动时遍历检查：
 *
 *      ① CanIf 的 Length > MCAL 对应对象的 PayloadSize
 *         —— 两个数字分别在两个 .c 文件的表里，同一个编译单元看不到
 *      ② COM 的某个信号位区间超出 PDU 长度
 *         —— BitPosition + BitSize 是运行时算的
 *      ③ COM 的两个信号位区间重叠
 *         —— 需要两两比较
 *      ④ COM 的 FirstSignalIdx + SignalCount 越过信号表尾
 *
 *  【真实 AUTOSAR 为什么不需要这个】
 *  因为它用"单一数据源（ARXML）+ 生成器"保证这些关系，生成期就错了不了。
 *  我们没有生成器，所以用运行期检查兜底。这是**模板的补偿措施，不是标准特性**。
 *
 *  【报错方式】
 *  复用 EcuM 的模块号（EcuM 是集成者），ApiId = ECUM_API_BSW_CHECK，
 *  InstanceId = 出问题的表项下标，ErrorId 说明是哪一类问题。
 *  → 在调试器里看 Det_InstanceId 就知道是第几行配置错了。
 *================================================================================================*/
#ifndef BSWCHECK_H
#define BSWCHECK_H

#include "Std_Types.h"

/**
 * @brief 严格模式开关
 *
 * @details 0 = 只上报 Det，继续启动（**默认**，便于调试）
 *          1 = 上报 Det 后停在死循环，便于用调试器立刻定位
 *
 * 开发期建议改 1：配置一错就不往下跑，绝不会出现"看起来在跑但数据不对"。
 * 量产版本 Det 已经关了，自检也应保持 0（或整体裁掉）。
 */
#ifndef BSWCHECK_STRICT
#define BSWCHECK_STRICT             (0U)
#endif

/**
 * @brief 自检结果的快照（调试器 Watch 直接看这个）
 */
typedef struct
{
    uint16 ErrorCount;      /**< 发现的配置问题总数（0 = 全部通过） */
    uint16 LastErrorId;     /**< 最后一个错误编号 */
    uint16 LastInstance;    /**< 最后一处出问题的表项下标 */
    uint16 CheckedCanIf;    /**< 检查了多少条 CanIf PDU（发送 + 接收） */
    uint16 CheckedComSig;   /**< 检查了多少个 COM 信号（发送 + 接收） */
} BswCheck_StatusType;

/** @brief 自检状态（上电后立刻可读） */
extern volatile BswCheck_StatusType g_BswCheckStatus;

/**
 * @brief 执行全部配置自检
 *
 * @details 必须在所有 BSW 模块 Init 之后调用（那时各模块的 Config 指针都有效）。
 *          EcuM_Init 在 Det_Start() 之后调用它。
 *
 * @return E_OK   全部通过
 *         E_NOT_OK 发现问题（细节已经通过 Det 上报）
 */
Std_ReturnType BswCheck_Run(void);

#endif /* BSWCHECK_H */
