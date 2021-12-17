/*修正履歴
	2018-6-13
		・受信待ちのタイムアウト時間に送信電文長を考慮するよう修正
		・UARTでのグラフメモリアクセスを*NG->*NIに変更。(通信量減らすため)
	2018-9-27
		・テキストメモリーアクセス用の関数群追加
			LtTMemWrite/Read､SPI_LtTMemWrite/Read
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
#define _stat stat
#endif
#endif
#include "goplt_if.h"

/*
	定数定義
*/

//使用環境により設定変更可能

//LtMemArrayReadで、同時に読込可能なメモリーの最大数を定義(最大128)
//本値を1増やす都度LtMemArrayReadでの使用スタックサイズが4バイトずつ増えます
#define MAX_NUM 16
//LtGMemReadコマンドで、読込可能なグラフのﾌﾟﾛｯﾄ数を定義(最大480)
//本値を1増やす都度LtGMemReadでの使用スタックサイズが1バイトずつ増えます
#define MAX_PLOT 480
//タイムアウト値(単位ms)
#ifdef __RL78__
#define TIMEOUT 100
#else
#define TIMEOUT 500	//WINDOWS環境ではレイテンシ長めのためタイムアウト長めに設定
#endif

//想定しうる最大のメモリー名バイト数
#define MAX_MEMNAME_SIZE 32
//想定しうる最大のメッセージ長バイト数
#define MAX_MESSAGE_SIZE 32
//以下は変更しないでください
//パケットのベースサイズ
#define PACKET_INFO_SIZE	12
//想定しうるデータ1個当たりの最大バイト数
#define ONCE_DATA_SIZE	11
/*
	GOPコマンド処理用関数群
*/

/*
	機能	チェックサム計算
	区分	static
	引数	cmd	sum計算する文字列の先頭アドレス
	戻り値	sum値
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
	機能	1バイト整数値を16進状文字列に変換
	区分	static
	引数	b	変換する値
	戻り値	変換された文字列のアドレス
	備考	戻り値は次に本関数を呼び出すまで有効
			したがって以下の使い方はNG
			char str10],*s1,*s2;
			s1=byte_tohexstr(0x12);
			s2=byte_tohexstr(0x34);
			strcpy(str,s1);
			strcat(str,s2);
			上記ではstrは"3434"となる
			
			"1234"を得たい場合
			s1=byte_tohexstr(0x12);
			strcpy(str,s1);
			s2=byte_tohexstr(0x34);
			strcat(str,s2);
			とする
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
	機能	16進状文字列を1バイト整数に変換
	区分	static
	引数	bstr	変換する文字列
	戻り値	変換された値
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
	機能	指定した文字列をuartに出力
	区分	static
	引数	cmd	出力する文字列
*/

static void ltputs(char FAR *cmd)
{
	while(*cmd){
		uart_putc(*cmd++);
	}
}
/*
	機能	sum値を計算しながら指定した文字列をuartに出力
	区分	static
	引数	cmd	出力する文字列
	戻り値	sum値
*/

static uint8_t puts_sum(char *cmd){
	uint8_t sum;
	sum=_calc_sum(cmd);
	ltputs(cmd);
	return sum;
}
//標準関数非依存対策
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
/*
	用途	通信エラー内容の保持
	区分	static
*/
static uint8_t _error_status;
/*
	機能	通信エラー内容の取得
	区分	public
	戻り値	エラーコード(定義内容はgoplt_if.h参照)
*/
uint8_t _get_error_code()
{
	return _error_status;
}
/*
	機能	受信データの取得
	区分	static
	引数	buf	受信データ格納バッファーのアドレス
			bufsize	バッファーのサイズ(受信可能な電文長はこれで規定されます)
			timeout	タイムアウト時間
	戻り値	NULL	通信失敗
					失敗理由は_get_error_codeで取得可
			NULL以外	受信電文のポインタ
						受信電文はstx,etx,sum分離され正味の文字列となります
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
				//ackまたはnacの場合
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
				//stx~データが始まらない=電文が壊れている場合
				_error_status|=ERROR_STX;
				//受信データをcrまで取り込んで関数を抜ける
				while((r=uart_getc())!='\r'){
					if(get_syscount()-st>timeout){
						_error_status|=ERROR_TIMEOUT;
						return NULL;
					}
				}				
				return NULL;
			}
			//それ以外(正しく電文が始まっている場合)
			s=buf;
			while(TRUE){
				while((r=uart_getc())==-1){
					//受信データ未達時のタイムアウト確認
					if(get_syscount()-st>timeout){
						_error_status|=ERROR_TIMEOUT;
						return NULL;
					}
				}
				st=get_syscount();
				if(r!=0x03){
					//etxでない場合はデータとしてsumを計算しながら取り込む
					*s++=r;
					sum^=(uint8_t)r;
					bufsize--;
					if(bufsize==0){
						//用意されたバッファーに収まらない電文を受信時
						//バッファーオーバーのエラーフラグをセットして
						_error_status|=ERROR_OVERFLOW;
						//受信データが終わるまでデータを取り出しておく
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
	機能	整数値を10進文字列に変換
	区分	static
	引数	val	変換する値
			buf	変換文字列格納用バッファーのアドレス(バッファーは11バイト以上確保されていること)
	戻り値	変換された文字列のポインタ
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
	機能	GOP-LTのRST_OUT操作
	区分	public
	引数	sta	出力状態　0:Low(リセット) 0以外:High
	備考	RST_OUTに接続されているIOを直接操作しています。ボード移植時等変更が必要です
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
	機能	GOP_READYピンの状態確認
	区分	public
	戻り値	TRUE	READY状態(通信可能)
			FALSE	BUSY状態(通信不可)
	備考	GOP_READYに接続されているIOを直接参照しています。ボード移植時等変更が必要です
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
	機能	SET_SEND_DATAピンの状態確認
	区分	public
	戻り値	TRUE	GOP-LTからの送出要求あり(ENQで取り出す必要あり)
			FALSE	GOP-LTからの送出要求なし
	備考	SET_SEND_DATAに接続されているIOを直接参照しています。ボード移植時等変更が必要です
*/
BOOL Is_SetData()
{
#ifdef __RL78__
	return (P1&0x02)?FALSE:TRUE;	//LOWアクティブのため
#elif defined(WIN32)
	return (getio() & 0x02) ? FALSE : TRUE;
#endif
}

#ifdef IS_USE_SPI
/*
	機能	SSL(SPIスレーブセレクト)ピンの操作
	区分	static
	引数	sta	0:スレーブ選択
				1:選択解除
	備考	SSLに接続されているIOを直接操作しています。ボード移植時等変更が必要です
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

//SPI操作API群
//以下はTPデザイナーLT用に作成したSPI通信ライブラリのRL向け移植です。
//goplt_ifライブラリでは以下のAPIをそのまま使用せずuart向けAPIと互換形式になるよう
//ラッパー関数を介して以下を使用します


//GOP-LT SPIコマンド定義
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
#define SPICMD_RAMWRITE				    0xd0    //RAMバッファーファイルデータ送信
#define SPICMD_RAMHEAD					0xd1    //ファイルオープン(ファイル名、ファイルサイズを通知)
#define SPICMD_RAMBLOCK					0xd2    //ファイルデータ送信(ブロック単位、既定サイズ受信で自動クローズ)

#define SPICMD_SPI_RESET				0xff

//GOP-LT SPI状態定義
#define SPI_STATE_BUSY		0x01	//コマンドによる動作中
#define SPI_STATE_ERROR		0x02	//コマンドを実行できない(ファイルにデータを書き込めない、メモリー名がおかしいなど)
#define SPI_STATE_RETRY		0x04	//受信コマンドがおかしい(SUM不一致、連番はずれなど)
#define SPI_STATE_STALL		0x08	//コマンドタイムアウト(コマンド未完成状態で一定時間無通信)
#define SPI_STATE_ERRCMD	0x10	//不正コマンド
#define SPI_STATE_READREADY	0x20	//返信データセット
#define SPI_STATE_ERRINT	0x40	//割り込みエラー
#define SPI_STATE_BLOCK		0x80	//ブロック不連続


/*
	機能	SPIのチェックサムを返す
	区分	static
	引数	buf	sum計算する領域の先頭アドレス
			start	sumの初期値
			size	sum計算するバイト数
	戻り値	sum値
	備考	uartのsumとは計算方法が異なります
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
	機能	GOPのSPIステートマシンの状態をリセットします
	区分	static
	備考	spiステータスがエラーになった場合などにSPI_RESETコマンドを送ります。
*/
void SpiCmd_SPI_RESET(){
    MD_STATUS status = MD_OK;
	uint8_t _buf[2]={SPICMD_SPI_RESET,0};
	//2バイトライト
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf, 2);
	SPI_CS(FALSE);
}

/*
	機能	GOPのSPIステートマシンの状態を読み込みます
	区分	static
	引数	sta	ステータスを格納するためのuint8_t型変数のポインタ
	戻り値	TRUE	SPI転送成功
			FALSE	SPI転送失敗
*/
BOOL SpiCmd_ReadStatus(uint8_t FAR *sta)
{
    MD_STATUS status = MD_OK;
	//バッファーにコマンドをセット(本コマンドではダミーです)
	uint8_t _buf[1]={SPICMD_READ_STATUS};
	//1バイトリード
	SPI_CS(TRUE);
	status = xfer_spi_R(_buf, 1);
	SPI_CS(FALSE);
	*sta=_buf[0];
	return status==MD_OK?TRUE:FALSE;
}


/*
	機能	GOPに送出要求(ENQコマンド)を送信します
			READSTATUSでSPI_STATE_READREADYがセットされると送出電文を取り出し可
	区分	static
	戻り値	TRUE	SPI転送成功
			FALSE	SPI転送失敗
*/
BOOL SpiCmd_ENQ_REQ(){
	MD_STATUS status = MD_OK;
	uint8_t _buf[2]={SPICMD_ENQ_REQ,0};
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf,1);
	SPI_CS(FALSE);
	return status==MD_OK?TRUE:FALSE;
}
//ENQ_READの結果のenum型
typedef enum {
	ENQ_NODATA=0,
	ENQ_OK,
	ENQ_ERROR=99
} ENQ_RETSTATUS;

/*
	機能	ENQコマンドの返信を取得します
	区分	static
	引数	read_buf	受信電文を格納するバッファーのアドレス
	戻り値	ENQ_NODATA	SPI転送成功、受信電文なし
			ENQ_OK		SPI転送成功、受信電文をbufに格納
			ENQ_ERROR	SPI転送失敗
	備考	read_bufは呼び出し側で想定される最大メッセージ長を格納できる長さ(終端含む)+1(sum領域)の領域を用意する
*/
ENQ_RETSTATUS SpiCmd_ENQ_READ(uint8_t FAR *read_buf){
	MD_STATUS status = MD_OK;
	int16_t len;
	uint8_t _buf[2]={SPICMD_ENQ_READ,0};
	SPI_CS(TRUE);
	//コマンド＆ダミーバイト(読み込みへ切り替え前にゴミ除去のため1バイト分送信)
	status = xfer_spi_W(_buf, 2);
	if(status!=MD_OK)goto err;
	status = xfer_spi_R(read_buf, 1);
	if(status!=MD_OK)goto err;

	if((len=read_buf[0])==0){
		//ダミーで1バイト読み込みCSを解除
		status = xfer_spi_R(read_buf, 1);
		if(status!=MD_OK)goto err;
		SPI_CS(FALSE);
		strcpy(read_buf,"");
		return ENQ_NODATA;
	}else{
		status = xfer_spi_R(read_buf, len+1);
		if(status!=MD_OK)goto err;
		SPI_CS(FALSE);
		//SUM確認
		if(read_buf[len]==CheckSum(read_buf,0,len)){
			return ENQ_OK;
		}else{
			strcpy(read_buf,"");
			return ENQ_ERROR;
		}
	}
err:
	SPI_CS(FALSE);
	return ENQ_ERROR;
}

/*
	機能	GOPにグラフメモリーの読み出し要求を送信します
			READSTATUSでSPI_STATE_READREADYがセットされるとグラフメモリーのリード可
	区分	static
	引数	memname	読み出しするグラフメモリー名
			offset	読み込み開始する位置
			num	読み込むプロット点数
	戻り値	TRUE	SPI転送成功
			FALSE	SPI転送失敗
*/
BOOL SpiCmd_MEM_BIOffset_READ_REQ(uint8_t FAR* memname,int16_t offset,int16_t num){
	MD_STATUS status = MD_OK;
	uint8_t _buf[6+MAX_MEMNAME_SIZE],sum;

	//データ パケット作成
	_buf[0]=SPICMD_MEM_BI_READ_REQ;
	_buf[1]=strlen(memname)+1;
	_buf[2]=(uint8_t)(offset/256);
	_buf[3]=(uint8_t)(offset%256);
	_buf[4]=(uint8_t)(num/256);
	_buf[5]=(uint8_t)(num%256);
	strcpy(_buf+4,memname);
	sum=CheckSum(memname,0,strlen(memname)+1);
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf, 6+strlen(memname)+1);
	SPI_CS(FALSE);
	return status==MD_OK?TRUE:FALSE;
}

/*
	機能	GOPにグラフメモリーの読み出し
	区分	static
	引数	num	読み込むプロット点数(SpiCmd_MEM_BIOffset_READ_REQで指定した値と同数指定すること)
			read_buf	読み込んだデータを格納する領域のアドレス(領域サイズはnum文確保されていること)
	戻り値	TRUE	SPI転送成功
			FALSE	SPI転送失敗
*/
BOOL SpiCmd_MEM_BI_READ_READ(int16_t num,uint8_t *read_buf)
{
	MD_STATUS status = MD_OK;
	uint8_t _buf[2]={SPICMD_MEM_BI_READ_READ,0},sum;
	memset(read_buf,0,num);
	SPI_CS(TRUE);
	//コマンド＆データ長+ダミーバイト(読み込みへ切り替え前にゴミ除去のため1バイト分送信)
	status = xfer_spi_W(_buf, 2);
	if(status!=MD_OK)goto err;
	//データ受信
	status = xfer_spi_R(read_buf,num);
	if(status!=MD_OK)goto err;
	//sum受信
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
	機能	GOPのグラフメモリーにデータを書き込みます
	区分	static
	引数	memname	書き込みグラフメモリー名
			offset	書き込み開始する位置
			num	書き込むプロット点数
			write_buf	書き込むデータが格納されている領域の先頭アドレス
	戻り値	TRUE	SPI転送成功
			FALSE	SPI転送失敗
*/
BOOL SpiCmd_MEM_BIOffset_WRITE(uint8_t FAR* memname,int16_t offset,int16_t num,uint8_t FAR * write_buf)
{
	MD_STATUS status = MD_OK;
	uint8_t _buf[6+MAX_MEMNAME_SIZE];
	uint8_t sum=0;

	//データ パケット作成
	_buf[0]=SPICMD_MEM_BIOffset_WRITE;
	_buf[1]=strlen(memname)+1;
	_buf[2]=offset/256;
	_buf[3]=offset%256;
	_buf[4]=num/256;
	_buf[5]=num%256;
	strcpy(_buf+6,memname);
	sum=CheckSum(memname,0,strlen(memname)+1);
	sum=CheckSum(write_buf,sum,num);
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf, 6+strlen(memname)+1);
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
	機能	GOPにメモリーの読み出し要求を送信します
			READSTATUSでSPI_STATE_READREADYがセットされるとメモリーのリード可
	区分	static
	引数	memname	読み出しする先頭のメモリー名
			num	連続読み込みするメモリー数
	戻り値	TRUE	SPI転送成功
			FALSE	SPI転送失敗
*/
BOOL SpiCmd_MEM_READ_REQ(uint8_t FAR * memname,int16_t num){
	MD_STATUS status = MD_OK;
	//1.1.0.0b23　SPIREAD挙動修正
	uint8_t _buf[3+MAX_MEMNAME_SIZE+6],sum;
	memset(_buf, 0, 3 + MAX_MEMNAME_SIZE + 6);
	//データ パケット作成
	_buf[0]=SPICMD_MEM_READ_REQ;
	_buf[1]=strlen(memname)+1;
	_buf[2]=(uint8_t)num;
	strcpy(_buf+3,memname);
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf, 5+strlen(memname)+1);
	SPI_CS(FALSE);
	return status==MD_OK?TRUE:FALSE;
}
/*
	機能	GOPのメモリーの読み出し
	区分	static
	引数	num	連続で読み込むメモリー数(SpiCmd_MEM_READ_REQで指定した値と同数指定すること)
			read_buf	読み込んだデータを格納する領域のアドレス(領域サイズはnum文確保されていること)
	戻り値	TRUE	SPI転送成功
			FALSE	SPI転送失敗
*/
#define EX_32(a) (((a) << 24) & 0xff000000) | (((a) << 8) & 0x00ff0000) | (((a) >> 8) & 0x0000ff00) | (((a) >> 24) & 0x000000ff);



BOOL SpiCmd_MEM_READ_READ(int16_t num,int32_t FAR *read_buf)
{
	MD_STATUS status = MD_OK;
	uint8_t _buf[2]={SPICMD_MEM_READ_READ,0},sum;
	int16_t i;
	//コマンド＆データ長+ダミーバイト(読み込みへ切り替え前にゴミ除去のため1バイト分送信)
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf, 2);
	if(status!=MD_OK)goto err;
	status = xfer_spi_R(read_buf,num*4);
	if(status!=MD_OK)goto err;
	status = xfer_spi_R(&sum,1);
	if(status!=MD_OK)goto err;
	SPI_CS(FALSE);
	if(sum==CheckSum(read_buf,0,num*4)){
		//エンディアン変更(BIG エンディアン系のCPUの場合不要)
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
	機能	GOPのメモリーにデータを書き込みます
	区分	static
	引数	memname	書き込みを開始する先頭のメモリー名
			num	連続で書き込むメモリー数
			write_buf	書き込むデータが格納されている領域の先頭アドレス
	戻り値	TRUE	SPI転送成功
			FALSE	SPI転送失敗
*/


BOOL SpiCmd_MEM_WRITE(uint8_t FAR *memname,int16_t num,int32_t FAR *write_buf)
{
    MD_STATUS status = MD_OK;
	uint8_t _buf[3+MAX_MEMNAME_SIZE];
	uint8_t sum=0;
	int32_t v;
	int16_t i;
	//データ パケット作成
	_buf[0]=SPICMD_MEM_WRITE;
	_buf[1]=strlen(memname)+1;
	_buf[2]=num;
	strcpy(_buf+3,memname);
	sum=CheckSum(memname,0,strlen(memname)+1);
	sum=CheckSum(write_buf,sum,num*4);
	SPI_CS(TRUE);
	status = xfer_spi_W(_buf, 3+strlen(memname)+1);
	if(status!=MD_OK)goto err;
	for(i=0;i<num;i++)
	{
		//エンディアン変更しながらint32_t１こずつ送る
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
//uart系のAPI
//uartを使用してGOP-LTとのコマンド通信を行うAPIです




/*
	機能	GOPの整数メモリーにデータを書き込みます
	区分	public
	引数	memname	書き込むメモリー名
			val	書き込む値
	戻り値	TRUE	通信成功
			FALSE	通信失敗
					失敗原因は_get_error_codeで取得できます。
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
機能	GOPのテキストメモリにデータを書き込みます
区分	public
引数	memname	書き込むメモリ名
val	書き込む値
戻り値	TRUE	通信成功
FALSE	通信失敗
失敗原因は_get_error_codeで取得できます。
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
	機能	GOPの連続する整数メモリーに連続してデータを書き込みます
	区分	public
	引数	memname	書き込む行う先頭のメモリー名
			num	連続して書き込むメモリーの数
			vals	書き込む値が格納された配列の先頭アドレス
	戻り値	TRUE	通信成功
			FALSE	通信失敗
					失敗原因は_get_error_codeで取得できます。
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
	機能	GOPの連続する整数メモリに連続してデータを書き込みます。
			書き込むデータを配列ではなく、可変長引数で引き渡します。
	区分	public
	引数	memname	書き込む行う先頭のメモリー名
			num	連続して書き込むメモリーの数
			･･･	書き込む値をnum数分、引数として渡す
	戻り値	TRUE	通信成功
			FALSE	通信失敗
					失敗原因は_get_error_codeで取得できます。
	備考	LtMemArrayWriteと同様の機能ですが、書き込み値を直接記述できます。
			配列を用意せずに使用することが可能ですが、関数呼び出しのオーバーヘッドやスタックのサイズに影響があります。
			使い方
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
	機能	GOPの整数メモリーの値を読み込みます
	区分	public
	引数	memname	読み込むメモリー名
			pval	読み込んだ値を格納する領域のポインタ
	戻り値	TRUE	通信成功
			FALSE	通信失敗
					失敗原因は_get_error_codeで取得できます。
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
機能	GOPのテキストメモリの値を読み込みます
区分	public
引数	memname	読み込むメモリ名
pval	読み込んだ値を格納する領域のポインタ
戻り値	TRUE	通信成功
FALSE	通信失敗
失敗原因は_get_error_codeで取得できます。
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
	機能	GOPの連続する整数メモリーの値を連続して読み込みます
	区分	public
	引数	memname	読み込む先頭のメモリー名
			num	連続して読み込むメモリーの数
			vals	読み込んだ値を格納する配列のポインタ
	戻り値	TRUE	通信成功
			FALSE	通信失敗
					失敗原因は_get_error_codeで取得できます。
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
	機能	GOPのグラフメモリーに値を書き込みます。
	区分	public
	引数	memname	書き込むグラフメモリー名
			offset	書き込みを開始するプロット位置
			num	書き込むプロットの数
			vals	書き込む値を格納した配列の先頭アドレス
	戻り値	TRUE	通信成功
			FALSE	通信失敗
					失敗原因は_get_error_codeで取得できます。
			
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
	//1.1.0.0b23 長い電文長の場合送信時間をタイムアウトに加味するため
	s=gets_lt(buf,sizeof(buf),TIMEOUT+num);
	if(s){
		if(*s==0x06){
			return TRUE;
		}
	}
	return FALSE;
}

/*
	機能	GOPのグラフメモリーから値を読み込みます。
	区分	public
	引数	memname	読み込むグラフメモリー名
			offset	読み込みを開始するプロット位置
			num	読み込むプロットの数
			vals	読み込む値を格納した配列の先頭アドレス
	戻り値	TRUE	通信成功
			FALSE	通信失敗
					失敗原因は_get_error_codeで取得できます。
			
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
	用途	メッセージハンドラテーブルのアドレス
	区分	static
*/
static ltMes_Callback FAR *mes_callback=NULL;
/*
	機能	メッセージハンドラーテーブルを登録します
	区分	public
	引数	p	メッセージハンドラテーブルのアドレス
*/

void LtSetMessageCallback(ltMes_Callback FAR *p)
{
	mes_callback=p;
}

/*
	機能	GOPのメッセージを取得し、対応するメッセージハンドラをコールバックします※。
			
	区分	public
	引数	rcv	メッセージ格納用バッファーのアドレス
				※メッセージを受信する必要ない場合NULLを指定
	戻り値	rcvがNULLの場合
				COMMERR(0xffffffff) 通信失敗
				NULL	通信成功
			rcvがNULL以外
				COMMERR(0xffffffff) 通信失敗
				NULL	メッセージなし
				NULL以外	取得したメッセージの先頭アドレス
			通信失敗原因は_get_error_codeで取得できます。
	備考
			※LtSetMessageCallbackでメッセージハンドラテーブルを登録している場合。
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
			s+=2;	//A$をとる
			//登録されているメッセージに対するコールバックを呼び出し
			while(p)
			{	
				if(strcmp(p[i].mes,s)==0){
					//受信文と一致すると対応するルーチンをコールバックしループ抜ける
					p[i].func();
					break;
				}
				i++;
				//レコードをずらす
				//番兵にあたるとループ抜ける
				if(p[i].mes==NULL)break;
			}
			if(rcv){
				//戻り値用バッファー指定時、受信文を返す。
				strcpy(rcv,s);
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
	機能	SPIを使用してGOPの連続する整数メモリーに連続してデータを書き込みます
	区分	public
	引数	memname	書き込む行う先頭のメモリー名
			num	連続して書き込むメモリーの数
			vals	書き込む値が格納された配列の先頭アドレス
	戻り値	TRUE	通信成功
			FALSE	通信失敗
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
			//BUSY以外のビットがたっていた場合転送失敗
			SpiCmd_SPI_RESET();
		}
	}
	return ret;
}
/*
	機能	SPIを使用してGOPの連続するメモリーの値を連続して読み込みます
	区分	public
	引数	memname	読み込む先頭のメモリー名
			num	連続して読み込むメモリーの数
			vals	読み込んだ値を格納する配列のポインタ
	戻り値	TRUE	通信成功
			FALSE	通信失敗
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
			//BUSY以外のビットがたっていた場合転送失敗
			SpiCmd_SPI_RESET();
			return FALSE;
		}
	}
	return SpiCmd_MEM_READ_READ(num,vals);
}

/*
	機能	SPIを使用してGOPのテキストメモリーの値を書き込みます
	区分	public
	引数	memname	書き込む先頭のメモリー名
			vals	書き込む値を格納する配列のポインタ
	戻り値	TRUE	通信成功
			FALSE	通信失敗
*/

BOOL SPI_LtTMemWrite(char FAR *memname, uint8_t FAR vals[])
{
	return SPI_LtGMemWrite(memname, 0, _strlen(vals), vals);
}

/*
	機能	SPIを使用してGOPのグラフメモリーに値を書き込みます。
	区分	public
	引数	memname	書き込むグラフメモリー名
			offset	書き込みを開始するプロット位置
			num	書き込むプロットの数
			vals	書き込む値を格納した配列の先頭アドレス
	戻り値	TRUE	通信成功
			FALSE	通信失敗
	備考	LtGMemWriteと異なり、本関数実行でvalsのデータは破壊されます。
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
			//BUSY以外のビットがたっていた場合転送失敗
			SpiCmd_SPI_RESET();
		}
	}

	return ret;
}

/*
	機能	SPIを使用してGOPのテキストメモリーから値を読み込みます。
	区分	public
	引数	memname	読み込むテキストメモリー名
			vals	読み込む値を格納する配列の先頭アドレス
	戻り値	TRUE	通信成功
			FALSE	通信失敗
			
*/
BOOL SPI_LtTMemRead(char FAR *memname, uint8_t FAR vals[])
{
	return  SPI_LtGMemRead(memname, 0, LT_TEXT_LENGTH-1, vals);
}
/*
	機能	SPIを使用してGOPのグラフメモリーから値を読み込みます。
	区分	public
	引数	memname	読み込むグラフメモリー名
			offset	読み込みを開始するプロット位置
			num	読み込むプロットの数
			vals	読み込む値を格納した配列の先頭アドレス
	戻り値	TRUE	通信成功
			FALSE	通信失敗
			
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
			//BUSY以外のビットがたっていた場合転送失敗
			SpiCmd_SPI_RESET();
			return FALSE;
		}
	}
	return SpiCmd_MEM_BI_READ_READ(num,vals);
}

/*
	機能	SPIを使用してGOPのメッセージを取得し、対応するメッセージハンドラをコールバックします※。
	区分	public
	引数	rcv	メッセージ格納用バッファーのアドレス
				※メッセージを受信する必要ない場合NULLを指定
	戻り値	rcvがNULLの場合
				COMMERR(0xffffffff) 通信失敗
				NULL	通信成功
			rcvがNULL以外
				COMMERR(0xffffffff) 通信失敗
				NULL	メッセージなし
				NULL以外	取得したメッセージの先頭アドレス
	備考
			※LtSetMessageCallbackでメッセージハンドラテーブルを登録している場合。
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
			//BUSY以外のビットがたっていた場合転送失敗
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
		s+=2;	//A$をとる
		//登録されているメッセージに対するコールバックを呼び出し
		while(p)
		{	
			if(strcmp(p[i].mes,s)==0){
				//受信文と一致すると対応するルーチンをコールバックしループ抜ける
				p[i].func();
				break;
			}
			i++;
			//レコードをずらす
			//番兵にあたるとループ抜ける
			if(p[i].mes==NULL)break;
		}
		if(rcv){
			//戻り値用バッファー指定時、受信文を返す。
			strcpy(rcv,s);
			return rcv;
		}
	}
	return NULL;
}
#endif

#define SPI_FILEXFER
#ifdef SPI_FILEXFER

#define SPI_SLEEP 10			//SPIコマンド間のディレイ
#define SPI_TIMEOUT 60000

#define FILEBLOCKSIZE 1024



#ifdef FILESYSTEM
struct fileinfo {
	const char* src_path;	//ホスト側ファイルシステムが参照可能な名称を指定します
	const char* dest_path;	//GOP-LTに書き込み債の名称を指定します。GOP.iniがあるフォルダをルートとし、フォルダ区切りは'/'で指定します。
	int size;				//ファイルサイズを記述。stat等で取得可能であれば実行時取得でも構いません
};

#ifdef WIN32
//windows上での動作サンプルでは、filetableを動的に生成します。
struct fileinfo* filetable=NULL;
LPCTSTR ShowFolderDlg();

/*
	フォルダ中のファイルを検索しfiletable構造体に値をセットします
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
				DWORD size_low, size_high;
				sprintf(fpath, "%s%s", full_path, fdat.cFileName);
				slen = strlen(fpath);
				p->src_path = malloc(slen+1);
				strcpy(p->src_path, fpath);
				if (!folderName) {
					sprintf(fname, "%s", fdat.cFileName);
				}
				else {
					sprintf(fname, "%s/%s", folderName, fdat.cFileName);
				}
				slen = strlen(fname);
				p->dest_path = malloc(slen + 1);
				strcpy(p->dest_path, fname);
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
	フォルダ中のファイル数をを検索します
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
	filetableをクリアします。
*/
void clen_table()
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
/*
	filetableを作成します
*/
void MakeFileCatalog()
{
	char* datapath;
	datapath = ShowFolderDlg();
	if (datapath) {
		struct fileinfo* p;
		int filenum;
		clen_table();
		filenum = _sub_MakeFileCatalog_NUM(datapath);
		filetable = (struct fileinfo*)malloc((filenum + 1) * sizeof(struct fileinfo));
		p = _sub_MakeFileCatalog(datapath, NULL, filetable);
		p->src_path = NULL;
		p->dest_path = NULL;
	}
}

#else	//WIN32
//filetableの動的生成を行わない場合、以下のように初期値として指定してください。
//ファイル名及びファイルサイズは実際の環境に合わせて変更してください。
struct fileinfo filetable[] = {
	//TPデザイナーで作成した画面データより、書き込みファイルのカタログを用意します。
	{"G:\\LTX_DATA\\SAMPLE\\GOP.ini","GOP.ini",127},
	{"G:\\LTX_DATA\\SAMPLE\\meminfo.txt","meminfo.txt",572},
	{"G:\\LTX_DATA\\SAMPLE\\pagedata.dat","pagedata.dat",26396},
	{"G:\\LTX_DATA\\SAMPLE\\GB00\\GB0000.gb","GB00/GB0000.gb",36136},
	{"G:\\LTX_DATA\\SAMPLE\\GB00\\GB0001.gb","GB00/GB0001.gb",36136},
	{"G:\\LTX_DATA\\SAMPLE\\SI00\\SI0001.lsd","SI00/SI0001.lsd",254},
	{"G:\\LTX_DATA\\SAMPLE\\SI00\\SI0002.lsd","SI00/SI0002.lsd",617},
	{NULL,NULL,0},	//(データの終了)
};
#endif //WIN32


#else	//FILESYSTEM
//ROMに転送データを配置する場合、初期化データとして出力ファイルのバイナリ表記変換データを{}内に指定してください。
const char* goi_ini_data ={};
const char* meminfo_txt_data={};
const char* pagedata_dat_data={};
const char* GB0000_gb_data={};
const char* GB0001_gb_data={};
const char* SI0001_lsd_data={};
const char* SI0002_lsd_data={};


struct fileinfo {
	const char* src_ptr;	//転送元データのポインタ
	const char* dest_path;	//GOP-LTに書き込み債の名称を指定します。GOP.iniがあるフォルダをルートとし、フォルダ区切りは'/'で指定します。
	int size;				//ファイルサイズを記述。stat等で取得可能であれば実行時取得でも構いません
} filetable[] = {
//TPデザイナーで作成した画面データより、書き込みファイルのカタログを用意します。
//ファイル名及びファイルサイズは実際の環境に合わせて変更してください。
	{goi_ini_data,"GOP.ini",127},
	{meminfo_txt_data,"meminfo.txt",572},
	{pagedata_dat_data,"pagedata.dat",26396},
	{GB0000_gb_data,"GB00/GB0000.gb",36136},
	{GB0001_gb_data,"GB00/GB0001.gb",36136},
	{SI0001_lsd_data,"SI00/SI0001.lsd",254},
	{SI0002_lsd_data,"SI00/SI0002.lsd",617},
	{NULL,NULL,0},	//(データの終了)
};
#endif	//FILESYSTEM






void delay(int dleaytime){
	int st=get_syscount();
	while(get_syscount()<st+dleaytime);
}

#ifdef IS_USE_SPI

BOOL SpiCmd_DATA_START()
{
	MD_STATUS status = MD_OK;
	uint32_t	wrlen=0,len;
	//コマンド+ダミー
	uint8_t	cmd[2]={SPICMD_DATA_START,0};
	//コマンド長1バイト+ダミーバイト1バイトで2バイト書き込み
    SPI_CS(TRUE);
    status = xfer_spi_W(cmd, 2);
    SPI_CS(FALSE);
    return status==MD_OK?TRUE:FALSE;
}



BOOL SpiCmd_DATA_ERASE()
{
	MD_STATUS status = MD_OK;
	uint32_t	wrlen=0,len;
	//コマンド+ダミー
	uint8_t	cmd[2]={SPICMD_DATA_ERASE,0};
	//コマンド長1バイト+ダミーバイト1バイトで2バイト書き込み
    SPI_CS(TRUE);
    status = xfer_spi_W(cmd, 2);
    SPI_CS(FALSE);
    return status==MD_OK?TRUE:FALSE;
}

BOOL SpiCmd_DATA_END()
{
	MD_STATUS status = MD_OK;
	uint32_t	wrlen=0,len;
	//コマンド+ダミー
	uint8_t	cmd[2]={SPICMD_DATA_END,0};
	//コマンド長1バイト+ダミーバイト1バイトで2バイト書き込み
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
	sendbuf[1]=strlen(fname)+1;
	sendbuf[2]=(flen>>24)&0x000000ff;
	sendbuf[3]=(flen>>16)&0x000000ff;
	sendbuf[4]=(flen>> 8)&0x000000ff;
	sendbuf[5]=(flen    )&0x000000ff;
	//ファイル名書き込み
	strcpy((char *)sendbuf+6,fname);
	//SUM書込み
	sendbuf[6+strlen(fname)+1]=CheckSum(sendbuf+6,0,strlen(fname)+1);
	//転送長
	len=strlen(fname)+8;	//パケット長=コマンド(1)+ファイル名長指定(1)+ファイルサイズ指定(4)+ファイル名(GetLength)+NULL(1)+SUM(1)

    SPI_CS(TRUE);
    status = xfer_spi_W(sendbuf, len+1);//ゴミ取りのためダミー1バイト追加
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
	len=FILEBLOCKSIZE+4;//パケット長=コマンド(1)+ブロック番号(2)+データ(1024)+SUM


    SPI_CS(TRUE);
    status = xfer_spi_W(sendbuf, len+1);//ゴミ取りのためダミー1バイト追加
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
	//転送長
	len = 6;	//パケット長=コマンド(1)+ブロックNO指定(2)+ファイルサイズ指定(3)

	SPI_CS(TRUE);
	status = xfer_spi_W(sendbuf, len + 1);//ゴミ取りのためダミー1バイト追加
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
	len = FILEBLOCKSIZE + 4;//パケット長=コマンド(1)+ブロック番号(2)+データ(1024)+SUM


	SPI_CS(TRUE);
	status = xfer_spi_W(sendbuf, len + 1);//ゴミ取りのためダミー1バイト追加
	SPI_CS(FALSE);

	return status == MD_OK ? TRUE : FALSE;
}



//エラーリカバリ用
//GOP側で受け取っている最終のブロック番号を返す
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
	rf = fopen(src, "rb");//バイナリモードで開きます(環境依存)

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
					//正常に書き込まれている最後のブロック番号を取得
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
	rf=fopen(src,"rb");//バイナリモードで開きます(環境依存)
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
					//正常に書き込まれている最後のブロック番号を取得
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
					//正常に書き込まれている最後のブロック番号を取得
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
	uint8_t sta;	//ReadStatus受信用
	int start_time;//タイムアウト計測用
	int retrycount = 0;//リトライカウント用


	//(1)状態がアイドル中なのを確認

	start_time = get_syscount();
	while (!Is_Gop_Ready())//GopReady確認 SPIコマンド送信前には必ずGOPRが許可なのを確認
	{
		if (get_syscount() > start_time + SPI_TIMEOUT)
		{
			//GOPR不許可タイムアウト
			goto error;
		}
	}

	while (1)
	{
		if (!SpiCmd_ReadStatus(&sta))
		{
			//SPI通信失敗
			goto error;
		}
		if (sta == 0) {
			//ステータスが0(アイドル)ならば抜ける
			break;
		}
		retrycount++;
		if (retrycount > 5) {
			//リトライカウントオーバー（不明のエラー）
			goto error;
		}
		delay(50);
	}

	//(2)データモードへ移行
	if (!SpiCmd_DATA_START())	//コマンド発行
	{
		//SPI通信失敗
		goto error;
	}
	start_time = get_syscount();
	while (!Is_Gop_Ready())
	{
		if (get_syscount() > start_time + SPI_TIMEOUT)
		{
			//GOPR不許可タイムアウト
			goto error;
		}
	}
	//モード移行完了待ちループ
	while (1)
	{
		if (!SpiCmd_ReadStatus(&sta))
		{
			//SPI通信失敗
			goto error;
		}
		if (sta & (~SPI_STATE_BUSY))
		{
			//ステータスがBusyビット以外のビットが立ってる
			goto error;
		}
		if (!(sta & SPI_STATE_BUSY))
		{
			break;//Busy解除されたら待ちループから抜ける
		}
		if (start_time + SPI_TIMEOUT < get_syscount())
		{
			//Busy解除待ちタイムアウト
			goto error;
		}
		delay(SPI_SLEEP);
	}
	//(3)データ消去
	if (!SpiCmd_DATA_ERASE())	//コマンド発行
	{
		//SPI通信失敗
		goto error;
	}
	start_time = get_syscount();
	while (!Is_Gop_Ready())
	{
		if (get_syscount() > start_time + SPI_TIMEOUT)
		{
			//GOPR不許可タイムアウト
			goto error;
		}
	}
	//消去完了待ちループ
	while (1) {
		if (!SpiCmd_ReadStatus(&sta))
		{
			//SPI通信失敗
			goto error;
		}
		if (sta & (~SPI_STATE_BUSY))
		{
			//ステータスがBusyビット以外のビットが立ってる
			goto error;
		}
		if (!(sta & SPI_STATE_BUSY))
		{
			break;//Busy解除されたら待ちループから抜ける
		}
		if (start_time + SPI_TIMEOUT < get_syscount())
		{
			//フラッシュ消去タイムアウト
			goto error;
		}
		delay(SPI_SLEEP);
	}
	//(4)データファイル書き込み開始
	if (!CopyAllSPI())
	{
		//ファイル転送失敗
		goto error;
	}
	//(5)終了処理
	if (!SpiCmd_DATA_END())	//コマンド発行
	{
		//SPI通信失敗
		goto error;
	}
	start_time = get_syscount();
	while (!Is_Gop_Ready())
	{
		if (get_syscount() > start_time + SPI_TIMEOUT)
		{
			//GOPR不許可タイムアウト
			goto error;
		}
	}
	start_time = get_syscount();
	while (1)
	{
		if (!SpiCmd_ReadStatus(&sta))
		{
			//SPI通信失敗
			goto error;
		}
		if (sta & (~SPI_STATE_BUSY))
		{
			//ステータスがBusyビット以外のビットが立ってる
			goto error;
		}
		if (!(sta & SPI_STATE_BUSY))
		{
			break;//Busy解除されたら待ちループから抜ける
		}
		if (start_time + SPI_TIMEOUT < get_syscount())
		{
			//Busy解除待ちタイムアウト)
			goto error;
		}
		delay(SPI_SLEEP);
	}
	//(6)GOP-LT再起動
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
		//2行以降
		while (1) {
			uint8_t sum = 0;
			uint8_t* rbuf=(uint8_t*)malloc(packsize);
			if (!rbuf)goto err;
			memset(rbuf,0, packsize);

			int rs = fread(rbuf, packsize,1,rf);
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
#endif
/*
	機能	GOPに任意のメッセージを送信し、返信を返す。
			
	区分	public
	引数	command 送信するメッセージ
	        rcv	メッセージ格納用バッファーのアドレス
				※メッセージを受信する必要ない場合NULLを指定
	戻り値	rcvがNULLの場合
				COMMERR(0xffffffff) 通信失敗
				NULL	通信成功
			rcvがNULL以外
				COMMERR(0xffffffff) 通信失敗
				NULL	メッセージなし
				NULL以外	取得したメッセージの先頭アドレス
			通信失敗原因は_get_error_codeで取得できます。
	備考
			※LtSetMessageCallbackでメッセージハンドラテーブルを登録している場合。
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
