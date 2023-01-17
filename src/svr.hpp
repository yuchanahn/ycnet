#pragma once
#include "ycnet_core.hpp"
#include "ycnet.hpp"

// *** packet
// PACKET이 정의된 헤더 파일을 로드 합니다.
#include "test_packets.hpp"
// --------------------------------------
#include <unordered_map>

struct user_t {
    std::string name{};
    size_t id{};
    bool is_login{};
};

inline int srv() {
    static ycnet::rudp_server<1000, 4, 7777> server([](const std::string& str) { printf("%s\n", str.c_str()); });
    static std::unordered_map<size_t, user_t> clients;

    apacket_var_chat_var::bind([](apacket_var_chat_var chat_data, const size_t client_id) {
        apacket_var_chat_var server_msg{};
        server_msg.session_id = client_id;
        server_msg.size = chat_data.size;
        std::copy_n(chat_data.message, chat_data.size, server_msg.message);
        for (const auto& id : clients | std::views::keys) {
            if (!clients[id].is_login) continue;
            if (id == client_id) continue;
            server.send(server_msg, id);
        }
    });

    server.set_connect_func([](const ycnet::core::endpoint_t ep, const size_t client_id) {
        printf("client connected : %s, id : %lld\n", ep.to_string().c_str(), client_id);
        clients[client_id] = user_t{ep.to_string(), client_id, true};
    });
    server.set_time_out_func([](const ycnet::core::endpoint_t ep, const size_t client_id) {
        printf("client time out : %lld [IP : %s]\n", client_id, ep.to_string().c_str());
        clients[client_id].is_login = false;
    });

    server.io_service_run();
    while (true) {
        server.intr_pkt();

        for (auto [id, user] : clients) {
            if (!user.is_login) continue;
            server.prc_resend(id, 0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}