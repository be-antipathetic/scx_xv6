#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

// 运行指定的程序，接收参数
void run ( char *program , char ** args)
{
    //创建子进程
    int pid = fork (); // 创建子进程，子进程从创建 fork() 节点开始执行程序
    if(pid == -1) 
    {
        printf("create child process failed \n");
        return ;
    }
    else if (pid ==0)
    {
        exec(program , args);
        exit(0);
    }
    return ;
}

// 假设输入参数  标准输入 hello world  ， 命令行输入参数  echo bye
int main (int argc , char * argv[])
{
    // 声明数组用来存储传入 xargs 的参数 
    char  buf [2048]; // 缓存从标准输入读取的数据
    char  *p = buf , * last_p =buf ; //定义两个头指针和尾指针

    char * program_argv_buf [128] ;  //要传入子程序的命令
    char ** args = program_argv_buf ; // 指针指向指针数组的头



    //将 xargs 的参数复制到 program_argv_buf 中
    for(int i = 1 ; i< argc ; i++)
    {
        *args = argv[i];
        args++;  // 记录完一个指针后，向后移动
    }
    // 循环完毕后， args 指向数组中的最后一个元素
    char **pa = args ;  // 复制指针

    // 从标准输入中读取数据，将其继续缓存在 program_argv_buf 中
    while(read(0,p,sizeof(char)) !=0 )
    {
        // 如果检测到换行符或者空格
        if(*p == ' ' || *p =='\n')
        {
            //如果检测到换行符，准备调用子程序
            if( *p == '\n'){
                *p = '\0' ; //替换换行符为字符串结束符'\0'
                *pa = last_p ; // 将最后一段数据传入 program_argv_buf
                last_p = p+1; // last_p 指向下一个字符串的开头 ，其实也可以不后移，如果不
                             // 后移，则会替换掉这个字符串
                run(argv[1],program_argv_buf); // 调用 run 函数
                pa = args; // 重置 pa 指针，准备读入下一行参数
            }
            else{ // 如果读取的是空格符，仅仅将其添加至 program_argv_buf 即可
                *p ='\0' ;
                *pa = last_p ;
                 pa ++ ;
                last_p = p+1 ;
                
            } 
            
        }
            // 继续读取数据
            p++;
    }

    //如果最后一行不是空行, 也就是说只有一行参数没有换行符
    if(pa != args)
    {
        *p = '\0';
        *(pa++) = last_p ; 
        *pa = 0 ; 
        run(argv[1],program_argv_buf);
    }
    
    //等待所有子进程结束
    while(wait(0) != -1){    }

    exit(0);

}