//服务器端
#include "serve.hpp"

int main(){
    //设置控制台为 UTF-8 编码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    /*1.初始化网络，2.创建服务器socket3.开启服务器监听 */
    if(InitServer() == -1){
        std::cout << "初始化失败" << std::endl;
        return -1;
    }

    /*listen() 负责“准备一个队列”，并“叫号”（完成三次握手）。客户端连接成功后，操作系统把“连接”这个对象放进队列。
    accept() 负责“从队列里取号”，并返回给程序。*/
    //4.等待客户端连接, accept函数是阻塞的，直到有客户端连接上来，才会继续往下执行,返回客户端的socket
    SOCKADDR_IN client_addr;
    int client_addr_len = sizeof(SOCKADDR_IN);
    printf("等待客户端连接...\n");
    SOCKET connect_socket = accept(g_listen_socket, (sockaddr*)&client_addr, &client_addr_len); //阻塞等待客户端连接，直到有客户端连接上来，才会继续往下执行
    printf("客户端连接成功,客户端IP: %s,客户端端口: %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    //5.等待客户端发送请求; 解决粘包：协议 和丢包：index指向
    char* buffer = (char*)malloc(BECV_BUFFER_SIZE);//非接收窗口，而是缓冲区（从接收窗口中拷贝到缓冲区中），recv()函数是阻塞的，直到有数据到来，才会继续往下执行
    //给他一个指针让他一次处理一个包的内容，多读到的内容的位置通过index保存下来
    //也就是构建一个可持续的缓存区
    int index = 0;
    int packet_len = 0;
    while(true){
        //返回接收数据的长度, 接收的内容存在buffer中0是接收标志，表示默认接收，阻塞
        
        //BECVG_BUFFER_SIZE - index代表缓冲区的大小
        int len = recv(connect_socket, buffer + index, BECV_BUFFER_SIZE - index, 0);//把客户端发送的数据拷贝到缓冲区中，sizeof(buffer)是接收数据的长度
        if(len > 0){
            //6.接收数据
            index += len;//缓冲区有效数字的总长度
            Packet* packet = ParsePacket(buffer, index);
            index = index - GetPacketLen(packet);//把一个包拿走后剩下的长度
            memmove(buffer, buffer + GetPacketLen(packet), index);//移动把buffer + index
            std::cout << "接收到客户端发送的数据" << packet->body << std::endl;

            //7.处理数据
            if(packet->header.cmd == 1){//表示客户端要获取数据
                //截取数据并发送给客户
                Packet* pck = PackPacket(packet->header.magic, packet->header.cmd, packet->body,  packet->header.body_len + 1);
                send(connect_socket, (char*)&pck->header.magic, GetPacketLen(pck), 0);//把buffer中的数据发送给客户端，sizeof(buffer)是发送数据的长度，0是发送标志，表示默认发送
                std::cout << "发送数据的内容为：" << pck->body << std::endl;
                std::cout << "---------------------" << std::endl;
                free(pck);
            }
            free(packet);
        }
        Sleep(500); //延时100毫秒
    }
    //关闭套接字
    delete[] buffer;
    closesocket(connect_socket); //关闭客户端套接字
    closesocket(g_listen_socket); //关闭服务器套接字
    //关闭
    WSACleanup(); //释放网络资源
    system("pause");
    return 0;
}