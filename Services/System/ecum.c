/*==================================================================================================
 *  Services/ecum.c —— EcuM 实现：初始化序列 + 任务启动
 *================================================================================================*/

/*==================================================================================================
 *                                        INCLUDES
 *================================================================================================*/
#include "ecum.h"
#include "ecum_cfg.h"

#include "FreeRTOS.h"
#include "task.h"

/* 板级初始化（RTD 配置代码） */
#include "Clock_Ip.h"
#include "Siul2_Port_Ip.h"
#include "IntCtrl_Ip_Cfg.h"
#include "IntCtrl_Ip.h"

/* BSW 各层 —— 注意 include 顺序体现依赖方向（下层在前） */
#include "can.h"
#include "can_cfg.h"
#include "canif_cfg.h"
#include "pdur_cfg.h"
#include "com_cfg.h"
#include "rte.h"

/* 启动配置自检（检查编译期算不出来的跨层关系） */
#include "bswcheck.h"

/* Det：开发错误追踪。它不属于任何一层，是所有模块共用的"报错热线"。
 * 必须**先于**所有 BSW 模块初始化 —— 否则别的模块报错时它还没准备好。 */
#include "Det.h"

/*==================================================================================================
 *                                    LOCAL DEFINES
 *================================================================================================*/

/** @brief Det 上报的便捷宏（DEV_ERROR_DETECT 关闭时被删成 ((void)0)） */
#if (ECUM_DEV_ERROR_DETECT == STD_ON)
    #define ECUM_DET_ERROR(api, err)                                    \
        ((void)Det_ReportError((uint16)ECUM_MODULE_ID,                  \
                               (uint8)ECUM_INSTANCE_ID,                 \
                               (uint8)(api),                            \
                               (uint8)(err)))
#else
    #define ECUM_DET_ERROR(api, err)    ((void)0)
#endif

/*==================================================================================================
 *                              调试观察变量
 *================================================================================================*/
volatile uint32 g_u32ComTaskCount   = 0U;   /**< COM 任务跑了几轮 */
volatile uint32 g_u32BusOffCount    = 0U;   /**< Bus-off 次数 */
volatile uint8  g_u8LastBusOffCtrl  = 0xFFU;
volatile uint32 g_u32ErrStateCount  = 0U;   /**< 错误状态迁移次数 */
volatile uint8  g_u8LastErrOld      = 0U;
volatile uint8  g_u8LastErrNew      = 0U;

/*==================================================================================================
 *                    状态类 hook —— 真实项目里归 CanSM，本工程由 EcuM 代管
 *
 * 【为什么放在 EcuM 而不是 COM 或 PduR】
 *  PduR 只管 PDU 数据流，不管控制器状态。
 *  Bus-off / 错误状态属于"网络状态管理"，AUTOSAR 里由 CanSM 处理，
 *  CanSM 再把结果报给 BswM/EcuM。我们没有 CanSM，所以由 EcuM 顶替。
 *================================================================================================*/
static void EcuM_CanIfBusOff(uint8 ControllerId)
{
    g_u32BusOffCount++;
    g_u8LastBusOffCtrl = ControllerId;

    /* 真实实现这里会：通知 CanSM → 触发恢复流程 → 可能重初始化控制器 */
}

static void EcuM_CanIfErrorState(uint8 ControllerId, Can_ErrorStateType OldState,
                                 Can_ErrorStateType NewState)
{
    (void)ControllerId;
    g_u32ErrStateCount++;
    g_u8LastErrOld = (uint8)OldState;
    g_u8LastErrNew = (uint8)NewState;
}

/*==================================================================================================
 *                                    EcuM_Init
 *================================================================================================*/
void EcuM_Init(void)
{
    uint8 i;

    /*==========================================================================
     * 第零阶段：Det 初始化
     *
     * 【为什么必须是第一个】
     *  Det 是"报错热线"。后面任何一个模块的 Init 都可能发现问题并上报，
     *  如果 Det 还没初始化就有人打这个电话，那条错误就丢了。
     *
     *  Det_Init()  清空错误记录数组（不是必须，但能保证每次上电从干净状态开始）
     *  Det_Start() 声明"初始化期结束"，之后才允许正式上报
     *
     * 【真实 AUTOSAR 的时序】
     *  通常 EcuM 在最早的 StartupOne 阶段就调 Det_Init，
     *  在 BSW 初始化完成后调 Det_Start。
     *========================================================================*/
    Det_Init();

    /*==========================================================================
     * 第一阶段：板级初始化
     *
     * 顺序不能乱：时钟 → 引脚 → 中断控制器
     * 这三步由 CT 生成的配置驱动，属于"RTD 配置代码"层。
     *========================================================================*/
    Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);

    Siul2_Port_Ip_Init(NUM_OF_CONFIGURED_PINS_PortContainer_0_BOARD_InitPeripherals,
                       g_pin_mux_InitConfigArr_PortContainer_0_BOARD_InitPeripherals);

    IntCtrl_Ip_Init(&IntCtrlConfig_0);

    /*==========================================================================
     * 第二阶段：BSW 初始化
     *
     * 【顺序是硬约束】下层必须在上层之前
     *      Can → CanIf → PduR → Com
     *
     * 原因：每一层的 Init 都会把自己注册到下一层。
     *      CanIf_Init   → 注册到 MCAL
     *      PduR_Init    → 注册到 CanIf
     *      Com_Init     → 不注册（COM 是"拉"模式，不向上回调）
     *
     * 【Det_Start 放在这里的理由】
     *  所有模块都初始化完了，此时才"允许上报"。
     *  这样 Debug 时看到的第一条 Det 记录，
     *  一定是真实运行期的问题，而不是初始化过程中的正常检查。
     *========================================================================*/
    Can_Init(&Can_Config);
    CanIf_Init(&CanIf_Config);
    PduR_Init(&PduR_Config);
    Com_Init(&Com_Config);
    Rte_Init();                     /* RTE 目前是空实现，但保留层次完整性 */

    Det_Start();

    /*==========================================================================
     * 【启动配置自检】
     *
     * 检查那些**编译期算不出来**的跨层关系：
     *    - CanIf 的 Length 是否超过 MCAL 对应对象的 PayloadSize
     *    - COM 的信号位区间是否越界 / 是否互相重叠
     *    - COM 的 FirstSignalIdx + SignalCount 是否越过信号表尾
     *
     * 已经用符号化 + 编译期断言解决的部分（句柄/PduId 引用、表项数、DestCount）
     * 这里再查一遍属于纵深防御。
     *
     * ⚠️ 必须放在 Det_Start() 之后、启动控制器之前：
     *    之前 Det 不接受上报，之后总线已通、配错了也已经跑起来了。
     *========================================================================*/
    if (BswCheck_Run() != E_OK)
    {
#if (BSWCHECK_STRICT == 1U)
        /* 严格模式：停在死循环里，让调试器能立刻接上并看 Det 记录。
         * 开发期建议打开（BSWCHECK_STRICT = 1）；
         * 量产保持 0（BSWCHECK_STRICT = 0），只记录不阻塞。 */
        for (;;)
        {
        }
#endif
    }

    /*==========================================================================
     * 【模板自检开关】故意制造 3 个开发错误，验证 Det 接线是否正常
     *
     *  默认 0 = 关闭（量产必须保持 0）。
     *
     *  【怎么用】
     *  新项目复制模板后，把它改成 1 → 编译烧录 → 在调试器 Watch 里看：
     *      Det_numEventErrors[0] = 3
     *  并在 Det_ModuleId[0][0..4] / Det_ApiId[0][0..4] / Det_ErrorId[0][0..4]
     *  里找到这三组：
     *      (50, 2, 3)   COM  / SendSignal / SignalId 越界
     *      (50, 2, 5)   COM  / SendSignal / 往接收信号写值（方向错）
     *      (60, 1, 3)   CanIf/ Transmit   / PduId 在配置表里不存在
     *  找到就说明 Det 全链路正常。
     *
     *  ⚠️ 槽位顺序不固定（Det 内部是链表），只要这三组都在即可，不要依赖下标。
     *  ⚠️ 这个开关必须与 Det 同时开着才有意义（BSW_DEV_ERROR_DETECT 不能是 STD_OFF）。
     *========================================================================*/
#define ECU_SELFTEST_DET    (0)

#if (ECU_SELFTEST_DET == 1)
    {
        uint8 dummy = 0U;

        /* ① SignalId 传 999（越界） → COM(50) / SEND_SIGNAL(2) / PARAM_SIGNAL_ID(3) */
        (void)Com_SendSignal(999U, &dummy);

        /* ② 往一个**接收**信号写值（方向错）→ COM(50) / SEND_SIGNAL(2) / SIGNAL_DIRECTION(5) */
        (void)Com_SendSignal(COM_SIG_RX_DOOR_OPEN, &dummy);

        /* ③ PduId 传 99（配置表里没有）→ CanIf(60) / TRANSMIT(1) / PARAM_PDU_ID(3) */
        {
            PduInfoType badPdu;
            badPdu.SduDataPtr  = &dummy;
            badPdu.MetaDataPtr = NULL_PTR;
            badPdu.SduLength   = 1U;
            (void)CanIf_Transmit(99U, &badPdu);
        }
    }
#endif

    /*==========================================================================
     * 第三阶段：状态类 hook
     *
     * ⚠️ RxIndication / TxConfirmation 两个槽位已被 PduR_Init 占用，
     *    这里只能注册 BusOff / ErrorState。
     *========================================================================*/
    CanIf_RegisterControllerBusOffHook(EcuM_CanIfBusOff);
    CanIf_RegisterControllerErrorStateHook(EcuM_CanIfErrorState);

    /*==========================================================================
     * 第四阶段：启动控制器 —— 永远是初始化序列的最后一步
     *
     * 一旦启动，硬件就能收帧。前面所有回调必须已经接好。
     *========================================================================*/
    for (i = 0U; i < CAN_CONTROLLER_COUNT; i++)
    {
        (void)CanIf_SetControllerMode(i, CAN_CS_STARTED);
    }
}

/*==================================================================================================
 *                                    EcuM_StartOS
 *================================================================================================*/
void EcuM_StartOS(void)
{
    uint8 i;

    if (EcuM_Config.Tasks == NULL_PTR)
    {
        ECUM_DET_ERROR(ECUM_API_START_OS, ECUM_E_INVALID_CONFIG);
        return;
    }

    /*--------------------------------------------------------------------------
     * 按任务表逐个创建
     *
     * 【xTaskCreate 失败要不要报 Det？】
     * 必须报。失败的唯一原因基本是 configTOTAL_HEAP_SIZE 不够，
     * 而这是**配置问题**，正是 Det 该抓的东西。
     *
     * 报了之后仍继续创建后续任务 —— 因为一旦某个任务没建起来，
     * 调度器启动后现象是"某功能不动"，比直接卡死更容易定位。
     *------------------------------------------------------------------------*/
    for (i = 0U; i < EcuM_Config.TaskCount; i++)
    {
        const EcuM_TaskConfigType* task = &EcuM_Config.Tasks[i];
        BaseType_t created;

        created = xTaskCreate(task->TaskEntry,
                              task->TaskName,
                              (configSTACK_DEPTH_TYPE)task->StackWords,
                              task->TaskArg,
                              (UBaseType_t)task->Priority,
                              NULL);

        if (created != pdPASS)
        {
#if (ECUM_DEV_ERROR_DETECT == STD_ON)
            /* 这里不能用 ECUM_DET_ERROR 宏 —— 因为要传**任务下标**做 InstanceId，
             * 而宏里 InstanceId 是写死的 ECUM_INSTANCE_ID（0）。
             * 直接调 Det_ReportError，所以必须自己用 #if 包住，
             * 否则 STD_OFF 时这个调用不会被编译掉（会多一次函数调用和符号引用）。 */
            (void)Det_ReportError((uint16)ECUM_MODULE_ID,
                                  (uint8)i,
                                  (uint8)ECUM_API_START_OS,
                                  (uint8)ECUM_E_TASK_CREATE_FAILED);
#endif
        }
    }

    vTaskStartScheduler();

    /* 正常不会到这里：堆不够导致空闲任务创建失败时才会返回 */
    for (;;)
    {
    }
}

/*==================================================================================================
 *                              EcuM_TaskCom —— BSW 周期任务
 *================================================================================================*/
void EcuM_TaskCom(void* pvArg)
{
    TickType_t xLastWake = xTaskGetTickCount();

    (void)pvArg;

    for (;;)
    {
        g_u32ComTaskCount++;

        /* COM 的两个主函数：一个管发送时机，一个管接收超时 */
        Com_MainFunctionTx();
        Com_MainFunctionRx();

        /* 用绝对延时，保证周期不累积漂移 */
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(COM_MAIN_FUNCTION_PERIOD_MS));
    }
}
