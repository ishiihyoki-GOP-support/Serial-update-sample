#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include "goplt_if.h"

#define SERIAL_PORT "/dev/ttyACM0"
// ファイルディスクリプタ

static int fd=0;

//システム時刻をms単位で取得
uint32_t get_syscount(){
    return (uint32_t)(clock()*1000/CLOCKS_PER_SEC);
}
//ポートから1バイト取得　データない場合は-1を返す
int uart_getc()
{
    char buf[1];
    int len=read(fd,buf,1);
    if(len==0)return -1;
    else return(int)buf[0];
}
//ポートから1バイト出力
void uart_putc(char c){
    char buf[1];
    buf[0]=c;
    write(fd,buf,1);
}


int main(int argc, char *argv[])
{
    char *s;
    char buf[255];             // バッファ
    struct termios tio;                 // シリアル通信設定
    int baudRate = B9600;
    int i;
    int len;
    int ret;
    int size;

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

    tio.c_iflag=IGNPAR | ICRNL; 

    cfmakeraw(&tio);                    // RAWモード
    tio.c_cc[VTIME]=0;
    tio.c_cc[VMIN]=0;
    tcsetattr( fd, TCSANOW, &tio );     // デバイスに設定を行う

    ioctl(fd, TCSETS, &tio);            // ポートの設定を有効にする
    s=(char *)LtSendCommand("UV",(char *)buf,255);
    if(s){
        printf("%s\n",s);
    }
    // 送受信処理ループ
    while(1) {
    }

    close(fd);                              // デバイスのクローズ
    return 0;
}