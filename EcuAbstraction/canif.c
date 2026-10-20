/*==================================================================================================
 *  EcuAbstraction/canif.c —— CanIf 实现
 *
 *  【本层做的全部事情：查表 + 转发】
 *
 *      发送：  PduId ──查表──> Hth + CanId + Len ──> Can_Write()
 *      接收：  Hrh   ──查表──> PduId ──> 上层 hook
 *      确认：  Hth   ──查表──> PduId ──> 上层 hook
 *
 *  本层**不做**任何硬件操作，也不理解邮箱/寄存器。它唯一的智能是"查表翻译"。
 *
 *  【为什么接收方向要用索引数组而不是遍历】
 *  接收在**中断上下文**里执行，每次中断都遍历一张表是不必要的开销。
 *  Hrh 的取值范围是 0..(CAN_HTH_FIRST_INDEX-1)，很小且连续，
 *  所以初始化时建一张"Hrh → 表下标"的数组，运行时 O(1) 直接索引。
 *
 *  【零拷贝约定】
 *  CanIf_RxIndication() 把 MCAL 的缓冲指针**原样**传给上层，不拷贝。
 *  前提是整条链同步执行（都在同一个中断里），指针在返回前一直有效。
 *  上层不得保存该指针 —— 见 canif.h 里的约定说明。
 *================================================================================================*/

/*==================================================================================================
 *                                        INCLUDES
 *================================================================================================*/
#include "canif.h"
#include "canif_cfg.h"

/* 本层需要调用 MCAL 的 API（Can_Write / Can_SetControllerMode 等）和注册 hook。
 * 注意：只有**实现文件**依赖 MCAL 的接口头；canif.h 不依赖。 */
#include "can.h"

/* 句柄空间常量（CAN_HTH_FIRST_INDEX / CAN_CONTROLLER_COUNT）来自 MCAL 配置头。
 * 只在实现文件里引入，避免污染 canif.h / canif_cfg.h。 */
#include "can_cfg.h"

/* Det（开发错误追踪）。注意：**只有实现文件 include Det.h**，
 * canif.h 不依赖它 —— 头文件里只放模块号和错误码定义。 */
#include "Det.h"

/*==================================================================================================
 *                                    LOCAL DEFINES
 *================================================================================================*/

/** @brief "该 Hrh 没有配置接收 PDU"的标记 */
#define CANIF_NO_PDU_INDEX          (0xFFFFU)

/**
 * @brief Det 上报用的便捷宏
 *
 * @details 为什么要有这个宏，而不是直接写 Det_ReportError(...)？
 *
 *  ① 少打很多字。原始写法是：
 *         (void)Det_ReportError(CANIF_MODULE_ID, CANIF_INSTANCE_ID, CANIF_API_TRANSMIT, CANIF_E_UNINIT);
 *     用宏之后：
 *         CANIF_DET_ERROR(CANIF_API_TRANSMIT, CANIF_E_UNINIT);
 *
 *  ② 关键：**DEV_ERROR_DETECT = STD_OFF 时，整个调用被预处理器删掉**。
 *     编译器根本看不到 Det_ReportError，也就不会产生任何代码。
 *     如果直接写函数调用，就只能靠链接器优化 / LTO，不保险。
 *
 *  ③ 统一 InstanceId。将来若 CanIf 有多个实例，只改宏一处。
 */
#if (CANIF_DEV_ERROR_DETECT == STD_ON)
    #define CANIF_DET_ERROR(api, err)                                   \
        ((void)Det_ReportError((uint16)CANIF_MODULE_ID,                 \
                               (uint8)CANIF_INSTANCE_ID,                \
                               (uint8)(api),                            \
                               (uint8)(err)))
#else
    #define CANIF_DET_ERROR(api, err)   ((void)0)
#endif

/*==================================================================================================
 *                                    LOCAL VARIABLES
 *================================================================================================*/

/** @brief 当前生效的配置 */
static const CanIf_ConfigType* CanIf_DriverConfig = NULL_PTR;

/** @brief 初始化完成标志 */
static boolean CanIf_Initialized = FALSE;

/**
 * @brief Hrh → CanIf_DriverConfig->RxPdus[] 的下标（O(1) 反查用）
 * @details 值 CANIF_NO_PDU_INDEX 表示该 Hrh 没有配置接收 PDU。
 *          索引数组长度用 CAN_HTH_FIRST_INDEX（= 接收句柄的总数）。
 */
static uint16 CanIf_RxIdxByHrh[CAN_HTH_FIRST_INDEX];

/** @brief 各控制器的 PDU 通道模式 */
static CanIf_PduModeType CanIf_PduMode[CAN_CONTROLLER_COUNT];

/** @brief 向上层（PduR）的 hook */
static CanIf_RxIndicationHookType        CanIf_RxUpHook         = NULL_PTR;
static CanIf_TxConfirmationHookType      CanIf_TxUpHook         = NULL_PTR;
static CanIf_ControllerBusOffHookType    CanIf_BusOffUpHook     = NULL_PTR;
static CanIf_ControllerErrorStateHookType CanIf_ErrStateUpHook  = NULL_PTR;

/*==================================================================================================
 *                              LOCAL FUNCTIONS
 *================================================================================================*/

/**
 * @brief Hth（发送句柄）→ 发送 PDU 表下标
 * @return 下标；找不到返回 CANIF_NO_PDU_INDEX
 * @details 用于发送确认：MCAL 报上来的是 Hth，要反查成 PduId 才能通知上层。
 */
static uint16 CanIf_FindTxIdxByHth(Can_HwHandleType Hth)
{
    uint16 i;

    if (CanIf_DriverConfig == NULL_PTR)
    {
        return CANIF_NO_PDU_INDEX;
    }

    for (i = 0U; i < CanIf_DriverConfig->TxPduCount; i++)
    {
        if (CanIf_DriverConfig->TxPdus[i].Hth == Hth)
        {
            return i;
        }
    }
    return CANIF_NO_PDU_INDEX;
}

/**
 * @brief PduId（逻辑发送 PDU 号）→ 发送 PDU 表下标
 * @return 下标；找不到返回 CANIF_NO_PDU_INDEX
 *
 * 【为什么按 PduId 查，而不是直接当数组下标用】
 * "PduId 恰好等于表下标"是个**隐含假设**：它要求 PduId 从 0 开始连续编号、
 * 且表的排列顺序与编号一致。现在成立，但将来 PduR 给各模块分配全局 PDU ID 时
 * （比如 CanIf 拿到 5,7,9 这样的号）就会失效。
 *
 * 一旦失效，直接当下标用会**静默**返回 E_NOT_OK —— 表现为"发不出去但不知道为什么"。
 * 改成显式查找，代价是 O(n)（n 是发送 PDU 数量，个位数），
 * 换来的是"编号怎么排都不会错"。
 */
static uint16 CanIf_FindTxIdxByPduId(PduIdType TxPduId)
{
    uint16 i;

    if (CanIf_DriverConfig == NULL_PTR)
    {
        return CANIF_NO_PDU_INDEX;
    }

    for (i = 0U; i < CanIf_DriverConfig->TxPduCount; i++)
    {
        if (CanIf_DriverConfig->TxPdus[i].TxPduId == TxPduId)
        {
            return i;
        }
    }
    return CANIF_NO_PDU_INDEX;
}

/**
 * @brief 检查某个控制器当前是否允许发送
 */
static boolean CanIf_IsTxAllowed(uint8 ControllerId)
{
    if (ControllerId >= CAN_CONTROLLER_COUNT)
    {
        return FALSE;
    }
    return (CanIf_PduMode[ControllerId] == CANIF_TX_ONLINE) ? TRUE : FALSE;
}

/*==================================================================================================
 *                                   INIT
 *================================================================================================*/
void CanIf_Init(const CanIf_ConfigType* Config)
{
    uint16 i;

    if (Config == NULL_PTR)
    {
        /* 传空指针 == 调用者写错了。这类错误量产后不该存在，所以报 Det。 */
        CANIF_DET_ERROR(CANIF_API_INIT, CANIF_E_PARAM_POINTER);
        return;
    }

    CanIf_DriverConfig = Config;

    /*--------------------------------------------------------------------------
     * 1. 建立 Hrh → RxPdu 下标索引
     *
     * 先全部填成"无"，再按配置逐项填。这样即使 RxPdus 表里只配了 2 个 Hrh，
     * 其余 Hrh 也不会索引到数组越界位置。
     *------------------------------------------------------------------------*/
    for (i = 0U; i < (uint16)CAN_HTH_FIRST_INDEX; i++)
    {
        CanIf_RxIdxByHrh[i] = CANIF_NO_PDU_INDEX;
    }

    for (i = 0U; i < Config->RxPduCount; i++)
    {
        uint16 hrh = (uint16)Config->RxPdus[i].Hrh;

        /* Hrh 必须落在"接收段"内（< CAN_HTH_FIRST_INDEX），否则是配置错误。
         * 这类错误在初始化时跳过并留空，运行时会表现为"这个 Hrh 收不到数据"，
         * 比越界写内存安全得多。 */
        if (hrh < (uint16)CAN_HTH_FIRST_INDEX)
        {
            CanIf_RxIdxByHrh[hrh] = i;
        }
        else
        {
            /* 配置表里写了一个越界的 Hrh —— 这是**配置错误**，不是运行故障。
             * 报 Det，让配置文件的问题在启动时就被发现，
             * 而不是等运行时"这个 Hrh 收不到数据"再回来找。 */
            CANIF_DET_ERROR(CANIF_API_INIT, CANIF_E_PARAM_HRH);
        }
    }

    /*--------------------------------------------------------------------------
     * 2. PDU 通道模式默认全开
     *------------------------------------------------------------------------*/
    for (i = 0U; i < (uint16)CAN_CONTROLLER_COUNT; i++)
    {
        CanIf_PduMode[i] = CANIF_TX_ONLINE;
    }

    /*--------------------------------------------------------------------------
     * 3. 把自己注册到 MCAL 的 hook 上
     *
     * 这一步建立了 **Can → CanIf** 的上报通路。
     * MCAL 不知道 CanIf 的存在，只知道"有这么几个函数指针要调"。
     *------------------------------------------------------------------------*/
    Can_RegisterRxHook(CanIf_RxIndication);
    Can_RegisterTxHook(CanIf_TxConfirmation);
    Can_RegisterBusOffHook(CanIf_ControllerBusOff);
    Can_RegisterErrorStateHook(CanIf_ControllerErrorState);

    CanIf_Initialized = TRUE;
}

/*==================================================================================================
 *                                   发送
 *================================================================================================*/
Std_ReturnType CanIf_Transmit(PduIdType TxPduId, const PduInfoType* PduInfoPtr)
{
    const CanIf_TxPduConfigType* txCfg;
    Can_PduType                  pdu;
    Std_ReturnType               ret;
    uint16                       idx;

    /*--------------------------------------------------------------------------
     * 1. 前置检查
     *
     * 【A 类：纯编程错误】—— 调用者违反了 API 契约
     *   未 Init 就调用、传 NULL 指针。量产后这类错误不该存在，
     *   所以 Det 上报放在 DEV_ERROR_DETECT 开关内。
     *
     * 【注意：防线本身要不要也编译掉？】
     *   AUTOSAR SWS 的写法是**连 if 一起包进 #if**，STD_OFF 时整段消失，
     *   追求极致性能。但那意味着传 NULL 会直接段错误。
     *   本工程选择**保留 if，只把 Det 调用编译掉** ——
     *   多花一个比较指令，换一个"不会崩"的底线。这是个有意的偏离。
     *------------------------------------------------------------------------*/
    if (CanIf_Initialized == FALSE)
    {
        CANIF_DET_ERROR(CANIF_API_TRANSMIT, CANIF_E_UNINIT);
        return E_NOT_OK;
    }
    if ((PduInfoPtr == NULL_PTR) || (PduInfoPtr->SduDataPtr == NULL_PTR))
    {
        CANIF_DET_ERROR(CANIF_API_TRANSMIT, CANIF_E_PARAM_POINTER);
        return E_NOT_OK;
    }

    /*--------------------------------------------------------------------------
     * 2. 查表：PduId → 发送硬件对象
     *
     * 用显式查找而不是把 PduId 当下标 —— 理由见 CanIf_FindTxIdxByPduId()。
     *
     * 【B 类：配置/输入错误】—— 可能来自上层传了没配过的 PduId
     *   这类错误如果在量产版被"优化掉"，就会变成越界访问。
     *   所以防线保留，只让 Det 上报可编译。
     *------------------------------------------------------------------------*/
    idx = CanIf_FindTxIdxByPduId(TxPduId);
    if (idx == CANIF_NO_PDU_INDEX)
    {
        CANIF_DET_ERROR(CANIF_API_TRANSMIT, CANIF_E_PARAM_PDU_ID);
        return E_NOT_OK;
    }

    txCfg = &CanIf_DriverConfig->TxPdus[idx];

    /*--------------------------------------------------------------------------
     * 3. 长度校验
     *
     * 两道：
     *   a) 不能超过该 PDU 配置的长度
     *   b) 不能超过本层支持的单 PDU 上限（CANIF_MAX_PDU_LEN）
     *
     * (b) 现在与 (a) 等价（都是 8），但保留它是为了将来开 FD：
     *     CANIF_MAX_PDU_LEN 改成 64 后，(b) 自动放宽，无需改本函数。
     *------------------------------------------------------------------------*/
    if ((uint32)PduInfoPtr->SduLength > (uint32)txCfg->Length)
    {
        CANIF_DET_ERROR(CANIF_API_TRANSMIT, CANIF_E_PARAM_LENGTH);
        return E_NOT_OK;
    }
    if ((uint32)PduInfoPtr->SduLength > (uint32)CANIF_MAX_PDU_LEN)
    {
        CANIF_DET_ERROR(CANIF_API_TRANSMIT, CANIF_E_PARAM_LENGTH);
        return E_NOT_OK;
    }

    /*--------------------------------------------------------------------------
     * 4. PDU 通道模式检查
     *
     * 控制器被置为 TX_OFFLINE / OFFLINE 时，本层直接拒绝发送，
     * 但**不**去停控制器 —— 这样接收仍可用（诊断会话切换就靠这个）。
     *
     * 【★ 这里刻意不报 Det】
     *  TX_OFFLINE 是**合法的运行状态**，不是程序员的错误：
     *  诊断会话切换时本来就要禁止应用报文发送。
     *  如果这里报 Det，Det 记录数组会被正常行为刷满，
     *  真正的错误反而被淹没。
     *
     *  判据：**这个方法被拒绝，是"写错了"还是"正常的业务状态"？**
     *        写错了 → 报 Det；正常状态 → 只用返回值表达（E_NOT_OK / CAN_BUSY）。
     *------------------------------------------------------------------------*/
    if (CanIf_IsTxAllowed(txCfg->ControllerId) == FALSE)
    {
        return E_NOT_OK;
    }

    /*--------------------------------------------------------------------------
     * 5. 构造 Can_PduType 并交给 MCAL
     *
     * CanId 直接来自配置，**原样传递**，不做任何推导或掩码。
     * 这是关键：bit31（扩展帧）和 bit30（FD 帧）都由配置决定，
     * 上层（PduR/COM）压根不知道这些位的存在。
     *------------------------------------------------------------------------*/
    pdu.swPduHandle = TxPduId;
    pdu.length      = (uint8)PduInfoPtr->SduLength;
    pdu.sdu         = PduInfoPtr->SduDataPtr;
    pdu.id          = txCfg->CanId;

    ret = Can_Write(txCfg->Hth, &pdu);

    /*--------------------------------------------------------------------------
     * 6. 返回值映射
     *
     * CAN_BUSY 原样往上透传，**不折叠成 E_NOT_OK**。
     * 理由与 MCAL 层一致：拥塞和故障是两回事，上层需要分别处理
     * （拥塞 → 稍后重试；故障 → 报错/统计）。
     *------------------------------------------------------------------------*/
    return ret;
}

/*==================================================================================================
 *                      以下四个函数由 MCAL 在中断上下文里回调
 *================================================================================================*/

void CanIf_RxIndication(Can_HwHandleType Hrh, const Can_PduType* PduInfo)
{
    PduInfoType pduInfo;
    uint16      idx;

    if ((CanIf_Initialized == FALSE) || (PduInfo == NULL_PTR))
    {
        /* ★ 注意：这个函数跑在 **CAN 接收中断** 里。
         *   所以 Det_ReportError 会在中断上下文被调用。
         *
         *   这就是为什么 Det 的实现只能"记数组"，不能打印、不能阻塞 ——
         *   RTD 的 Det.c 就是往 Det_ModuleId[][]. 里写，几条赋值完事，安全。
         *
         *   如果 Det 实现里加了 printf 或任何可能阻塞的东西，
         *   这里就会把整个 CAN 中断拖长几十微秒，直接导致丢帧。
         */
        CANIF_DET_ERROR(CANIF_API_RX_INDICATION, CANIF_E_UNINIT);
        return;
    }

    /*--------------------------------------------------------------------------
     * 1. Hrh → RxPdu 下标（O(1)）
     *------------------------------------------------------------------------*/
    if (Hrh >= (Can_HwHandleType)CAN_HTH_FIRST_INDEX)
    {
        return;     /* 越界：不是接收段内的句柄 */
    }

    idx = CanIf_RxIdxByHrh[Hrh];
    if (idx == CANIF_NO_PDU_INDEX)
    {
        /* 这个 Hrh 没配接收 PDU —— 硬件收到了但我们不关心，直接丢弃。
         * 正常情况下不会发生：只有 MCAL 的接收表里配置过的邮箱才会触发回调。 */
        return;
    }

    /*--------------------------------------------------------------------------
     * 2. 组装 PduInfoType 并上报
     *
     * 【零拷贝】SduDataPtr 直接指向 MCAL 的静态接收缓冲。
     * 该缓冲在本次中断返回后随时会被下一帧覆盖，所以上层必须同步消费。
     *------------------------------------------------------------------------*/
    pduInfo.SduDataPtr  = PduInfo->sdu;
    pduInfo.MetaDataPtr = NULL_PTR;                     /* 本阶段不支持 MetaData */
    pduInfo.SduLength   = (PduLengthType)PduInfo->length;

    if (CanIf_RxUpHook != NULL_PTR)
    {
        CanIf_RxUpHook(CanIf_DriverConfig->RxPdus[idx].RxPduId, &pduInfo);
    }
}

void CanIf_TxConfirmation(Can_HwHandleType Hth)
{
    uint16 idx;

    if (CanIf_Initialized == FALSE)
    {
        return;
    }

    idx = CanIf_FindTxIdxByHth(Hth);
    if (idx == CANIF_NO_PDU_INDEX)
    {
        /* Hth 没配发送 PDU —— 不可能发生（只有配过的句柄才会被 Can_Write 使用），
         * 留作防御。 */
        return;
    }

    if (CanIf_TxUpHook != NULL_PTR)
    {
        CanIf_TxUpHook(CanIf_DriverConfig->TxPdus[idx].TxPduId, E_OK);
    }
}

void CanIf_ControllerBusOff(uint8 ControllerId)
{
    if (CanIf_Initialized == FALSE)
    {
        return;
    }

    if (CanIf_BusOffUpHook != NULL_PTR)
    {
        CanIf_BusOffUpHook(ControllerId);
    }
}

void CanIf_ControllerErrorState(uint8 ControllerId,
                                Can_ErrorStateType OldState,
                                Can_ErrorStateType NewState)
{
    if (CanIf_Initialized == FALSE)
    {
        return;
    }

    if (CanIf_ErrStateUpHook != NULL_PTR)
    {
        CanIf_ErrStateUpHook(ControllerId, OldState, NewState);
    }
}

/*==================================================================================================
 *                                  控制器模式（转发）
 *================================================================================================*/

Std_ReturnType CanIf_SetControllerMode(uint8 ControllerId,
                                       Can_ControllerStateType ControllerMode)
{
    if (CanIf_Initialized == FALSE)
    {
        return E_NOT_OK;
    }
    return Can_SetControllerMode(ControllerId, ControllerMode);
}

Std_ReturnType CanIf_GetControllerMode(uint8 ControllerId,
                                       Can_ControllerStateType* ControllerModePtr)
{
    if (CanIf_Initialized == FALSE)
    {
        return E_NOT_OK;
    }
    return Can_GetControllerMode(ControllerId, ControllerModePtr);
}

/*==================================================================================================
 *                                  PDU 通道模式
 *================================================================================================*/

Std_ReturnType CanIf_SetPduMode(uint8 ControllerId, CanIf_PduModeType PduModeRequest)
{
    if ((CanIf_Initialized == FALSE) || (ControllerId >= CAN_CONTROLLER_COUNT))
    {
        return E_NOT_OK;
    }
    if (PduModeRequest > CANIF_TX_ONLINE)
    {
        return E_NOT_OK;
    }

    CanIf_PduMode[ControllerId] = PduModeRequest;
    return E_OK;
}

Std_ReturnType CanIf_GetPduMode(uint8 ControllerId, CanIf_PduModeType* PduModePtr)
{
    if ((CanIf_Initialized == FALSE) ||
        (ControllerId >= CAN_CONTROLLER_COUNT) ||
        (PduModePtr == NULL_PTR))
    {
        return E_NOT_OK;
    }

    *PduModePtr = CanIf_PduMode[ControllerId];
    return E_OK;
}

/*==================================================================================================
 *                                HOOK 注册（向上层）
 *================================================================================================*/
void CanIf_RegisterRxIndicationHook(CanIf_RxIndicationHookType hook)
{
    CanIf_RxUpHook = hook;
}

void CanIf_RegisterTxConfirmationHook(CanIf_TxConfirmationHookType hook)
{
    CanIf_TxUpHook = hook;
}

void CanIf_RegisterControllerBusOffHook(CanIf_ControllerBusOffHookType hook)
{
    CanIf_BusOffUpHook = hook;
}

void CanIf_RegisterControllerErrorStateHook(CanIf_ControllerErrorStateHookType hook)
{
    CanIf_ErrStateUpHook = hook;
}
