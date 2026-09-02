#include "../socket_send.hpp"
#include <stdio.h>
#include <iostream>
#include <memory>
#include <vector>
#include <Windows.h> //操作系统接口
#include <gdiplus.h> //使用GDI+
using namespace Gdiplus;
#pragma comment(lib, "ws2_32.lib") //链接库文件，Windows Socket 2.0 库文件

enum CMD{
    CMD_SCREEN = 1,
    CMD_MOUSE = 2,
    CMD_KEYBOARD = 4,
    CMD_TEST = 2026
};

#define RECV_BUFFER_LEN 1024 *1024*10
#define PACKET_MAGE 0x55AA77CC
SOCKET g_connect_socket;
SocketSender g_socket_sender;
HWND g_hwnd = NULL; 

//只要是你通过网络发送的二进制数据，定义结构体时必须确保它在任何平台上大小都一样！
//网络通信（Socket 收发）
#pragma pack(push, 1) //设置结构体对齐方式为1字节对齐
struct PacketHeader{//数据包头部结构体
    int magic; //魔数，用于标识数据包的合法性
    int cmd; //四字节命令号
    int body_len; //数据体长度
};
struct Packet{//数据包结构体
    PacketHeader header; //数据包头部
    char body[]; //数据包体, 不固定长度
};
#pragma pack(pop) //恢复结构体对齐方式为默认值

// Packet 使用 malloc 分配，智能指针析构时必须用 free 释放。
struct PacketDeleter{
    void operator()(Packet* packet) const noexcept{
        free(packet);
    }
};
using PacketPtr = std::unique_ptr<Packet, PacketDeleter>;

//鼠标信息有哪些？
//1.按键：左键，右键，中键. 2.状态:按下，抬起，移动. 3.坐标：x,y
enum class ENUM_MOUSE{
    MOVE = 1,//鼠标移动
    LDOWN = 2,//鼠标左键按下
    LUP = 3,//鼠标左键抬起
    RDOWN = 4,//鼠标右键按下
    RUP = 5,//鼠标右键抬起
    MDOWN = 6,//鼠标中键按下
    MUP = 7,//鼠标中键抬起
    //8/9/10 原为单击动作，已删除（单击由 DOWN+UP 表达，不需要单独动作）
    LDLICK = 11,//鼠标左键双击
    RDLICK = 12,//鼠标右键双击
    MDLICK = 13,//鼠标中键双击

};
struct Mouse{
    int action; //鼠标动作 ENUM_MOUSE
    POINT ptXY; //鼠标坐标 X , Y
};

//键盘信息
struct Keyboard{
    int virtual_code;//虚拟键码
    int key_state;//按键状态 0:抬起 1:按下
};

//包长, packet的长度
int GetPacketLen(const PacketPtr& pck){
    if(pck != nullptr){
        return pck->header.body_len + sizeof(PacketHeader);
    }
    return 0;
}
//封装要发送的数据
PacketPtr PackPacket(int magic, int cmd, char* buffer, int buffer_len){
    PacketPtr pck((Packet*)malloc(buffer_len + sizeof(PacketHeader)));
    pck->header.magic = magic; 
    pck->header.cmd = cmd;
    pck->header.body_len = buffer_len; //数据体长度
    if(buffer_len > 0){
        memcpy(pck->body, buffer, pck->header.body_len); //把buffer中的数据拷贝到packet的body中
    }
    return pck;
}
//解析接收到的数据
PacketPtr ParsePacket(char* buffer, int len){
    Packet pck;
    PacketPtr pck_ptr;
    //4字节包头，4字节命令号，4字节数据长度，数据
    int i = 0;
    for(;i < len; i++){
        //找包头
        //当i=0时，int*第一个地址开始解析为int
        //int类型就代表再往后4个字节的内容，*(int*)(buffer + i)就是把buffer+i的地址强制转换为int*类型，然后取这个地址的值
        if(*(int*)(buffer + i) == PACKET_MAGE){
            //找到了包头
            pck.header.magic = *(int*)(buffer + i);
            i += 4;
            break;
        }
    }
    if(i + 8 > len){// magic 没找到，或找到后不够读 cmd+body_len
        return nullptr;
    }
    pck.header.cmd = *(int*)(buffer + i);
    i += 4;
    pck.header.body_len = *(int*)(buffer + i);
    i += 4;
    //获取数据,必须先创建pck去存pck.header.body_len不然不知道长度
    if(pck.header.body_len <= 0 || pck.header.body_len > len - i){  // body 长度越界
        //当len长度为0的时候也要执行，获取的是命令
        if(pck.header.body_len ==0){
            pck_ptr = PacketPtr((Packet*)malloc(sizeof(PacketHeader)));
            memcpy(&pck_ptr->header, &pck.header, sizeof(PacketHeader));
            return pck_ptr;
        }
        return nullptr;
    }
    //创建接受缓存区
    pck_ptr = PacketPtr((Packet*)malloc(sizeof(PacketHeader) + pck.header.body_len));
    memcpy(pck_ptr->body, buffer + i, pck.header.body_len);
    memcpy(pck_ptr.get(), &pck.header, sizeof(PacketHeader));
    return pck_ptr;
}


//开辟一条新的线程，用来不断接受和发送对屏幕数据的请求和数据
//返回值 调用约定 函数名
Gdiplus::Bitmap* g_image = NULL;  // 全局图片，WM_PAINT 绘制要用
int g_remote_width = -1;  // 远程屏幕宽度，-1 表示未知
int g_remote_height = -1; // 远程屏幕高度，-1 表示未知
IStream* g_stream = NULL;          // GDI+ 懒解码，流必须和 Bitmap 一起活着
CRITICAL_SECTION g_cri_sec; //锁，避免多线程同时改变一个变量
DWORD WINAPI SendScreenCallBack (LPVOID lpThreadParameter){
    //不停的发送数据，解析数据
    std::vector<char> recv_buffer(RECV_BUFFER_LEN);   // 接收缓冲，RAII 自动释放
    int index = 0;   // 累积缓冲：已收到、还没取走的字节数

    //先发第一个屏幕请求
    {
        PacketPtr req = PackPacket(PACKET_MAGE, CMD_SCREEN, NULL, 0);
        if(!g_socket_sender.Send(
               g_connect_socket,
               reinterpret_cast<const char*>(&req->header.magic),
               GetPacketLen(req))){
            return 0;
        }
    }

    while(true){
        //收一个完整包（累积，收不完整就一直 recv，不发请求）
        PacketPtr pck;
        while(pck == nullptr){
            int len = recv(g_connect_socket, recv_buffer.data() + index, RECV_BUFFER_LEN - index, 0);
            if(len <= 0) return 0;   //连接断开
            index += len;
            pck = ParsePacket(recv_buffer.data(), index);
        }

        //这个包解析出来了（数据已复制进 pck），从累积缓冲里挪走，剩下前移
        int pck_len = GetPacketLen(pck);
        index -= pck_len;
        memmove(recv_buffer.data(), recv_buffer.data() + pck_len, index);

        /*你手里有一袋咖啡豆（pck->body）。咖啡机（GDI+）只认“咖啡粉盒”（IStream），
        不认散装豆子。所以你只能把豆子先磨成粉（写入流），装进粉盒（IStream），才能塞进机器里冲泡。*/
        //把收到的字节数据装进内存流
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, pck->header.body_len);
        if(hMem != NULL){
            //创建内存流（TRUE 表示流 Release 时自动释放 hMem，之后不要再 GlobalFree）
            IStream* pStream = NULL;
            HRESULT ret = CreateStreamOnHGlobal(hMem, TRUE, &pStream);
            if(ret == S_OK){
                ULONG length = 0;
                pStream->Write(pck->body, pck->header.body_len, &length);
                //把pStream的指针移到开头
                LARGE_INTEGER lg = {0};
                pStream->Seek(lg, STREAM_SEEK_SET, NULL);

                //原来 g_image.Load(pStream)，现在用 GDI+ 从流里解码
                Gdiplus::Bitmap* newImage = Gdiplus::Bitmap::FromStream(pStream);
                if(newImage != NULL){
                    //锁，避免多线程同时改变一个变量
                    EnterCriticalSection(&g_cri_sec);
                    //换新图前先释放旧的（顺序：先删图，再放流）
                    if(g_image) delete g_image;
                    if(g_stream) g_stream->Release();
                    g_image = newImage;
                    g_stream = pStream;   //不能 Release！Bitmap 解码 PNG 时还要读它
                    if(g_remote_width ==-1 && g_remote_height == -1){
                        g_remote_width = g_image->GetWidth();
                        g_remote_height = g_image->GetHeight();
                    }
                    //解锁
                    LeaveCriticalSection(&g_cri_sec);
                    //通知UI线程绘制
                    InvalidateRect(g_hwnd, NULL, FALSE);
                } else {
                    pStream->Release();   //解码失败，释放流（连带释放 hMem）
                }
            }
        }
        pck.reset(); //保持原释放时机，避免在等待下一帧时继续占用屏幕包内存

        //这一帧处理完了，才请求下一帧
        PacketPtr req = PackPacket(PACKET_MAGE, CMD_SCREEN, NULL, 0);
        if(!g_socket_sender.Send(
               g_connect_socket,
               reinterpret_cast<const char*>(&req->header.magic),
               GetPacketLen(req))){
            return 0;
        }
    }
}

void DOMOUSEACKTION(int Action, HWND hwnd, WPARAM wPatam, LPARAM lParam, ULONGLONG& moustick){
//拿到的是客户区的鼠标位置
    int xPos = LOWORD(lParam); //低字节是x坐标
    int yPos = HIWORD(lParam); //高字节是y坐标
    if(g_remote_width == -1 || g_remote_height == -1) return;
    //和 WM_PAINT 一样的"保留宽高比 fit"，算出画面实际绘制的区域
    RECT client_rect;
    GetClientRect(hwnd, &client_rect);
    int client_width = client_rect.right - client_rect.left;
    int client_height = client_rect.bottom - client_rect.top;
    float scale_w = (float)client_width / g_remote_width;
    float scale_h = (float)client_height / g_remote_height;
    float scale = scale_w < scale_h ? scale_w : scale_h;
    int draw_w = (int)(g_remote_width * scale);
    int draw_h = (int)(g_remote_height * scale);
    int draw_x = (client_width - draw_w) / 2;
    int draw_y = (client_height - draw_h) / 2;
    //把客户区坐标换算成远程屏幕坐标（减去留白、按实际绘制区域缩放、夹紧边界）
    int rxPox = (xPos - draw_x) * g_remote_width / draw_w;
    int ryPos = (yPos - draw_y) * g_remote_height / draw_h;
    if(rxPox < 0) rxPox = 0;  if(rxPox > g_remote_width)  rxPox = g_remote_width;
    if(ryPos < 0) ryPos = 0;  if(ryPos > g_remote_height) ryPos = g_remote_height;
    //发送数据
    Mouse mouse;
    mouse.action = Action;
    mouse.ptXY.x = rxPox;
    mouse.ptXY.y = ryPos;
    if(GetTickCount64()-moustick < 50 && Action == static_cast<int>(ENUM_MOUSE::MOVE)) return; //鼠标移动消息间隔至少100毫秒{
    PacketPtr packet = PackPacket(PACKET_MAGE, CMD::CMD_MOUSE, (char*)&mouse, sizeof(Mouse)); //打包数据
    if(!g_socket_sender.Send(
           g_connect_socket,
           reinterpret_cast<const char*>(&packet->header.magic),
           GetPacketLen(packet))){
        return;
    }
    moustick = GetTickCount64(); //更新鼠标移动时间戳
}

//响应，处理消息msg的函数
LRESULT CALLBACK winProc(HWND hwnd, UINT msg, WPARAM wPatam, LPARAM lParam){
    //获取自系统启动以来经过的毫秒数
    static ULONGLONG moustick = GetTickCount64(); //鼠标移动的时间戳，避免频繁发送鼠标移动消息
    switch(msg)
    {
        //不擦背景（配合双缓冲，避免白屏闪烁）
        case WM_ERASEBKGND:
            return 1;
        //绘制屏幕图像（双缓冲，避免闪烁/抖动）
        case WM_PAINT:{
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            EnterCriticalSection(&g_cri_sec);
            if(g_image != NULL){
                RECT client_rect;
                GetClientRect(hwnd, &client_rect);
                int client_width = client_rect.right - client_rect.left;
                int client_height = client_rect.bottom - client_rect.top;

                //远程图片原始尺寸
                int image_width = g_image->GetWidth();
                int image_height = g_image->GetHeight();

                //按客户区缩放，保持宽高比（fit，不变形），并居中
                float scale_w = (float)client_width / image_width;
                float scale_h = (float)client_height / image_height;
                float scale = scale_w < scale_h ? scale_w : scale_h;
                int draw_w = (int)(image_width * scale);
                int draw_h = (int)(image_height * scale);
                int draw_x = (client_width - draw_w) / 2;
                int draw_y = (client_height - draw_h) / 2;

                //双缓冲：先画到内存 DC，再一次 BitBlt 整块贴到窗口
                HDC mem_dc = CreateCompatibleDC(hdc);
                HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, client_width, client_height);
                HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

                Gdiplus::Graphics graphics(mem_dc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
                graphics.DrawImage(g_image, draw_x, draw_y, draw_w, draw_h);

                //一次性把整块贴到窗口，中间不露背景
                BitBlt(hdc, 0, 0, client_width, client_height, mem_dc, 0, 0, SRCCOPY);

                SelectObject(mem_dc, old_bmp);
                DeleteObject(mem_bmp);
                DeleteDC(mem_dc);
            }
            LeaveCriticalSection(&g_cri_sec);
            EndPaint(hwnd, &ps);
            break;
        }
        //鼠标消息
        case WM_MOUSEMOVE:{//鼠标移动
            DOMOUSEACKTION(static_cast<int>(ENUM_MOUSE::MOVE), hwnd, wPatam, lParam, moustick);
            break;
        }
        case WM_LBUTTONUP: //鼠标左键抬起
            DOMOUSEACKTION(static_cast<int>(ENUM_MOUSE::LUP), hwnd, wPatam, lParam, moustick);
            break;
        case WM_LBUTTONDOWN://鼠标左键按下
            DOMOUSEACKTION(static_cast<int>(ENUM_MOUSE::LDOWN), hwnd, wPatam, lParam, moustick);
            break;
        case WM_RBUTTONUP://鼠标右键抬起
            DOMOUSEACKTION(static_cast<int>(ENUM_MOUSE::RUP), hwnd, wPatam, lParam, moustick);
            break;
        case WM_RBUTTONDOWN://鼠标右键按下
            DOMOUSEACKTION(static_cast<int>(ENUM_MOUSE::RDOWN), hwnd, wPatam, lParam, moustick);
            break;
        case WM_LBUTTONDBLCLK://鼠标左键双击
            DOMOUSEACKTION(static_cast<int>(ENUM_MOUSE::LDLICK), hwnd, wPatam, lParam, moustick);
            break;
        case WM_RBUTTONDBLCLK://鼠标右键双击
            DOMOUSEACKTION(static_cast<int>(ENUM_MOUSE::RDLICK), hwnd, wPatam, lParam, moustick);
            break;
        case WM_MBUTTONDBLCLK://鼠标中键双击
            DOMOUSEACKTION(static_cast<int>(ENUM_MOUSE::MDLICK), hwnd, wPatam, lParam, moustick);
            break;
        case WM_MBUTTONDOWN://鼠标中键按下
            DOMOUSEACKTION(static_cast<int>(ENUM_MOUSE::MDOWN), hwnd, wPatam, lParam, moustick);
            break;
        case WM_MBUTTONUP://鼠标中键抬起
            DOMOUSEACKTION(static_cast<int>(ENUM_MOUSE::MUP), hwnd, wPatam, lParam, moustick);
            break;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:{
            Keyboard key_board;
            key_board.virtual_code = wPatam;
            key_board.key_state = 0;   // 0 = 按下（不设 KEYUP 标志）
            PacketPtr packet = PackPacket(PACKET_MAGE, CMD::CMD_KEYBOARD, (char*)&key_board, sizeof(Keyboard)); //打包数据
            g_socket_sender.Send(
                g_connect_socket,
                reinterpret_cast<const char*>(&packet->header.magic),
                GetPacketLen(packet));
            break;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:{
            Keyboard key_board;
            key_board.virtual_code = wPatam;
            key_board.key_state = KEYEVENTF_KEYUP;   // 抬起
            PacketPtr packet = PackPacket(PACKET_MAGE, CMD::CMD_KEYBOARD, (char*)&key_board, sizeof(Keyboard)); //打包数据
            g_socket_sender.Send(
                g_connect_socket,
                reinterpret_cast<const char*>(&packet->header.magic),
                GetPacketLen(packet));
            break;
        }
        default:
            return DefWindowProc(hwnd, msg, wPatam, lParam);
            break;
    }
    return 0;
}



//创建一个窗口的过程
int InitWindow(HINSTANCE hInstance, int nCmdShow){
    //1注册一个窗口
    WNDCLASS ws = {};
    LPCWSTR CLASS_NAME = L"MainWindow";
    ws.lpfnWndProc = winProc;//窗口消息处理函数
    ws.hInstance = hInstance;
    ws.lpszClassName = CLASS_NAME;
    ws.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    ws.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    ws.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    if(!RegisterClass(&ws)){
        MessageBox(NULL, L"窗口注册失败", L"错误", MB_OK | MB_ICONERROR);
        return 0;
    }
    //2.创建一个窗口
    g_hwnd =  CreateWindow(
        CLASS_NAME,  //窗口类名
        L"远程控制",  //窗口标题
        WS_OVERLAPPEDWINDOW, //窗口样式
        CW_USEDEFAULT, CW_USEDEFAULT,//窗口坐标x, y的位置
        600, 400, //窗口的宽高
        NULL,
        NULL,
        hInstance,
        NULL);
    if(g_hwnd == NULL){
        //MessageBox弹出一个选择框
        MessageBox(NULL, L"窗口创建失败", L"错误", MB_OK | MB_ICONERROR);
        return 0;
    }
    //3 显示窗口
    ShowWindow(g_hwnd, nCmdShow);
    //4.更新窗口
    UpdateWindow(g_hwnd);

    return 0;
}
//连接服务器
int InitSocket(){
    //0.初始化 Winsock 环境
    WSADATA wsaData;
    if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0){
        printf("Winsock 初始化失败，错误码：%d\n", WSAGetLastError());
        return -1;
    }

    //1.创建socket连接
    g_connect_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(g_connect_socket == INVALID_SOCKET){    
        printf("创建客户端套接字失败，错误码：%d\n", WSAGetLastError());
        return -1;
    }
    
    return 0;
}
