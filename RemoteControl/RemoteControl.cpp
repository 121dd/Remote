//服务器端
#include "serve.hpp"
int main(){
    // ⭐ 设置控制台为 UTF-8 编码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
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

    /*
    2.创建服务器socket
    创建一个套接字，AF_INET：表示使用IPv4协议，SOCK_STREAM：表示使用TCP协议，0：表示使用默认的协议
    */
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket == INVALID_SOCKET){
        printf("创建服务器套接字失败，错误码：%d\n", WSAGetLastError());
        return -1;
    }

    //给服务器绑定地址和端口
    //准备一个地址  SOCKADDR_IN用于存储 IPv4 地址和端口信息的结构体
    //SOCKADDR_IN是IPV4的结构体，SOCKADDR_IN6是IPV6的结构体，SOCKADDR是通用的结构体，SOCKADDR_IN和SOCKADDR_IN6都是SOCKADDR的子类

    SOCKADDR_IN server_addr; //声明一个结构体变量，用来存储服务器的地址和端口信息
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9988); // 使用 htons() 函数将端口号转换为网络字节序
    server_addr.sin_addr.S_un.S_addr = inet_addr("0.0.0.0"); // 0.0.0.0 监听服务器上所有的IP（电脑上可能不只有一张网卡；
    if(bind(server_socket, (sockaddr*)&server_addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR){
        printf("绑定地址和端口失败，错误码：%d\n", WSAGetLastError());
        return -1;
    }
    
    //3.开启服务器监听 backlog:已完成三次握手、但还未被 accept() 取走的客户端连接
    if( listen(server_socket, 1) == SOCKET_ERROR){
        printf("开启服务器监听失败，错误码：%d\n", WSAGetLastError());
        return -1;
    }

    /*listen() 负责“准备一个队列”，并“叫号”（完成三次握手）。
    客户端连接成功后，操作系统把“连接”这个对象放进队列。
    accept() 负责“从队列里取号”，并返回给程序。*/

    //4.等待客户端连接, accept函数是阻塞的，直到有客户端连接上来，才会继续往下执行,返回客户端的socket
    SOCKADDR_IN client_addr;
    int client_addr_len = sizeof(SOCKADDR_IN);
    printf("等待客户端连接...\n");
    SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_addr_len); //阻塞等待客户端连接，直到有客户端连接上来，才会继续往下执行
    printf("客户端连接成功,客户端IP: %s,客户端端口: %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    //5.等待客户端发送请求
    char buffer[1024] = {0};//非接收窗口，而是缓冲区（从接收窗口中拷贝到缓冲区中），recv()函数是阻塞的，直到有数据到来，才会继续往下执行
    while(true){
        //返回接收数据的长度, 接收的内容存在buffer中0是接收标志，表示默认接收，阻塞
        int len = recv(client_socket, buffer, sizeof(buffer), 0);//把客户端发送的数据拷贝到缓冲区中，sizeof(buffer)是接收数据的长度

        if(len > 0){
            buffer[len] = '\0'; //在接收到的数据后面加上字符串结束符
            Packet* packet = ParsePacket(buffer, len);
            std::cout << "接收到客户端发送的数据" << packet->body << std::endl;
            std::cout << "---------------------" << std::endl;
            free(packet);
            // fwrite(buffer, 1, len, stdout); //把缓冲区中的数据写入到标准输出流中
            // fwrite("\r\n---\r\n", 1, 8, stdout); //换行
        }

        //6.发送数据
        //send(client_socket, buffer, sizeof(buffer), 0);//把buffer中的数据发送给客户端，sizeof(buffer)是发送数据的长度，0是发送标志，表示默认发送
        //std::cout << "发送数据的内容为：" << buffer << std::endl;
        Sleep(1000); //延时100毫秒
    }
    //关闭套接字
    closesocket(client_socket); //关闭客户端套接字
    closesocket(server_socket); //关闭服务器套接字
    //关闭
    WSACleanup(); //释放网络资源
    system("pause");
    return 0;
}