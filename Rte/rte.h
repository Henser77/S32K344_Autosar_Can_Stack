/*==================================================================================================
 *  Rte/rte.h —— RTE（Runtime Environment）公共接口
 *
 *  【RTE 在 AUTOSAR 里的位置】
 *
 *      应用 SWC（手写）
 *          ↕  Rte_Write_<Port>_<Data>/ Rte_Read_<Port>_<Data>   ← 应用只看到这个
 *      RTE（生成物）
 *          ↕  Com_SendSignal / Com_ReceiveSignal
 *      COM
 *
 *  【RTE 到底做什么 —— 三件事】
 *      1. **名字映射**：把"端口.数据元"翻译成 COM 的信号 ID（本文件）
 *      2. **Runnable 触发**：按事件/周期调用 SWC 的 runnable（本工程由 OS 任务直接调）
 *      3. **数据一致性**：多个 runnable 访问同一数据元时的保护（本工程未实现）
 *
 *  【⚠️ 重要：真实 RTE 是生成器产出的，不是手写的】
 *  本目录下的 `rte_*.c` 是**手工模拟生成器的输出**，目的有两个：
 *      - 让应用层真的做到"不知道 COM 存在"
 *      - 让你看清 RTE 的内容有多"薄"——它几乎只有转发
 *
 *  真实项目里，这些函数由 RTE 生成器根据 ARXML 里的 PortPrototype /
 *  DataElement / RunnableEntity 定义自动生成，开发者一行都不写。
 *================================================================================================*/
#ifndef RTE_H
#define RTE_H

#include "Std_Types.h"

/**
 * @brief RTE 初始化（对应 AUTOSAR 的 Rte_Start）
 *
 * @details 真实 RTE 会在这里：
 *          - 初始化端口的数据缓冲
 *          - 注册接收回调（让 COM 收到数据时通知 RTE）
 *          - 初始化模式管理
 *
 *          本工程是"拉"模式（应用主动读），所以这里几乎没事可做。
 *          保留这个函数是为了把"RTE 也有生命周期"这件事表达出来。
 */
void Rte_Init(void);

#endif /* RTE_H */
