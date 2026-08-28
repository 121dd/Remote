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
        if(*(int*)(buffer + i) == 0x55AA77CC){
            //找到了包头
            pck.header.magic = *(int*)(buffer + i);
            i += 4;
            break;
        }
    }
    pck.header.cmd = *(int*)(buffer + i);
    i += 4;
    pck.header.body_len = *(int*)(buffer + i);
    i += 4;
    //获取数据,必须先创建pck去存pck.header.body_len不然不知道长度
    if(pck.header.body_len > 0){
        //创建接受缓存区
        pck_ptr = (Packet*)malloc(sizeof(PacketHeader) + pck.header.body_len);
        memcpy(pck_ptr->body, buffer + i, pck.header.body_len);
        memcpy(&pck_ptr->header, &pck.header, sizeof(PacketHeader));
    }
    return pck_ptr;
}

int GetPacketLen(Packet* pck){
    if(pck != NULL){
        return pck->header.body_len + sizeof(PacketHeader);
    }
    return 0;
}