#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main (int argc , char *argv[])
{
    // 创建两个管道，pp2c 用于父进程与子进程通信， pc2p 用于子进程与父进程通信
    int pp2c[2], pc2p[2];

    //创建管道
    int ret1 = pipe(pp2c);
    int ret2 = pipe(pc2p);

    if( ret1 == -1 || ret2 == -1)
    {
        //如果管道创建失败，打印错误信息并返回
        printf(" Pipe creation failed\n");
        exit(1);
    }
    
    //创建进程  fork 系统调用。父进程调用会返回子进程的 pid。进程调用则会返回 0 . 如果创建失败，则返回 -1
    int ret3 = fork();
    if (ret3 == -1)
    {
        //如果子进程创建失败，打印错误信息并返回
        printf(" child process creation failed\n");
        exit(1);
    }
    // 如果是子进程，则调用以下代码
    else if ( ret3 ==0)
    {
        //子进程从父进程中读取数据
        char buf ;
        read(pp2c[0],&buf ,1 );
        printf("<%d>: recieved ping\n",getpid());

        //子进程向管道中输出
        write(pc2p[1],",",1);
        close(pc2p[1]);      

    }
    //如果是父进程
    else
    {
        //父进程向管道中输出
        write(pp2c[1],".",1);
        //写入完成后关闭
        close(pp2c[1]);

        //从另一个管道中读取数据，从子进程中读取数据
        char  buf ;
        read (pc2p[0],&buf ,1);
        printf("<%d>: recieved pong\n" , getpid());
        //等待子进程结束
        wait(0);
    }

    //关闭两个通道的读端
    close(pp2c[0]);
    close(pc2p[0]);
    exit(0);
}