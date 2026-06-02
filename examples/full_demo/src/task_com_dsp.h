#ifndef __TASK_COM_DSP_H__
#define __TASK_COM_DSP_H__

/* DSP 模块专属消息 ID */
typedef enum {
    COM_DSP_MSG_NONE = 0,
    COM_DSP_MSG_MODE_NORMAL,       /* 切换到正常轮询模式 */
    COM_DSP_MSG_MODE_PASSTHROUGH,  /* 切换到透传模式 */
    COM_DSP_MSG_MODE_UPDATE,       /* 切换到固件升级模式 */
    COM_DSP_MSG_UPGRADE_FRAME,     /* 升级数据帧（透传给 DSP） */
    COM_DSP_MSG_UPGRADE_DONE,      /* 升级完成，切回 NORMAL */
} com_dsp_msg_id_e;

void task_com_dsp_entry(void *param);

#endif
