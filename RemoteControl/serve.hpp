#include <stdio.h>
#include <iostream>
#include <Windows.h> //操作系统接口
#include <gdiplus.h> //使用GDI+

using namespace Gdiplus;

#pragma comment(lib, "ws2_32.lib") //链接库文件，Windows Socket 2.0 库文件

#define BECV_BUFFER_SIZE 1024*1024*1
#define PACKET_MAGE 0x55AA77CC
//枚举（enum）定义, 给枚举成员赋值了 1、2、4 这样的二进制位标志（Bit Flags）值。
enum CMD{
    CMD_SCREEN = 1,
    CMD_MOUSE = 2,
    CMD_KEYBOARD = 4,
    CMD_TEST = 2026
};

SOCKET g_listen_socket;
SOCKET g_connect_socket;

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

//解包，提取一次数据
Packet* ParsePacket(char* buffer, int len){
    Packet pck;
    Packet* pck_ptr;
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
        return NULL;
    }
    pck.header.cmd = *(int*)(buffer + i);
    i += 4;
    pck.header.body_len = *(int*)(buffer + i);
    i += 4;
    //获取数据,必须先创建pck去存pck.header.body_len不然不知道长度
    if(pck.header.body_len <= 0 || pck.header.body_len > len - i){  // body 长度越界
        return NULL;
    }
        //创建接受缓存区
    pck_ptr = (Packet*)malloc(sizeof(PacketHeader) + pck.header.body_len);
    memcpy(pck_ptr->body, buffer + i, pck.header.body_len);
    memcpy(&pck_ptr->header, &pck.header, sizeof(PacketHeader));
    return pck_ptr;
}

//包长
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

int HandleScreen(Packet* pck){
    //GDI+ 初始化和 DPI 感知已移到 main，这里只负责截屏打包

    int sWidth  = GetSystemMetrics(SM_CXSCREEN);
    int sHeight = GetSystemMetrics(SM_CYSCREEN);
    std::cout << "屏幕宽高: " << sWidth << "x" << sHeight << std::endl;

    //1.拿屏幕 DC 并创建兼容的内存 DC 和位图
    HDC hScreen = GetDC(NULL); //获取屏幕的DC
    //“空壳画架”
    HDC hMemDC = CreateCompatibleDC(hScreen);
    //真正的“画布”
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, sWidth, sHeight);
    //绑定
    HGDIOBJ hOld = SelectObject(hMemDC, hBitmap);

    //5.把屏幕复制到位图（BitBlt 截屏）
    BitBlt(hMemDC, 0, 0, sWidth, sHeight, hScreen, 0, 0, SRCCOPY);

    //3.清理 GDI 资源
    SelectObject(hMemDC, hOld);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreen);

    //4.用 GDI+ 包装 HBITMAP 并保存到内存流
    //放在作用域块里，让 Bitmap 在函数结束时先析构
    {
        Bitmap bitmap(hBitmap, NULL);

        //找到 PNG 编码器的 CLSID
        CLSID pngClsid;
        if(GetEncoderClsid(L"image/png", &pngClsid) == -1){
            std::cout << "找不到 PNG 编码器" << std::endl;
            DeleteObject(hBitmap);
            return -1;
        }


        //句柄是一个"身份证号"或"门票编号"，用来让程序告诉操作系统："我要操作那个资源！"

        //从堆上申请可变化的内存块（对应 HGLOBAL hMen = GlobalAlloc(GMEM_MOVEABLE, 0)）
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, 0); //指向一块空内存
        if(hMem == NULL){
            std::cout << "GlobalAlloc 失败" << std::endl;
            DeleteObject(hBitmap);
            return -1;
        }
        //创建内存流（TRUE 表示流 Release 时自动释放 hMem，之后不要再 GlobalFree）
        IStream* pStream = NULL;
        HRESULT ret = CreateStreamOnHGlobal(hMem, TRUE, &pStream);
        if(ret != S_OK){
            std::cout << "CreateStreamOnHGlobal 失败" << std::endl;
            GlobalFree(hMem);
            DeleteObject(hBitmap);
            return -1;
        }

        //保存 PNG 到内存流（MinGW 的 Save 第 3 个参数必须传 NULL）
        Gdiplus::Status st = bitmap.Save(pStream, &pngClsid, NULL); //内存不足导致扩容GlobalReAlloc 
        //现在 IStream 管理的可能已经不是原来的内存块地址了但 hMem 变量依然保存的是最初的值！
        if(st != Gdiplus::Ok){
            std::cout << "保存失败, Status=" << st << std::endl;
            pStream->Release();   // 释放流，连带释放 hMem
            DeleteObject(hBitmap);
            return -1;
        }

        //流增长时 GlobalReAlloc 可能移动句柄，必须用 GetHGlobalFromStream 重新拿当前句柄，存在hCurrent
        //否则 GlobalLock 读到的可能不是正确的 PNG 数据
        HGLOBAL hCurrent = NULL;
        GetHGlobalFromStream(pStream, &hCurrent); //从流对象里反向拿到内存句柄
        char* pdata = (char*)GlobalLock(hCurrent);//锁定内存，获取直接访问指针
        int len = GlobalSize(hCurrent);//获取内存块的实际大小
        std::cout << "PNG 字节数: " << len << std::endl;

        //发送数据
        Packet* packet = PackPacket(PACKET_MAGE, CMD_SCREEN, pdata, len);
        send(g_connect_socket, (char*)&packet->header.magic, sizeof(PacketHeader) + packet->header.body_len, 0);
        //TODO: 把 pdata 的前 len 个字节塞进 Packet 发回客户端
        free(packet);
        GlobalUnlock(hCurrent);
        pStream->Release();   // 释放流，连带释放 hMem，不要再 GlobalFree
    } //Bitmap 在这里析构

    //释放 HBITMAP
    DeleteObject(hBitmap);
    return 0;
}

int HandleMouse(Packet* pck){
    return 0;
}

int HandleKeyboard(Packet* pck){
    return 0;
}

int HandleTest(Packet* pck){
    return 0;
}

//处理命令
int HandleCommand(Packet* pck){
    int ret = 0;
    switch(pck->header.cmd){
        //发送屏幕
        case CMD_SCREEN: 
            ret = HandleScreen(pck);
            break;
        //鼠标事件
        case CMD_MOUSE: 
            ret = HandleMouse(pck);
            break;
        //键盘命令
        case CMD_KEYBOARD: 
            ret = HandleKeyboard(pck);
            break;
        //测试命令
        case CMD_TEST:
            ret = HandleTest(pck);
            break;
        default: break;
    }
    return ret;
}