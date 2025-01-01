#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

//筛选素数函数， 接收左邻居管道
void  sieve ( int pleft[2])
{
    //从左邻居管道中读取的第一个整数一定是质数
    int p ;
    read(pleft[0],&p,sizeof(p));
    if(p ==-1)  //如果读取到-1，代表结束，退出进程
    {
        exit(0);
    }
    printf("prime %d\n" ,p); // 直接输出
    
    //创建一个新的管道，用于右邻居
    int pright[2] ;
    pipe(pright);

    //接着创建子进程
    int pid ;
    pid = fork();
    if(pid ==-1)
    {
        printf("create child process failed\n");
        exit(1);
    }
    //右邻居
    else if (pid ==0)
    {   
        close(pright[1]); // 右邻居不需要这个管道端口
        close(pleft[0]); // 右邻居也不需要这个端口
        //递归调用 sieve 函数，不停创建子进程
        sieve(pright);
    }
    // 筛选数据，将符合条件的数据输入管道
    else 
    {
        //关闭右邻居读管道，当前进程使用不到这个管道
        close(pright[0]); 
        //从左邻居读取数据  
        int buf;
        read(pleft[0],&buf,sizeof(int));
        while( buf != -1)
        {
            if( buf % p != 0) //如果读取到的数据不是第一个整数的倍数，则向右邻居传递 
            {
                write(pright[1],&buf,sizeof(int));
            }
            read(pleft[0],&buf,sizeof(int));
        }
        //读取完所有整数后，向右邻居传递结束标志
        buf = -1 ;
        write(pright[1],&buf,sizeof(int));
        wait(0); //等待子进程结束
        exit(0);
        
    }

}

int main()
{
    //初始化管道
    int input_pipe[2] ;
    pipe(input_pipe);

    //创建子进程
    int pid ;
    pid = fork();
    if(pid==-1)
    {
        printf("Create child process failed\n");
        exit(1);
    }
    //如果子进程创建成功，则调用质数筛选函数，再筛选函数中再次创建子进程
    else if(pid == 0)
    {
        close(input_pipe[1]); //右邻居不需要这个管道的写端
        sieve(input_pipe); //调用筛选函数
        exit(0);
    }
    else
    {
        //筛选2~35整数数据，输入管道中
        close(input_pipe[0]); // 父进程只会向右邻居管道写入
        for(int i =2 ; i<=35 ; i++)
        {
            write(input_pipe[1],&i,sizeof(i));
        }
        //写入整数完毕后，写入结束标志
        int end = -1 ;
        write(input_pipe[1],&end,sizeof(end));
    }

    wait(0); //等待子进程结束
    //注意：这里无法等待子进程的子进程，因此子进程在执行过程中
    //也需要执行 wait(0) 从而等待自己的子进程
    exit(0);
}