#pragma once
#include "ycnet_core.hpp"
#include "ycnet.hpp"

// *** packet
// PACKET이 정의된 헤더 파일을 로드 합니다.
#include "test_packets.hpp"
// --------------------------------------

inline int clnt() {
# pragma region setup
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == SOCKET_ERROR) {
        WSACleanup();
        return -1;
    }
    const auto sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (-1 == sock) {
        printf("socket 생성 실패");
        exit(1);
    }

    sockaddr_in server_addr = {};
    server_addr.sin_family = PF_INET;
    printf("input server addr [IP:PORT] : ");
    char ip[30] = "127.0.0.1";
    u_short port = 12345;

    std::cin >> ip;

    std::string endpoint = ip;
    auto pos = endpoint.find(':');
    if (pos != std::string::npos) {
        port = std::stoi(endpoint.substr(pos + 1));
        endpoint = endpoint.substr(0, pos);
    }
    server_addr.sin_addr.s_addr = inet_addr(endpoint.c_str());
    server_addr.sin_port = htons(port);
#pragma  endregion
    
    auto send_to = [server_addr, sock](const char* buf, const size_t len) {
        sendto(sock, buf, len, 0, reinterpret_cast<const sockaddr*>(&server_addr), sizeof server_addr);
    };

    ycnet::rudp_handler handler(
        [](const std::string& msg) { printf("%s\n", msg.c_str()); },
        send_to);

    bool send_once = false;
    auto read = std::thread([&] {
        while (true) {
            if (!send_once) continue;
            char buf[1024] = {};
            sockaddr_in recv_addr = {};
            int addr_len = sizeof recv_addr;
            const int len = recvfrom(sock, buf, 1024, 0, reinterpret_cast<sockaddr*>(&recv_addr), &addr_len);
            if (len == -1) {
                printf("recvfrom error code : %d\n", WSAGetLastError());
                getchar();
                exit(1);
            }
            handler.on_recv(buf, len);
        }
    });

    //IP : 127.0.0.1:7777

    apacket_var_chat_var::bind([](const apacket_var_chat_var chat, auto) {
        std::cout << std::format("{} : {}", chat.session_id, chat.message).c_str() << std::endl;
    });
    
    std::mutex mtx;
    
    auto input_thread = std::thread([&] {
        while (true) {
            apacket_var_chat_var packet{};
            std::cin >> packet.message;
            const auto len = std::strlen(packet.message) + 1;
            if(len >= 512) {
                std::cout << "too long message" << std::endl;
                continue;
            }
            packet.size = len;
            mtx.lock();
            handler.send(packet);
            mtx.unlock();
            send_once = true;
        }
    });
    
    while (true) {
        if(!send_once) continue;
        mtx.lock();
        handler.prc_resend();
        mtx.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
