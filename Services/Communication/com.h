/*==================================================================================================
 *  Services/com.h —— 服务层 COM（Communication）对外接口
 *
 *  【本层在架构里的位置】
 *
 *      Application SWC
 *          ↕  Rte_Write_<Port>_<Data>() / Rte_Read_<Port>_<Data>()
 *        RTE（生成的胶水）
 *          ↕  Com_SendSignal() / Com_ReceiveSignal()
 *    ┌───────────────────────────────────┐
 *    │            COM  ← 本层             │   物理量 ↔ 位域
 *    └───────────────────────────────────┘
 *          ↕                          ↕
 *    PduR_ComTransmit()      Com_RxIndication()
 *                            Com_TxConfirmation()
 *          ↕
 *        PduR
 *
 *  【本层是"两个世界"的分界线】
 *
 *      本层以上：**物理量**（60 km/h、门开、窗 50%、12.6 V）
 *      本层以下：**裸字节**（65 C1 5D 00 00 00 00 00）
 *
 *  COM 是整条栈里唯一知道"这 8 个字节里哪几位是车速、怎么换算"的模块。
 *  PduR / CanIf / Can 全都只看到"8 个字节"。
 *
 *  【本层的 4 类职责】
 *    1. 信号 ↔ PDU 的位打包/解包（含跨字节、大小端、符号扩展）
 *    2. 物理值 ↔ 原始值的缩放换算
 *    3. 发送时机控制（周期 / 事件 / 混合）—— 本层引入"时间维度"
 *    4. 接收信号监控（超时、更新位）—— 见 TODO
 *
 *  【第一轮的实现范围（诚实声明）】
 *    ✅ 整数信号：BOOLEAN / UINT8..32 / SINT8..32 / UINT8_N（字节数组）
 *    ✅ 两种字节序：Intel(小端) / Motorola(大端) / OPAQUE(不转换)
 *    ✅ 整数缩放：physical = raw × factor + offset（factor/offset 都是 sint32）
 *    ✅ 三种传输属性 + 四种 TX 模式
 *    ✅ 接收信号超时监控
 *    ❌ 浮点信号（FLOAT32/FLOAT64）—— 物理值统一用 sint32 承载，见下方说明
 *    ❌ 发送重复次数（ComTxModeNumberOfRepetitions）
 *    ❌ I-PDU 组（Com_IpduGroupStart/Stop）
 *    ❌ 分段传输（TP 类型 PDU，属于 CanTp 的领域）
 *================================================================================================*/
#ifndef COM_H
#define COM_H

/*==================================================================================================
 *                                        INCLUDES
 *================================================================================================*/
#include "Std_Types.h"
#include "ComStack_Types.h"     /* PduIdType / PduLengthType / PduInfoType */

/*==================================================================================================
 *                                    DEFINES AND MACROS
 *================================================================================================*/

/** @brief COM 的 AUTOSAR 模块号（SWS_COM 分配） */
#define COM_MODULE_ID                   (0x32U)

/** @brief 单实例模块，实例号固定 0 */
#define COM_INSTANCE_ID                 (0U)

/**
 * @brief 开发错误检测开关
 * @details 优先级：本模块单独定义 > 全局 BSW_DEV_ERROR_DETECT > 默认 STD_ON
 */
#ifndef COM_DEV_ERROR_DETECT
    #ifdef BSW_DEV_ERROR_DETECT
        #define COM_DEV_ERROR_DETECT    (BSW_DEV_ERROR_DETECT)
    #else
        #define COM_DEV_ERROR_DETECT    (STD_ON)
    #endif
#endif

/** @brief COM 的 API 编号（Det 上报时用） */
typedef enum
{
    COM_API_INIT                    = 0U,
    COM_API_DEINIT                  = 1U,
    COM_API_SEND_SIGNAL             = 2U,
    COM_API_RECEIVE_SIGNAL          = 3U,
    COM_API_INVALIDATE_SIGNAL       = 4U,
    COM_API_RX_INDICATION           = 5U,
    COM_API_TX_CONFIRMATION         = 6U,
    COM_API_MAIN_FUNCTION_TX        = 7U,
    COM_API_MAIN_FUNCTION_RX        = 8U,
    COM_API_GET_VERSION_INFO        = 9U
} Com_ApiIdType;

/** @brief COM 的错误编号（Det 上报时用） */
typedef enum
{
    COM_E_PARAM_POINTER         = 0x01U,    /**< 传了 NULL 指针 */
    COM_E_UNINIT                = 0x02U,    /**< 未初始化就调用 API */
    COM_E_PARAM_SIGNAL_ID       = 0x03U,    /**< SignalId 越界 */
    COM_E_PARAM_PDU_ID          = 0x04U,    /**< RxPduId / TxPduId 不在配置表里 */
    COM_E_SIGNAL_DIRECTION      = 0x05U,    /**< 对接收信号调了 SendSignal（或反之） */
    COM_E_UNSUPPORTED_TYPE      = 0x06U,    /**< 该信号类型不支持此操作（如 UINT8_N 读取） */
    COM_E_INVALID_CONFIG        = 0x07U     /**< 容量不足 / 配置表自相矛盾 */
} Com_ErrorIdType;

#define COM_VENDOR_ID                   (43U)
#define COM_SW_MAJOR_VERSION            (1U)
#define COM_SW_MINOR_VERSION            (0U)
#define COM_SW_PATCH_VERSION            (0U)
#define COM_AR_RELEASE_MAJOR_VERSION    (4U)
#define COM_AR_RELEASE_MINOR_VERSION    (4U)

#ifndef COM_VERSION_INFO_API
#define COM_VERSION_INFO_API            (STD_ON)
#endif

/**
 * @brief 主函数调用周期（ms）
 * @details COM 的"时间"是以主函数调用次数计量的：
 *              1 个 tick = 1 次 Com_MainFunctionTx()
 *          `ComTxModeTimePeriod` 配的是"多少个 tick"，不是毫秒。
 *          本工程让 FreeRTOS 任务每 10ms 调一次，所以 1 tick = 10ms。
 *
 *          ⚠️ 改这个值会让所有配置里的"周期 tick 数"含义一起改变。
 */
#define COM_MAIN_FUNCTION_PERIOD_MS     (10U)

/*==================================================================================================
 *                                 STRUCTURES AND OTHER TYPEDEFS
 *================================================================================================*/

/** @brief 信号 ID（= 信号配置表下标） */
typedef uint16 Com_SignalIdType;

/** @brief COM 配置类型前向声明（完整定义在 com_cfg.h） */
struct Com_ConfigType;
typedef struct Com_ConfigType Com_ConfigType;

/**
 * @brief 信号的数据类型（对应 AUTOSAR 的 ComSignalType 子集）
 *
 * 【为什么需要这个枚举】
 * 位宽（BitSize）只说明"占几位"，不说明"怎么解释这几位"：
 *      bit 全 1、8 位 → 是 255 还是 -1？
 *      bit 全 1、16 位 → 是 65535 还是 -1？
 * 所以必须同时知道**类型**和**位宽**。
 *
 * 【UINT8_N 是特殊的】
 * 它不是"整数值"，而是"一段字节数组"（如 VIN 码、软件版本号）。
 * 对这种信号：
 *      BitPosition = **字节偏移**（不是位偏移）
 *      BitSize     = **字节个数**（不是位数）
 *      缩放        = 不适用（必须 factor=1, offset=0）
 *      字节序      = 必须 OPAQUE（逐字节拷贝，不做任何位重排）
 */
typedef enum
{
    COM_SIGNAL_TYPE_BOOLEAN = 0,    /**< 1 位，0/1 */
    COM_SIGNAL_TYPE_UINT8,          /**< 无符号 8 位 */
    COM_SIGNAL_TYPE_UINT16,         /**< 无符号 16 位 */
    COM_SIGNAL_TYPE_UINT32,         /**< 无符号 32 位（注意见下方的精度限制） */
    COM_SIGNAL_TYPE_SINT8,          /**< 有符号 8 位 */
    COM_SIGNAL_TYPE_SINT16,         /**< 有符号 16 位 */
    COM_SIGNAL_TYPE_SINT32,         /**< 有符号 32 位 */
    COM_SIGNAL_TYPE_UINT8_N         /**< 字节数组（不打包，逐字节拷贝） */
} Com_SignalTypeType;

/**
 * @brief 字节序（对应 AUTOSAR 的 ComSignalEndianess）
 *
 * 【两种编号体系的区别 —— 这是 COM 里最容易错的地方】
 *
 *  AUTOSAR 对 LITTLE 和 BIG 用的**位编号规则不同**：
 *
 *  LITTLE（Intel，小端）：线性编号，bit 0 = 字节0 的最低位（LSB）
 *      位号:   0  1  2  3  4  5  6  7 | 8  9 10 11 ...
 *      字节:  └────── 字节0 ──────┘ └── 字节1 ──
 *              LSB              MSB
 *      信号的 MSB 在**高位号**一侧
 *
 *  BIG（Motorola，大端）：锯齿编号，bit 0 = 字节0 的**最高位**（MSB）
 *      位号:   0  1  2  3  4  5  6  7 | 8  9 10 11 ...
 *      字节:  └────── 字节0 ──────┘ └── 字节1 ──
 *              MSB              LSB
 *      信号的 MSB 在 **ComBitPosition 处**，向位号增大方向延伸到 LSB
 *
 *  所以同一个 ComBitPosition 在两种字节序下指向**物理上相反的位置**。
 *  配置时必须和 DBC/通信矩阵严格对应，否则数据会"看起来对、数值不对"。
 */
typedef enum
{
    COM_ENDIAN_LITTLE = 0,  /**< Intel：bit0 = 字节0 的 LSB */
    COM_ENDIAN_BIG,         /**< Motorola：bit0 = 字节0 的 MSB */
    COM_ENDIAN_OPAQUE       /**< 不做位重排（UINT8_N 专用） */
} Com_EndianType;

/**
 * @brief 传输属性（对应 AUTOSAR 的 ComTransferProperty）
 * @details 决定"写这个信号会不会触发发送"
 */
typedef enum
{
    COM_TRANSFER_PENDING = 0,           /**< 只更新影子，等周期发送 */
    COM_TRANSFER_TRIGGERED,             /**< 每次写都触发一次发送 */
    COM_TRANSFER_TRIGGERED_ON_CHANGE    /**< 只有值变化才触发 */
} Com_TransferPropertyType;

/**
 * @brief I-PDU 的发送模式（对应 AUTOSAR 的 ComTxModeMode）
 */
typedef enum
{
    COM_TX_MODE_NONE = 0,   /**< 不主动发（只能被 PduR 的请求带动，或纯接收 PDU） */
    COM_TX_MODE_PERIODIC,   /**< 周期发送，不管信号变没变 */
    COM_TX_MODE_DIRECT,     /**< 事件驱动：有 txRequested 才发 */
    COM_TX_MODE_MIXED       /**< 周期 + 事件，谁先到算谁 */
} Com_TxModeType;

/*==================================================================================================
 *                                 GLOBAL FUNCTION PROTOTYPES
 *================================================================================================*/

/**
 * @brief 初始化 COM（SWS_COM 的 Com_Init）
 * @details 做四件事：
 *          1. 保存配置指针
 *          2. 按配置初始化所有信号的影子值（从 InitValue 换算成 raw 并打包进 TX 影子缓冲）
 *          3. 派生反向索引（信号 → 所属 PDU）
 *          4. 清零所有运行时状态（计时器、更新标志、超时计数）
 *
 * 【调用顺序】
 *      Can_Init → CanIf_Init → PduR_Init → **Com_Init** → 建任务 → 启动调度器
 */
void Com_Init(const Com_ConfigType* Config);

/** @brief 反初始化（SWS_COM 的 Com_DeInit） */
void Com_DeInit(void);

/**
 * @brief 写一个信号（SWS_COM 的 Com_SendSignal）
 *
 * @param[in] SignalId       信号 ID（配置表下标）
 * @param[in] SignalDataPtr  **物理值**指针。类型必须与配置的 Com_SignalTypeType 一致：
 *                              BOOLEAN → boolean*
 *                              UINT8   → uint8*      SINT8  → sint8*
 *                              UINT16  → uint16*     SINT16 → sint16*
 *                              UINT32  → uint32*     SINT32 → sint32*
 *                              UINT8_N → uint8*（指向 BitSize 个字节）
 *
 * @return E_OK      写入成功（并已按传输属性决定要不要触发发送）
 * @return E_NOT_OK  未初始化 / SignalId 越界 / 该信号属于接收 PDU / 空指针
 *
 * 【本函数做了什么】
 *      1. 物理值 → 原始值（除以缩放、减偏移、四舍五入、按位宽钳位）
 *      2. 把原始值按位写进该 PDU 的**影子缓冲**（立即生效，不等主函数）
 *      3. 按传输属性置位 `txRequested`（真正发出去要等 Com_MainFunctionTx）
 *
 * 【本函数没做什么 —— 重要】
 *      它**不会**立刻发到总线上。即使 TransferProperty = TRIGGERED，
 *      也只是标记"要发"，实际发送发生在下一次 Com_MainFunctionTx。
 *      这样做的原因：多个信号可能在同一周期内先后被写，必须攒齐了整包发。
 *
 * 【调用上下文】任务上下文。不要在中断里调。
 */
Std_ReturnType Com_SendSignal(Com_SignalIdType SignalId, const void* SignalDataPtr);

/**
 * @brief 读一个信号（SWS_COM 的 Com_ReceiveSignal）
 *
 * @param[out] SignalDataPtr  **物理值**的输出缓冲，类型规则同 Com_SendSignal
 *
 * @return E_OK / E_NOT_OK
 *
 * 【数据从哪来】
 *      Com_RxIndication()（在 CAN 中断里）已经把收到的 PDU 解包成各信号的物理值，
 *      存在信号影子值里。本函数只是把这个值拷出来，不做任何位运算。
 *      所以本函数很快，可以在任意任务里调用。
 *
 * 【调用上下文】任务上下文。
 */
Std_ReturnType Com_ReceiveSignal(Com_SignalIdType SignalId, void* SignalDataPtr);

/**
 * @brief 把一个接收信号标记为失效（SWS_COM 的 Com_InvalidateSignal）
 * @details 按 SWS，失效后 Com_ReceiveSignal 应返回该信号的"替代值"。
 *          本实现用 InitValue 作为替代值。
 */
Std_ReturnType Com_InvalidateSignal(Com_SignalIdType SignalId);

/*--------------------------------------------------------------------------------------------------
 *                        以下两个被 PduR 调用（SWS 规定的名字）
 *------------------------------------------------------------------------------------------------*/

/**
 * @brief 收到一帧 I-PDU（被 PduR 调用）
 *
 * @param[in] RxPduId     **COM 自己的 RxPduId**（不是 CanIf 的！PduR 已翻译过）
 * @param[in] PduInfoPtr  数据（**零拷贝**指针，指向 MCAL 的接收缓冲）
 *
 * 【本函数做的事】
 *      1. 按 RxPduId 找到 RX PDU
 *      2. 长度校验（收到的长度 < 配置长度时按 SWS 拒绝并返回）
 *      3. 把整包字节拷进 RX 影子缓冲
 *      4. 逐信号解包：位提取 → 符号扩展 → 缩放 → 存进信号影子值
 *      5. 重置该 PDU 下所有信号的超时计数
 *
 * 【调用上下文：中断！】
 *      CAN 接收中断 → CanIf → PduR → 这里。
 *      所以：不能阻塞、不能调 FreeRTOS 非 FromISR API。
 *      信号影子值用 32 位对齐的 sint32 存储，Cortex-M 的单次对齐访问是原子的，
 *      所以任务里读、中断里写不会撕裂。
 *
 * 【零拷贝】PduInfoPtr->SduDataPtr 指向 MCAL 的缓冲，本函数返回后就可能被覆盖。
 *      所以必须在本次调用内拷走（第 3 步就是拷贝）。
 */
void Com_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr);

/**
 * @brief 一帧发送完成（被 PduR 调用）
 *
 * @param[in] TxPduId  **COM 自己的 TxPduId**（PduR 已把 CanIf 的编号翻译过来）
 * @param[in] result   发送结果
 *
 * 【本函数做的事】
 *      清除该 PDU 的 txInProgress 标志，允许下一次发送。
 *      若配了重复发送（本轮未实现），这里会启动重发计数。
 *
 * ⚠️ 1:N 扇出时本函数会被调用**多次**（每个目标一次），TxPduId 相同。
 *    本实现采用 PduRTransmissionConfirmation = FALSE 的语义：
 *    第一次确认就把 txInProgress 清掉，后续的确认是重复的（幂等，无害）。
 */
void Com_TxConfirmation(PduIdType TxPduId, Std_ReturnType result);

/*--------------------------------------------------------------------------------------------------
 *                                  主函数（周期调用）
 *------------------------------------------------------------------------------------------------*/

/**
 * @brief 发送主函数（SWS_COM 的 Com_MainFunctionTx）
 * @details 遍历所有 TX PDU，按各自的传输模式决定这轮要不要发：
 *            PERIODIC : 周期计数到 → 发
 *            DIRECT   : txRequested → 发
 *            MIXED    : 两者任一 → 发
 *          真正发送 = 把影子缓冲交给 PduR_ComTransmit()。
 *
 * 【调用周期】由 FreeRTOS 任务每 COM_MAIN_FUNCTION_PERIOD_MS 调一次。
 * 【调用上下文】任务上下文。
 */
void Com_MainFunctionTx(void);

/**
 * @brief 接收主函数（SWS_COM 的 Com_MainFunctionRx）
 * @details 检查所有接收信号的超时：
 *            超过 ComTimeoutTicks 个 tick 没更新 → 用 InitValue 替换。
 *
 * 【为什么接收也需要主函数】
 *      超时监控是"时间"相关的，只能在周期任务里判断。
 *      数据的解包在中断里已经完成了，这里只做时间检查。
 */
void Com_MainFunctionRx(void);

/** @brief 读取版本信息（SWS_COM 的 Com_GetVersionInfo） */
#if (COM_VERSION_INFO_API == STD_ON)
void Com_GetVersionInfo(Std_VersionInfoType* versioninfo);
#endif

#endif /* COM_H */
