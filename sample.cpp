#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include "goplt_if.h"

///////////  シリアル通信用

#define SERIAL_PORT "/dev/ttyACM0"  // ファイルディスクリプタ

static int fd=0;
BOOL Serial_Init(){
    struct termios tio;                 // シリアル通信設定
    int baudRate = B115200;
    int i;
    int len;
    int ret;
    int size;
//シリアルポートの初期化
    fd = open(SERIAL_PORT, O_RDWR);     // デバイスをオープンする
    if (fd < 0) {
        printf("open error\n");
        return -1;
    }
    //非カノニカルモードで動作させるための設定
    tio.c_cflag += CREAD;               // 受信有効
    tio.c_cflag += CLOCAL;              // ローカルライン（モデム制御なし）
    tio.c_cflag += CS8;                 // データビット:8bit
    tio.c_cflag += 0;                   // ストップビット:1bit
    tio.c_cflag += 0;                   // パリティ:None
    cfsetispeed( &tio, baudRate );      //USB接続では設定に影響ない
    cfsetospeed( &tio, baudRate );  
    tio.c_iflag=IGNPAR | ICRNL; 

    cfmakeraw(&tio);                    // RAWモード
    //受信タイムアウトを待たないよう設定
    tio.c_cc[VTIME]=0;                  
    tio.c_cc[VMIN]=0;
    tcsetattr( fd, TCSANOW, &tio );     // デバイスに設定を行う

    ioctl(fd, TCSETS, &tio);            // ポートの設定を有効にする

}

#define LOG_OUTPUT //コンソールに通信内容出力する場合はコメントアウト
#ifdef LOG_OUTPUT
void debug_put_console(int c)
{
    if(c!=-1){
        if(c==0x0d){
            printf("\n");
        }else if(c==0x02){
            printf("[stx]");
        }else if(c==0x03){
            printf("[etx]");
        }else if(c==0x06){
            printf("[ack]");
        }else if(c==0x15){
            printf("[nak]");
        }else{
            printf("%c",c);
        }
    }
}
#else
#define debug_put_console(a) //
#endif

//ポートから1バイト取得　データない場合は-1を返す
int uart_getc(){
    char buf[1];
    int c;
    if(read(fd,buf,1)){
        c=buf[0];
    }else{
        c=-1;
    }
    debug_put_console(c);
    return c;
}

//ポートから1バイト出力
void uart_putc(char c){
    char buf[1];
    buf[0]=c;
    write(fd,buf,1);
    debug_put_console(c);
}



//システム時刻をms単位で取得
uint32_t get_syscount(){
    uint32_t _systime=0;
    static struct timespec _sttime={0,0};
    if(_sttime.tv_sec==0&&_sttime.tv_nsec==0){
        clock_gettime(CLOCK_REALTIME, &_sttime);
    }else{
        struct timespec _nowtime;
        clock_gettime(CLOCK_REALTIME, &_nowtime);
        _systime=(_nowtime.tv_sec-_sttime.tv_sec)*1000+(_nowtime.tv_nsec-_sttime.tv_nsec)/1000000;

    }

    return _systime;
}

//////GOPアプリケーション動作用

int32_t sv_1,pv_1;
BOOL run_flag=FALSE;    //運転状態のフラグ

//メッセージ"START"受信時 運転フラグをセット
void fnSTART()
{
    run_flag=TRUE;
}
//メッセージ"STOP"受信時 運転フラグをリセット
void fnSTOP()
{
    run_flag=FALSE;
}
//メッセージ"SET"受信時 GOPのメモリーSV_1の値をホスト側の変数sv_1に読み込み
void fnSET()
{
    LtMemRead("SV_1",&sv_1);
}

//メッセージ対するハンドラー関数の設定
ltMes_Callback tbl[]={
    {"START",fnSTART},
    {"STOP",fnSTOP},
    {"SET",fnSET},
    {NULL,NULL} //ハンドラーテーブルの終端を示すため、最後の行に｛NULL,NULL｝を登録
};

//運転フラグセット時の動作
//pv_1をsv_1に近づけるよう値を増減
//増減した結果をGOPのメモリーPV_1に書き込み
void run()
{
    int d=sv_1-pv_1;
    if(d<=10){
        pv_1-=3;
    }else{
        if(d>100){
            d=10;
        }else {
            d/=10;
        }
        pv_1+=d;
    }
    LtMemWrite("PV_1",pv_1);
}

int main(int argc, char *argv[])
{
    //シリアル通信の初期化
    Serial_Init();

    //メッセージハンドラーを登録
    LtSetMessageCallback(tbl);

    // メインループ
    while(1) {
        if(run_flag){
            run();
        }

        LtEnq(NULL);    //メッセージの受信処理　この関数をメインループで呼ぶとメッセージに応じたハンドラ関数を呼び出します。
    }

    close(fd);                              // デバイスのクローズ
    return 0;
}