/*==================================================================================================
 *  Services/pdur.c —— PduR 实现
 *
 *  【本层做的全部事情：按路由表转发】
 *
 *      下行：PduR_ComTransmit(TxPduId)      →  查表 → CanIf_Transmit(dest, ...) × N
 *      上行：PduR_CanIfRxIndication(RxPduId) →  查表 → dest->RxIndication(target, ...) × N
 *      确认：PduR_CanIfTxConfirmation(TxPduId) → 反查表 → SrcTxConfirmation(src, result)
 *
 *  【为什么"没有独立 PduId 空间"却仍然要做翻译】
 *
 *  PduR 的表被**源侧的 PduId 索引**（接收方向的 CanIf RxPduId、
 *  发送方向的 COM TxPduId）。所以"查表"这一步不需要翻译。
 *
 *  但**目标侧的 PduId 必须翻译**：CanIf 的 TxPduId 和 COM 的 TxPduId
 *  是两套编号，PduR 的表就是那个"翻译字典"。
 *
 *  发送确认方向最典型：
 *      CanIf 说"3 号发出了" → PduR 反查 → 告诉 COM"你的 2 号发出了"
 *================================================================================================*/

/*==================================================================================================
 *                                        INCLUDES
 *================================================================================================*/
#include "pdur.h"
#include "pdur_cfg.h"

/* PduR 真的依赖 CanIf（向下调用）—— 这是代码级依赖，与"CanIf 不依赖 PduR"形成对比。 */
#include "canif.h"

/* Det（开发错误追踪）—— 叶子模块，任何层引用它都不破坏分层。 */
#include "Det.h"

/*==================================================================================================
 *                                    LOCAL DEFINES
 *================================================================================================*/

/** @brief Det 上报的便捷宏（DEV_ERROR_DETECT 关闭时被删成 ((void)0)） */
#if (PDUR_DEV_ERROR_DETECT == STD_ON)
    #define PDUR_DET_ERROR(api, err)                                    \
        ((void)Det_ReportError((uint16)PDUR_MODULE_ID,                  \
                               (uint8)PDUR_INSTANCE_ID,                 \
                               (uint8)(api),                            \
                               (uint8)(err)))
#else
    #define PDUR_DET_ERROR(api, err)    ((void)0)
#endif

/*==================================================================================================
 *                                    LOCAL VARIABLES
 *================================================================================================*/

/** @brief 当前生效的配置 */
static const PduR_ConfigType* PduR_ConfigPtr = NULL_PTR;

/** @brief 初始化完成标志 */
static boolean PduR_Initialized = FALSE;

/*==================================================================================================
 *                              LOCAL FUNCTIONS —— 查表
 *================================================================================================*/

/**
 * @brief 按源 PduId 查 TX 路由路径
 * @return 路径指针；找不到返回 NULL_PTR
 */
static const PduR_TxPathConfigType* PduR_FindTxPath(PduIdType SrcPduId)
{
    uint16 i;

    if (PduR_ConfigPtr == NULL_PTR)
    {
        return NULL_PTR;
    }

    for (i = 0U; i < PduR_ConfigPtr->TxPathCount; i++)
    {
        if (PduR_ConfigPtr->TxPaths[i].SrcPduId == SrcPduId)
        {
            return &PduR_ConfigPtr->TxPaths[i];
        }
    }
    return NULL_PTR;
}

/**
 * @brief 按源 PduId 查 RX 路由路径
 */
static const PduR_RxPathConfigType* PduR_FindRxPath(PduIdType SrcPduId)
{
    uint16 i;

    if (PduR_ConfigPtr == NULL_PTR)
    {
        return NULL_PTR;
    }

    for (i = 0U; i < PduR_ConfigPtr->RxPathCount; i++)
    {
        if (PduR_ConfigPtr->RxPaths[i].SrcPduId == SrcPduId)
        {
            return &PduR_ConfigPtr->RxPaths[i];
        }
    }
    return NULL_PTR;
}

/*==================================================================================================
 *                                   INIT
 *================================================================================================*/
void PduR_Init(const PduR_ConfigType* ConfigPtr)
{
    if (ConfigPtr == NULL_PTR)
    {
        PDUR_DET_ERROR(PDUR_API_INIT, PDUR_E_PARAM_POINTER);
        return;
    }

    PduR_ConfigPtr = ConfigPtr;

    /*--------------------------------------------------------------------------
     * 把自己注册到 CanIf 的上报 hook 上
     *
     * 这一步建立了 **CanIf → PduR** 的上报通路。
     *
     * 【注意依赖方向】
     * 这里是 PduR **向下**调用 CanIf 的注册函数 —— 方向是"上层依赖下层"，正常。
     * 反方向上，CanIf 只会调函数指针，从不 include pdur.h。
     *------------------------------------------------------------------------*/
    CanIf_RegisterRxIndicationHook(PduR_CanIfRxIndication);
    CanIf_RegisterTxConfirmationHook(PduR_CanIfTxConfirmation);

    PduR_Initialized = TRUE;
}

#if (PDUR_VERSION_INFO_API == STD_ON)
void PduR_GetVersionInfo(Std_VersionInfoType* versioninfo)
{
    if (versioninfo == NULL_PTR)
    {
        PDUR_DET_ERROR(PDUR_API_GET_VERSION_INFO, PDUR_E_PARAM_POINTER);
        return;
    }

    versioninfo->vendorID         = (uint16)PDUR_VENDOR_ID;
    versioninfo->moduleID         = (uint16)PDUR_MODULE_ID;
    versioninfo->sw_major_version = (uint8)PDUR_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = (uint8)PDUR_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = (uint8)PDUR_SW_PATCH_VERSION;
}
#endif

/*==================================================================================================
 *                        下行：上层（COM）→ PduR → CanIf
 *================================================================================================*/
Std_ReturnType PduR_ComTransmit(PduIdType TxPduId, const PduInfoType* PduInfoPtr)
{
    const PduR_TxPathConfigType* path;
    uint8                        i;
    Std_ReturnType               result = E_OK;

    /*--------------------------------------------------------------------------
     * 1. 前置检查
     *------------------------------------------------------------------------*/
    if (PduR_Initialized == FALSE)
    {
        PDUR_DET_ERROR(PDUR_API_COM_TRANSMIT, PDUR_E_UNINIT);
        return E_NOT_OK;
    }
    if (PduInfoPtr == NULL_PTR)
    {
        PDUR_DET_ERROR(PDUR_API_COM_TRANSMIT, PDUR_E_PARAM_POINTER);
        return E_NOT_OK;
    }

    /*--------------------------------------------------------------------------
     * 2. 查路由路径
     *
     * 找不到 = 这条 PDU 没有配置路由。属于**配置错误**（或调用方用错编号），
     * 直接拒绝，不能静默丢弃 —— 否则表现为"发送成功但总线上没东西"。
     *------------------------------------------------------------------------*/
    path = PduR_FindTxPath(TxPduId);
    if (path == NULL_PTR)
    {
        PDUR_DET_ERROR(PDUR_API_COM_TRANSMIT, PDUR_E_PARAM_PDU_ID);
        return E_NOT_OK;
    }

    /*--------------------------------------------------------------------------
     * 3. 逐个目标下发（1:N 扇出）
     *
     * 【返回值聚合策略】—— 按 SWS："全部成功才算成功"
     *
     *   任一目标返回非 E_OK → 整体 E_NOT_OK
     *
     * 【这个策略的诚实说明】
     * 1:N 时"部分成功"会被报成失败。上层必须知道这一点：
     * 返回 E_NOT_OK 不代表"什么都没发出去"，可能已经有一路发出去了。
     * 如果将来需要精确知道每一路的结果，就要扩展接口（非标准），
     * 或者用 TX 确认（CanIf 会逐路回调 PduR_CanIfTxConfirmation）来精确跟踪。
     *
     * 【注意 CAN_BUSY 被折叠成 E_NOT_OK】
     * CanIf 可能返回 CAN_BUSY（邮箱忙）。SWS 规定 PduR_ComTransmit 只返回
     * E_OK / E_NOT_OK 两态，所以这里把 CAN_BUSY 折叠成失败。
     * 这与 MCAL 层"不要把 CAN_BUSY 折叠掉"的原则**不矛盾** ——
     * 不同层的契约不同：MCAL 明确保留三态，PduR 的 SWS 契约是两态。
     * 上层（COM）应对 E_NOT_OK 做周期性重试，效果等价。
     *------------------------------------------------------------------------*/
    for (i = 0U; i < path->DestCount; i++)
    {
        Std_ReturnType ret = CanIf_Transmit(path->Dests[i].DestPduId, PduInfoPtr);

        if (ret != E_OK)
        {
            result = E_NOT_OK;
        }
    }

    return result;
}

/*==================================================================================================
 *                        上行：CanIf → PduR → 上层（COM / Dcm）
 *================================================================================================*/
void PduR_CanIfRxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr)
{
    const PduR_RxPathConfigType* path;
    uint8                        i;

    if ((PduR_Initialized == FALSE) || (PduInfoPtr == NULL_PTR))
    {
        /* ★ 这个函数由 CanIf 在 **CAN 接收中断** 里回调，
         *   所以这里上报 Det 也发生在中断上下文 —— Det 实现必须快（见 canif.c 同类注释）。 */
        PDUR_DET_ERROR(PDUR_API_CANIF_RX_INDICATION,
                       (PduR_Initialized == FALSE) ? PDUR_E_UNINIT : PDUR_E_PARAM_POINTER);
        return;
    }

    /*--------------------------------------------------------------------------
     * 查表：CanIf 的 RxPduId → 路由路径
     *
     * 找不到 = 这条接收 PDU 没配路由。
     * 这是正常可发生的：CanIf 配了接收，但上层暂时没人要。
     * 直接丢弃（和 CanIf 里 Hrh 没配 PDU 时的处理一致）。
     *------------------------------------------------------------------------*/
    path = PduR_FindRxPath(RxPduId);
    if (path == NULL_PTR)
    {
        return;
    }

    /*--------------------------------------------------------------------------
     * 逐个目标分发（1:N 扇出）
     *
     * 【零拷贝】PduInfoPtr 原样传给每个目标，不拷贝。
     * 所有目标拿到的是**同一个指针**，指向 MCAL 的接收缓冲。
     * 因此每个目标都必须在本函数返回前消费完（或自己 memcpy）。
     *------------------------------------------------------------------------*/
    for (i = 0U; i < path->DestCount; i++)
    {
        if (path->Dests[i].RxIndication != NULL_PTR)
        {
            path->Dests[i].RxIndication(path->Dests[i].DestPduId, PduInfoPtr);
        }
    }
}

/*==================================================================================================
 *                        发送确认：CanIf → PduR → 上层（反向翻译编号）
 *================================================================================================*/
void PduR_CanIfTxConfirmation(PduIdType TxPduId, Std_ReturnType result)
{
    uint16 i;
    uint8  j;

    if (PduR_Initialized == FALSE)
    {
        return;
    }

    /*--------------------------------------------------------------------------
     * 【本函数的核心：编号空间的反向翻译】
     *
     * 进来的是 **CanIf 的 TxPduId**，要报给上层的是 **COM 的 TxPduId**。
     * 但路由表是按"源 PduId"建的索引，目标 PduId 只是普通字段 ——
     * 所以必须**全表扫描**，在目标字段里找匹配项。
     *
     * 复杂度 O(路径数 × 目标数)。数量都是个位数，开销可忽略。
     * 真实 AUTOSAR 的实现通常也会生成一张反向查找表来避免扫描，
     * 但那属于优化，不影响语义。
     *
     * ⚠️ 配置约束：同一个 CanIf TxPduId 不应出现在多条路径里。
     *    否则这里会命中多条，给多个源各报一次确认 —— 语义就错了。
     *    本实现选择"全部命中都上报"而不是"只报第一个"，
     *    因为静默丢弃比多报更难查。真要防住，靠配置纪律（见 pdur_cfg.c 注释）。
     *
     * 【本实现对应 AUTOSAR 的 PduRTransmissionConfirmation = FALSE】
     *
     *  AUTOSAR 用这个配置参数控制"发送确认要不要聚合"：
     *
     *    FALSE（本实现）
     *        每个目标各自回一次确认，PduR 立刻转发给源模块。
     *        → 1:N 时源模块会收到 **N 次**确认（每次 TxPduId 相同）。
     *        优点：确认及时。缺点：源模块要自己处理"发一次收 N 个确认"。
     *
     *    TRUE
     *        PduR 记住已确认的路数，等 N 路全确认完，只给源模块报 **1 次**。
     *        优点：语义直观。缺点：要维护每路状态（多一份 RAM），
     *              且任一路丢帧就永远等不到确认。
     *
     *  我们选 FALSE：实现简单、不需要额外状态存储。
     *  将来若要聚合，给每条 TX 路径加一个"已确认路数"计数器即可。
     *------------------------------------------------------------------------*/
    for (i = 0U; i < PduR_ConfigPtr->TxPathCount; i++)
    {
        const PduR_TxPathConfigType* path = &PduR_ConfigPtr->TxPaths[i];

        for (j = 0U; j < path->DestCount; j++)
        {
            if (path->Dests[j].DestPduId == TxPduId)
            {
                if (path->SrcTxConfirmation != NULL_PTR)
                {
                    /* 把"CanIf 的编号"翻译回"源模块（COM）的编号" */
                    path->SrcTxConfirmation(path->SrcPduId, result);
                }
            }
        }
    }
}
