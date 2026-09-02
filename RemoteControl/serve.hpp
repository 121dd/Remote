#include <winsock2.h>   //必须最先：定义 _WINSOCK2API_，否则 iphlpapi.h 不声明 GetAdaptersAddresses
#include "../socket_send.hpp"
#include "../dirty_matrix.hpp"
#include "../screen_protocol.hpp"
#include <stdio.h>
#include <iostream>
#include <Windows.h> //操作系统接口
#include <gdiplus.h> //使用GDI+
#include <iphlpapi.h>   //GetAdaptersAddresses 枚举网卡
#include <vector>
#include <memory>
#include <cstring>

using namespace Gdiplus;

#pragma comment(lib, "ws2_32.lib") //链接库文件，Windows Socket 2.0 库文件

#define BECV_BUFFER_SIZE 1024*1024*10
#define PACKET_MAGE 0x55AA77CC
//枚举（enum）定义, 给枚举成员赋值了 1、2、4 这样的二进制位标志（Bit Flags）值。
enum CMD{
    CMD_SCREEN = 1,
    CMD_MOUSE = 2,
    CMD_KEYBOARD = 4,
    CMD_TEST = 2026
};

SOCKET g_listen_socket;//监听socket
SOCKET g_connect_socket;//连接socket

//上一张已经成功发送的 32 位 BGRA 屏幕，用于按 64x64 网格检测变化。
std::vector<unsigned char> g_previous_screen;
int g_previous_screen_width = 0;
int g_previous_screen_height = 0;

unsigned long handle_screen_thread_id = 0; //系统分配给这条处理屏幕的“身份证号”
unsigned long handle_mouse_thread_id = 1; //系统分配给这条处理鼠标的“身份证号”
unsigned long handle_keyboard_thread_id = 2; //系统分配给这条处理键盘的“身份证号”
#define WM_HANDEL_SCREEN (WM_USER + 1)
#define WM_HANDEL_MOUSE (WM_USER + 2)
#define WM_HANDEL_KEYBOARD (WM_USER + 3)
#define WM_HANDEL_INVOKE_MSG_LOOP (WM_USER + 4) //用来启动消息队列

#pragma pack(push, 1) //设置结构体对齐方式为1字节对齐
//数据包头部结构体
struct PacketHeader{
    int magic; //魔数，用于标识数据包的合法性
    int cmd; //四字节命令号
    int body_len; //数据体长度
};
//数据包结构体
struct Packet{
    PacketHeader header; //数据包头部
    char body[]; //数据包体, 不固定长度
};
#pragma pack(pop) //恢复结构体对齐方式为默认值

//定义智能指针：Packet 用 malloc 分配（body 长度不定），所以删除器要调 free 而不是 delete
struct PacketDeleter{ void operator()(Packet* p) const { free(p); }};
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

//键盘
struct Keyboard{
    int virtual_code;//虚拟键码
    int key_state;//按键状态 0:抬起 1:按下
};

//解包，提取一次数据
PacketPtr ParsePacket(char* buffer, int len){
    Packet pck;
    PacketPtr pck_ptr;
    //4字节包头，4字节命令号，4字节数据长度，数据
    int i = 0;
    for(;i < len; i++){
        //找包头
        //当i=0时，int*第一个地址开始解析为int
        //int类型就代表再往后4个字节的内容，*(int*)(buffer + i)就是把buffer+i的地址强制转换为int*类型，然后取这个地址的值
        if(*(int*)(buffer + i) == 0x55AA77CC){
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

    //当len长度为0的时候也要执行
    if(pck.header.body_len <= 0 || pck.header.body_len > len - i){  // body 长度越界
        //当len长度为0的时候也要执行，获取的是命令
        if(pck.header.body_len ==0){
            pck_ptr = PacketPtr((Packet*)malloc(sizeof(PacketHeader))); //构造一个临时对象然后移动赋值
            memcpy(&pck_ptr->header, &pck.header, sizeof(PacketHeader));
            return pck_ptr;
        }
        else{
            return nullptr;
        }
    }
    //创建接受缓存区
    pck_ptr = PacketPtr((Packet*)malloc(sizeof(PacketHeader) + pck.header.body_len));
    memcpy(pck_ptr->body, buffer + i, pck.header.body_len);
    memcpy(pck_ptr.get(), &pck.header, sizeof(PacketHeader));
    return pck_ptr;
}
//包长, packet的长度
int GetPacketLen(const PacketPtr& pck){
    if(pck != NULL){
        return pck->header.body_len + sizeof(PacketHeader);
    }
    return 0;
}
//封装要发送的数据
PacketPtr PackPacket(int magic, int cmd, char* buffer, int buffer_len){
    PacketPtr pck = PacketPtr((Packet*)malloc(buffer_len + sizeof(PacketHeader)));
    pck->header.magic = magic; 
    pck->header.cmd = cmd;
    pck->header.body_len = buffer_len; //数据体长度
    memcpy(pck->body, buffer, pck->header.body_len); //把buffer中的数据拷贝到packet的body中
    return pck;
}

//查找指定 MIME 类型的图片编码器 CLSID（GDI+ 没有 ImageFormatPNG 这种常量，要自己查）
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid){
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if(size == 0) return -1;
    Gdiplus::ImageCodecInfo* pInfo = (Gdiplus::ImageCodecInfo*)malloc(size);
    Gdiplus::GetImageEncoders(num, size, pInfo);
    for(UINT i = 0; i < num; ++i){
        if(wcscmp(pInfo[i].MimeType, format) == 0){
            *pClsid = pInfo[i].Clsid;
            free(pInfo);
            return 0;
        }
    }
    free(pInfo);
    return -1;
}

//处理屏幕命令：按 64x64 网格比较两帧，只编码覆盖脏块的最小矩形。
int HandleScreen(const Packet* pck){
    const int screen_width = GetSystemMetrics(SM_CXSCREEN);
    const int screen_height = GetSystemMetrics(SM_CYSCREEN);
    const int stride = screen_width * 4;
    if(screen_width <= 0 || screen_height <= 0) return -1;

    if(pck->header.body_len != 0 && pck->header.body_len != sizeof(ScreenRequest)){
        return -1;
    }
    bool force_full = false;
    if(pck->header.body_len == sizeof(ScreenRequest)){
        ScreenRequest request{};
        memcpy(&request, pck->body, sizeof(request));
        force_full = request.force_full != 0;
    }

    //使用自顶向下的 32 位 DIB，BitBlt 后可以直接读取连续 BGRA 像素。
    HDC screen_dc = GetDC(NULL);
    HDC memory_dc = CreateCompatibleDC(screen_dc);
    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = screen_width;
    bitmap_info.bmiHeader.biHeight = -screen_height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP screen_bitmap = CreateDIBSection(
        screen_dc, &bitmap_info, DIB_RGB_COLORS, &pixels, NULL, 0);
    if(screen_dc == NULL || memory_dc == NULL || screen_bitmap == NULL || pixels == nullptr){
        if(screen_bitmap) DeleteObject(screen_bitmap);
        if(memory_dc) DeleteDC(memory_dc);
        if(screen_dc) ReleaseDC(NULL, screen_dc);
        return -1;
    }

    HGDIOBJ old_bitmap = SelectObject(memory_dc, screen_bitmap);
    const BOOL captured = BitBlt(
        memory_dc, 0, 0, screen_width, screen_height,
        screen_dc, 0, 0, SRCCOPY | CAPTUREBLT);
    SelectObject(memory_dc, old_bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);
    if(!captured){
        DeleteObject(screen_bitmap);
        return -1;
    }

    std::vector<unsigned char> current_screen(
        static_cast<std::size_t>(stride) * screen_height);
    memcpy(current_screen.data(), pixels, current_screen.size());
    DeleteObject(screen_bitmap);

    const bool dimensions_changed =
        g_previous_screen_width != screen_width ||
        g_previous_screen_height != screen_height;
    const unsigned char* previous =
        (!dimensions_changed && !g_previous_screen.empty())
            ? g_previous_screen.data() : nullptr;
    DirtyRegion region = FindDirtyRegion(
        previous, current_screen.data(), screen_width, screen_height,
        stride, 64, 40);
    if(force_full){
        region = FullFrameRegion(
            screen_width, screen_height,
            ((screen_width + 63) / 64) * ((screen_height + 63) / 64));
    }

    ScreenUpdateHeader update{};
    update.screen_width = screen_width;
    update.screen_height = screen_height;
    update.frame_type = SCREEN_FRAME_UNCHANGED;

    //即使画面没变化也必须回复，否则客户端会一直阻塞在 recv。
    if(!region.has_changes){
        PacketPtr packet = PackPacket(
            PACKET_MAGE, CMD_SCREEN,
            reinterpret_cast<char*>(&update), sizeof(update));
        return SendAll(
            g_connect_socket,
            reinterpret_cast<const char*>(&packet->header.magic),
            GetPacketLen(packet)) ? 0 : -1;
    }

    update.frame_type = region.full_frame ? SCREEN_FRAME_FULL : SCREEN_FRAME_DIRTY;
    update.x = region.x;
    update.y = region.y;
    update.width = region.width;
    update.height = region.height;

    CLSID png_clsid;
    if(GetEncoderClsid(L"image/png", &png_clsid) == -1) return -1;

    HGLOBAL stream_memory = GlobalAlloc(GMEM_MOVEABLE, 0);
    if(stream_memory == NULL) return -1;
    IStream* stream = NULL;
    if(CreateStreamOnHGlobal(stream_memory, TRUE, &stream) != S_OK){
        GlobalFree(stream_memory);
        return -1;
    }

    Gdiplus::Bitmap patch(
        region.width, region.height, stride,
        PixelFormat32bppRGB, //DIB 的第 4 字节未定义，按 RGB 读取可避免被当成透明 alpha。
        current_screen.data() +
            static_cast<std::size_t>(region.y) * stride +
            static_cast<std::size_t>(region.x) * 4);
    const Gdiplus::Status encode_status = patch.Save(stream, &png_clsid, NULL);
    if(encode_status != Gdiplus::Ok){
        stream->Release();
        return -1;
    }

    HGLOBAL current_memory = NULL;
    if(GetHGlobalFromStream(stream, &current_memory) != S_OK){
        stream->Release();
        return -1;
    }
    STATSTG stream_stat{};
    if(stream->Stat(&stream_stat, STATFLAG_NONAME) != S_OK ||
       stream_stat.cbSize.QuadPart <= 0 ||
       stream_stat.cbSize.QuadPart > SCREEN_MAX_IMAGE_BYTES){
        stream->Release();
        return -1;
    }
    char* png_data = static_cast<char*>(GlobalLock(current_memory));
    const int png_size = static_cast<int>(stream_stat.cbSize.QuadPart);
    if(png_data == nullptr){
        if(png_data) GlobalUnlock(current_memory);
        stream->Release();
        return -1;
    }

    update.image_length = png_size;
    std::vector<char> body(sizeof(update) + update.image_length);
    memcpy(body.data(), &update, sizeof(update));
    memcpy(body.data() + sizeof(update), png_data, update.image_length);
    GlobalUnlock(current_memory);
    stream->Release();

    PacketPtr packet = PackPacket(
        PACKET_MAGE, CMD_SCREEN, body.data(), static_cast<int>(body.size()));
    const bool sent = SendAll(
        g_connect_socket,
        reinterpret_cast<const char*>(&packet->header.magic),
        GetPacketLen(packet));
    if(sent){
        g_previous_screen = std::move(current_screen);
        g_previous_screen_width = screen_width;
        g_previous_screen_height = screen_height;
        std::cout << (region.full_frame ? "完整帧" : "脏矩形")
                  << ": " << region.width << "x" << region.height
                  << ", PNG字节数: " << update.image_length << std::endl;
    }
    return sent ? 0 : -1;
}
//处理鼠标命令
int HandleMouse(const Packet* pck){
    Mouse mouse;
    memcpy(&mouse.action, pck->body, pck->header.body_len);
    std::cout << "鼠标动作: " << mouse.action << ", 坐标: (" << mouse.ptXY.x << ", " << mouse.ptXY.y << ")" << std::endl;
    //模拟鼠标事件
    //设置鼠标位置
    SetCursorPos(mouse.ptXY.x, mouse.ptXY.y);
    switch (static_cast<ENUM_MOUSE>(mouse.action))
    {
    case ENUM_MOUSE::MOVE:
        SetCursorPos(mouse.ptXY.x, mouse.ptXY.y);
        //鼠标移动
        break;
    //可以改成SendInput更加安全
    case ENUM_MOUSE::LDOWN:
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, GetMessageExtraInfo());
        break;
    case ENUM_MOUSE::LUP:
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, GetMessageExtraInfo());
        break;
    case ENUM_MOUSE::RDOWN:
        mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, GetMessageExtraInfo());     
        break;
    case ENUM_MOUSE::RUP:
        mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, GetMessageExtraInfo());
        break;
    case ENUM_MOUSE::MDOWN:
        mouse_event(MOUSEEVENTF_MIDDLEDOWN, 0, 0, 0, GetMessageExtraInfo());
        break;
    case ENUM_MOUSE::MUP:
        mouse_event(MOUSEEVENTF_MIDDLEUP, 0, 0, 0, GetMessageExtraInfo());
        break;
    case ENUM_MOUSE::LDLICK:
        //只发一次点击（第一次点击的 DOWN/UP 已由单击消息发过，避免"双击变三击"）
        mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, GetMessageExtraInfo());
        break;
    case ENUM_MOUSE::RDLICK:
        mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0 , 0, 0, GetMessageExtraInfo());
        break;
    case ENUM_MOUSE::MDLICK:
        mouse_event(MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP, 0, 0, 0, GetMessageExtraInfo());
        break;
    default:
        std::cout << "未知鼠标动作: " << mouse.action << std::endl;
        break;
    }

    return 0;
}
//处理键盘命令
int HandleKeyboard(const Packet* pck){
    Keyboard key_board;
    memcpy(&key_board.virtual_code, pck->body, pck->header.body_len);
    std::cout << "键盘动作: " << key_board.virtual_code << ", 状态: " << key_board.key_state << std::endl;
    INPUT input = {0};
    input.type = INPUT_KEYBOARD; //输入类型为键盘
    input.ki.wVk = key_board.virtual_code; //虚拟键码
    input.ki.wScan = 0; //硬件扫描码
    input.ki.time = 0;
    input.ki.dwFlags = key_board.key_state; //按钮状态 0 按下，1松开
    input.ki.dwExtraInfo = 0;
    int ret = SendInput(1, &input, sizeof(INPUT));
    if(ret > 0){
        std::cout << "键盘事件发送成功:" << key_board.virtual_code << std::endl;
    }
    return 0;
}
//测试
int HandleTest(const Packet* pck){
    return 0;
}

//处理命令
int HandleCommand(PacketPtr pck){
    int ret = 0;
    switch(pck->header.cmd){
        //发送屏幕
        case CMD_SCREEN: {
            //release() 把所有权交出去，裸指针塞进消息投给线程
            Packet* raw = pck.release();
            if(!PostThreadMessage(handle_screen_thread_id, WM_HANDEL_SCREEN, 0, (LPARAM)raw))
                free(raw);   //投递失败，没人接管，这里释放
            break;
        }
        //鼠标事件
        case CMD_MOUSE: {
            Packet* raw = pck.release();
            if(!PostThreadMessage(handle_mouse_thread_id, WM_HANDEL_MOUSE, 0, (LPARAM)raw))
                free(raw);
            break;
        }
        //键盘命令
        case CMD_KEYBOARD: {
            Packet* raw = pck.release();
            if(!PostThreadMessage(handle_keyboard_thread_id, WM_HANDEL_KEYBOARD, 0, (LPARAM)raw))
                free(raw);
            break;
        }
        //测试命令（同步处理，std::move 转所有权，函数结束自动释放）
        case CMD_TEST:
            break;
        //未知命令：pck 是局部变量，函数结束自动释放
        default:
            break;
    }
    return ret;
}

//处理命令的线程  回调函数
DWORD WINAPI HandleScreenThreadFuc(LPVOID lpThreadParameter){
    //第一次往线程里面post消息，消息队列还没创建好，消息就丢了
    MSG msg;
    while(GetMessage(&msg, 0, 0, 0)){ //该线程在这里永久的等待消息
        if(msg.message == WM_HANDEL_SCREEN){
            PacketPtr pck((Packet*)msg.lParam);   // 线程持有所有权，作用域结束自动 free
            if(HandleScreen(pck.get()) != 0){
                //请求已经被消费却无法回复时，主动断开，避免客户端永远阻塞在 recv。
                shutdown(g_connect_socket, SD_BOTH);
                break;
            }
        }
    }
    return 0;
}
DWORD WINAPI HandleMouseThreadFuc(LPVOID lpThreadParameter){
     MSG msg;
    while(GetMessage(&msg, 0, 0, 0)){
        if(msg.message == WM_HANDEL_MOUSE){
            PacketPtr pck((Packet*)msg.lParam);   // 线程持有所有权，作用域结束自动 free
            HandleMouse(pck.get());                // 借用裸指针给 HandleMouse
        }
    }
    return 0;
}
DWORD WINAPI HandleKeyboardThreadFuc(LPVOID lpThreadParameter){
    MSG msg;
    while(GetMessage(&msg, 0, 0, 0)){
        if(msg.message == WM_HANDEL_KEYBOARD){
            PacketPtr pck((Packet*)msg.lParam);   // 线程持有所有权，作用域结束自动 free
            HandleKeyboard(pck.get());             // 借用裸指针给 HandleKeyboard
        }
    }
    return 0;
}


//初始化网络并且开启监听
int InitServer(){
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
    g_listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(g_listen_socket == INVALID_SOCKET){
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
    if(bind(g_listen_socket, (sockaddr*)&server_addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR){
        printf("绑定地址和端口失败，错误码：%d\n", WSAGetLastError());
        return -1;
    }
    
    //3.开启服务器监听 backlog:已完成三次握手、但还未被 accept() 取走的客户端连接
    if( listen(g_listen_socket, 1) == SOCKET_ERROR){
        printf("开启服务器监听失败，错误码：%d\n", WSAGetLastError());
        return -1;
    }
    
    return 0;
}

//打印本机所有激活的 IPv4 地址（方便客户端知道该填什么 IP）
void PrintLocalIPs(){
    ULONG bufLen = 0;
    GetAdaptersAddresses(AF_INET, 0, NULL, NULL, &bufLen);
    IP_ADAPTER_ADDRESSES* addrs = (IP_ADAPTER_ADDRESSES*)malloc(bufLen);
    if(GetAdaptersAddresses(AF_INET, 0, NULL, addrs, &bufLen) == NO_ERROR){
        for(IP_ADAPTER_ADDRESSES* a = addrs; a != NULL; a = a->Next){
            if(a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;   //跳过回环 127.0.0.1
            if(a->OperStatus != IfOperStatusUp) continue;          //只打印激活的网卡
            for(IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u != NULL; u = u->Next){
                if(u->Address.lpSockaddr->sa_family == AF_INET){
                    char* ip = inet_ntoa(((sockaddr_in*)u->Address.lpSockaddr)->sin_addr);
                    printf("本机IP: %s (%ls)\n", ip, a->FriendlyName);
                }
            }
        }
    }
    free(addrs);
}
