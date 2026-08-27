#include <stdio.h>
#include <iostream>
#include <Windows.h> //操作系统接口
#pragma comment(lib, "ws2_32.lib") //链接库文件，Windows Socket 2.0 库文件 

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