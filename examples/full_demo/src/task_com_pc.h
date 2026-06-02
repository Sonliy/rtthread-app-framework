#ifndef __TASK_COM_PC_H__
#define __TASK_COM_PC_H__

/* RS485 模块专属消息 ID */
typedef enum {
    COM_PC_MSG_NONE = 0,
    COM_PC_MSG_MODE_NORMAL,       /* 切换到正常通信模式 */
    COM_PC_MSG_MODE_PASSTHROUGH,  /* 切换到透传模式 */
    COM_PC_MSG_SEND_DATA,         /* 主动发送数据 */
    COM_PC_MSG_TEST_LOOPBACK,     /* 自测：回环收发 */
} com_pc_msg_id_e;

void task_com_pc_entry(void *param);

#endif
