#ifdef __RL78__
#include "r_cg_userdefine.h"
#else
#ifdef _WIN32
#include "vs/win_rapper.h"
#else
#include <sys/types.h>
#define uint8_t		u_int8_t
#define uint16_t	u_int16_t
#define uint32_t	u_int32_t
#ifndef BOOL
#define BOOL int
#define TRUE 1
#define FALSE 0
#endif
#endif
#endif
#define FILESYSTEM	//�t�@�C���V�X�e�������Ȃ������Ŏg�p����ꍇ�R�����g�A�E�g
//#define IS_USE_SPI //SPI�g�p����ꍇ�R�����g�A�E�g

//�ȉ���3�֐��̓V�X�e�����ŗp�ӂ��Ă�������
#ifdef __cplusplus
extern "C"{
#endif
uint32_t get_syscount();	//�V�X�e��������ms�P�ʂŎ擾
int uart_getc();			//�|�[�g����1�o�C�g�擾�@�f�[�^�Ȃ��ꍇ��-1��Ԃ�
void uart_putc(char c);		//�|�[�g����1�o�C�g�o��

#define COMMERR (-1)
#define ERROR_TIMEOUT	0x01
#define ERROR_STX		0x02
#define ERROR_OVERFLOW	0x04
#define ERROR_SUM		0x08
#define ERROR_NAK		0x10

//�e�L�X�g�������[�̒���(80����+�I�[1)
#define LT_TEXT_LENGTH 81


#ifdef __RL78__
#define FAR __far
#else
#define FAR
#endif
typedef struct _ltmes_callback{
	const char FAR *mes;
	void (FAR *func)(void);
} ltMes_Callback;


void ResetOut(BOOL sta);
BOOL Is_Gop_Ready(void);
BOOL Is_SetData(void);
void LtSetMessageCallback(ltMes_Callback FAR *p);
uint8_t _get_error_code();

BOOL LtMemWrite(char FAR *memname,int32_t val);
BOOL LtTMemWrite(char FAR *memname, int8_t *val);
BOOL LtMemArrayWrite(char FAR *memname,int16_t num,int32_t FAR vals[]);
BOOL LtMemVaListWrite(char FAR *memname,int16_t num,...);
BOOL LtMemRead(char FAR *memname,int32_t FAR *pval);
BOOL LtTMemRead(char FAR *memname, char FAR *pval);
BOOL LtMemArrayRead(char FAR *memname,int16_t num,int32_t FAR vals[]);
BOOL LtGMemWrite(char FAR *memname,int16_t offset,int16_t num,uint8_t FAR vals[]);
BOOL LtGMemRead(char FAR *memname,int16_t offset,int16_t num,uint8_t FAR vals[]);
const char FAR *LtSendCommand(char FAR *command,char FAR *buf,int buf_size);

const char FAR *LtEnq(char FAR *rcv);
#ifdef IS_USE_SPI
BOOL SPI_LtMemArrayWrite(char FAR *memname,int32_t num,int32_t FAR vals[]);
BOOL SPI_LtMemArrayRead(char FAR *memname,int16_t num,int32_t FAR vals[]);
BOOL SPI_LtTMemWrite(char FAR *memname, uint8_t FAR vals[]);
BOOL SPI_LtTMemRead(char FAR *memname, uint8_t FAR vals[]);
BOOL SPI_LtGMemWrite(char FAR *memname,int16_t offset,int16_t num,uint8_t FAR vals[]);
BOOL SPI_LtGMemRead(char FAR *memname,int16_t offset,int16_t num,uint8_t FAR vals[]);
const char FAR *SPI_LtEnq(char FAR *rcv);
#endif

#ifdef FILESYSTEM
int RamUpload(char *srcname, int blockno, int packsize);
BOOL DoTransfarDataSer(char *pathname);
#endif

#ifdef __cplusplus
}
#endif