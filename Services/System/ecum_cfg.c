/*==================================================================================================
 *  Services/ecum_cfg.c —— OS 任务表
 *
 *  【改任务划分只改这里】
 *
 *  【优先级的设计考虑】
 *      Task_Com     优先级 3  —— COM 主函数对时间敏感（发送周期、接收超时），
 *                                放最高，保证不被应用任务拖慢
 *      Task_WinCtrl 优先级 2  —— 应用业务逻辑，100ms 周期，要求不高
 *      定时器服务任务 优先级 2  —— FreeRTOS 内部创建（configTIMER_TASK_PRIORITY）
 *      空闲任务      优先级 0  —— FreeRTOS 内部创建
 *
 *  【栈深度的单位是"字"不是"字节"】
 *      256 words = 1024 bytes
 *      栈要放局部变量 + 函数调用深度。应用层有状态机 + 结构体，给 256 字较宽松。
 *================================================================================================*/

#include "ecum_cfg.h"
#include "ecum.h"                   /* EcuM_TaskCom */
#include "app_windowcontrol.h"      /* App_WindowControl_Task */

/*==================================================================================================
 *                                     任务表
 *================================================================================================*/
static const EcuM_TaskConfigType EcuM_Tasks[] =
{
    /* TaskName,        TaskEntry,                  TaskArg,  StackWords, Priority */
    {  "Task_Com",       EcuM_TaskCom,              NULL,     256U,       3U },
    {  "Task_WinCtrl",   App_WindowControl_Task,    NULL,     256U,       2U },
};

/*==================================================================================================
 *                                  顶层配置对象
 *================================================================================================*/
const EcuM_ConfigType EcuM_Config =
{
    .Tasks     = EcuM_Tasks,
    .TaskCount = (uint8)(sizeof(EcuM_Tasks) / sizeof(EcuM_Tasks[0])),
};
