/*�C������
	2018-6-13
		�E��M�҂��̃^�C���A�E�g���Ԃɑ��M�d�������l������悤�C��
		�EUART�ł̃O���t�������A�N�Z�X��*NG->*NI�ɕύX�B(�ʐM�ʌ��炷����)
	2018-9-27
		�E�e�L�X�g�������[�A�N�Z�X�p�̊֐��Q�ǉ�
			LtTMemWrite/Read�SPI_LtTMemWrite/Read
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef __RL78__
#include "iodefine.h"
#include "r_cg_macrodriver.h"
#include "r_cg_serial.h"
#include "r_cg_port.h"
#include "r_cg_userdefine.h"
#define xfer_spi_R xfer_spi
#define xfer_spi_W xfer_spi
#else
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <dirent.h>
#include <unistd.h>
#define _stat stat
#endif
#endif
#include "goplt_if.h"


/*
	�萔��`
*/

//�g�p���ɂ��ݒ�ύX�\

//LtMemArrayRead�ŁA�����ɓǍ��\�ȃ������[�̍ő吔���`(�ő�128)
//�{�l��1���₷�s�xLtMemArrayRead�ł̎g�p�X�^�b�N�T�C�Y��4�o�C�g�������܂�
#define MAX_NUM 16
//LtGMemRead�R�}���h�ŁA�Ǎ��\�ȃO���t����ۯĐ����`(�ő�480)
//�{�l��1���₷�s�xLtGMemRead�ł̎g�p�X�^�b�N�T�C�Y��1�o�C�g�������܂�
#define MAX_PLOT 480
//�^�C���A�E�g�l(�P��ms)
#ifdef __RL78__
#define TIMEOUT 100
#else
#define TIMEOUT 500	//WINDOWS���ł̓��C�e���V���߂̂��߃^�C���A�E�g���߂ɐݒ�
#endif

//�z�肵����ő�̃������[���o�C�g��
#define MAX_MEMNAME_SIZE 32
//�z�肵����ő�̃��b�Z�[�W���o�C�g��
#define MAX_MESSAGE_SIZE 32
//�ȉ��͕ύX���Ȃ��ł�������
//�p�P�b�g�̃x�[�X�T�C�Y
#define PACKET_INFO_SIZE	12
//�z�肵����f�[�^1������̍ő�o�C�g��
#define ONCE_DATA_SIZE	11
/*
	GOP�R�}���h�����p�֐��Q
*/

/*
	�@�\	�`�F�b�N�T���v�Z
	�敪	static
	����	cmd	sum�v�Z���镶����̐擪�A�h���X
	�߂�l	sum�l
*/
static uint8_t _calc_sum(char *cmd)
{
	uint8_t sum=0;
	while(*cmd!=0x00&&*cmd!=0x03)
	{
		sum^=*cmd;
		cmd++;
	}
	return sum;
}

/*
	�@�\	1�o�C�g�����l��16�i�󕶎���ɕϊ�
	�敪	static
	����	b	�ϊ�����l
	�߂�l	�ϊ����ꂽ������̃A�h���X
	���l	�߂�l�͎��ɖ{�֐����Ăяo���܂ŗL��
			���������Ĉȉ��̎g������NG
			char str10],*s1,*s2;
			s1=byte_tohexstr(0x12);
			s2=byte_tohexstr(0x34);
			_strcpy(str,s1);
			_strcat(str,s2);
			��L�ł�str��"3434"�ƂȂ�
			
			"1234"�𓾂����ꍇ
			s1=byte_tohexstr(0x12);
			_strcpy(str,s1);
			s2=byte_tohexstr(0x34);
			_strcat(str,s2);
			�Ƃ���
*/
static char *byte_tohexstr(uint8_t b)
{
	static char buf[3];
	const char hexarray[]="0123456789abcdef";
	buf[0]=hexarray[b>>4];
	buf[1]=hexarray[b&0x0f];
	buf[2]=0;
	return buf;
}
/*
	�@�\	16�i�󕶎����1�o�C�g�����ɕϊ�
	�敪	static
	����	bstr	�ϊ����镶����
	�߂�l	�ϊ����ꂽ�l
*/
static uint8_t hexstr_tobyte(char *bstr)
{
	uint8_t ret=0;
	int i;
	for(i=0;i<2;i++,bstr++)
	{
		if(*bstr>='0'&&	*bstr<='9')ret=ret*0x10+*bstr-'0';
		else if(*bstr>='a'&& *bstr<='f')ret=ret*0x10+*bstr-'a'+0x0a;
		else if(*bstr>='A'&& *bstr<='F')ret=ret*0x10+*bstr-'A'+0x0a;
	}
	return ret;
}
/*
	�@�\	�w�肵���������uart�ɏo��
	�敪	static
	����	cmd	�o�͂��镶����
*/

static void ltputs(char FAR *cmd)
{
	while(*cmd){
		uart_putc(*cmd++);
	}
}
/*
	�@�\	sum�l���v�Z���Ȃ���w�肵���������uart�ɏo��
	�敪	static
	����	cmd	�o�͂��镶����
	�߂�l	sum�l
*/

static uint8_t puts_sum(char *cmd){
	uint8_t sum;
	sum=_calc_sum(cmd);
	ltputs(cmd);
	return sum;
}
//�W���֐���ˑ��΍�
static int _strlen(char *buf) {
	int ret = 0;
	while (buf[ret] != 0)ret++;
	return ret;
}

static void _strcpy(char *dest, char *src){
	int len = 0;
	while (*src) {
		*dest++ = *src++;
		len++;
		if (len > LT_TEXT_LENGTH-1)break;
	}
	*dest = '\0';
}

static void _strcat(char *dest, char *src){
	int len = 0;
	while (*dest) {
		*dest++;
		len++;
		if (len > LT_TEXT_LENGTH-1)break;
	}
	while (*src) {
		*dest++ = *src++;
		len++;
		if (len > LT_TEXT_LENGTH-1)break;
	}
	*dest = '\0';
}
#if defined(__linux__)
void delay(int dleaytime){
	usleep((useconds_t)dleaytime*1000);
}
#else
void delay(int dleaytime){
	int st=get_syscount();
	while(get_syscount()<st+dleaytime);
}
#endif


/*
	�p�r	�ʐM�G���[���e�̕ێ�
	�敪	static
*/
static uint8_t _error_status;
/*
	�@�\	�ʐM�G���[���e�̎擾
	�敪	public
	�߂�l	�G���[�R�[�h(��`���e��goplt_if.h�Q��)
*/
uint8_t _get_error_code()
{
	return _error_status;
}
/*
	�@�\	��M�f�[�^�̎擾
	�敪	static
	����	buf	��M�f�[�^�i�[�o�b�t�@�[�̃A�h���X
			bufsize	�o�b�t�@�[�̃T�C�Y(��M�\�ȓd�����͂���ŋK�肳��܂�)
			timeout	�^�C���A�E�g����
	�߂�l	NULL	�ʐM���s
					���s���R��_get_error_code�Ŏ擾��
			NULL�ȊO	��M�d���̃|�C���^
						��M�d����stx,etx,sum�������ꐳ���̕�����ƂȂ�܂�
*/
static char FAR * gets_lt(char FAR *buf,int16_t bufsize,uint32_t timeout)
{
	char *s;
	uint8_t sum=0;
	uint32_t st=get_syscount();
	int r;
	_error_status=0;
	while(TRUE){
		r=uart_getc();
		if(r!=-1){
			if(r==0x06||r==0x25){
				//ack�܂���nac�̏ꍇ
				s=buf;
				*s++=r;
				if(r==0x25){
					_error_status|=ERROR_NAK;
				}
				while((r=uart_getc())!='\r'){
					if(get_syscount()-st>timeout){
						_error_status|=ERROR_TIMEOUT;
						return NULL;
					}
				}
				*s=0x00;
				return buf;
			}
			if(r!=0x02){
				//stx~�f�[�^���n�܂�Ȃ�=�d�������Ă���ꍇ
				_error_status|=ERROR_STX;
				//��M�f�[�^��cr�܂Ŏ�荞��Ŋ֐��𔲂���
				while((r=uart_getc())!='\r'){
					if(get_syscount()-st>timeout){
						_error_status|=ERROR_TIMEOUT;
						return NULL;
					}
				}				
				return NULL;
			}
			//����ȊO(�������d�����n�܂��Ă���ꍇ)
			s=buf;
			while(TRUE){
				while((r=uart_getc())==-1){
					//��M�f�[�^���B���̃^�C���A�E�g�m�F
					if(get_syscount()-st>timeout){
						_error_status|=ERROR_TIMEOUT;
						return NULL;
					}
				}
				st=get_syscount();
				if(r!=0x03){
					//etx�łȂ��ꍇ�̓f�[�^�Ƃ���sum���v�Z���Ȃ����荞��
					*s++=r;
					sum^=(uint8_t)r;
					bufsize--;
					if(bufsize==0){
						//�p�ӂ��ꂽ�o�b�t�@�[�Ɏ��܂�Ȃ��d������M��
						//�o�b�t�@�[�I�[�o�[�̃G���[�t���O���Z�b�g����
						_error_status|=ERROR_OVERFLOW;
						//��M�f�[�^���I���܂Ńf�[�^�����o���Ă���
						while((r=uart_getc())!='\r'){
							if(get_syscount()-st>timeout){
								_error_status|=ERROR_TIMEOUT;
								return NULL;
							}
						}
						return NULL;
					}
				}else{
					char sumstr[3];
					*s='\0';
					while((r=uart_getc())==-1){
						if(get_syscount()-st>timeout){
							_error_status|=ERROR_TIMEOUT;
							return NULL;
						}
					}
					sumstr[0]=r;
					while((r=uart_getc())==-1){
						if(get_syscount()-st>timeout){
							_error_status|=ERROR_TIMEOUT;
							return NULL;
						}
					}
					sumstr[1]=r;
					while((r=uart_getc())!='\r'){
						if(get_syscount()-st>timeout){
							_error_status|=ERROR_TIMEOUT;
							return NULL;
						}
					}
					sumstr[2]='\0';
					if(sum!=hexstr_tobyte(sumstr))
					{
						_error_status|=ERROR_SUM;
						return NULL;
						
					}
					return buf;
				}
			}
		}else{
			if(get_syscount()-st>timeout){
				_error_status|=ERROR_TIMEOUT;
				return NULL;
			}
		}
	}
	return NULL;
}
/*
	�@�\	�����l��10�i������ɕϊ�
	�敪	static
	����	val	�ϊ�����l
			buf	�ϊ�������i�[�p�o�b�t�@�[�̃A�h���X(�o�b�t�@�[��11�o�C�g�ȏ�m�ۂ���Ă��邱��)
	�߂�l	�ϊ����ꂽ������̃|�C���^
*/
static char *itoa(int32_t val,char *buf)
{
	char *s;
	BOOL sign=FALSE;
	s=buf+11;
	*s='\0';
	if(val==0){
		*--s='0';
		return s;
	}
	if(val<0)
	{
		sign=TRUE;
		val*=-1;
	}
	while(val){
		*--s=val%10+'0';
		val/=10;
	}
	if(sign){
		*--s='-';
	}
	return s;
}
static char *uctoh(uint8_t val, char *buf)
{
	const char hex[] = "0123456789abcdef";
	char *s;
	BOOL sign = FALSE;
	s = buf;
	*s++ = hex[(val >> 4) & 0x0f];
	*s++ = hex[(val >> 0) & 0x0f];
	*s++ = '\0';
	return buf;
}

/*
	�@�\	GOP-LT��RST_OUT����
	�敪	public
	����	sta	�o�͏�ԁ@0:Low(���Z�b�g) 0�ȊO:High
	���l	RST_OUT�ɐڑ�����Ă���IO�𒼐ڑ��삵�Ă��܂��B�{�[�h�ڐA�����ύX���K�v�ł�
*/
void ResetOut(BOOL sta)
{
#ifdef __RL78__
	if(sta)
	{
		P1|=0x04;
	}else{
		P1&=~0x04;
	}
#elif defined(WIN32)
	return reset_out(sta);
#endif
}
/*
	�@�\	GOP_READY�s���̏�Ԋm�F
	�敪	public
	�߂�l	TRUE	READY���(�ʐM�\)
			FALSE	BUSY���(�ʐM�s��)
	���l	GOP_READY�ɐڑ�����Ă���IO�𒼐ڎQ�Ƃ��Ă��܂��B�{�[�h�ڐA�����ύX���K�v�ł�
*/
BOOL Is_Gop_Ready()
{
#ifdef __RL78__
	return (P1&0x01)?TRUE:FALSE;
#elif defined(WIN32)
	return (getio() & 0x01) ? TRUE: FALSE;
#endif
}
/*
	�@�\	SET_SEND_DATA�s���̏�Ԋm�F
	�敪	public
	�߂�l	TRUE	GOP-LT����̑��o�v������(ENQ�Ŏ��o���K�v����)
			FALSE	GOP-LT����̑��o�v���Ȃ�
	���l	SET_SEND_DATA�ɐڑ�����Ă���IO�𒼐ڎQ�Ƃ��Ă��܂��B�{�[�h�ڐA�����ύX���K�v�ł�
*/
BOOL Is_SetData()
{
#ifdef __RL78__
	return (P1&0x02)?FALSE:TRUE;	//LOW�A�N�e�B�u�̂���
#elif defined(WIN32)
	return (getio() & 0x02) ? FALSE : TRUE;
#endif
}

#ifdef IS_USE_SPI
/*
	�@�\	SSL(SPI�X���[�u�Z���N�g)�s���̑���
	�敪	static
	����	sta	0:�X���[�u�I��
				1:�I������
	���l	SSL�ɐڑ�����Ă���IO�𒼐ڑ��삵�Ă��܂��B�{�[�h�ڐA�����ύX���K�v�ł�
*/
static void SPI_CS(BOOL sta)
{
#ifdef __RL78__
	if(!sta)
	{
		P7|=0x08;
	}else{
		P7&=~0x08;
	}
#elif defined(WIN32)
	spi_cs(sta);
#endif
}

//SPI����API�Q
//�ȉ���TP�f�U�C�i�[LT�p�ɍ쐬����SPI�ʐM���C�u������RL�����ڐA�ł��B
//goplt_if���C�u�����ł͈ȉ���API�����̂܂܎g�p����uart����API�ƌ݊��`���ɂȂ�悤
//���b�p�[�֐�����Ĉȉ����g�p���܂�


//GOP-LT SPI�R�}���h��`
#define SPICMD_READ_STATUS				0x00
#define SPICMD_DATA_START				0xa0
#define SPICMD_DATA_ERASE				0xa1
#define SPICMD_FILE_HEAD				0xa2
#define SPICMD_FILE_BLOCK				0xa3
#define SPICMD_DATA_END					0xa4
#define SPICMD_READ_LASTBLOCK			0xa8


#define SPICMD_MEM_READ_REQ				0xc0
#define SPICMD_MEM_READ_READ			0xc1
#define SPICMD_MEM_WRITE				0xc2
#define SPICMD_ENQ_REQ					0xc3
#define SPICMD_ENQ_READ					0xc4
#define SPICMD_MEM_BI_READ_REQ			0xc5
#define SPICMD_MEM_BIOffset_READ_REQ	0xcd
#define SPICMD_MEM_BI_READ_READ			0xc6
#define SPICMD_MEM_BI_WRITE				0xc7
#define SPICMD_MEM_BIOffset_WRITE		0xcf
#define SPICMD_RAMWRITE				    0xd0    //RAM�o�b�t�@�[�t�@�C���f�[�^���M
#define SPICMD_RAMHEAD					0xd1    //�t�@�C���I�[�v��(�t�@�C�����A�t�@�C���T�C�Y��ʒm)
#define SPICMD_RAMBLOCK					0xd2    //�t�@�C���f�[�^���M(�u���b�N�P�ʁA����T�C�Y��M�Ŏ����N���[�Y)

#define SPICMD_SPI_RESET				0xff

//GOP-LT SPI��Ԓ�`
#define SPI_STATE_BUSY		0x01	//�R�}���h�ɂ�铮�쒆
#define SPI_STATE_ERROR		0x02	//�R�}���h�����s�ł��Ȃ�(�t�@�C���Ƀf�[�^���������߂Ȃ��A�������[�������������Ȃ�)
#define SPI_STATE_RETRY		0x04	//��M�R�}���h����������(SUM�s��v�A�A�Ԃ͂���Ȃ�)
#define SPI_STATE_STALL		0x08	//�R�}���h�^�C���A�E�g(�R�}���h��������Ԃň�莞�Ԗ��ʐM)
#define SPI_STATE_ERRCMD	0x10	//�s���R�}���h
#define SPI_STATE_READREADY	0x20	//�ԐM�f�[�^�Z�b�g
#define SPI_STATE_ERRINT	0x40	//���荞�݃G���[
#define SPI_STATE_BLOCK		0x80	//�u���b�N�s�A��


/*
	�@�\	SPI�̃`�F�b�N�T����Ԃ�
	�敪	static
	����	buf	sum�v�Z����̈�̐擪�A�h���X
			start	sum�̏����l
			size	sum�v�Z����o�C�g��
	�߂�l	sum�l
	���l	uart��sum�Ƃ͌v�Z���@���قȂ�܂�
*/
static uint8_t CheckSum(uint8_t *buf,uint8_t start,int size)
{
	int i;
	int ret=start;
	for(i=0;i<size;i++){
		ret=ret+*buf++;	//
	}
	return (uint8_t)(ret&0x000000ff);
}

/*
	�@�\	GOP��SPI�X�e�[�g�}�V���̏�Ԃ����Z�b�g���܂�
	�敪	static
	���l	spi�X�e�[�^�X���G���[�ɂȂ����ꍇ�Ȃǂ�SPI_RESET�R�}���h�𑗂�܂��B
*/
void SpiCmd_SPI_RESET(){
    MD_STATUS status = MD_OK;
	uint8_t _buf[2]={SPICMD_SPI_RESET,0};
	//2�o�C�g���C�g
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf, 2);
	SPI_CS(FALSE);
}

/*
	�@�\	GOP��SPI�X�e�[�g�}�V���̏�Ԃ�ǂݍ��݂܂�
	�敪	static
	����	sta	�X�e�[�^�X���i�[���邽�߂�uint8_t�^�ϐ��̃|�C���^
	�߂�l	TRUE	SPI�]������
			FALSE	SPI�]�����s
*/
BOOL SpiCmd_ReadStatus(uint8_t FAR *sta)
{
    MD_STATUS status = MD_OK;
	//�o�b�t�@�[�ɃR�}���h���Z�b�g(�{�R�}���h�ł̓_�~�[�ł�)
	uint8_t _buf[1]={SPICMD_READ_STATUS};
	//1�o�C�g���[�h
	SPI_CS(TRUE);
	status = xfer_spi_R(_buf, 1);
	SPI_CS(FALSE);
	*sta=_buf[0];
	return status==MD_OK?TRUE:FALSE;
}


/*
	�@�\	GOP�ɑ��o�v��(ENQ�R�}���h)�𑗐M���܂�
			READSTATUS��SPI_STATE_READREADY���Z�b�g�����Ƒ��o�d�������o����
	�敪	static
	�߂�l	TRUE	SPI�]������
			FALSE	SPI�]�����s
*/
BOOL SpiCmd_ENQ_REQ(){
	MD_STATUS status = MD_OK;
	uint8_t _buf[2]={SPICMD_ENQ_REQ,0};
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf,1);
	SPI_CS(FALSE);
	return status==MD_OK?TRUE:FALSE;
}
//ENQ_READ�̌��ʂ�enum�^
typedef enum {
	ENQ_NODATA=0,
	ENQ_OK,
	ENQ_ERROR=99
} ENQ_RETSTATUS;

/*
	�@�\	ENQ�R�}���h�̕ԐM���擾���܂�
	�敪	static
	����	read_buf	��M�d�����i�[����o�b�t�@�[�̃A�h���X
	�߂�l	ENQ_NODATA	SPI�]�������A��M�d���Ȃ�
			ENQ_OK		SPI�]�������A��M�d����buf�Ɋi�[
			ENQ_ERROR	SPI�]�����s
	���l	read_buf�͌Ăяo�����őz�肳���ő僁�b�Z�[�W�����i�[�ł��钷��(�I�[�܂�)+1(sum�̈�)�̗̈��p�ӂ���
*/
ENQ_RETSTATUS SpiCmd_ENQ_READ(uint8_t FAR *read_buf){
	MD_STATUS status = MD_OK;
	int16_t len;
	uint8_t _buf[2]={SPICMD_ENQ_READ,0};
	SPI_CS(TRUE);
	//�R�}���h���_�~�[�o�C�g(�ǂݍ��݂֐؂�ւ��O�ɃS�~�����̂���1�o�C�g�����M)
	status = xfer_spi_W(_buf, 2);
	if(status!=MD_OK)goto err;
	status = xfer_spi_R(read_buf, 1);
	if(status!=MD_OK)goto err;

	if((len=read_buf[0])==0){
		//�_�~�[��1�o�C�g�ǂݍ���CS������
		status = xfer_spi_R(read_buf, 1);
		if(status!=MD_OK)goto err;
		SPI_CS(FALSE);
		_strcpy(read_buf,"");
		return ENQ_NODATA;
	}else{
		status = xfer_spi_R(read_buf, len+1);
		if(status!=MD_OK)goto err;
		SPI_CS(FALSE);
		//SUM�m�F
		if(read_buf[len]==CheckSum(read_buf,0,len)){
			return ENQ_OK;
		}else{
			_strcpy(read_buf,"");
			return ENQ_ERROR;
		}
	}
err:
	SPI_CS(FALSE);
	return ENQ_ERROR;
}

/*
	�@�\	GOP�ɃO���t�������[�̓ǂݏo���v���𑗐M���܂�
			READSTATUS��SPI_STATE_READREADY���Z�b�g�����ƃO���t�������[�̃��[�h��
	�敪	static
	����	memname	�ǂݏo������O���t�������[��
			offset	�ǂݍ��݊J�n����ʒu
			num	�ǂݍ��ރv���b�g�_��
	�߂�l	TRUE	SPI�]������
			FALSE	SPI�]�����s
*/
BOOL SpiCmd_MEM_BIOffset_READ_REQ(uint8_t FAR* memname,int16_t offset,int16_t num){
	MD_STATUS status = MD_OK;
	uint8_t _buf[6+MAX_MEMNAME_SIZE],sum;

	//�f�[�^ �p�P�b�g�쐬
	_buf[0]=SPICMD_MEM_BI_READ_REQ;
	_buf[1]=_strlen(memname)+1;
	_buf[2]=(uint8_t)(offset/256);
	_buf[3]=(uint8_t)(offset%256);
	_buf[4]=(uint8_t)(num/256);
	_buf[5]=(uint8_t)(num%256);
	_strcpy(_buf+4,memname);
	sum=CheckSum(memname,0,_strlen(memname)+1);
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf, 6+_strlen(memname)+1);
	SPI_CS(FALSE);
	return status==MD_OK?TRUE:FALSE;
}

/*
	�@�\	GOP�ɃO���t�������[�̓ǂݏo��
	�敪	static
	����	num	�ǂݍ��ރv���b�g�_��(SpiCmd_MEM_BIOffset_READ_REQ�Ŏw�肵���l�Ɠ����w�肷�邱��)
			read_buf	�ǂݍ��񂾃f�[�^���i�[����̈�̃A�h���X(�̈�T�C�Y��num���m�ۂ���Ă��邱��)
	�߂�l	TRUE	SPI�]������
			FALSE	SPI�]�����s
*/
BOOL SpiCmd_MEM_BI_READ_READ(int16_t num,uint8_t *read_buf)
{
	MD_STATUS status = MD_OK;
	uint8_t _buf[2]={SPICMD_MEM_BI_READ_READ,0},sum;
	memset(read_buf,0,num);
	SPI_CS(TRUE);
	//�R�}���h���f�[�^��+�_�~�[�o�C�g(�ǂݍ��݂֐؂�ւ��O�ɃS�~�����̂���1�o�C�g�����M)
	status = xfer_spi_W(_buf, 2);
	if(status!=MD_OK)goto err;
	//�f�[�^��M
	status = xfer_spi_R(read_buf,num);
	if(status!=MD_OK)goto err;
	//sum��M
	status = xfer_spi_R(&sum,1);
	if(status!=MD_OK)goto err;
	SPI_CS(FALSE);
	if(sum==CheckSum(read_buf,0,num)){
		return TRUE;
	}else{
		return FALSE;
	}
err:
	SPI_CS(FALSE);
	return FALSE;
}

/*
	�@�\	GOP�̃O���t�������[�Ƀf�[�^���������݂܂�
	�敪	static
	����	memname	�������݃O���t�������[��
			offset	�������݊J�n����ʒu
			num	�������ރv���b�g�_��
			write_buf	�������ރf�[�^���i�[����Ă���̈�̐擪�A�h���X
	�߂�l	TRUE	SPI�]������
			FALSE	SPI�]�����s
*/
BOOL SpiCmd_MEM_BIOffset_WRITE(uint8_t FAR* memname,int16_t offset,int16_t num,uint8_t FAR * write_buf)
{
	MD_STATUS status = MD_OK;
	uint8_t _buf[6+MAX_MEMNAME_SIZE];
	uint8_t sum=0;

	//�f�[�^ �p�P�b�g�쐬
	_buf[0]=SPICMD_MEM_BIOffset_WRITE;
	_buf[1]=_strlen(memname)+1;
	_buf[2]=offset/256;
	_buf[3]=offset%256;
	_buf[4]=num/256;
	_buf[5]=num%256;
	_strcpy(_buf+6,memname);
	sum=CheckSum(memname,0,_strlen(memname)+1);
	sum=CheckSum(write_buf,sum,num);
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf, 6+_strlen(memname)+1);
	if(status!=MD_OK)goto err;
	status = xfer_spi_W(write_buf, num);
	if(status!=MD_OK)goto err;
	_buf[0]=sum;
	_buf[1]=0;
	status = xfer_spi_W(_buf, 2);
	if(status!=MD_OK)goto err;
	SPI_CS(FALSE);
	return TRUE;
err:
	SPI_CS(FALSE);
	return FALSE;
}

/*
	�@�\	GOP�Ƀ������[�̓ǂݏo���v���𑗐M���܂�
			READSTATUS��SPI_STATE_READREADY���Z�b�g�����ƃ������[�̃��[�h��
	�敪	static
	����	memname	�ǂݏo������擪�̃������[��
			num	�A���ǂݍ��݂��郁�����[��
	�߂�l	TRUE	SPI�]������
			FALSE	SPI�]�����s
*/
BOOL SpiCmd_MEM_READ_REQ(uint8_t FAR * memname,int16_t num){
	MD_STATUS status = MD_OK;
	//1.1.0.0b23�@SPIREAD�����C��
	uint8_t _buf[3+MAX_MEMNAME_SIZE+6],sum;
	memset(_buf, 0, 3 + MAX_MEMNAME_SIZE + 6);
	//�f�[�^ �p�P�b�g�쐬
	_buf[0]=SPICMD_MEM_READ_REQ;
	_buf[1]=_strlen(memname)+1;
	_buf[2]=(uint8_t)num;
	_strcpy(_buf+3,memname);
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf, 5+_strlen(memname)+1);
	SPI_CS(FALSE);
	return status==MD_OK?TRUE:FALSE;
}
/*
	�@�\	GOP�̃������[�̓ǂݏo��
	�敪	static
	����	num	�A���œǂݍ��ރ������[��(SpiCmd_MEM_READ_REQ�Ŏw�肵���l�Ɠ����w�肷�邱��)
			read_buf	�ǂݍ��񂾃f�[�^���i�[����̈�̃A�h���X(�̈�T�C�Y��num���m�ۂ���Ă��邱��)
	�߂�l	TRUE	SPI�]������
			FALSE	SPI�]�����s
*/
#define EX_32(a) (((a) << 24) & 0xff000000) | (((a) << 8) & 0x00ff0000) | (((a) >> 8) & 0x0000ff00) | (((a) >> 24) & 0x000000ff);



BOOL SpiCmd_MEM_READ_READ(int16_t num,int32_t FAR *read_buf)
{
	MD_STATUS status = MD_OK;
	uint8_t _buf[2]={SPICMD_MEM_READ_READ,0},sum;
	int16_t i;
	//�R�}���h���f�[�^��+�_�~�[�o�C�g(�ǂݍ��݂֐؂�ւ��O�ɃS�~�����̂���1�o�C�g�����M)
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf, 2);
	if(status!=MD_OK)goto err;
	status = xfer_spi_R(read_buf,num*4);
	if(status!=MD_OK)goto err;
	status = xfer_spi_R(&sum,1);
	if(status!=MD_OK)goto err;
	SPI_CS(FALSE);
	if(sum==CheckSum(read_buf,0,num*4)){
		//�G���f�B�A���ύX(BIG �G���f�B�A���n��CPU�̏ꍇ�s�v)
		for(i=0;i<num;i++){
			read_buf[i] = EX_32(read_buf[i]);
		}
		return TRUE;
	}else{
		return FALSE;
	}
err:
	SPI_CS(FALSE);
	return FALSE;
}




/*
	�@�\	GOP�̃������[�Ƀf�[�^���������݂܂�
	�敪	static
	����	memname	�������݂��J�n����擪�̃������[��
			num	�A���ŏ������ރ������[��
			write_buf	�������ރf�[�^���i�[����Ă���̈�̐擪�A�h���X
	�߂�l	TRUE	SPI�]������
			FALSE	SPI�]�����s
*/


BOOL SpiCmd_MEM_WRITE(uint8_t FAR *memname,int16_t num,int32_t FAR *write_buf)
{
    MD_STATUS status = MD_OK;
	uint8_t _buf[3+MAX_MEMNAME_SIZE];
	uint8_t sum=0;
	int32_t v;
	int16_t i;
	//�f�[�^ �p�P�b�g�쐬
	_buf[0]=SPICMD_MEM_WRITE;
	_buf[1]=_strlen(memname)+1;
	_buf[2]=num;
	_strcpy(_buf+3,memname);
	sum=CheckSum(memname,0,_strlen(memname)+1);
	sum=CheckSum(write_buf,sum,num*4);
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf, 3+_strlen(memname)+1);
	if(status!=MD_OK)goto err;
	for(i=0;i<num;i++)
	{
		//�G���f�B�A���ύX���Ȃ���int32_t�P��������
//		v = (write_buf[i] >> 24) | (write_buf[i] >> 16) | (write_buf[i] >> 8) | (write_buf[i]);
		v = ((write_buf[i] << 24)&0xff000000) | ((write_buf[i] << 8)&0x00ff0000) | ((write_buf[i] >> 8)&0x0000ff00) | ((write_buf[i]>>24)&0x000000ff);
		status = xfer_spi_W(&v, 4);
//		status = xfer_spi_W(&write_buf[i], 4);
		if(status!=MD_OK)goto err;
	}
	_buf[0]=sum;
	_buf[1]=0;	//dummy
	status = xfer_spi_W(_buf, 2);
	if(status!=MD_OK)goto err;
	SPI_CS(FALSE);
	return TRUE;
err:
	SPI_CS(FALSE);
	return FALSE;
}
#endif
//uart�n��API
//uart���g�p����GOP-LT�Ƃ̃R�}���h�ʐM���s��API�ł�




/*
	�@�\	GOP�̐����������[�Ƀf�[�^���������݂܂�
	�敪	public
	����	memname	�������ރ������[��
			val	�������ޒl
	�߂�l	TRUE	�ʐM����
			FALSE	�ʐM���s
					���s������_get_error_code�Ŏ擾�ł��܂��B
*/

BOOL LtMemWrite(char FAR *memname,int32_t val)
{
	uint8_t sum=0;
	char *s,buf[PACKET_INFO_SIZE];

	ltputs("\x02");
	sum=puts_sum("WN ");
	sum^=puts_sum(memname);
	sum^=puts_sum(" ");
	s=itoa(val,buf);
	sum^=puts_sum(s);
	ltputs("\x03");
	ltputs(byte_tohexstr(sum));
	ltputs("\r");
	s=gets_lt(buf,sizeof(buf),TIMEOUT);
	if(s){
		if(*s==0x06){
			return TRUE;
		}
	}
	return FALSE;
}

/*
�@�\	GOP�̃e�L�X�g�������Ƀf�[�^���������݂܂�
�敪	public
����	memname	�������ރ�������
val	�������ޒl
�߂�l	TRUE	�ʐM����
FALSE	�ʐM���s
���s������_get_error_code�Ŏ擾�ł��܂��B
*/
BOOL LtTMemWrite(char FAR *memname, char *val)
{
	uint8_t sum = 0;
	char *s, buf[PACKET_INFO_SIZE];

	ltputs("\x02");
	sum = puts_sum("WN ");
	sum ^= puts_sum(memname);
	sum ^= puts_sum(" ");
	sum ^= puts_sum(val);
	ltputs("\x03");
	ltputs(byte_tohexstr(sum));
	ltputs("\r");
	s = gets_lt(buf, sizeof(buf), TIMEOUT);
	if (s) {
		if (*s == 0x06) {
			return TRUE;
		}
	}
	return FALSE;
}
/*
	�@�\	GOP�̘A�����鐮���������[�ɘA�����ăf�[�^���������݂܂�
	�敪	public
	����	memname	�������ލs���擪�̃������[��
			num	�A�����ď������ރ������[�̐�
			vals	�������ޒl���i�[���ꂽ�z��̐擪�A�h���X
	�߂�l	TRUE	�ʐM����
			FALSE	�ʐM���s
					���s������_get_error_code�Ŏ擾�ł��܂��B
*/

BOOL LtMemArrayWrite(char FAR *memname,int16_t num,int32_t FAR vals[])
{
	int i;
	uint8_t sum=0;
	char *s,buf[PACKET_INFO_SIZE];

	ltputs("\x02");
	sum=puts_sum("WNB ");
	s=itoa(num,buf);
	sum^=puts_sum(s);
	sum^=puts_sum(",");
	sum^=puts_sum(memname);
	for(i=0;i<num;i++){
		sum^=puts_sum(",");
		s=itoa(vals[i],buf);
		sum^=puts_sum(s);
	}
	ltputs("\x03");
	ltputs(byte_tohexstr(sum));
	ltputs("\r");

	s=gets_lt(buf,sizeof(buf),TIMEOUT+num);
	if(s){
		if(*s==0x06){
			return TRUE;
		}
	}
	return FALSE;
}
/*
	�@�\	GOP�̘A�����鐮���������ɘA�����ăf�[�^���������݂܂��B
			�������ރf�[�^��z��ł͂Ȃ��A�ϒ������ň����n���܂��B
	�敪	public
	����	memname	�������ލs���擪�̃������[��
			num	�A�����ď������ރ������[�̐�
			���	�������ޒl��num�����A�����Ƃ��ēn��
	�߂�l	TRUE	�ʐM����
			FALSE	�ʐM���s
					���s������_get_error_code�Ŏ擾�ł��܂��B
	���l	LtMemArrayWrite�Ɠ��l�̋@�\�ł����A�������ݒl�𒼐ڋL�q�ł��܂��B
			�z���p�ӂ����Ɏg�p���邱�Ƃ��\�ł����A�֐��Ăяo���̃I�[�o�[�w�b�h��X�^�b�N�̃T�C�Y�ɉe��������܂��B
			�g����
			LtMemVaListWrite("MEM1",3,0,0,0);
			
*/

BOOL LtMemVaListWrite(char FAR *memname,int16_t num,...)
{
	va_list list;
	int i;
	uint8_t sum=0;
	int32_t v;
	char *s,buf[PACKET_INFO_SIZE];
	va_start(list,num);

	ltputs("\x02");
	sum=puts_sum("WNB ");
	s=itoa(num,buf);
	sum^=puts_sum(s);
	sum^=puts_sum(",");
	sum^=puts_sum(memname);
	for(i=0;i<num;i++){
		sum^=puts_sum(",");
		v=va_arg( list , int32_t );
		s=itoa(v,buf);
		sum^=puts_sum(s);
	}
	va_end( list );
	ltputs("\x03");
	ltputs(byte_tohexstr(sum));
	ltputs("\r");

	s=gets_lt(buf,sizeof(buf),TIMEOUT+num);
	if(s){
		if(*s==0x06){
			return TRUE;
		}
	}
	return FALSE;
}
/*
	�@�\	GOP�̐����������[�̒l��ǂݍ��݂܂�
	�敪	public
	����	memname	�ǂݍ��ރ������[��
			pval	�ǂݍ��񂾒l���i�[����̈�̃|�C���^
	�߂�l	TRUE	�ʐM����
			FALSE	�ʐM���s
					���s������_get_error_code�Ŏ擾�ł��܂��B
*/
BOOL LtMemRead(char FAR *memname,int32_t FAR *pval)
{
	uint8_t sum=0;
	char *s,buf[MAX_MEMNAME_SIZE+PACKET_INFO_SIZE+ONCE_DATA_SIZE];

	ltputs("\x02");
	sum=puts_sum("RN ");
	sum^=puts_sum(memname);
	ltputs("\x03");
	ltputs(byte_tohexstr(sum));
	ltputs("\r");

	s=gets_lt(buf,sizeof(buf),TIMEOUT);
	if(s){
		if(*s=='R'){
			while(*s++!='=');
			*pval=atol(s);
			return TRUE;
		}
	}
	return FALSE;
}
/*
�@�\	GOP�̃e�L�X�g�������̒l��ǂݍ��݂܂�
�敪	public
����	memname	�ǂݍ��ރ�������
pval	�ǂݍ��񂾒l���i�[����̈�̃|�C���^
�߂�l	TRUE	�ʐM����
FALSE	�ʐM���s
���s������_get_error_code�Ŏ擾�ł��܂��B
*/
BOOL LtTMemRead(char FAR *memname, char FAR *pval)
{
	uint8_t sum = 0;
	char *s, buf[MAX_MEMNAME_SIZE + PACKET_INFO_SIZE + LT_TEXT_LENGTH];

	ltputs("\x02");
	sum = puts_sum("RN ");
	sum ^= puts_sum(memname);
	ltputs("\x03");
	ltputs(byte_tohexstr(sum));
	ltputs("\r");

	s = gets_lt(buf, sizeof(buf), TIMEOUT);
	if (s) {
		if (*s == 'R') {
			while (*s++ != '=');
			_strcpy(pval, s);
			return TRUE;
		}
	}
	return FALSE;
}

/*
	�@�\	GOP�̘A�����鐮���������[�̒l��A�����ēǂݍ��݂܂�
	�敪	public
	����	memname	�ǂݍ��ސ擪�̃������[��
			num	�A�����ēǂݍ��ރ������[�̐�
			vals	�ǂݍ��񂾒l���i�[����z��̃|�C���^
	�߂�l	TRUE	�ʐM����
			FALSE	�ʐM���s
					���s������_get_error_code�Ŏ擾�ł��܂��B
*/
BOOL LtMemArrayRead(char FAR *memname,int16_t num,int32_t FAR vals[])
{
	uint8_t sum=0;
	char *s,buf[MAX_MEMNAME_SIZE+ONCE_DATA_SIZE*MAX_NUM+PACKET_INFO_SIZE];

	ltputs("\x02");
	sum=puts_sum("RNB ");
	s=itoa(num,buf);
	sum^=puts_sum(s);
	sum^=puts_sum(",");
	sum^=puts_sum(memname);
	ltputs("\x03");
	ltputs(byte_tohexstr(sum));
	ltputs("\r");
	s=gets_lt(buf,sizeof(buf),TIMEOUT+num);
	if(s){
		if(*s=='R'){
			char *tok;
			int i;
			s++;
			tok=s;
			while(*s!=','&&*s!='\0')s++;
			*s='\0';
			if(num!=atoi(tok))return FALSE;
			s++;
			tok=s;
			while(*s!=','&&*s!='\0')s++;
			*s='\0';
			if(strcmp(tok,memname))return FALSE;
			for(i=0;i<num;i++)
			{
				s++;
				tok=s;
				while(*s!=','&&*s!='\0')s++;
				*s='\0';
				vals[i]=atol(tok);
			}
			return TRUE;
		}
	}
	return FALSE;
}

/*
	�@�\	GOP�̃O���t�������[�ɒl���������݂܂��B
	�敪	public
	����	memname	�������ރO���t�������[��
			offset	�������݂��J�n����v���b�g�ʒu
			num	�������ރv���b�g�̐�
			vals	�������ޒl���i�[�����z��̐擪�A�h���X
	�߂�l	TRUE	�ʐM����
			FALSE	�ʐM���s
					���s������_get_error_code�Ŏ擾�ł��܂��B
			
*/
#define USE_WNI


BOOL LtGMemWrite(char FAR *memname,int16_t offset,int16_t num,uint8_t FAR vals[])
{
	int i;
	uint8_t sum=0;
	char *s,buf[PACKET_INFO_SIZE];

	ltputs("\x02");
#ifndef USE_WNI
	sum=puts_sum("WNGO ");
#else
	sum = puts_sum("WNIO ");
#endif
	s=itoa(num,buf);
	sum^=puts_sum(s);
	sum^=puts_sum(",");
	sum^=puts_sum(memname);
	sum^=puts_sum(",");
	s=itoa(offset,buf);
	sum^=puts_sum(s);
#ifdef USE_WNI
	sum ^= puts_sum(",");
#endif
	for(i=0;i<num;i++){
#ifndef USE_WNI
		sum ^= puts_sum(",");
		s=itoa(vals[i],buf);
#else
		s = byte_tohexstr(vals[i]);
#endif
		sum^=puts_sum(s);
	}
	ltputs("\x03");
	ltputs(byte_tohexstr(sum));
	ltputs("\r");
	//1.1.0.0b23 �����d�����̏ꍇ���M���Ԃ��^�C���A�E�g�ɉ������邽��
	s=gets_lt(buf,sizeof(buf),TIMEOUT+num);
	if(s){
		if(*s==0x06){
			return TRUE;
		}
	}
	return FALSE;
}

/*
	�@�\	GOP�̃O���t�������[����l��ǂݍ��݂܂��B
	�敪	public
	����	memname	�ǂݍ��ރO���t�������[��
			offset	�ǂݍ��݂��J�n����v���b�g�ʒu
			num	�ǂݍ��ރv���b�g�̐�
			vals	�ǂݍ��ޒl���i�[�����z��̐擪�A�h���X
	�߂�l	TRUE	�ʐM����
			FALSE	�ʐM���s
					���s������_get_error_code�Ŏ擾�ł��܂��B
			
*/

BOOL LtGMemRead(char FAR *memname,int16_t offset,int16_t num,uint8_t FAR vals[])
{
	uint8_t sum=0;
	char *s,buf[MAX_MEMNAME_SIZE+ONCE_DATA_SIZE*MAX_NUM+PACKET_INFO_SIZE];

	ltputs("\x02");
#ifndef USE_WNI
	sum=puts_sum("RNGO ");
#else
	sum = puts_sum("RNIO ");
#endif
	s=itoa(num,buf);
	sum^=puts_sum(s);
	sum^=puts_sum(",");
	sum^=puts_sum(memname);
	sum^=puts_sum(",");
	s=itoa(offset,buf);
	sum^=puts_sum(s);
	ltputs("\x03");
	ltputs(byte_tohexstr(sum));
	ltputs("\r");
	s=gets_lt(buf,sizeof(buf),TIMEOUT+num);
	if(s){
		if(*s=='R'){
			char *tok;
			int i;
			s++;
			tok=s;
			while(*s!=','&&*s!='\0')s++;
			*s='\0';
			if(num!=atoi(tok))return FALSE;
			s++;
			tok=s;
			while(*s!=','&&*s!='\0')s++;
			*s='\0';
			if(strcmp(tok,memname))return FALSE;
			s++;
			tok=s;
			while(*s!=','&&*s!='\0')s++;
			*s='\0';
			if(offset!=atoi(tok))return FALSE;
			for(i=0;i<num;i++)
			{
				s++;
#ifndef USE_WNI
				tok=s;
				while(*s!=','&&*s!='\0')s++;
				*s='\0';
				vals[i]=(uint8_t)atoi(tok);
#else
				vals[i] = hexstr_tobyte(s);
				s += 2;

#endif
			}
			return TRUE;
		}
	}
	return FALSE;
}

/*
	�p�r	���b�Z�[�W�n���h���e�[�u���̃A�h���X
	�敪	static
*/
static ltMes_Callback FAR *mes_callback=NULL;
/*
	�@�\	���b�Z�[�W�n���h���[�e�[�u����o�^���܂�
	�敪	public
	����	p	���b�Z�[�W�n���h���e�[�u���̃A�h���X
*/

void LtSetMessageCallback(ltMes_Callback FAR *p)
{
	mes_callback=p;
}

/*
	�@�\	GOP�̃��b�Z�[�W���擾���A�Ή����郁�b�Z�[�W�n���h�����R�[���o�b�N���܂����B
			
	�敪	public
	����	rcv	���b�Z�[�W�i�[�p�o�b�t�@�[�̃A�h���X
				�����b�Z�[�W����M����K�v�Ȃ��ꍇNULL���w��
	�߂�l	rcv��NULL�̏ꍇ
				COMMERR(0xffffffff) �ʐM���s
				NULL	�ʐM����
			rcv��NULL�ȊO
				COMMERR(0xffffffff) �ʐM���s
				NULL	���b�Z�[�W�Ȃ�
				NULL�ȊO	�擾�������b�Z�[�W�̐擪�A�h���X
			�ʐM���s������_get_error_code�Ŏ擾�ł��܂��B
	���l
			��LtSetMessageCallback�Ń��b�Z�[�W�n���h���e�[�u����o�^���Ă���ꍇ�B
*/
const char FAR *LtEnq(char FAR *rcv)
{
	uint8_t sum=0;
	char *s,buf[MAX_MESSAGE_SIZE+PACKET_INFO_SIZE];
	ltMes_Callback FAR *p=mes_callback;

	ltputs("\x02");
	sum=puts_sum("ENQ");
	ltputs("\x03");
	ltputs(byte_tohexstr(sum));
	ltputs("\r");
	s=gets_lt(buf,sizeof(buf),TIMEOUT);
	if(s){
		if(*s=='A'){
			int i=0;
			s+=2;	//A$���Ƃ�
			//�o�^����Ă��郁�b�Z�[�W�ɑ΂���R�[���o�b�N���Ăяo��
			while(p)
			{	
				if(strcmp(p[i].mes,s)==0){
					//��M���ƈ�v����ƑΉ����郋�[�`�����R�[���o�b�N�����[�v������
					p[i].func();
					break;
				}
				i++;
				//���R�[�h�����炷
				//�ԕ��ɂ�����ƃ��[�v������
				if(p[i].mes==NULL)break;
			}
			if(rcv){
				//�߂�l�p�o�b�t�@�[�w�莞�A��M����Ԃ��B
				_strcpy(rcv,s);
				return rcv;
			}
			return NULL;
		}
		if(*s=='N'){
			return NULL;
		}
	}else{
		printf("Err no=%d\n",_get_error_code());
	}
	return (const char *)COMMERR;
}
#ifdef IS_USE_SPI
/*
	�@�\	SPI���g�p����GOP�̘A�����鐮���������[�ɘA�����ăf�[�^���������݂܂�
	�敪	public
	����	memname	�������ލs���擪�̃������[��
			num	�A�����ď������ރ������[�̐�
			vals	�������ޒl���i�[���ꂽ�z��̐擪�A�h���X
	�߂�l	TRUE	�ʐM����
			FALSE	�ʐM���s
*/

BOOL SPI_LtMemArrayWrite(char FAR *memname,int32_t num,int32_t FAR vals[])
{
	BOOL ret;
	ret=SpiCmd_MEM_WRITE(memname,num,vals);
	while(!Is_Gop_Ready());
	while(TRUE){
		uint8_t sta;
		SpiCmd_ReadStatus(&sta);
		if((sta&SPI_STATE_BUSY)==0)break;
		if(sta&~SPI_STATE_BUSY){
			//BUSY�ȊO�̃r�b�g�������Ă����ꍇ�]�����s
			SpiCmd_SPI_RESET();
		}
	}
	return ret;
}
/*
	�@�\	SPI���g�p����GOP�̘A�����郁�����[�̒l��A�����ēǂݍ��݂܂�
	�敪	public
	����	memname	�ǂݍ��ސ擪�̃������[��
			num	�A�����ēǂݍ��ރ������[�̐�
			vals	�ǂݍ��񂾒l���i�[����z��̃|�C���^
	�߂�l	TRUE	�ʐM����
			FALSE	�ʐM���s
*/

BOOL SPI_LtMemArrayRead(char FAR *memname,int16_t num,int32_t FAR vals[])
{
	BOOL ret;
	ret=SpiCmd_MEM_READ_REQ(memname,num);
	if(!ret)return FALSE;
	while(!Is_Gop_Ready());
	while(TRUE){
		uint8_t sta;
		ret=SpiCmd_ReadStatus(&sta);
		if(!ret)return FALSE;
		if((sta&SPI_STATE_BUSY)==0)break;
		if(sta&~SPI_STATE_BUSY){
			//BUSY�ȊO�̃r�b�g�������Ă����ꍇ�]�����s
			SpiCmd_SPI_RESET();
			return FALSE;
		}
	}
	return SpiCmd_MEM_READ_READ(num,vals);
}

/*
	�@�\	SPI���g�p����GOP�̃e�L�X�g�������[�̒l���������݂܂�
	�敪	public
	����	memname	�������ސ擪�̃������[��
			vals	�������ޒl���i�[����z��̃|�C���^
	�߂�l	TRUE	�ʐM����
			FALSE	�ʐM���s
*/

BOOL SPI_LtTMemWrite(char FAR *memname, uint8_t FAR vals[])
{
	return SPI_LtGMemWrite(memname, 0, _strlen(vals), vals);
}

/*
	�@�\	SPI���g�p����GOP�̃O���t�������[�ɒl���������݂܂��B
	�敪	public
	����	memname	�������ރO���t�������[��
			offset	�������݂��J�n����v���b�g�ʒu
			num	�������ރv���b�g�̐�
			vals	�������ޒl���i�[�����z��̐擪�A�h���X
	�߂�l	TRUE	�ʐM����
			FALSE	�ʐM���s
	���l	LtGMemWrite�ƈقȂ�A�{�֐����s��vals�̃f�[�^�͔j�󂳂�܂��B
*/
BOOL SPI_LtGMemWrite(char FAR *memname,int16_t offset,int16_t num,uint8_t FAR vals[])
{
	BOOL ret;
	ret=SpiCmd_MEM_BIOffset_WRITE(memname,offset,num,vals);
	while(!Is_Gop_Ready());
	while(TRUE){
		uint8_t sta;
		SpiCmd_ReadStatus(&sta);
		if((sta&SPI_STATE_BUSY)==0)break;
		if(sta&~SPI_STATE_BUSY){
			//BUSY�ȊO�̃r�b�g�������Ă����ꍇ�]�����s
			SpiCmd_SPI_RESET();
		}
	}

	return ret;
}

/*
	�@�\	SPI���g�p����GOP�̃e�L�X�g�������[����l��ǂݍ��݂܂��B
	�敪	public
	����	memname	�ǂݍ��ރe�L�X�g�������[��
			vals	�ǂݍ��ޒl���i�[����z��̐擪�A�h���X
	�߂�l	TRUE	�ʐM����
			FALSE	�ʐM���s
			
*/
BOOL SPI_LtTMemRead(char FAR *memname, uint8_t FAR vals[])
{
	return  SPI_LtGMemRead(memname, 0, LT_TEXT_LENGTH-1, vals);
}
/*
	�@�\	SPI���g�p����GOP�̃O���t�������[����l��ǂݍ��݂܂��B
	�敪	public
	����	memname	�ǂݍ��ރO���t�������[��
			offset	�ǂݍ��݂��J�n����v���b�g�ʒu
			num	�ǂݍ��ރv���b�g�̐�
			vals	�ǂݍ��ޒl���i�[�����z��̐擪�A�h���X
	�߂�l	TRUE	�ʐM����
			FALSE	�ʐM���s
			
*/

BOOL SPI_LtGMemRead(char FAR *memname,int16_t offset,int16_t num,uint8_t FAR vals[])
{
	BOOL ret;
	ret=SpiCmd_MEM_BIOffset_READ_REQ(memname,offset,num);
	if(!ret)return FALSE;
	while(!Is_Gop_Ready());
	while(TRUE){
		uint8_t sta;
		ret=SpiCmd_ReadStatus(&sta);
		if(!ret)return FALSE;
		if((sta&SPI_STATE_BUSY)==0)break;
		if(sta&~SPI_STATE_BUSY){
			//BUSY�ȊO�̃r�b�g�������Ă����ꍇ�]�����s
			SpiCmd_SPI_RESET();
			return FALSE;
		}
	}
	return SpiCmd_MEM_BI_READ_READ(num,vals);
}

/*
	�@�\	SPI���g�p����GOP�̃��b�Z�[�W���擾���A�Ή����郁�b�Z�[�W�n���h�����R�[���o�b�N���܂����B
	�敪	public
	����	rcv	���b�Z�[�W�i�[�p�o�b�t�@�[�̃A�h���X
				�����b�Z�[�W����M����K�v�Ȃ��ꍇNULL���w��
	�߂�l	rcv��NULL�̏ꍇ
				COMMERR(0xffffffff) �ʐM���s
				NULL	�ʐM����
			rcv��NULL�ȊO
				COMMERR(0xffffffff) �ʐM���s
				NULL	���b�Z�[�W�Ȃ�
				NULL�ȊO	�擾�������b�Z�[�W�̐擪�A�h���X
	���l
			��LtSetMessageCallback�Ń��b�Z�[�W�n���h���e�[�u����o�^���Ă���ꍇ�B
*/
const char FAR *SPI_LtEnq(char FAR *rcv)
{
	BOOL ret;
	ENQ_RETSTATUS enq_ret;
	char *s,buf[MAX_MESSAGE_SIZE];
	ltMes_Callback FAR *p=mes_callback;

	ret=SpiCmd_ENQ_REQ();
	if(!ret)return FALSE;
	while(!Is_Gop_Ready());
	while(TRUE){
		uint8_t sta;
		ret=SpiCmd_ReadStatus(&sta);
		if(!ret)return FALSE;
		if((sta&SPI_STATE_BUSY)==0)break;
		if(sta&~SPI_STATE_BUSY){
			//BUSY�ȊO�̃r�b�g�������Ă����ꍇ�]�����s
			SpiCmd_SPI_RESET();
			return FALSE;
		}
	}
	enq_ret= SpiCmd_ENQ_READ(buf);
	if(enq_ret==ENQ_ERROR){
		return COMMERR;
	}else if(enq_ret==ENQ_NODATA){
		return NULL;
	}
	s=buf;
	if(*s=='A'){
		int i=0;
		s+=2;	//A$���Ƃ�
		//�o�^����Ă��郁�b�Z�[�W�ɑ΂���R�[���o�b�N���Ăяo��
		while(p)
		{	
			if(strcmp(p[i].mes,s)==0){
				//��M���ƈ�v����ƑΉ����郋�[�`�����R�[���o�b�N�����[�v������
				p[i].func();
				break;
			}
			i++;
			//���R�[�h�����炷
			//�ԕ��ɂ�����ƃ��[�v������
			if(p[i].mes==NULL)break;
		}
		if(rcv){
			//�߂�l�p�o�b�t�@�[�w�莞�A��M����Ԃ��B
			_strcpy(rcv,s);
			return rcv;
		}
	}
	return NULL;
}
#endif

#define SPI_FILEXFER
#ifdef SPI_FILEXFER

#define SPI_SLEEP 10			//SPI�R�}���h�Ԃ̃f�B���C
#define SPI_TIMEOUT 60000

#define FILEBLOCKSIZE 1024



#ifdef FILESYSTEM
struct fileinfo {
	char* src_path;	//�z�X�g���t�@�C���V�X�e�����Q�Ɖ\�Ȗ��̂��w�肵�܂�
	char* dest_path;	//GOP-LT�ɏ������ݍ̖��̂��w�肵�܂��BGOP.ini������t�H���_�����[�g�Ƃ��A�t�H���_��؂��'/'�Ŏw�肵�܂��B
	int size;				//�t�@�C���T�C�Y���L�q�Bstat���Ŏ擾�\�ł���Ύ��s���擾�ł��\���܂���
};
/*
	filetable���N���A���܂��B
*/
struct fileinfo* filetable=NULL;

void clean_table()
{
	if (filetable) {
		struct fileinfo* p = filetable;
		while (p->src_path) 
		{
			free(p->src_path);
			free(p->dest_path);
			p++;
		}
		free(filetable);
		filetable = NULL;
	}
}

#ifdef WIN32
//windows��ł̓���T���v���ł́Afiletable�𓮓I�ɐ������܂��B
LPCTSTR ShowFolderDlg();

/*
	�t�H���_���̃t�@�C����������filetable�\���̂ɒl���Z�b�g���܂�
*/
struct fileinfo* _sub_MakeFileCatalog(LPCTSTR full_path, LPCTSTR folderName,struct fileinfo *p) {
	HANDLE hFind;
	WIN32_FIND_DATA fdat;
	char findpath[MAX_PATH];
	sprintf(findpath, "%s*", full_path);
	hFind = FindFirstFile(findpath, &fdat);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			char fpath[MAX_PATH];
			char fname[MAX_PATH];
			if (fdat.dwFileAttributes == FILE_ATTRIBUTE_DIRECTORY && fdat.cFileName[0] != '.') {
				char fname[MAX_PATH];
				sprintf(fpath, "%s%s\\", full_path, fdat.cFileName);
				if (!folderName) {
					sprintf(fname, "%s", fdat.cFileName);
				}
				else {
					sprintf(fname, "%s/%s", folderName, fdat.cFileName);
				}
				p=_sub_MakeFileCatalog(fpath,fname,p);
			}
			else if(fdat.cFileName[0] != '.') {
				int slen;
				HANDLE hFile;
				uint32_t size_low, size_high;
				sprintf(fpath, "%s%s", full_path, fdat.cFileName);
				slen = _strlen(fpath);
				p->src_path = malloc(slen+1);
				_strcpy(p->src_path, fpath);
				if (!folderName) {
					sprintf(fname, "%s", fdat.cFileName);
				}
				else {
					sprintf(fname, "%s/%s", folderName, fdat.cFileName);
				}
				slen = _strlen(fname);
				p->dest_path = malloc(slen + 1);
				_strcpy(p->dest_path, fname);
				hFile=CreateFile(fpath,GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				size_low = GetFileSize(hFile, &size_high);
				CloseHandle(hFile);
				p->size = size_low;
				p++;
			}
		} while (FindNextFile(hFind, &fdat));
		FindClose(hFind);
	}
	return p;
}
/*
	�t�H���_���̃t�@�C���������������܂�
*/

int _sub_MakeFileCatalog_NUM(LPCTSTR datapath) {
	HANDLE hFind;
	WIN32_FIND_DATA fdat;
	char findpath[MAX_PATH];
	int num = 0;
	sprintf(findpath, "%s*", datapath);
	hFind = FindFirstFile(findpath, &fdat);
	if (hFind!= INVALID_HANDLE_VALUE) {
		do {
			printf("%s\n", fdat.cFileName);
			if (fdat.dwFileAttributes == FILE_ATTRIBUTE_DIRECTORY && fdat.cFileName[0] != '.') {
				char folderpath[MAX_PATH];
				sprintf(folderpath, "%s%s\\", datapath, fdat.cFileName);
				printf(">%s\n", folderpath);
				num += _sub_MakeFileCatalog_NUM(folderpath);
			}
			else if (fdat.cFileName[0] != '.') {
				printf("%s\n", fdat.cFileName);
				num++;
			}
		} while (FindNextFile(hFind, &fdat));
		FindClose(hFind);
	}
	return num;
}
/*
	filetable���쐬���܂�
*/
void MakeFileCatalog()
{
	char* datapath;
	datapath = ShowFolderDlg();
	if (datapath) {
		struct fileinfo* p;
		int filenum;
		clean_table();
		filenum = _sub_MakeFileCatalog_NUM(datapath);
		filetable = (struct fileinfo*)malloc((filenum + 1) * sizeof(struct fileinfo));
		p = _sub_MakeFileCatalog(datapath, NULL, filetable);
		p->src_path = NULL;
		p->dest_path = NULL;
	}
}

#else	//WIN32


/*
	�t�H���_���̃t�@�C����������filetable�\���̂ɒl���Z�b�g���܂�
*/
struct fileinfo* _sub_MakeFileCatalog(char* src_path, char* dest_path,struct fileinfo *p) {
	DIR *dir;
	struct dirent *entry;
	if((dir=opendir(src_path))){
		while((entry=readdir(dir))){
			if (entry->d_name[0] != '.') {
				char spath[1025];
				char dpath[256];
				struct stat info;
				if(dest_path){
					_strcpy(dpath,dest_path);
					_strcat(dpath,"/");
					_strcat(dpath,entry->d_name);
				}else{
					_strcpy(dpath,entry->d_name);
				}
				_strcpy(spath, src_path);
				_strcat(spath, entry->d_name);
				if (!stat(spath, &info) ){
					if (S_ISDIR(info.st_mode)){
						_strcat(spath, "/");
						p=_sub_MakeFileCatalog(spath, dpath,p);
					}else{
						int slen;
						slen = _strlen(spath);
						p->src_path = (char *)malloc(slen+1);
						_strcpy(p->src_path,spath);
						slen = _strlen(dpath);
						p->dest_path = (char *)malloc(slen+1);
						_strcpy(p->dest_path,dpath);
						printf("_sub_MakeFileCatalog %s %s\n",p->src_path,p->dest_path);
						p++;
					}
				}
			}	
		}					
	}
	return p;
}
/*
	�t�H���_���̃t�@�C���������������܂�
*/

int _sub_MakeFileCatalog_NUM(char* src_path) {
	DIR *dir;
	int num = 0;
	struct dirent *entry;
	if((dir=opendir(src_path))){
		while((entry=readdir(dir))){
			if (entry->d_name[0] != '.') {
				char spath[1025];
				struct stat info;
				_strcpy(spath, src_path);
				_strcat(spath, entry->d_name);
				printf("%s\n",spath);
				if (!stat(spath, &info) ){
					if (S_ISDIR(info.st_mode)){
						_strcat(spath, "/");
						num+=_sub_MakeFileCatalog_NUM(spath);
					}else{
						num++;
					}
				}
			}						
		}
	}
	return num;
}


void MakeFileCatalog(char *datapath)
{
	if (datapath) {
		struct fileinfo* p;
		int filenum;
		clean_table();
		filenum = _sub_MakeFileCatalog_NUM(datapath);
		filetable = (struct fileinfo*)malloc((filenum + 1) * sizeof(struct fileinfo));
		p = _sub_MakeFileCatalog(datapath, NULL, filetable);
		p->src_path = NULL;
		p->dest_path = NULL;
	}
}
#endif //WIN32


#else	//FILESYSTEM
//ROM�ɓ]���f�[�^��z�u����ꍇ�A�������f�[�^�Ƃ��ďo�̓t�@�C���̃o�C�i���\�L�ϊ��f�[�^��{}���Ɏw�肵�Ă��������B
const char* goi_ini_data ={};
const char* meminfo_txt_data={};
const char* pagedata_dat_data={};
const char* GB0000_gb_data={};
const char* GB0001_gb_data={};
const char* SI0001_lsd_data={};
const char* SI0002_lsd_data={};


struct fileinfo {
	const char* src_ptr;	//�]�����f�[�^�̃|�C���^
	const char* dest_path;	//GOP-LT�ɏ������ݍ̖��̂��w�肵�܂��BGOP.ini������t�H���_�����[�g�Ƃ��A�t�H���_��؂��'/'�Ŏw�肵�܂��B
	int size;				//�t�@�C���T�C�Y���L�q�Bstat���Ŏ擾�\�ł���Ύ��s���擾�ł��\���܂���
} filetable[] = {
//TP�f�U�C�i�[�ō쐬������ʃf�[�^���A�������݃t�@�C���̃J�^���O��p�ӂ��܂��B
//�t�@�C�����y�уt�@�C���T�C�Y�͎��ۂ̊��ɍ��킹�ĕύX���Ă��������B
	{goi_ini_data,"GOP.ini",127},
	{meminfo_txt_data,"meminfo.txt",572},
	{pagedata_dat_data,"pagedata.dat",26396},
	{GB0000_gb_data,"GB00/GB0000.gb",36136},
	{GB0001_gb_data,"GB00/GB0001.gb",36136},
	{SI0001_lsd_data,"SI00/SI0001.lsd",254},
	{SI0002_lsd_data,"SI00/SI0002.lsd",617},
	{NULL,NULL,0},	//(�f�[�^�̏I��)
};
#endif	//FILESYSTEM







#ifdef IS_USE_SPI

BOOL SpiCmd_DATA_START()
{
	MD_STATUS status = MD_OK;
	uint32_t	wrlen=0,len;
	//�R�}���h+�_�~�[
	uint8_t	cmd[2]={SPICMD_DATA_START,0};
	//�R�}���h��1�o�C�g+�_�~�[�o�C�g1�o�C�g��2�o�C�g��������
    SPI_CS(TRUE);
    status = xfer_spi_W(cmd, 2);
    SPI_CS(FALSE);
    return status==MD_OK?TRUE:FALSE;
}



BOOL SpiCmd_DATA_ERASE()
{
	MD_STATUS status = MD_OK;
	uint32_t	wrlen=0,len;
	//�R�}���h+�_�~�[
	uint8_t	cmd[2]={SPICMD_DATA_ERASE,0};
	//�R�}���h��1�o�C�g+�_�~�[�o�C�g1�o�C�g��2�o�C�g��������
    SPI_CS(TRUE);
    status = xfer_spi_W(cmd, 2);
    SPI_CS(FALSE);
    return status==MD_OK?TRUE:FALSE;
}

BOOL SpiCmd_DATA_END()
{
	MD_STATUS status = MD_OK;
	uint32_t	wrlen=0,len;
	//�R�}���h+�_�~�[
	uint8_t	cmd[2]={SPICMD_DATA_END,0};
	//�R�}���h��1�o�C�g+�_�~�[�o�C�g1�o�C�g��2�o�C�g��������
    SPI_CS(TRUE);
    status = xfer_spi_W(cmd, 2);
    SPI_CS(FALSE);
    return status==MD_OK?TRUE:FALSE;
}
BOOL SpiCmd_FILE_HEAD(char *fname,unsigned int filesize)
{
	MD_STATUS status = MD_OK;
	uint32_t	wrlen=0,len,flen=filesize;
	uint8_t	sendbuf[256];
	memset(sendbuf,0,256);
	sendbuf[0]=SPICMD_FILE_HEAD;
	sendbuf[1]=_strlen(fname)+1;
	sendbuf[2]=(flen>>24)&0x000000ff;
	sendbuf[3]=(flen>>16)&0x000000ff;
	sendbuf[4]=(flen>> 8)&0x000000ff;
	sendbuf[5]=(flen    )&0x000000ff;
	//�t�@�C������������
	_strcpy((char *)sendbuf+6,fname);
	//SUM������
	sendbuf[6+_strlen(fname)+1]=CheckSum(sendbuf+6,0,_strlen(fname)+1);
	//�]����
	len=_strlen(fname)+8;	//�p�P�b�g��=�R�}���h(1)+�t�@�C�������w��(1)+�t�@�C���T�C�Y�w��(4)+�t�@�C����(GetLength)+NULL(1)+SUM(1)

    SPI_CS(TRUE);
    status = xfer_spi_W(sendbuf, len+1);//�S�~���̂��߃_�~�[1�o�C�g�ǉ�
    SPI_CS(FALSE);
    return status==MD_OK?TRUE:FALSE;

}

BOOL SpiCmd_FILE_BLOCK(unsigned short no,unsigned char *buf)
{
	MD_STATUS status = MD_OK;
	uint32_t	wrlen=0,len;
	uint8_t	sendbuf[FILEBLOCKSIZE+5];
	int ret=TRUE;
	memset(sendbuf,0,FILEBLOCKSIZE+5);
	sendbuf[0]=SPICMD_FILE_BLOCK;
	sendbuf[1]=(no>>8)&0x00ff;
	sendbuf[2]=(no   )&0x00ff;
	memcpy(sendbuf+3,buf,FILEBLOCKSIZE);
	sendbuf[3+FILEBLOCKSIZE]=CheckSum(buf,0,FILEBLOCKSIZE);
	len=FILEBLOCKSIZE+4;//�p�P�b�g��=�R�}���h(1)+�u���b�N�ԍ�(2)+�f�[�^(1024)+SUM


    SPI_CS(TRUE);
    status = xfer_spi_W(sendbuf, len+1);//�S�~���̂��߃_�~�[1�o�C�g�ǉ�
    SPI_CS(FALSE);

    return status==MD_OK?TRUE:FALSE;
}

BOOL SpiCmd_RAMHEAD(int blockno, unsigned int filesize)
{
	MD_STATUS status = MD_OK;
	uint32_t	wrlen = 0, len, flen = filesize;
	uint8_t	sendbuf[256];
	memset(sendbuf, 0, 256);
	sendbuf[0] = SPICMD_RAMHEAD;
	sendbuf[1] = (uint8_t)((blockno >> 8) & 0x000000ff);
	sendbuf[2] = (uint8_t)((blockno >> 0) & 0x000000ff);
	sendbuf[3] = (uint8_t)((flen >> 16) & 0x000000ff);
	sendbuf[4] = (uint8_t)((flen >> 8) & 0x000000ff);
	sendbuf[5] = (uint8_t)((flen >> 0) & 0x000000ff);
	//�]����
	len = 6;	//�p�P�b�g��=�R�}���h(1)+�u���b�NNO�w��(2)+�t�@�C���T�C�Y�w��(3)

	SPI_CS(TRUE);
	status = xfer_spi_W(sendbuf, len + 1);//�S�~���̂��߃_�~�[1�o�C�g�ǉ�
	SPI_CS(FALSE);
	return status == MD_OK ? TRUE : FALSE;

}

BOOL SpiCmd_RAMBLOCK(unsigned short no, unsigned char* buf)
{
	MD_STATUS status = MD_OK;
	uint32_t	wrlen = 0, len;
	uint8_t	sendbuf[FILEBLOCKSIZE + 5];
	int ret = TRUE;
	memset(sendbuf, 0, FILEBLOCKSIZE + 5);
	sendbuf[0] = SPICMD_RAMBLOCK;
	sendbuf[1] = (no >> 8) & 0x00ff;
	sendbuf[2] = (no) & 0x00ff;
	memcpy(sendbuf + 3, buf, FILEBLOCKSIZE);
	sendbuf[3 + FILEBLOCKSIZE] = CheckSum(buf, 0, FILEBLOCKSIZE);
	len = FILEBLOCKSIZE + 4;//�p�P�b�g��=�R�}���h(1)+�u���b�N�ԍ�(2)+�f�[�^(1024)+SUM


	SPI_CS(TRUE);
	status = xfer_spi_W(sendbuf, len + 1);//�S�~���̂��߃_�~�[1�o�C�g�ǉ�
	SPI_CS(FALSE);

	return status == MD_OK ? TRUE : FALSE;
}



//�G���[���J�o���p
//GOP���Ŏ󂯎���Ă���ŏI�̃u���b�N�ԍ���Ԃ�
BOOL SpiCmd_ReadLastBlock(uint16_t *no)
{
	MD_STATUS status = MD_OK;
	uint32_t wrlen = 0, len;
	uint8_t writebuf[2] = { SPICMD_READ_LASTBLOCK,0 };
	uint8_t readbuf[2];
	SPI_CS(TRUE);
	status = xfer_spi_W(writebuf, 2);
	status = xfer_spi_W(readbuf, 2);
	SPI_CS(FALSE);
	*no = readbuf[0] + readbuf[1] * 0x0100;
	return status == MD_OK ? TRUE : FALSE;
}


BOOL SpiRecovery()
{
	int st=get_syscount();
	while(TRUE){
		uint8_t sta;
		delay(SPI_SLEEP);
		SpiCmd_SPI_RESET();
		delay(SPI_SLEEP);
		if(get_syscount()>st+SPI_TIMEOUT){
			return FALSE;
		}
		if(!SpiCmd_ReadStatus(&sta)){
			return FALSE;
		}
		if(sta==0){
			return TRUE;
		}
	}
}

#ifdef FILESYSTEM

BOOL SpiRamSend(char* src, int blockno, int size)
{
	unsigned short no = 0;
	int now;
	FILE* rf;
	if (size == 0) {
		struct _stat sbuf;
		_stat(src, &sbuf);
		size = sbuf.st_size;
	}
	rf = fopen(src, "rb");//�o�C�i�����[�h�ŊJ���܂�(���ˑ�)

	if (!SpiCmd_RAMHEAD(blockno, size)) {
		return FALSE;
	}
	now = get_syscount();
	while (!Is_Gop_Ready()) {
		if (get_syscount() > now + SPI_TIMEOUT)return FALSE;
	}
	now = get_syscount();
	while (1) {
		uint8_t sta;
		if (!SpiCmd_ReadStatus(&sta)) {
			goto error;
		}
		if (sta == 0)break;
		if (get_syscount() > now + SPI_TIMEOUT)return FALSE;
		SpiCmd_SPI_RESET();
		delay(SPI_SLEEP);
	}
	while (1)
	{
		uint8_t buf[FILEBLOCKSIZE];
		int len;
		int retryct = 0;
	retry:
		retryct++;
		if (retryct == 10) {
			goto error;
		}
		memset(buf, 0, FILEBLOCKSIZE);
		fseek(rf, no * FILEBLOCKSIZE, SEEK_SET);
		len = fread(buf, 1, FILEBLOCKSIZE, rf);
		if (len == 0) {
			break;
		}
		if (!SpiCmd_RAMBLOCK(no, buf)) {
			goto error;
		}
		now = get_syscount();
		while (!Is_Gop_Ready()) {
			if (get_syscount() > now + SPI_TIMEOUT) {
				goto error;
			}
		}
		now = get_syscount();
		while (1) {
			uint8_t sta;
			if (!SpiCmd_ReadStatus(&sta)) {
				goto error;
			}
			if (sta & SPI_STATE_STALL)
			{
				if (!SpiRecovery())goto error;
				else goto retry;

			}
			if (sta & SPI_STATE_ERRCMD) {
				if (!SpiRecovery())goto error;
				else goto retry;
			}
			if (sta & SPI_STATE_BUSY) {
				break;
			}
			if (sta & SPI_STATE_RETRY) {
				if (!SpiRecovery())goto error;
				else {
					uint16_t tmp_no;
					//����ɏ������܂�Ă���Ō�̃u���b�N�ԍ����擾
					if (!SpiCmd_ReadLastBlock(&tmp_no))
					{
						goto error;
					}
					no = tmp_no + 1;
					retryct = 0;
					delay(SPI_SLEEP);
				}
				goto retry;
			}
			else if (sta & SPI_STATE_ERROR) {
				goto error;
			}
			if (get_syscount() > now + SPI_TIMEOUT)return FALSE;
			delay(SPI_SLEEP);
		}
		if (len < 1024) {
			break;
		}
		no++;
	}
	fclose(rf);
	return TRUE;
error:
	fclose(rf);
	return FALSE;
}


BOOL SpiFileSend(char *src,char *dest,int size)
{
	unsigned short no=0;
	int now;
	FILE *rf;
	rf=fopen(src,"rb");//�o�C�i�����[�h�ŊJ���܂�(���ˑ�)
	if(!SpiCmd_FILE_HEAD(dest,size)){
		return FALSE;
	}
	now=get_syscount();
	while(!Is_Gop_Ready()){
		if(get_syscount()>now+SPI_TIMEOUT)return FALSE;
	}
	now=get_syscount();
	while(1){
		uint8_t sta;
		if(!SpiCmd_ReadStatus(&sta)){
			goto error;
		}
		if(sta==0)break;
		if(get_syscount()>now+SPI_TIMEOUT)return FALSE;
		SpiCmd_SPI_RESET();
		delay(SPI_SLEEP);
	}
	while(1)
	{
		uint8_t buf[FILEBLOCKSIZE];
		int len;
		int retryct=0;
retry:
		retryct++;
		if(retryct==10){
			goto error;
		}
		memset(buf,0,FILEBLOCKSIZE);
		fseek(rf,no*FILEBLOCKSIZE,SEEK_SET);
		len=fread(buf,1,FILEBLOCKSIZE,rf);
		if(len==0){
			break;
		}
		if(!SpiCmd_FILE_BLOCK(no,buf)){
			goto error;
		}
		now=get_syscount();
		while(!Is_Gop_Ready()){
			if(get_syscount()>now+SPI_TIMEOUT){
				goto error;
			}
		}
		now=get_syscount();
		while(1){
			uint8_t sta;
			if(!SpiCmd_ReadStatus(&sta)){
				goto error;
			}
			if(sta&SPI_STATE_STALL)
			{
				if(!SpiRecovery())goto error;
				else goto retry;

			}
			if(sta&SPI_STATE_ERRCMD){
				if(!SpiRecovery())goto error;
				else goto retry;
			}
			if(sta&SPI_STATE_BUSY){
				break;
			}
			if(sta&SPI_STATE_RETRY){
				if(!SpiRecovery())goto error;
				else{
					uint16_t tmp_no;
					//����ɏ������܂�Ă���Ō�̃u���b�N�ԍ����擾
					if(!SpiCmd_ReadLastBlock(&tmp_no))
					{
						goto error;
					}
					no=tmp_no+1;
					retryct=0;
					delay(SPI_SLEEP);
				}
				goto retry;
			}else if(sta&SPI_STATE_ERROR){
				goto error;
			}
			if(get_syscount()>now+SPI_TIMEOUT)return FALSE;
			delay(SPI_SLEEP);
		}
		if(len<1024){
			break;
		}
		no++;
	}
	fclose(rf);
	return TRUE;
error:
	fclose(rf);
	return FALSE;
}


BOOL CopyAllSPI() {
	int i = 0;
	while (TRUE) {
		if (filetable[i].src_path == NULL) {
			break;
		}
		else {
			if (!SpiFileSend(filetable[i].src_path, filetable[i].dest_path, filetable[i].size)) {
				return FALSE;
			}
		}
		i++;
	}
	return TRUE;
}

#else
BOOL SpiFileSend(char *src,char *dest,int size)
{
	unsigned short no=0;
	int now;
	if(!SpiCmd_FILE_HEAD(dest,size)){
		return FALSE;
	}
	now=get_syscount();
	while(!Is_Gop_Ready()){
		if(get_syscount()>now+SPI_TIMEOUT)return FALSE;
	}
	now=get_syscount();
	while(1){
		uint8_t sta;
		if(!SpiCmd_ReadStatus(&sta)){
			goto error;
		}
		if(sta==0)break;
		if(get_syscount()>now+SPI_TIMEOUT)return FALSE;
		SpiCmd_SPI_RESET();
		delay(SPI_SLEEP);
	}
	while(1)
	{
		uint8_t buf[FILEBLOCKSIZE];
		int len;
		int retryct=0;
retry:
		retryct++;
		if(retryct==10){
			goto error;
		}
		memset(buf,0,FILEBLOCKSIZE);
		memcpy(buf,src+no*FILEBLOCKSIZE,FILEBLOCKSIZE);
		len-=FILEBLOCKSIZE;
		if(len==0){
			break;
		}
		if(!SpiCmd_FILE_BLOCK(no,buf)){
			goto error;
		}
		now=get_syscount();
		while(!Is_Gop_Ready()){
			if(get_syscount()>now+SPI_TIMEOUT){
				goto error;
			}
		}
		now=get_syscount();
		while(1){
			uint8_t sta;
			if(!SpiCmd_ReadStatus(&sta)){
				goto error;
			}
			if(sta&SPI_STATE_STALL)
			{
				if(!SpiRecovery())goto error;
				else goto retry;

			}
			if(sta&SPI_STATE_ERRCMD){
				if(!SpiRecovery())goto error;
				else goto retry;
			}
			if(sta&SPI_STATE_BUSY){
				break;
			}
			if(sta&SPI_STATE_RETRY){
				if(!SpiRecovery())goto error;
				else{
					uint16_t tmp_no;
					//����ɏ������܂�Ă���Ō�̃u���b�N�ԍ����擾
					if(!SpiCmd_ReadLastBlock(&tmp_no))
					{
						goto error;
					}
					no=tmp_no+1;
					retryct=0;
					delay(SPI_SLEEP);
				}
				goto retry;
			}else if(sta&SPI_STATE_ERROR){
				goto error;
			}
			if(get_syscount()>now+SPI_TIMEOUT)return FALSE;
			delay(SPI_SLEEP);
		}
		if(len<1024){
			break;
		}
		no++;
	}
	return TRUE;
error:
	return FALSE;
}


BOOL CopyAllSPI() 
{
	int i = 0;
	while (TRUE) {
		if (filetable[i].src_ptr == NULL) {
			break;
		}
		else {
			if (!SpiFileSend(filetable[i].src_ptr, filetable[i].dest_path, filetable[i].size)) {
				return FALSE;
			}
		}
		i++;
	}
	return TRUE;
}
#endif


BOOL DoTransfarDataSPI()
{
	uint8_t sta;	//ReadStatus��M�p
	int start_time;//�^�C���A�E�g�v���p
	int retrycount = 0;//���g���C�J�E���g�p


	//(1)��Ԃ��A�C�h�����Ȃ̂��m�F

	start_time = get_syscount();
	while (!Is_Gop_Ready())//GopReady�m�F SPI�R�}���h���M�O�ɂ͕K��GOPR�����Ȃ̂��m�F
	{
		if (get_syscount() > start_time + SPI_TIMEOUT)
		{
			//GOPR�s���^�C���A�E�g
			goto error;
		}
	}

	while (1)
	{
		if (!SpiCmd_ReadStatus(&sta))
		{
			//SPI�ʐM���s
			goto error;
		}
		if (sta == 0) {
			//�X�e�[�^�X��0(�A�C�h��)�Ȃ�Δ�����
			break;
		}
		retrycount++;
		if (retrycount > 5) {
			//���g���C�J�E���g�I�[�o�[�i�s���̃G���[�j
			goto error;
		}
		delay(50);
	}

	//(2)�f�[�^���[�h�ֈڍs
	if (!SpiCmd_DATA_START())	//�R�}���h���s
	{
		//SPI�ʐM���s
		goto error;
	}
	start_time = get_syscount();
	while (!Is_Gop_Ready())
	{
		if (get_syscount() > start_time + SPI_TIMEOUT)
		{
			//GOPR�s���^�C���A�E�g
			goto error;
		}
	}
	//���[�h�ڍs�����҂����[�v
	while (1)
	{
		if (!SpiCmd_ReadStatus(&sta))
		{
			//SPI�ʐM���s
			goto error;
		}
		if (sta & (~SPI_STATE_BUSY))
		{
			//�X�e�[�^�X��Busy�r�b�g�ȊO�̃r�b�g�������Ă�
			goto error;
		}
		if (!(sta & SPI_STATE_BUSY))
		{
			break;//Busy�������ꂽ��҂����[�v���甲����
		}
		if (start_time + SPI_TIMEOUT < get_syscount())
		{
			//Busy�����҂��^�C���A�E�g
			goto error;
		}
		delay(SPI_SLEEP);
	}
	//(3)�f�[�^����
	if (!SpiCmd_DATA_ERASE())	//�R�}���h���s
	{
		//SPI�ʐM���s
		goto error;
	}
	start_time = get_syscount();
	while (!Is_Gop_Ready())
	{
		if (get_syscount() > start_time + SPI_TIMEOUT)
		{
			//GOPR�s���^�C���A�E�g
			goto error;
		}
	}
	//���������҂����[�v
	while (1) {
		if (!SpiCmd_ReadStatus(&sta))
		{
			//SPI�ʐM���s
			goto error;
		}
		if (sta & (~SPI_STATE_BUSY))
		{
			//�X�e�[�^�X��Busy�r�b�g�ȊO�̃r�b�g�������Ă�
			goto error;
		}
		if (!(sta & SPI_STATE_BUSY))
		{
			break;//Busy�������ꂽ��҂����[�v���甲����
		}
		if (start_time + SPI_TIMEOUT < get_syscount())
		{
			//�t���b�V�������^�C���A�E�g
			goto error;
		}
		delay(SPI_SLEEP);
	}
	//(4)�f�[�^�t�@�C���������݊J�n
	if (!CopyAllSPI())
	{
		//�t�@�C���]�����s
		goto error;
	}
	//(5)�I������
	if (!SpiCmd_DATA_END())	//�R�}���h���s
	{
		//SPI�ʐM���s
		goto error;
	}
	start_time = get_syscount();
	while (!Is_Gop_Ready())
	{
		if (get_syscount() > start_time + SPI_TIMEOUT)
		{
			//GOPR�s���^�C���A�E�g
			goto error;
		}
	}
	start_time = get_syscount();
	while (1)
	{
		if (!SpiCmd_ReadStatus(&sta))
		{
			//SPI�ʐM���s
			goto error;
		}
		if (sta & (~SPI_STATE_BUSY))
		{
			//�X�e�[�^�X��Busy�r�b�g�ȊO�̃r�b�g�������Ă�
			goto error;
		}
		if (!(sta & SPI_STATE_BUSY))
		{
			break;//Busy�������ꂽ��҂����[�v���甲����
		}
		if (start_time + SPI_TIMEOUT < get_syscount())
		{
			//Busy�����҂��^�C���A�E�g)
			goto error;
		}
		delay(SPI_SLEEP);
	}
	//(6)GOP-LT�ċN��
	ResetOut(0);
	delay(200);
	ResetOut(1);
	return TRUE;
error:
	return FALSE;
}

#endif
#endif
#define DATASIZE 256
#define PACKSIZE (DATASIZE+8)
#define FASTUPLOAD_TIMEOUT 60000
void send_cmd(char* cmd) {
	uint8_t sum = 0;
	ltputs("\x02");
	sum = puts_sum(cmd);
	ltputs("\x03");
	ltputs(byte_tohexstr(sum));
	ltputs("\r");
}
#ifdef FILESYSTEM
int RamUpload(char *srcname, int blockno, int packsize)
{
	FILE* rf;
	struct _stat sbuf;
	int size, addr = 0;
	_stat(srcname, &sbuf);
	size = sbuf.st_size;
	rf=fopen(srcname,"r");
	if(rf){
		int rlen;
		char cmd_buf[64], * cmd;
		uint32_t now; 
		if (packsize) {
			sprintf(cmd_buf,"RAMUPLOAD %04x %d %d", blockno, size,packsize);
		}
		else {
			sprintf(cmd_buf, "RAMUPLOAD %04x %d", blockno, size);
			packsize = DATASIZE;
		}
		send_cmd(cmd_buf);
		now = get_syscount();
		while (!(cmd = gets_lt(cmd_buf, sizeof(cmd_buf), TIMEOUT)));
		if (cmd[0] != 0x06) {
			goto err;
		}
		//2�s�ȍ~
		while (1) {
			uint8_t sum = 0;
			uint8_t* rbuf=(uint8_t*)malloc(packsize);
			if (!rbuf)goto err;
			memset(rbuf,0, packsize);

			int rs = fread(rbuf,1, packsize,rf);
			if (rs == 0)break;
			if (rs != 0) {
				int i;
				uart_putc(0x02);
				uart_putc((uint8_t)((addr >> 0) & 0x000000ff));
				uart_putc((uint8_t)((addr >> 8) & 0x000000ff));
				uart_putc((uint8_t)((addr >> 16) & 0x000000ff));
				uart_putc((uint8_t)((addr >> 24) & 0x000000ff));
				sum = (uint8_t)((addr >> 0) & 0x000000ff) ^ (uint8_t)((addr >> 8) & 0x000000ff) ^ (uint8_t)((addr >> 16) & 0x000000ff) ^ (uint8_t)((addr >> 24) & 0x000000ff);
				for (i = 0; i < packsize; i++) {
					uart_putc( rbuf[i]);
					sum ^= rbuf[i];
				}
				uart_putc(0x03);
				uart_putc(sum);
				uart_putc(0x0d);
				addr += packsize;
			}
			free(rbuf);
			now = get_syscount();
			while (!(cmd = gets_lt(cmd_buf, sizeof(cmd_buf), TIMEOUT)));
			if (cmd[0] != 0x06) {
				goto err;
			}
			if (rs < packsize)break;

		}
		fclose(rf);
		return TRUE;
err:
		fclose(rf);
	}
	return FALSE;
}

#define DATASIZE 256
#define PACKSIZE (DATASIZE+8)
#define FASTUPLOAD_TIMEOUT 60000

void debug_binary(int mode);

int FileUpload(char *src,char *dest)
{
	FILE *rf;
	struct _stat sbuf;
	int rlen;
	int size,addr=0;
	char cmd_buf[64], * cmd;
	_stat(src, &sbuf);
	size = sbuf.st_size;
	rf=fopen(src,"r");
	printf("FileUpload %s %s\n",src,dest);
	if(rf){
		uint32_t now;
		unsigned sum;
		int posid = 0;
		//1�s��
		sprintf(cmd_buf,"FILEUPLOAD %s %d", dest, size);
		send_cmd(cmd_buf);
		now=get_syscount();
		while (!(cmd = gets_lt(cmd_buf, sizeof(cmd_buf), TIMEOUT)));
		if (cmd[0] != 0x06) {
			goto err;
		}
		//2�s�ȍ~
		while(1){
			uint8_t rbuf[DATASIZE];
			memset(rbuf,0,DATASIZE);
			int rs = fread(rbuf, 1,DATASIZE,rf);
			if(rs==0)break;
		 	debug_binary(TRUE);
			if (rs != 0) {
				int i;
				uart_putc(0x02);
				uart_putc((uint8_t)((addr >> 0) & 0x000000ff));
				uart_putc((uint8_t)((addr >> 8) & 0x000000ff));
				uart_putc((uint8_t)((addr >> 16) & 0x000000ff));
				uart_putc((uint8_t)((addr >> 24) & 0x000000ff));
				sum = (uint8_t)((addr >> 0) & 0x000000ff) ^ (uint8_t)((addr >> 8) & 0x000000ff) ^ (uint8_t)((addr >> 16) & 0x000000ff) ^ (uint8_t)((addr >> 24) & 0x000000ff);
				for (i = 0; i < DATASIZE; i++) {
					uart_putc( rbuf[i]);
					sum ^= rbuf[i];
				}
				uart_putc(0x03);
				uart_putc(sum);
				uart_putc(0x0d);
				addr += DATASIZE;
			}
		 	debug_binary(FALSE);
			now = get_syscount();
			while (!(cmd = gets_lt(cmd_buf, sizeof(cmd_buf), TIMEOUT)));
			if (cmd[0] != 0x06) {
				goto err;
			}
			if (rs < DATASIZE)break;

		}
		fclose(rf);
	}
	return TRUE;
err:
	fclose(rf);
	return FALSE;

}
BOOL CopyAllSer() 
{
	int i = 0;
	while (TRUE) {
		if (filetable[i].src_path == NULL) {
			break;
		}
		else {
			if (!FileUpload(filetable[i].src_path, filetable[i].dest_path)) {
				return FALSE;
			}
		}
		i++;
	}
	return TRUE;
}


BOOL Serial_Init();
void Serial_Close();
#define ERASE_TIMEOUT 1000000
#define REBOOT_TIMEOUT 10000

BOOL DoTransfarDataSer(char *pathname){
	uint32_t now;
	BOOL ret;
	char cmd_buf[64], * cmd;
	//�X�V�f�[�^������t�H���_���m�F���A�������݃f�[�^�̃��X�g���쐬���܂��B
	MakeFileCatalog(pathname);
	if(filetable==NULL){
		//�X�V�f�[�^���X�g����������Ȃ���΃G���[
		return FALSE;
	}
	now	= get_syscount();
	printf("GOP-CT���f�[�^�����������[�h�ōċN�����܂�\n");

	send_cmd("REWRITEMODE_SETFLAG");
	now = get_syscount();
	while (!(cmd = gets_lt(cmd_buf, sizeof(cmd_buf), TIMEOUT)));
	if (cmd[0] != 0x06) {
		return FALSE;
	}

	printf("�ċN�����E�E�E");
	send_cmd("RESET");
	Serial_Close();//GOP���ċN�����邽�߈�x�|�[�g����܂�
	delay(3000);	//�ċN���҂�
	now = get_syscount();
	//�ċN����V���A���|�[�g�Đڑ�
	while (!Serial_Init())
	{
		if (get_syscount() > now + REBOOT_TIMEOUT)
		{
			printf("�ċN����A�|�[�g�ăI�[�v���^�C���A�E�g\n");
			return FALSE;
		}
	}
	printf("�f�[�^�����������[�h�Ɉڍs\n");
	send_cmd("DATASTART");
	now = get_syscount();
	while (!(cmd = gets_lt(cmd_buf, sizeof(cmd_buf), TIMEOUT)));
	if (cmd[0] != 0x06) {
		//ack�ȊO�ŃG���[
		return FALSE;
	}
	printf("�f�[�^�������E�E�E\n");
	send_cmd("DATAERASE");
	now = get_syscount();
	//�f�[�^�����͎��Ԃ������邽�߃^�C���A�E�g�͒��߂ɂ��܂�
	while (!(cmd = gets_lt(cmd_buf, sizeof(cmd_buf), ERASE_TIMEOUT)));
	if (cmd[0] != 0x06) {
		return FALSE;
	}
	printf("�f�[�^�������ݒ��E�E�E\n");
	//�X�V�f�[�^���X�g�̃t�@�C�������ׂē]�����܂��B
	ret=CopyAllSer();
	if (!ret) {
		return FALSE;
	}
	//�������������̃N���[�W���O���s���܂��B
	send_cmd("DATAEND");
	now = get_syscount();
	while (!(cmd = gets_lt(cmd_buf, sizeof(cmd_buf), ERASE_TIMEOUT)));
	if (cmd[0] != 0x06) {
		return FALSE;
	}
	printf("�f�[�^�������݊����B�ċN�����܂��B���΂炭���҂���������\n");
	send_cmd("RESET");
	Serial_Close();
	delay(3000);
	now = get_syscount();
	while (!Serial_Init())
	{
		if (get_syscount() > now + REBOOT_TIMEOUT)
		{
			printf("�ċN����A�|�[�g�ăI�[�v���^�C���A�E�g\n");
			return FALSE;
		}
	}
	return TRUE;
}




#endif
/*
	�@�\	GOP�ɔC�ӂ̃��b�Z�[�W�𑗐M���A�ԐM��Ԃ��B
			
	�敪	public
	����	command ���M���郁�b�Z�[�W
	        rcv	���b�Z�[�W�i�[�p�o�b�t�@�[�̃A�h���X
				�����b�Z�[�W����M����K�v�Ȃ��ꍇNULL���w��
	�߂�l	rcv��NULL�̏ꍇ
				COMMERR(0xffffffff) �ʐM���s
				NULL	�ʐM����
			rcv��NULL�ȊO
				COMMERR(0xffffffff) �ʐM���s
				NULL	���b�Z�[�W�Ȃ�
				NULL�ȊO	�擾�������b�Z�[�W�̐擪�A�h���X
			�ʐM���s������_get_error_code�Ŏ擾�ł��܂��B
	���l
			��LtSetMessageCallback�Ń��b�Z�[�W�n���h���e�[�u����o�^���Ă���ꍇ�B
*/
const char FAR *LtSendCommand(char FAR *command,char FAR *buf,int buf_size)
{
	uint8_t sum=0;
	char *s;
	ltMes_Callback FAR *p=mes_callback;

	ltputs("\x02");
	sum=puts_sum(command);
	ltputs("\x03");
	ltputs(byte_tohexstr(sum));
	ltputs("\r");
	s=gets_lt(buf,buf_size,TIMEOUT);
	if(s){
		
		return s;
	}
	return (const char *)COMMERR;
}
