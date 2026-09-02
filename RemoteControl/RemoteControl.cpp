//服务器端
#include "serve.hpp"

int main(){
    //设置控制台为 UTF-8 编码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    //DPI 感知：拿真实物理分辨率，否则 GetSystemMetrics 返回逻辑分辨率，截图会模糊/偏小
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    //这是获取屏幕的初始化
    //GDI+ 初始化（整个程序只初始化一次，HandleScreen 里不再启停）
    GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusInput, NULL);

    /*1.初始化网络，2.创建服务器socket3.开启服务器监听 */
    if(InitServer() == -1){
        std::cout << "初始化失败" << std::endl;
        return -1;
    }


    //为每一个命令开辟一个线程去处理，避免阻塞主线程
    CreateThread(NULL, 0, HandleScreenThreadFuc, NULL, 0, &handle_screen_thread_id);
    CreateThread(NULL, 0, HandleMouseThreadFuc, NULL, 0, &handle_mouse_thread_id);
    CreateThread(NULL, 0, HandleKeyboardThreadFuc, NULL, 0, &handle_keyboard_thread_id);
    //线程的消息队列在第一次 GetMessage 时才创建，所以刚 CreateThread 完立刻 post 会丢，
    //这里用 Sleep(100) 等线程把消息队列建好，之后 HandleCommand 里投递的命令才不会丢
    Sleep(100);
    //（原来的 PostThreadMessage(WM_HANDEL_INVOKE_MSG_LOOP) 那几行是死代码：投递时队列还没建，一定丢失，已删）

    /*listen() 负责“准备一个队列”，并“叫号”（完成三次握手）。客户端连接成功后，操作系统把“连接”这个对象放进队列。
    accept() 负责“从队列里取号”，并返回给程序。*/
    //4.等待客户端连接, accept函数是阻塞的，直到有客户端连接上来，才会继续往下执行,返回客户端的socket
    SOCKADDR_IN client_addr;
    int client_addr_len = sizeof(SOCKADDR_IN);
    PrintLocalIPs();   //打印本机 IP，方便客户端填
    printf("等待客户端连接...\n");
    g_connect_socket = accept(g_listen_socket, (sockaddr*)&client_addr, &client_addr_len); //阻塞等待客户端连接，直到有客户端连接上来，才会继续往下执行
    printf("客户端连接成功,客户端IP: %s,客户端端口: %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    //5.等待客户端发送请求; 解决粘包：协议 和丢包：index指向
    std::vector<char> buffer(BECV_BUFFER_SIZE);
    //非接收窗口，而是缓冲区（从接收窗口中拷贝到缓冲区中），recv()函数是阻塞的，直到有数据到来，才会继续往下执行
    //给他一个指针让他一次处理一个包的内容，多读到的内容的位置通过index保存下来
    //也就是构建一个可持续的缓存区

    int index = 0;
    int packet_len = 0;
    while(true){
        //返回接收数据的长度, 接收的内容存在buffer中0是接收标志，表示默认接收，阻塞
        //BECVG_BUFFER_SIZE - index代表缓冲区的大小
        int len = recv(g_connect_socket, buffer.data() + index, BECV_BUFFER_SIZE - index, 0);//把客户端发送的数据拷贝到缓冲区中，sizeof(buffer)是接收数据的长度
        if(len <= 0) break;   //连接断开/出错，退出主循环
        //6.接收数据
        index += len;//缓冲区有效数字的总长度
        PacketPtr packet = ParsePacket(buffer.data(), index); //解析数据

        //数据不完整，继续接收数据
        while(packet == nullptr){
            //如果解析失败，说明数据不完整，继续接收数据
            len = recv(g_connect_socket, buffer.data() + index, BECV_BUFFER_SIZE - index, 0);
            if(len <= 0) break;   //断开/出错，跳出内层
            index += len;
            packet = ParsePacket(buffer.data(), index);
            if(index >= BECV_BUFFER_SIZE) break;   //防呆：缓冲满仍无完整包，丢弃防死循环
        }
        if(packet == nullptr) break;   //内层跳出后仍无完整包 → 断开/异常，退出
        //已经有数据就都处理完再接收下一个数据
        while(packet != nullptr){
            //如果解析成功，说明数据完整，处理数据
            index = index - GetPacketLen(packet);//把一个包拿走后剩下的长度
            memmove(buffer.data(), buffer.data() + GetPacketLen(packet), index);//移动把buffer + index
            HandleCommand(std::move(packet)); //把包的所有权转移给 HandleCommand
            packet = (index > 0) ? ParsePacket(buffer.data(), index) : nullptr;   // 继续解析下一个
        }
    }
    
    //关闭套接字
    closesocket(g_connect_socket); //关闭客户端套接字
    closesocket(g_listen_socket); //关闭服务器套接字

    //关闭
    GdiplusShutdown(gdiplusToken); //释放 GDI+
    WSACleanup(); //释放网络资源
    system("pause");
    return 0;
}