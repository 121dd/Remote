#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>

#include "../socket_send.hpp"

namespace {

std::string g_received;
int g_send_calls = 0;
std::atomic<int> g_active_sends(0);
std::atomic<int> g_max_active_sends(0);

int WSAAPI SendThreeBytesAtATime(SOCKET, const char* data, int length, int flags){
    assert(flags == 0);
    const int chunk_length = std::min(length, 3);
    g_received.append(data, chunk_length);
    ++g_send_calls;
    return chunk_length;
}

int WSAAPI FailAfterFirstChunk(SOCKET, const char* data, int length, int flags){
    assert(flags == 0);
    ++g_send_calls;
    if(g_send_calls == 1){
        const int chunk_length = std::min(length, 2);
        g_received.append(data, chunk_length);
        return chunk_length;
    }
    return SOCKET_ERROR;
}

int WSAAPI ObserveConcurrentSend(SOCKET, const char*, int length, int flags){
    assert(flags == 0);
    const int active_sends = ++g_active_sends;
    int previous_max = g_max_active_sends.load();
    while(active_sends > previous_max &&
          !g_max_active_sends.compare_exchange_weak(previous_max, active_sends)){}
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    --g_active_sends;
    return length;
}

void ResetSender(){
    g_received.clear();
    g_send_calls = 0;
}

void TestRetriesUntilEveryByteIsSent(){
    ResetSender();
    const std::string payload = "abcdefghij";

    const bool sent = SendAll(
        INVALID_SOCKET, payload.data(), static_cast<int>(payload.size()),
        SendThreeBytesAtATime);

    assert(sent);
    assert(g_received == payload);
    assert(g_send_calls == 4);
}

void TestReportsFailureWhenAChunkCannotBeSent(){
    ResetSender();
    const std::string payload = "abcdef";

    const bool sent = SendAll(
        INVALID_SOCKET, payload.data(), static_cast<int>(payload.size()),
        FailAfterFirstChunk);

    assert(!sent);
    assert(g_received == "ab");
    assert(g_send_calls == 2);
}

void TestSerializesConcurrentSends(){
    g_active_sends = 0;
    g_max_active_sends = 0;
    std::atomic<int> ready_threads(0);
    std::atomic<bool> start(false);
    SocketSender sender(ObserveConcurrentSend);

    const auto send_packet = [&](){
        ++ready_threads;
        while(!start.load()){
            std::this_thread::yield();
        }
        assert(sender.Send(INVALID_SOCKET, "packet", 6));
    };

    std::thread first(send_packet);
    std::thread second(send_packet);
    while(ready_threads.load() < 2){
        std::this_thread::yield();
    }
    start = true;
    first.join();
    second.join();

    assert(g_max_active_sends.load() == 1);
}

} // namespace

int main(){
    TestRetriesUntilEveryByteIsSent();
    TestReportsFailureWhenAChunkCannotBeSent();
    TestSerializesConcurrentSends();
    return 0;
}
