#include "ycnet_core.hpp"
#include "yc_packet.hpp"
#include "yc_rudp.hpp"
#include "ycnet.hpp"

// *** packet
// PACKET이 정의된 헤더 파일을 로드 합니다.
#include "test_packets.hpp"
// --------------------------------------

#include <unordered_map>


int main() {
    static ycnet::server server1([](std::string str) { printf("%s\n", str.c_str()); });
    static std::unordered_map<size_t, std::string> clients;
    apacket_chat::bind([](apacket_chat chat_data, const size_t client_id) {
        clients[client_id] = !clients.contains(client_id) ? chat_data.message : clients[client_id];

        const std::string msg = std::format("{} : {}\n", clients[client_id], chat_data.message);
        apacket_chat server_msg{};
        std::copy_n(msg.data(), msg.length(), server_msg.message);
        for(const auto& id : clients | std::views::keys) {
            if(id == client_id) continue;
            server1.send(server_msg, id);
        }
    });

    server1.run();

    while (true) {
        server1.intr_pkt();
        for(auto id : clients | std::views::keys) server1.prc_resend(id, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    return 0;
}
