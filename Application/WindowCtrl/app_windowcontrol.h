/*==================================================================================================
 *  Application/app_windowcontrol.h —— 车窗控制器 SWC
 *
 *  【这是一个真实的汽车功能：电动车窗控制】
 *
 *  输入（经 RTE 从总线读）：
 *      - 车窗目标位置（0..100 %）—— 驾驶员通过开关设定的
 *      - 车门开关状态
 *      - 中控锁状态
 *      - 车速
 *
 *  输出（经 RTE 写回总线）：
 *      - 车窗实际位置（电机按速度曲线积分出来的）
 *      - 其余输入信号回显（便于在分析仪上观察 ECU 收到了什么）
 *
 *  【应用层看到的世界】
 *  这个文件里**不会出现** COM、PduId、CAN ID、字节、位偏移。
 *  只有 Rte_Read/Rte_Write 和业务概念。
 *
 *  【安全规则（真实车窗 ECU 都有）】
 *      R1  车门打开     → 车窗强制全开（逃生通路）
 *      R2  中控锁锁止   → 禁止一切动作
 *      R3  车速 > 20km/h → 禁止上升（防夹安全），允许下降
 *      R4  梯形速度曲线 → 距离远快、近了慢（机械与噪声考虑）
 *      R5  运动超时     → 连续运动 4 秒未到位，判定堵转/故障
 *      R6  故障保持     → 故障保持 2 秒后自动复位
 *================================================================================================*/
#ifndef APP_WINDOWCONTROL_H
#define APP_WINDOWCONTROL_H

#include "Std_Types.h"

/*==================================================================================================
 *                                   状态机状态
 *================================================================================================*/
typedef enum
{
    APP_WIN_IDLE = 0,           /**< 已在目标位置，电机停 */
    APP_WIN_MOVING_UP,          /**< 上升中 */
    APP_WIN_MOVING_DOWN,        /**< 下降中 */
    APP_WIN_BLOCKED_LOCKED,     /**< R2：中控锁锁止，禁止动作 */
    APP_WIN_BLOCKED_SPEED,      /**< R3：车速过高，禁止上升 */
    APP_WIN_FAULT_OVERTIME      /**< R5：运动超时（堵转/机械卡死） */
} App_WinStateType;

/*==================================================================================================
 *                              对外可见的运行状态（调试用）
 *================================================================================================*/
typedef struct
{
    App_WinStateType State;             /**< 当前状态 */
    uint8  TargetPos;                   /**< 总线给的目标位置 */
    uint8  EffectiveTarget;             /**< 经安全规则 R1 修正后的有效目标 */
    uint8  ActualPos;                   /**< 电机积分出的实际位置 */
    uint8  CurrentStep;                 /**< 本周期走的步长（0..6） */
    uint8  DoorOpen;                    /**< 回显：门状态 */
    uint8  LockState;                   /**< 回显：锁状态 */
    uint16 VehicleSpeed;                /**< 回显：车速（0.01 km/h） */
    uint16 MoveTicks;                   /**< 连续运动周期数（R5 用） */
    uint32 TotalTicks;                  /**< 任务总周期数 */
    uint32 UpTicks;                     /**< 上升周期数 */
    uint32 DownTicks;                   /**< 下降周期数 */
    uint32 BlockedCount;                /**< 被安全规则拦截的次数 */
    uint32 FaultCount;                  /**< 进入故障的次数 */
} App_WindowStatusType;

/** @brief 运行状态（调试器 Watch 窗口直接看这个） */
extern volatile App_WindowStatusType g_AppWindowStatus;

/**
 * @brief 调试注入：模拟电机堵转
 *
 * @details 在调试器里把它置 TRUE，电机就不动了。
 *          4 秒后 R5 超时保护会触发，状态进入 APP_WIN_FAULT_OVERTIME。
 *          这是验证安全逻辑最简单的方法。
 */
extern volatile boolean g_AppSimMotorStuck;

/*==================================================================================================
 *                                    任务入口
 *================================================================================================*/
/**
 * @brief 车窗控制任务（由 EcuM 按周期调度，100ms）
 * @details 签名符合 FreeRTOS 的 TaskFunction_t，便于直接登记到任务表。
 */
void App_WindowControl_Task(void* pvArg);

#endif /* APP_WINDOWCONTROL_H */
