/*==================================================================================================
 *  Application/app_windowcontrol.c —— 车窗控制器实现
 *
 *  【这一层只做业务逻辑】
 *  不碰字节、不碰位、不知道 CAN、不知道 PduId。
 *  它只通过 RTE 读"物理量"、写"物理量"。
 *================================================================================================*/

#include "app_windowcontrol.h"
#include "rte.h"
#include "rte_doorstatus.h"

#include "FreeRTOS.h"
#include "task.h"

/*==================================================================================================
 *                                   业务常量
 *================================================================================================*/
#define APP_WIN_TASK_PERIOD_MS      (100U)      /**< 任务周期 */

#define APP_WIN_SPEED_LOCK_RAWH     (2000U)     /**< R3：车速阈值 20.00 km/h */

#define APP_WIN_DIST_FAST           (30U)       /**< R4：距离 > 30% 全速 */
#define APP_WIN_DIST_MID            (10U)       /**< R4：距离 > 10% 中速 */
#define APP_WIN_STEP_FAST           (6U)        /**< 全速步长（% / 周期） */
#define APP_WIN_STEP_MID            (3U)        /**< 中速步长 */
#define APP_WIN_STEP_SLOW           (1U)        /**< 慢速步长 */

#define APP_WIN_OVERTIME_TICKS      (40U)       /**< R5：连续运动 40 周期（4 秒）未到位 */
#define APP_WIN_FAULT_HOLD_TICKS    (20U)       /**< R6：故障保持 20 周期（2 秒） */

#define APP_WIN_POS_MAX             (100U)      /**< 位置上限 */
#define APP_WIN_POS_MIN             (0U)        /**< 位置下限 */

/*==================================================================================================
 *                                   调试可见变量
 *================================================================================================*/
volatile App_WindowStatusType g_AppWindowStatus;
volatile boolean              g_AppSimMotorStuck = FALSE;

/*==================================================================================================
 *                              本地工具：梯形速度曲线（R4）
 *================================================================================================*/
/**
 * @brief 按剩余距离决定本周期的步长
 * @details 真实车窗电机用 PWM 调速；这里用"每周期移动的百分比"简化模拟。
 *          距离远走快（省时间），距离近走慢（减小冲击和噪声）。
 */
static uint8 App_Win_CalcStep(uint8 distance)
{
    if (distance > APP_WIN_DIST_FAST) { return APP_WIN_STEP_FAST; }
    if (distance > APP_WIN_DIST_MID)  { return APP_WIN_STEP_MID;  }
    return APP_WIN_STEP_SLOW;
}

/*==================================================================================================
 *                                    任务入口
 *================================================================================================*/
void App_WindowControl_Task(void* pvArg)
{
    TickType_t xLastWake = xTaskGetTickCount();

    /* ── 任务内的持久状态（跨周期保留）──────────────────────────────── */
    App_WinStateType state        = APP_WIN_IDLE;
    uint8            actualPos    = 0U;      /**< 电机积分出的实际位置 */
    uint16           moveTicks    = 0U;      /**< 连续运动周期数（R5） */
    uint16           faultHold    = 0U;      /**< 故障保持周期数（R6） */

    (void)pvArg;

    for (;;)
    {
        /* ── 1. 经 RTE 读输入 ──────────────────────────────────────── */
        boolean doorOpen   = FALSE;
        uint8   targetPos  = 0U;
        uint8   lockState  = 0U;
        uint16  speed      = 0U;

        (void)Rte_Read_DoorStatus_DoorOpen(&doorOpen);
        (void)Rte_Read_DoorStatus_WindowPos(&targetPos);
        (void)Rte_Read_DoorStatus_LockState(&lockState);
        (void)Rte_Read_DoorStatus_VehicleSpeed(&speed);

        /* ── 2. R1：门开 → 强制全开 ───────────────────────────────── */
        uint8 effectiveTarget = targetPos;
        if (doorOpen != FALSE)
        {
            effectiveTarget = APP_WIN_POS_MAX;
        }

        /* ── 3. 状态机 ────────────────────────────────────────────── */
        uint8 step = 0U;

        if (state == APP_WIN_FAULT_OVERTIME)
        {
            /* R6：故障保持一段时间后自动复位 */
            faultHold++;
            if (faultHold >= APP_WIN_FAULT_HOLD_TICKS)
            {
                state     = APP_WIN_IDLE;
                faultHold = 0U;
                moveTicks = 0U;
            }
        }
        else
        {
            /* R2：中控锁锁止 → 禁止一切动作 */
            if (lockState != 0U)
            {
                if ((state != APP_WIN_BLOCKED_LOCKED) && (state != APP_WIN_BLOCKED_SPEED))
                {
                    g_AppWindowStatus.BlockedCount++;
                }
                state     = APP_WIN_BLOCKED_LOCKED;
                moveTicks = 0U;
            }
            /* R3：想上升但车速过高 → 禁止上升（允许下降） */
            else if ((actualPos < effectiveTarget) && (speed > APP_WIN_SPEED_LOCK_RAWH))
            {
                if ((state != APP_WIN_BLOCKED_LOCKED) && (state != APP_WIN_BLOCKED_SPEED))
                {
                    g_AppWindowStatus.BlockedCount++;
                }
                state     = APP_WIN_BLOCKED_SPEED;
                moveTicks = 0U;
            }
            else
            {
                /* 正常运动流程 */
                if (actualPos < effectiveTarget)
                {
                    state = APP_WIN_MOVING_UP;
                    step  = App_Win_CalcStep((uint8)(effectiveTarget - actualPos));

                    /* 调试注入：模拟电机堵转 → 位置不动 → R5 会触发 */
                    if (g_AppSimMotorStuck == FALSE)
                    {
                        uint16 next = (uint16)actualPos + (uint16)step;
                        actualPos = (next > (uint16)effectiveTarget)
                                  ? effectiveTarget : (uint8)next;
                    }
                    moveTicks++;
                    g_AppWindowStatus.UpTicks++;
                }
                else if (actualPos > effectiveTarget)
                {
                    state = APP_WIN_MOVING_DOWN;
                    step  = App_Win_CalcStep((uint8)(actualPos - effectiveTarget));

                    if (g_AppSimMotorStuck == FALSE)
                    {
                        uint8 next = (actualPos >= step) ? (uint8)(actualPos - step) : APP_WIN_POS_MIN;
                        actualPos = (next < effectiveTarget) ? effectiveTarget : next;
                    }
                    moveTicks++;
                    g_AppWindowStatus.DownTicks++;
                }
                else
                {
                    state     = APP_WIN_IDLE;
                    step      = 0U;
                    moveTicks = 0U;
                }

                /* R5：连续运动超时 → 判定堵转 */
                if ((moveTicks > APP_WIN_OVERTIME_TICKS) &&
                    ((state == APP_WIN_MOVING_UP) || (state == APP_WIN_MOVING_DOWN)))
                {
                    state     = APP_WIN_FAULT_OVERTIME;
                    faultHold = 0U;
                    moveTicks = 0U;
                    g_AppWindowStatus.FaultCount++;
                }
            }
        }

        /* ── 4. 经 RTE 写输出 ─────────────────────────────────────── */
        (void)Rte_Write_DoorStatus_DoorOpen(doorOpen);
        (void)Rte_Write_DoorStatus_WindowPos(actualPos);
        (void)Rte_Write_DoorStatus_LockState(lockState);
        (void)Rte_Write_DoorStatus_VehicleSpeed(speed);

        /* ── 5. 更新调试状态 ─────────────────────────────────────── */
        g_AppWindowStatus.State           = state;
        g_AppWindowStatus.TargetPos       = targetPos;
        g_AppWindowStatus.EffectiveTarget = effectiveTarget;
        g_AppWindowStatus.ActualPos       = actualPos;
        g_AppWindowStatus.CurrentStep     = step;
        g_AppWindowStatus.DoorOpen        = ((doorOpen != FALSE) ? 1U : 0U);
        g_AppWindowStatus.LockState       = lockState;
        g_AppWindowStatus.VehicleSpeed    = speed;
        g_AppWindowStatus.MoveTicks       = moveTicks;
        g_AppWindowStatus.TotalTicks++;

        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(APP_WIN_TASK_PERIOD_MS));
    }
}
