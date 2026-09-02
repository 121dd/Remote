#pragma once

#include <winsock2.h>
#include <mutex>
//保证TCP流式数据完整发送 + 多线程互斥保护。
using SocketSendFunction = int (WSAAPI *)(SOCKET, const char*, int, int);

inline bool SendAll(
    SOCKET socket,
    const char* data,
    int length,
    SocketSendFunction send_function = ::send)
{
    if(length < 0 || (length > 0 && data == nullptr)){
        return false;
    }

    int sent_length = 0;
    while(sent_length < length){
        const int current_length = send_function(
            socket, data + sent_length, length - sent_length, 0);
        if(current_length == SOCKET_ERROR || current_length == 0){
            return false;
        }
        sent_length += current_length;
    }
    return true;
}

class SocketSender{
public:
    explicit SocketSender(SocketSendFunction send_function = ::send)
        : send_function_(send_function){}

    bool Send(SOCKET socket, const char* data, int length){
        std::lock_guard<std::mutex> lock(mutex_);
        return SendAll(socket, data, length, send_function_);
    }

private:
    std::mutex mutex_;
    SocketSendFunction send_function_;
};
