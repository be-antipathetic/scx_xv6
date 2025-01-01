#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

//递归查找函数，查找路径为 path 的目录下是否有目标文件 target
void find (char *path , char *target )
{
    // buff 用来迭代目录，可能目标文件在 path 的后几级目录
    char buf[512], *p;
    int fd;
    // 目录文件中存储的目录项数据
    struct dirent de; // 表示目录项
    struct stat st; // 文件或目录的状态信息

    //打开目标路径，如果不能打开，则输出错误信息
    // linux 中，目录也是一种特殊的文件，文件内容为目录项，记录着文件的 inode 和名称
    if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
    }

    //获取文件的状态信息
    if((stat(path,&st))<0)
    {
        printf("1\n");
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd); // 及时释放文件描述符
        return;
    }
    //根据文件类型选择不同的处理方式
    switch (st.type)
    {
    // 如果是文件，检查文件名是否与目标文件名匹配  
    // 例如 path =/home/sanshui/target  target = /target
    case T_FILE:
        if(strcmp(path+strlen(path)-strlen(target),target) == 0)
        {
            printf("%s\n", path);
        }
        break;
    // 如果是目录文件，则需要在其中的目录项中找到目标文件
    case T_DIR:
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
        printf("find: path too long\n");
        break;
        }
        
        strcpy(buf,path); // 拷贝 path 路径到 buf 中
        p = buf+strlen(buf) ; // p 指针指针 buf 数组中的最后一个字符,也就是'\0'
        *p++ = '/'; // 此时 buf = path =/home/sanshui/target/
        //printf("buf : %s\n",buf); 

        //开始读取目录中的目录项数据，fd 已经在最开始获取到了 path 的文件描述符
        //read 系统调用，如果读取成功则会返回读取到的字节数，如果没有能读取的，会返回 0 
        //读取失败会返回 -1 ，可以处理下读取失败时的情况
        while(read(fd,&de,sizeof(de)) == sizeof(de))
        {   
            //读取到的目录项现在记录在 de 中
            if(de.inum == 0)  // 直接跳过，不做深入处理，以免引发异常。
            {
                continue;
            }
            // 更新目录 buf  ，比如此时目录项文件为 c_prj
            memmove(p,de.name,DIRSIZ); // buf = path =/home/sanshui/target/c_prj
            p[DIRSIZ] = 0;
            if(stat(buf,&st) <0)
            {
                printf("find: cannot stat %s\n", buf);
                continue;  // 跳过这个文件项目
            }
            // 排除 "." 和 ".." 目录
            if(strcmp(buf+strlen(buf)-2, "/.")!= 0 && strcmp(buf+strlen(buf)-3, "/..")!= 0)
            {
                find(buf,target);
            }
        }
        break;
    default:
        break;
    }
    close(fd); // 释放文件描述符
}

int main ( int argc , char *argv[])
{
    if ( argc <3 )
    {
        printf("input arguments insuffient\n");
        exit(0);
    }
    char target [512] ;
    target[0] = '/';   // 为查找的文件名添加 '/' 后缀
    strcpy(target+1 , argv[2]); // 将目标文件名存储在 target 字符串数组中  
    find(argv[1],target); // 调用查找函数
    exit(0);
    
}