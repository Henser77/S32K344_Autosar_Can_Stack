/*==================================================================================================
 *  Services/ecum.h —— EcuM（ECU State Manager）接口
 *
 *  【本模块在 AUTOSAR 里的位置】
 *  A UTOSAR 的 Services Layer 里有两个"系统服务"模块：
 *      EcuM —— 负责 ECU 的启动/关机/唤醒流程
 *      BswM —— 负责运行期的模式仲裁
 *
 *  本工程只实现启动这一半：
 *      EcuM_Init()    板级初始化 + BSW 模块初始化 + 启动控制器
 *      EcuM_StartOS() 创建 OS 任务并启动调度器
 *
 *  【EcuM 存在的意义：初始化顺序有了一个唯一的家】
 *  各 BSW 模块之间有严格的依赖顺序：
 *      Clock → Port → IntCtrl → Can → CanIf → PduR → Com → RTE → App
 *  这个顺序以前埋在 main.c 里，现在集中到 EcuM 一处。
 *  main.c 只剩两行，应用/集成者不需要再记住顺序。
 *================================================================================================*/
#ifndef ECUM_H
#define ECUM_H

#include "Std_Types.h"

/*==================================================================================================
 *                    模块标识与开发错误检测（Det）配置
 *================================================================================================*/

/** @brief EcuM 的标准 AUTOSAR 模块号（0x0A = 10） */
#define ECUM_MODULE_ID                  (0x0AU)

/** @brief 单实例模块，实例号固定 0 */
#define ECUM_INSTANCE_ID                (0U)

/**
 * @brief 开发错误检测开关
 * @details 优先级：本模块单独定义 > 全局 BSW_DEV_ERROR_DETECT > 默认 STD_ON
 */
#ifndef ECUM_DEV_ERROR_DETECT
    #ifdef BSW_DEV_ERROR_DETECT
        #define ECUM_DEV_ERROR_DETECT   (BSW_DEV_ERROR_DETECT)
    #else
        #define ECUM_DEV_ERROR_DETECT   (STD_ON)
    #endif
#endif

/** @brief EcuM 的 API 编号 */
typedef enum
{
    ECUM_API_INIT                   = 0U,
    ECUM_API_START_OS               = 1U,
    ECUM_API_BSW_CHECK              = 2U    /**< 启动配置自检（见 bswcheck.c） */
} EcuM_ApiIdType;

/** @brief EcuM 的错误编号 */
typedef enum
{
    ECUM_E_PARAM_POINTER        = 0x01U,    /**< 传了 NULL 指针 */
    ECUM_E_INVALID_CONFIG       = 0x02U,    /**< 配置表非法（任务表为空等） */
    ECUM_E_TASK_CREATE_FAILED   = 0x03U,    /**< OS 任务创建失败（通常是堆不足） */

    /*----------------------------------------------------------------------
     * 以下是**启动配置自检**专用（ApiId = ECUM_API_BSW_CHECK）
     *
     * 上报时 InstanceId 填出问题的表项下标 —— 调试时据此直接定位到哪一行配置。
     *----------------------------------------------------------------------*/
    ECUM_E_CHECK_TX_LENGTH      = 0x20U,    /**< CanIf 的 Length > MCAL 的 PayloadSize */
    ECUM_E_CHECK_HTH_MISSING    = 0x21U,    /**< CanIf 的 Hth 在 MCAL 里不存在 */
    ECUM_E_CHECK_HRH_MISSING    = 0x22U,    /**< CanIf 的 Hrh 在 MCAL 里不存在 */
    ECUM_E_CHECK_SIGNAL_OVERLAP = 0x23U,    /**< COM 两个信号的位区间重叠 */
    ECUM_E_CHECK_SIGNAL_BOUNDS  = 0x24U,    /**< COM 信号超出 PDU 位宽 */
    ECUM_E_CHECK_SIGNAL_RANGE   = 0x25U     /**< COM 的 FirstSignalIdx+SignalCount 越过信号表尾 */
} EcuM_ErrorIdType;

/**
 * @brief 初始化 ECU：板级 + BSW + 启动控制器
 *
 * @details 内部顺序（不可调换）：
 *          1. 板级：Clock → Siul2_Port → IntCtrl
 *          2. MCAL：Can_Init
 *          3. ECU 抽象层：CanIf_Init（注册到 MCAL）
 *          4. 服务层：PduR_Init（注册到 CanIf）、Com_Init
 *          5. 状态类 hook 注册（BusOff / ErrorState）
 *          6. 启动控制器（SetControllerMode STARTED）
 *
 * 【为什么第 6 步必须在最后】
 *  控制器一启动就能收帧。如果上层回调没接全，那一帧就丢了。
 *  所以"启动总线"永远是初始化序列的最后一件事。
 */
void EcuM_Init(void);

/**
 * @brief 创建 OS 任务并启动调度器
 *
 * @details 按 ecum_cfg.c 里的任务表逐个创建，然后调用 vTaskStartScheduler()。
 *          正常情况下永不返回。
 */
void EcuM_StartOS(void);

/**
 * @brief BSW 周期任务：驱动 COM 的主函数
 *
 * @details 为什么放在 EcuM 里：COM 的 MainFunction 属于"BSW 的调度职责"，
 *          不属于应用。真实 AUTOSAR 里由 SchM（Schedule Manager）或 OS 任务触发。
 *
 *          本任务每 COM_MAIN_FUNCTION_PERIOD_MS 调一次：
 *              Com_MainFunctionTx()  检查发送时机
 *              Com_MainFunctionRx()  检查接收超时
 */
void EcuM_TaskCom(void* pvArg);

#endif /* ECUM_H */
