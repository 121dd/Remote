#include <stdio.h>
#include <iostream>
#include <Windows.h> //操作系统接口
#include <gdiplus.h> //使用GDI+
using namespace Gdiplus;
#pragma comment(lib, "ws2_32.lib") //链接库文件，Windows Socket 2.0 库文件
#define RECV_BUFFER_LEN 1024 *1024*10
#define PACKET_MAGE 0x55AA77CC
enum CMD{
    CMD_SCREEN = 1,
    CMD_MOUSE = 2,
    CMD_KEYBOARD = 4,
    CMD_TEST = 2026
};


SOCKET g_connect_socket;
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

//包长, packet的长度
int GetPacketLen(Packet* pck){
    if(pck != NULL){
        return pck->header.body_len + sizeof(PacketHeader);
    }
    return 0;
}

//封装要发送的数据
Packet* PackPacket(int magic, int cmd, char* buffer, int buffer_len){
    Packet* pck = (Packet*)malloc(buffer_len + sizeof(PacketHeader));
    pck->header.magic = magic; 
    pck->header.cmd = cmd;
    pck->header.body_len = buffer_len; //数据体长度
    memcpy(pck->body, buffer, pck->header.body_len); //把buffer中的数据拷贝到packet的body中
    return pck;
}

//解析接收到的数据
Packet* ParsePacket(char* buffer, int len){
    Packet pck;
    Packet* pck_ptr;
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
        return NULL;
    }
    pck.header.cmd = *(int*)(buffer + i);
    i += 4;
    pck.header.body_len = *(int*)(buffer + i);
    i += 4;
    //获取数据,必须先创建pck去存pck.header.body_len不然不知道长度
    if(pck.header.body_len <= 0 || pck.header.body_len > len - i){  // body 长度越界
        //当len长度为0的时候也要执行，获取的是命令
        if(pck.header.body_len ==0){
            pck_ptr = (Packet*)malloc(sizeof(PacketHeader));
            memcpy(&pck_ptr->header, &pck.header, sizeof(PacketHeader));
            return pck_ptr;
        }
        return NULL;
    }
        //创建接受缓存区
    pck_ptr = (Packet*)malloc(sizeof(PacketHeader) + pck.header.body_len);
    memcpy(pck_ptr->body, buffer + i, pck.header.body_len);
    memcpy(&pck_ptr->header, &pck.header, sizeof(PacketHeader));
    return pck_ptr;
}


//开辟一条新的线程
//返回值 调用约定 函数名
Gdiplus::Bitmap* g_image = NULL;  // 全局图片，WM_PAINT 绘制要用
IStream* g_stream = NULL;          // GDI+ 懒解码，流必须和 Bitmap 一起活着
CRITICAL_SECTION g_cri_sec;
DWORD WINAPI SendScreenCallBack (LPVOID lpThreadParameter){
    //不停的发送数据，解析数据
    char* recv_buffer = (char*)malloc(RECV_BUFFER_LEN);
    int index = 0;   // 累积缓冲：已收到、还没取走的字节数

    //先发第一个屏幕请求
    Packet* req = PackPacket(PACKET_MAGE, CMD_SCREEN, NULL, 0);
    send(g_connect_socket, (char*)&req->header.magic, GetPacketLen(req), 0);
    free(req);

    while(true){
        //收一个完整包（累积，收不完整就一直 recv，不发请求）
        Packet* pck = NULL;
        while(pck == NULL){
            int len = recv(g_connect_socket, recv_buffer + index, RECV_BUFFER_LEN - index, 0);
            if(len <= 0) return 0;   //连接断开
            index += len;
            pck = ParsePacket(recv_buffer, index);
        }

        //这个包解析出来了（数据已复制进 pck），从累积缓冲里挪走，剩下前移
        int pck_len = GetPacketLen(pck);
        index -= pck_len;
        memmove(recv_buffer, recv_buffer + pck_len, index);

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
                    EnterCriticalSection(&g_cri_sec);
                    //换新图前先释放旧的（顺序：先删图，再放流）
                    if(g_image) delete g_image;
                    if(g_stream) g_stream->Release();
                    g_image = newImage;
                    g_stream = pStream;   //不能 Release！Bitmap 解码 PNG 时还要读它
                    LeaveCriticalSection(&g_cri_sec);
                    //通知UI线程绘制
                    InvalidateRect(g_hwnd, NULL, FALSE);
                } else {
                    pStream->Release();   //解码失败，释放流（连带释放 hMem）
                }
            }
        }
        free(pck);

        //这一帧处理完了，才请求下一帧
        Packet* req = PackPacket(PACKET_MAGE, CMD_SCREEN, NULL, 0);
        send(g_connect_socket, (char*)&req->header.magic, GetPacketLen(req), 0);
        free(req);
        Sleep(100); 
    }
}

//响应，处理消息msg的函数
LRESULT CALLBACK winProc(HWND hwnd, UINT msg, WPARAM wPatam, LPARAM lParam){
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

                //双缓冲：先画到内存 DC，再一次 BitBlt 整块贴到窗口
                HDC mem_dc = CreateCompatibleDC(hdc);
                HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, client_width, client_height);
                HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

                Gdiplus::Graphics graphics(mem_dc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(g_image, 0, 0, client_width, client_height);

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
    ws.style = CS_HREDRAW | CS_VREDRAW;
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
