//服务器端
#include "serve.h"
int main(){
    //服务器网络编程
    /*1.初始化网络环境
    申请"使用网络功能"的许可证, WSAStartup 是第一步申请服务;

    MAKEWORD(主版本号, 副版本号)，用于表示请求的 Winsock 版本， 版本 2.2，也可以写成0x0202，因为MAKEWORD是一个宏函数返回值就是 0x0202

    Winsock = Windows Socket，socket=<主机号：端口号>，是网络通信的一个端点;
    TCP连接：：= <socket1, socket2>，socket1和socket2是两个通信端点;

    WSAStartup(MAKEWORD(2, 2), &wsaData);就是请求权限，就可以创建套接字，完整的 Windows Socket 网络编程用户权限
    */

    WSADATA wsaData; //声明一个结构体变量，用来存储 Windows Socket 的“启动信息, 即一份申请表
    WSAStartup(MAKEWORD(2, 2), &wsaData); //向 Windows 申请“我要使用网络功能”，并完成初始化
    //2.创建服务器socket
    //3.开启服务器监听
    //4.等待客户端连接
    //5.等待客户端发送请求

    //关闭
    WSACleanup(); //释放网络资源
    return 0;
}