#include "../socket_send.hpp"
#include "../screen_protocol.hpp"
#include "../packet_protocol.hpp"
#include <stdio.h>
#include <iostream>
#include <memory>
#include <vector>
#include <Windows.h> //操作系统接口
#include <gdiplus.h> //使用GDI+
using namespace Gdiplus;
#pragma comment(lib, "ws2_32.lib") //链接库文件，Windows Socket 2.0 库文件

#define RECV_BUFFER_LEN 1024 *1024*10
SOCKET g_connect_socket;
SocketSender g_socket_sender;
HWND g_hwnd = NULL; 

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

//开辟一条新的线程，用来不断接受和发送对屏幕数据的请求和数据
//返回值 调用约定 函数名
Gdiplus::Bitmap* g_image = NULL;  // 全局图片，WM_PAINT 绘制要用
int g_remote_width = -1;  // 远程屏幕宽度，-1 表示未知
int g_remote_height = -1; // 远程屏幕高度，-1 表示未知
CRITICAL_SECTION g_cri_sec; //锁，避免多线程同时改变一个变量
DWORD WINAPI SendScreenCallBack (LPVOID lpThreadParameter){
    std::vector<char> recv_buffer(RECV_BUFFER_LEN);   // 接收缓冲，RAII 自动释放
    int index = 0;   // 累积缓冲：已收到、还没取走的字节数
    bool force_full_next = true;

    //首次连接必须请求完整关键帧，之后才能在完整画布上叠加局部更新。
    {
        ScreenRequest request{1};
        auto req = remote::PacketBuffer::Build(
            remote::Command::Screen, &request, sizeof(request));
        if(!g_socket_sender.Send(
               g_connect_socket,
               reinterpret_cast<const char*>(req.data()),
               static_cast<int>(req.size()))){
            return 0;
        }
    }

    while(true){
        //收一个完整包（累积，收不完整就一直 recv，不发请求）
        std::optional<remote::PacketBuffer> pck;
        while(!pck.has_value()){
            int len = recv(g_connect_socket, recv_buffer.data() + index, RECV_BUFFER_LEN - index, 0);
            if(len <= 0) return 0;   //连接断开
            index += len;
            auto parsed = remote::ParsePacket(
                reinterpret_cast<const std::uint8_t*>(recv_buffer.data()),
                static_cast<std::size_t>(index));
            if(parsed.status == remote::ParseStatus::Invalid) return 0;
            if(parsed.status == remote::ParseStatus::Incomplete){
                if(parsed.discarded_prefix > 0){
                    index -= static_cast<int>(parsed.discarded_prefix);
                    memmove(
                        recv_buffer.data(),
                        recv_buffer.data() + parsed.discarded_prefix,
                        static_cast<std::size_t>(index));
                }
                continue;
            }

            const std::size_t consumed =
                parsed.discarded_prefix + parsed.packet_length;
            pck = std::move(parsed.packet.value());
            index -= static_cast<int>(consumed);
            memmove(
                recv_buffer.data(), recv_buffer.data() + consumed,
                static_cast<std::size_t>(index));
        }

        bool applied = false;
        bool changed = false;
        if(pck->header().command == static_cast<std::int32_t>(remote::Command::Screen) &&
           pck->header().body_length >= static_cast<int>(sizeof(ScreenUpdateHeader))){
            ScreenUpdateHeader update{};
            memcpy(&update, pck->body(), sizeof(update));
            if(IsValidScreenUpdate(update, pck->header().body_length)){
                if(update.frame_type == SCREEN_FRAME_UNCHANGED){
                    applied = true;
                } else {
                    const char* png_data = reinterpret_cast<const char*>(
                        pck->body() + sizeof(ScreenUpdateHeader));
                    HGLOBAL image_memory = GlobalAlloc(GMEM_MOVEABLE, update.image_length);
                    IStream* image_stream = NULL;
                    if(image_memory != NULL &&
                       CreateStreamOnHGlobal(image_memory, TRUE, &image_stream) == S_OK){
                        ULONG written = 0;
                        if(image_stream->Write(png_data, update.image_length, &written) == S_OK &&
                           written == static_cast<ULONG>(update.image_length)){
                            LARGE_INTEGER beginning{};
                            image_stream->Seek(beginning, STREAM_SEEK_SET, NULL);
                            Gdiplus::Bitmap* patch = Gdiplus::Bitmap::FromStream(image_stream);
                            if(patch != NULL && patch->GetLastStatus() == Gdiplus::Ok &&
                               patch->GetWidth() == static_cast<UINT>(update.width) &&
                               patch->GetHeight() == static_cast<UINT>(update.height)){
                                if(update.frame_type == SCREEN_FRAME_FULL){
                                    Gdiplus::Bitmap* new_canvas = new Gdiplus::Bitmap(
                                        update.screen_width, update.screen_height,
                                        PixelFormat32bppARGB);
                                    Gdiplus::Graphics canvas_graphics(new_canvas);
                                    if(canvas_graphics.DrawImage(patch, 0, 0) == Gdiplus::Ok){
                                        EnterCriticalSection(&g_cri_sec);
                                        if(g_image) delete g_image;
                                        g_image = new_canvas;
                                        g_remote_width = update.screen_width;
                                        g_remote_height = update.screen_height;
                                        LeaveCriticalSection(&g_cri_sec);
                                        applied = true;
                                        changed = true;
                                    } else {
                                        delete new_canvas;
                                    }
                                } else {
                                    EnterCriticalSection(&g_cri_sec);
                                    if(g_image != NULL &&
                                       g_remote_width == update.screen_width &&
                                       g_remote_height == update.screen_height){
                                        Gdiplus::Graphics canvas_graphics(g_image);
                                        if(canvas_graphics.DrawImage(patch, update.x, update.y) == Gdiplus::Ok){
                                            applied = true;
                                            changed = true;
                                        }
                                    }
                                    LeaveCriticalSection(&g_cri_sec);
                                }
                            }
                            delete patch;
                        }
                        image_stream->Release();
                    } else if(image_memory != NULL){
                        GlobalFree(image_memory);
                    }
                }
            }
        }
        force_full_next = !applied;
        if(changed){
            InvalidateRect(g_hwnd, NULL, FALSE);
        }
        pck.reset(); //保持原释放时机，避免在等待下一帧时继续占用屏幕包内存

        //限制轮询到约 20 FPS；无变化帧也不会形成占满 CPU 的请求循环。
        Sleep(50);
        ScreenRequest request{force_full_next ? 1 : 0};
        auto req = remote::PacketBuffer::Build(
            remote::Command::Screen, &request, sizeof(request));
        if(!g_socket_sender.Send(
               g_connect_socket,
               reinterpret_cast<const char*>(req.data()),
               static_cast<int>(req.size()))){
            return 0;
        }
    }
}

void DOMOUSEACKTION(int Action, HWND hwnd, WPARAM wPatam, LPARAM lParam, ULONGLONG& moustick){
//拿到的是客户区的鼠标位置
    int xPos = LOWORD(lParam); //低字节是x坐标
    int yPos = HIWORD(lParam); //高字节是y坐标
    EnterCriticalSection(&g_cri_sec);
    const int remote_width = g_remote_width;
    const int remote_height = g_remote_height;
    LeaveCriticalSection(&g_cri_sec);
    if(remote_width == -1 || remote_height == -1) return;
    //和 WM_PAINT 一样的"保留宽高比 fit"，算出画面实际绘制的区域
    RECT client_rect;
    GetClientRect(hwnd, &client_rect);
    int client_width = client_rect.right - client_rect.left;
    int client_height = client_rect.bottom - client_rect.top;
    if(client_width <= 0 || client_height <= 0) return;
    float scale_w = (float)client_width / remote_width;
    float scale_h = (float)client_height / remote_height;
    float scale = scale_w < scale_h ? scale_w : scale_h;
    int draw_w = (int)(remote_width * scale);
    int draw_h = (int)(remote_height * scale);
    if(draw_w <= 0 || draw_h <= 0) return;
    int draw_x = (client_width - draw_w) / 2;
    int draw_y = (client_height - draw_h) / 2;
    //把客户区坐标换算成远程屏幕坐标（减去留白、按实际绘制区域缩放、夹紧边界）
    int rxPox = (xPos - draw_x) * remote_width / draw_w;
    int ryPos = (yPos - draw_y) * remote_height / draw_h;
    if(rxPox < 0) rxPox = 0;  if(rxPox >= remote_width)  rxPox = remote_width - 1;
    if(ryPos < 0) ryPos = 0;  if(ryPos >= remote_height) ryPos = remote_height - 1;
    //发送数据
    Mouse mouse;
    mouse.action = Action;
    mouse.ptXY.x = rxPox;
    mouse.ptXY.y = ryPos;
    if(GetTickCount64()-moustick < 50 && Action == static_cast<int>(ENUM_MOUSE::MOVE)) return; //鼠标移动消息间隔至少100毫秒{
    auto packet = remote::PacketBuffer::Build(
        remote::Command::Mouse, &mouse, sizeof(mouse));
    if(!g_socket_sender.Send(
           g_connect_socket,
           reinterpret_cast<const char*>(packet.data()),
           static_cast<int>(packet.size()))){
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
            auto packet = remote::PacketBuffer::Build(
                remote::Command::Keyboard, &key_board, sizeof(key_board));
            g_socket_sender.Send(
                g_connect_socket,
                reinterpret_cast<const char*>(packet.data()),
                static_cast<int>(packet.size()));
            break;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:{
            Keyboard key_board;
            key_board.virtual_code = wPatam;
            key_board.key_state = KEYEVENTF_KEYUP;   // 抬起
            auto packet = remote::PacketBuffer::Build(
                remote::Command::Keyboard, &key_board, sizeof(key_board));
            g_socket_sender.Send(
                g_connect_socket,
                reinterpret_cast<const char*>(packet.data()),
                static_cast<int>(packet.size()));
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
