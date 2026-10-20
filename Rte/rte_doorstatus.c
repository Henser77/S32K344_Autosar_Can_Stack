/*==================================================================================================
 *  Rte/rte_doorstatus.c —— DoorStatus 端口的 RTE 实现
 *
 *  【这个文件的内容有多"薄"】
 *  每个函数只有一行：把 Rte_* 的参数转给 Com_*。
 *
 *  真实 RTE 生成器产出的代码也就这么薄 —— 它的复杂性不在逻辑，
 *  而在"从 ARXML 里推导出这些映射"这件事上。
 *
 *  【注意：这里用 ComConf_* 宏，不用 COM_SIG_* 枚举】
 *      ComConf_ComSignal_WindowPos  是"对外契约"（AUTOSAR 标准命名）
 *      COM_SIG_WINDOW_POS           是"内部编号"（只有 COM 自己该知道）
 *  应用侧只用前者。
 *================================================================================================*/

#include "rte_doorstatus.h"
#include "com.h"
#include "com_cfg.h"        /* ComConf_* 宏 + Com_Config */

/*==================================================================================================
 *                              R-Port：读
 *================================================================================================*/
Std_ReturnType Rte_Read_DoorStatus_DoorOpen(boolean* data)
{
    return Com_ReceiveSignal(ComConf_ComSignal_RxDoorOpen, data);
}

Std_ReturnType Rte_Read_DoorStatus_WindowPos(uint8* data)
{
    return Com_ReceiveSignal(ComConf_ComSignal_RxWindowPos, data);
}

Std_ReturnType Rte_Read_DoorStatus_LockState(uint8* data)
{
    return Com_ReceiveSignal(ComConf_ComSignal_RxLockState, data);
}

Std_ReturnType Rte_Read_DoorStatus_VehicleSpeed(uint16* data)
{
    return Com_ReceiveSignal(ComConf_ComSignal_RxVehicleSpeed, data);
}

/*==================================================================================================
 *                              S-Port：写
 *
 *  ⚠️ 这些调用**不会立刻发到总线上**。
 *     Com_SendSignal 只更新影子缓冲，真正发送由 Com_MainFunctionTx 按 PDU 的
 *     发送模式决定（TX PDU 0 配的是 PERIODIC 100ms）。
 *================================================================================================*/
Std_ReturnType Rte_Write_DoorStatus_DoorOpen(boolean data)
{
    return Com_SendSignal(ComConf_ComSignal_DoorOpen, &data);
}

Std_ReturnType Rte_Write_DoorStatus_WindowPos(uint8 data)
{
    return Com_SendSignal(ComConf_ComSignal_WindowPos, &data);
}

Std_ReturnType Rte_Write_DoorStatus_LockState(uint8 data)
{
    return Com_SendSignal(ComConf_ComSignal_LockState, &data);
}

Std_ReturnType Rte_Write_DoorStatus_VehicleSpeed(uint16 data)
{
    return Com_SendSignal(ComConf_ComSignal_VehicleSpeed, &data);
}
