/*==================================================================================================
 *  Rte/rte_doorstatus.h —— DoorStatus 端口的 RTE 接口
 *
 *  【这个文件模拟的是 RTE 生成器为"DoorStatus 端口"产出的头文件】
 *
 *  AUTOSAR 的端口命名规则：
 *      Rte_Write_<PortName>_<DataElementName>()   发送（S-Port）
 *      Rte_Read_<PortName>_<DataElementName>()    接收（R-Port）
 *
 *  【应用层看到的世界就到这里为止】
 *  应用代码里不会出现 COM_SIG_xxx、不会出现 Com_SendSignal、
 *  不会出现 PduId、不会出现 CAN ID。
 *  它只知道"有个 DoorStatus 端口，里面有个 WindowPos 数据元"。
 *================================================================================================*/
#ifndef RTE_DOORSTATUS_H
#define RTE_DOORSTATUS_H

#include "Std_Types.h"

/*==================================================================================================
 *                          R-Port：从总线**接收**的数据元
 *
 *  这些数据来自 RX PDU 0（CAN0 / ID 0x100），由 COM 解包后存在信号影子里。
 *  Rte_Read 只是把影子值拷出来。
 *================================================================================================*/

/** @brief 读取：车门是否打开（TRUE = 开） */
Std_ReturnType Rte_Read_DoorStatus_DoorOpen(boolean* data);

/** @brief 读取：车窗目标位置（0..100 %） */
Std_ReturnType Rte_Read_DoorStatus_WindowPos(uint8* data);

/** @brief 读取：门锁状态（0 = 未锁，1 = 锁定） */
Std_ReturnType Rte_Read_DoorStatus_LockState(uint8* data);

/** @brief 读取：车速（物理单位 0.01 km/h，6000 = 60.00 km/h） */
Std_ReturnType Rte_Read_DoorStatus_VehicleSpeed(uint16* data);

/*==================================================================================================
 *                          S-Port：发往**总线**的数据元
 *
 *  这些数据会进入 TX PDU 0（CAN0 / ID 0x100），由 COM 打包后按周期发送。
 *  Rte_Write 只是转发给 Com_SendSignal，**不立即发送**。
 *================================================================================================*/

/** @brief 写入：车门是否打开 */
Std_ReturnType Rte_Write_DoorStatus_DoorOpen(boolean data);

/** @brief 写入：车窗实际位置（0..100 %） */
Std_ReturnType Rte_Write_DoorStatus_WindowPos(uint8 data);

/** @brief 写入：门锁状态 */
Std_ReturnType Rte_Write_DoorStatus_LockState(uint8 data);

/** @brief 写入：车速 */
Std_ReturnType Rte_Write_DoorStatus_VehicleSpeed(uint16 data);

#endif /* RTE_DOORSTATUS_H */
