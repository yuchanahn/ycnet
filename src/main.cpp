#include "ycnet_core.hpp"
#include "yc_packet.hpp"
#include "yc_rudp.hpp"
#include "ycnet.hpp"

// *** packet
// PACKET이 정의된 헤더 파일을 로드 합니다.
#include "test_packets.hpp"
// --------------------------------------

int main() {
    apacket_chat::bind([](apacket_chat chat_data, const size_t client_id) {
        const auto str = std::format("client {} : {}\n", client_id, chat_data.message);
        printf("%s", str.c_str());
    });

    ycnet::server server1([](std::string str) { printf("%s\n", str.c_str()); });
    server1.run();
    while (true) {
        server1.intr_pkt();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}
