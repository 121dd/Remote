#include <stdio.h>
#include <iostream>
#include<Windows.h> //操作系统接口

#pragma comment(lib, "ws2_32.lib") //链接库文件，Windows Socket 2.0 库文件  

int main(){
    // ⭐ 设置控制台为 UTF-8 编码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    //0.初始化 Winsock 环境
    WSADATA wsaData;
    if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0){
        printf("Winsock 初始化失败，错误码：%d\n", WSAGetLastError());
        return -1;
    }

    //1.创建socket连接
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(client_socket == INVALID_SOCKET){    
        printf("创建客户端套接字失败，错误码：%d\n", WSAGetLastError());
        return -1;
    }
    SOCKADDR_IN server_addr; //声明一个结构体变量，用来存储服务器的地址和端口信息
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9988); // 使用 htons() 函数将端口号转换为网络字节序
    server_addr.sin_addr.S_un.S_addr = inet_addr("192.168.1.30"); // 服务器的 IP 地址
    
    //2.连接服务器
    //服务器必须bind一个固定端口，客户端通过connect可以随机分配一个端口，并且系统自动配置client_socket;
    if(connect(client_socket, (sockaddr*)&server_addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR){
        printf("连接服务器失败，错误码：%d\n", WSAGetLastError());
        return -1;
    }  
    std::cout << "连接服务器成功" <<"服务器IP地址:"<< inet_ntoa(server_addr.sin_addr) <<"端口:"<< ntohs(server_addr.sin_port) << std::endl;
    //3.发送数据 connet后就连接成功
    char buffer[1024] = "hello, server!";
    char recv_buffer[1024] = {0};
    send(client_socket, buffer, strlen(buffer), 0);//发送给缓冲区
    //等待接收数据
    recv(client_socket, recv_buffer, sizeof(recv_buffer), 0);
    if(strlen(recv_buffer) > 0){
        printf("接收到服务器数据：%s\n", recv_buffer);
    }
    WSACleanup(); //释放 Winsock 资源
    system("pause");
    return 0;
}