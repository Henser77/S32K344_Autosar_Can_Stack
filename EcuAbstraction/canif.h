/*==================================================================================================
 *  EcuAbstraction/canif.h —— ECU 抽象层 CanIf 对外接口
 *
 *  【本层在架构中的位置】
 *
 *      PduR（服务层）
 *        ↑ ↓
 *      CanIf（本层）   ← 把"PDU"翻译成"硬件句柄 + ID + 长度"
 *        ↑ ↓
 *      Can（MCAL）     ← 把"硬件句柄 + ID + 长度"翻译成"邮箱操作"
 *        ↑ ↓
 *      FlexCAN 硬件
 *
 *  【本层职责】
 *   1. PDU ID ↔ 硬件句柄（Hth / Hrh）的双向映射
 *   2. 发送：CanIf_Transmit() 查表 → Can_Write()
 *   3. 接收：被 Can 驱动回调 → 查表得 PduId → 上报 PduR
 *   4. 发送确认：被 Can 驱动回调 → 查表得 PduId → 上报 PduR
 *   5. 控制器模式转发（CanIf_SetControllerMode → Can_SetControllerMode）
 *   6. PDU 通道模式（TX_OFFLINE / TX_ONLINE）
 *   7. Bus-off / 错误状态向上转发
 *
 *  【本文件不包含 RTD / MCAL 实现细节】
 *  只依赖 AUTOSAR 标准类型：
 *    Std_Types.h            uint8 / E_OK / E_NOT_OK / NULL_PTR / TRUE / FALSE
 *    ComStack_Types.h       PduIdType / PduLengthType / PduInfoType
 *    Can_GeneralTypes.h     Can_HwHandleType / Can_PduType / Can_ControllerStateType
 *                           / Can_ErrorStateType
 *
 *  **不 include "can.h"** —— 上层不需要看到 Can_Write 之类的驱动 API，
 *  只需要数据类型。这样将来换 CAN 驱动（甚至换成 CAN XL）时，
 *  上层代码一行不用改。
 *================================================================================================*/
#ifndef CANIF_H
#define CANIF_H

/*==================================================================================================
 *                                        INCLUDES
 *================================================================================================*/
#include "Std_Types.h"
#include "ComStack_Types.h"     /* PduIdType / PduLengthType / PduInfoType */
#include "Can_GeneralTypes.h"   /* Can_HwHandleType / Can_PduType / Can_ControllerStateType
                                    / Can_ErrorStateType */

/*==================================================================================================
 *                    模块标识与开发错误检测（Det）配置
 *
 *  【这是 AUTOSAR 每个 BSW 模块头文件的标配段落】
 *  AUTOSAR 规定每个模块必须在自己的头文件里声明四样东西：
 *    1. <MOD>_MODULE_ID         标准模块号 —— 全联盟统一，**不能自己编**
 *    2. <MOD>_INSTANCE_ID       本实例号 —— 同一模块有多个实例时区分
 *    3. <MOD>_DEV_ERROR_DETECT  开发错误检测开关 —— 开发期 ON，量产 OFF
 *    4. <MOD>_ApiIdType / <MOD>_ErrorIdType —— 哪个 API 出了什么错
 *
 *  【为什么模块号不能自己编】
 *  这个词要写进错误记录、被调试工具解析、被跨公司的人看懂。
 *  标准值：Det=15、Com=50、PduR=53、CanIf=60、Can=80。
 *================================================================================================*/

/** @brief CanIf 的标准 AUTOSAR 模块号（0x3C = 60，来自 AUTOSAR 规范） */
#define CANIF_MODULE_ID                     (0x3CU)

/** @brief 单实例模块，实例号固定 0 */
#define CANIF_INSTANCE_ID                   (0U)

/**
 * @brief 开发错误检测开关
 *
 * @details 优先级（从高到低）：
 *            ① 本模块单独定义  CANIF_DEV_ERROR_DETECT
 *            ② 全局开关        BSW_DEV_ERROR_DETECT（由编译选项 -D 提供）
 *            ③ 默认值          STD_ON
 *
 * 【为什么这样写：靠编译选项一次切换全部模块，不改源码】
 *   Debug 构建：  （不加任何 -D）                    → 全部走默认 STD_ON
 *   Release 构建：-DBSW_DEV_ERROR_DETECT=STD_OFF     → 全部变成 STD_OFF
 *
 *  这样源码里永远是"开发模式"，量产配置只存在于构建配置文件中，
 *  不会出现"手改了源码忘记改回来"的事故。
 */
#ifndef CANIF_DEV_ERROR_DETECT
    #ifdef BSW_DEV_ERROR_DETECT
        #define CANIF_DEV_ERROR_DETECT      (BSW_DEV_ERROR_DETECT)
    #else
        #define CANIF_DEV_ERROR_DETECT      (STD_ON)
    #endif
#endif

/**
 * @brief CanIf 的 API 编号
 * @details 告诉 Det"是哪个函数出的错"。编号在本模块内唯一即可，
 *          但建议照 AUTOSAR SWS 的顺序排，方便对照文档。
 */
typedef enum
{
    CANIF_API_INIT                  = 0U,
    CANIF_API_TRANSMIT              = 1U,
    CANIF_API_RX_INDICATION         = 2U,
    CANIF_API_TX_CONFIRMATION       = 3U,
    CANIF_API_SET_CONTROLLER_MODE   = 4U,
    CANIF_API_GET_CONTROLLER_MODE   = 5U,
    CANIF_API_GET_VERSION_INFO      = 6U
} CanIf_ApiIdType;

/**
 * @brief CanIf 的错误编号
 * @details 告诉 Det"是什么错"。ErrorId 的**具体含义由本模块自己定义**，
 *          AUTOSAR 只规定了取值范围（0x01..0xFF）和大类习惯。
 */
typedef enum
{
    CANIF_E_PARAM_POINTER       = 0x01U,    /**< 传了 NULL 指针 */
    CANIF_E_UNINIT              = 0x02U,    /**< 未初始化就调用 API */
    CANIF_E_PARAM_PDU_ID        = 0x03U,    /**< PDU ID 在配置表里找不到 */
    CANIF_E_PARAM_LENGTH        = 0x04U,    /**< 长度超过配置值或本层上限 */
    CANIF_E_PARAM_HRH           = 0x05U,    /**< Hrh 越界（配置写错） */
    CANIF_E_PARAM_CONTROLLER_ID = 0x06U,    /**< 控制器号越界 */
    CANIF_E_TX_OFFLINE          = 0x07U,    /**< PDU 通道离线，拒绝发送 */
    CANIF_E_INVALID_CONFIG      = 0x08U     /**< 配置表内部自相矛盾 */
} CanIf_ErrorIdType;

/*==================================================================================================
 *                                 STRUCTURES AND OTHER TYPEDEFS
 *================================================================================================*/

/**
 * @brief CanIf 配置类型前向声明（完整定义在 canif_cfg.h）
 *
 * 同 can.h 里 Can_ConfigType 的写法：只声明、不定义。
 * 上层调用 CanIf_Init() 时不必知道配置表结构。
 */
struct CanIf_ConfigType;
typedef struct CanIf_ConfigType CanIf_ConfigType;

/**
 * @brief PDU 通道模式（对应 AUTOSAR CanIf_PduModeType）
 *
 * 【这是什么，和控制器模式有什么区别】
 *   控制器模式（CAN_CS_STARTED / STOPPED）管的是**硬件**：控制器是否在总线上。
 *   PDU 通道模式管的是**软件**：这个控制器上的 PDU 允不允许发送。
 *
 * 典型用途：诊断会话切换时，禁止应用报文发送，但总线保持在线
 * （这样诊断报文还能收发）—— 这时就是把 PDU 模式切到 TX_OFFLINE，
 * 而不是把控制器停掉。
 *
 *     CANIF_TX_ONLINE    正常收发
 *     CANIF_TX_OFFLINE   照常接收，但 CanIf_Transmit() 直接拒绝
 *     CANIF_OFFLINE      收发都停（接收方向不发往上层）
 */
typedef enum
{
    CANIF_OFFLINE = 0,      /**< 收发都关闭 */
    CANIF_TX_OFFLINE,       /**< 只关发送，接收照常 */
    CANIF_TX_ONLINE         /**< 正常 */
} CanIf_PduModeType;

/*==================================================================================================
 *                        向上层（PduR）的接口 —— 与 MCAL 的 hook 风格保持一致
 *
 * 【为什么用 hook 注册，而不是直接调 PduR_CanIfRxIndication()】
 * 直接调用会让 CanIf **链接期依赖** PduR，于是 PduR 没写完时 CanIf 无法单独编译测试。
 * 用注册机制，CanIf 可以脱离 PduR 独立验证（和现在 main.c 验证 MCAL 的做法一样）。
 *
 * 等 PduR 就位后，PduR_Init() 里注册一次即可：
 *     CanIf_RegisterRxIndicationHook(PduR_CanIfRxIndication);
 *     CanIf_RegisterTxConfirmationHook(PduR_CanIfTxConfirmation);
 *
 * ⚠️ 零拷贝约定（这是 CanIf 与上层之间最重要的契约）
 *    RxIndication 里的 PduInfo->SduDataPtr 指向的是 **MCAL 的静态接收缓冲**，
 *    该缓冲在本次中断返回后随时可能被下一帧覆盖。
 *    所以上层**不得保存这个指针**，必须在本函数返回之前把数据拷走或处理完。
 *    整条链（Can ISR → CanIf → PduR → COM）是同步调用的，指针在返回前一直有效。
 *================================================================================================*/

/** @brief 收到一帧报文（→ PduR_CanIfRxIndication） */
typedef void (*CanIf_RxIndicationHookType)(PduIdType RxPduId, const PduInfoType* PduInfo);

/** @brief 一帧发送完成（→ PduR_CanIfTxConfirmation） */
typedef void (*CanIf_TxConfirmationHookType)(PduIdType TxPduId, Std_ReturnType Result);

/** @brief 控制器进入 Bus-off（→ PduR / Dem） */
typedef void (*CanIf_ControllerBusOffHookType)(uint8 ControllerId);

/** @brief 控制器错误状态迁移（→ Dem） */
typedef void (*CanIf_ControllerErrorStateHookType)(uint8 ControllerId,
                                                   Can_ErrorStateType OldState,
                                                   Can_ErrorStateType NewState);

void CanIf_RegisterRxIndicationHook(CanIf_RxIndicationHookType hook);
void CanIf_RegisterTxConfirmationHook(CanIf_TxConfirmationHookType hook);
void CanIf_RegisterControllerBusOffHook(CanIf_ControllerBusOffHookType hook);
void CanIf_RegisterControllerErrorStateHook(CanIf_ControllerErrorStateHookType hook);

/*==================================================================================================
 *                                 GLOBAL FUNCTION PROTOTYPES
 *================================================================================================*/

/**
 * @brief 初始化 CanIf
 * @details 做三件事：
 *          1. 保存配置指针
 *          2. 建立 Hrh → RxPdu 的索引（O(1) 查表用）
 *          3. 把本层的四个函数注册到 MCAL 的 hook 上
 *
 *          ⚠️ 调用顺序必须是：Can_Init() → CanIf_Init() → Can_SetControllerMode(STARTED)
 *          因为 CanIf_Init 会往 MCAL 注册回调，必须在控制器启动之前完成，
 *          否则第一帧可能到了但回调还没装上。
 */
void CanIf_Init(const CanIf_ConfigType* Config);

/**
 * @brief 发送一个 PDU（对应 AUTOSAR SWS_CanIf_Transmit）
 *
 * @param[in] TxPduId     逻辑发送 PDU 编号（本层配置表的下标）
 * @param[in] PduInfoPtr  待发数据（只读，本层不修改）
 *
 * @return E_OK          已交给硬件发送
 * @return CAN_BUSY      硬件邮箱被上一次发送占用，稍后重试（**不是错误**）
 * @return E_NOT_OK      参数非法 / 未初始化 / PDU 通道离线 / 长度超限
 *
 * ⚠️ CAN_BUSY 是叠加返回值（SWS_Can_00039 的同类机制）。
 *    调用方必须区分"拥塞"和"真失败"，不能把两者当同一件事处理。
 */
Std_ReturnType CanIf_Transmit(PduIdType TxPduId, const PduInfoType* PduInfoPtr);

/*--------------------------------------------------------------------------------------------------
 * 以下四个函数是**被 MCAL 主动调用的**（通过 Can_RegisterXxxHook 注册），
 * 不是给上层调用的。签名必须与 MCAL 的 hook 类型完全一致。
 *------------------------------------------------------------------------------------------------*/

/** @brief MCAL 收到一帧有效报文时回调本函数 */
void CanIf_RxIndication(Can_HwHandleType Hrh, const Can_PduType* PduInfo);

/** @brief MCAL 一帧发送完成时回调本函数 */
void CanIf_TxConfirmation(Can_HwHandleType Hth);

/** @brief MCAL 报告控制器进入 Bus-off */
void CanIf_ControllerBusOff(uint8 ControllerId);

/** @brief MCAL 报告控制器错误状态迁移 */
void CanIf_ControllerErrorState(uint8 ControllerId,
                                Can_ErrorStateType OldState,
                                Can_ErrorStateType NewState);

/*--------------------------------------------------------------------------------------------------
 * 控制器模式（转发给 MCAL）
 *------------------------------------------------------------------------------------------------*/
Std_ReturnType CanIf_SetControllerMode(uint8 ControllerId,
                                       Can_ControllerStateType ControllerMode);

Std_ReturnType CanIf_GetControllerMode(uint8 ControllerId,
                                       Can_ControllerStateType* ControllerModePtr);

/*--------------------------------------------------------------------------------------------------
 * PDU 通道模式
 *------------------------------------------------------------------------------------------------*/
Std_ReturnType CanIf_SetPduMode(uint8 ControllerId, CanIf_PduModeType PduModeRequest);

Std_ReturnType CanIf_GetPduMode(uint8 ControllerId, CanIf_PduModeType* PduModePtr);

#endif /* CANIF_H */
