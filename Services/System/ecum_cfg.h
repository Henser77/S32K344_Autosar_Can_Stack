/*==================================================================================================
 *  Services/ecum_cfg.h —— EcuM 配置类型（OS 任务表）
 *
 *  【真实 AUTOSAR 里这张表在哪】
 *  任务表其实是 **OS 配置**（OsTask / OsTaskAutostart）的一部分，由生成器产出。
 *  EcuM 只负责调 StartOS()，不自己定义任务。
 *
 *  我们没有 AUTOSAR OS，用 FreeRTOS 代替。所以把"任务表"放在 EcuM 配置里，
 *  它扮演的是"OS 配置"的角色 —— 这是诚实的简化，不是标准做法。
 *================================================================================================*/
#ifndef ECUM_CFG_H
#define ECUM_CFG_H

#include "Std_Types.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief 一个 OS 任务的描述
 */
typedef struct
{
    const char*    TaskName;      /**< 任务名（调试器里显示） */
    TaskFunction_t TaskEntry;     /**< 入口函数 */
    void*          TaskArg;       /**< 入口参数（不用，填 NULL） */
    uint16         StackWords;    /**< 栈深度，单位是**字**（word = 4 字节） */
    uint8          Priority;      /**< 优先级（0..configMAX_PRIORITIES-1） */
} EcuM_TaskConfigType;

/**
 * @brief EcuM 顶层配置
 */
typedef struct
{
    const EcuM_TaskConfigType* Tasks;
    uint8                      TaskCount;
} EcuM_ConfigType;

extern const EcuM_ConfigType EcuM_Config;

#endif /* ECUM_CFG_H */
