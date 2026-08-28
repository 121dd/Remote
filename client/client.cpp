#include "client_head.hpp"
#define RECV_BUFFER_LEN 1024 *1024*1

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
    SOCKET connect_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(connect_socket == INVALID_SOCKET){    
        printf("创建客户端套接字失败，错误码：%d\n", WSAGetLastError());
        return -1;
    }
    SOCKADDR_IN server_addr; //声明一个结构体变量，用来存储服务器的地址和端口信息
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9988); // 使用 htons() 函数将端口号转换为网络字节序
    server_addr.sin_addr.S_un.S_addr = inet_addr("192.168.1.30"); // 服务器的 IP 地址
    
    //2.连接服务器
    //服务器必须bind一个固定端口，客户端通过connect可以随机分配一个端口，并且系统自动配置client_socket;
    if(connect(connect_socket, (sockaddr*)&server_addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR){
        printf("连接服务器失败，错误码：%d\n", WSAGetLastError());
        return -1;
    }  
    std::cout << "连接服务器成功"<< std::endl;
    std::cout <<"服务器IP地址:"<< inet_ntoa(server_addr.sin_addr) <<"端口:"<< ntohs(server_addr.sin_port) << std::endl;
    //3.发送数据 connet后就连接成功
    
    char* buffer = new char[RECV_BUFFER_LEN];
    char* recv_buffer = new char[RECV_BUFFER_LEN];
    //int count = 0;
    int index = 0;
    while(true){
        //准备发送的数据 sprintf:把一个格式化的字符串写入一个字符数组中
        // sprintf_s(buffer, sizeof(buffer), "packet:%d", count++);
        // std::cout << "发送数据的内容为：" << buffer << std::endl;
        std::cout << "请输入要发送的数据：";
        fgets(buffer, RECV_BUFFER_LEN, stdin);
        //创建数据包,这种情况下malloc比new好，因为结构体Packet使用的是柔性结构，得根据数据去确定他的内存空间
        Packet* packet = PackPacket(0x55AA77CC, 2000, buffer, strlen(buffer) + 1 );//预留一字节给/0
        send(connect_socket, (char*)&packet->header.magic, packet->header.body_len + sizeof(PacketHeader), 0);//发送给客户端的缓冲区
        free(packet); //释放内存
        //再由网络自动将数据发送给服务器

        //等待接收数据
        int len = recv(connect_socket, recv_buffer + index, RECV_BUFFER_LEN - index, 0);
        if(len > 0){
            index += len;//缓冲区有效数字的总长度
            Packet* pck = ParsePacket(recv_buffer,  index);
            index = index - GetPacketLen(pck);//把一个包拿走后剩下的长度
            memmove(recv_buffer, recv_buffer + GetPacketLen(pck), index);//移动把buffer + index
            std::cout << "接收到服务器数据：" << pck->body  << std::endl;
            free(pck);
        }
        Sleep(100); //延时100毫秒
    }
    delete[] buffer;
    delete[] recv_buffer;
    //关闭套接字
    closesocket(connect_socket); //关闭客户端套接字
    WSACleanup(); //释放 Winsock 资源
    system("pause");
    return 0;
}