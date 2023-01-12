#pragma once
#include "ycnet_core.hpp"
#include "yc_packet.hpp"
#include "yc_rudp.hpp"
#include "nto_memory.hpp"
#include <ranges>
#include <utility>

// ReSharper disable once IdentifierTypo
namespace ycnet
{
    template <typename Packet>
    concept is_ack_packet = requires { Packet::__packet__id; Packet::is_apacket; };
    template <typename Packet>
    concept is_no_ack_packet = requires { Packet::__packet__id; } && !is_ack_packet<Packet>;

    struct raw_packet_t {
        char buf[PACKET_SIZE_MAX];
        size_t len;
        core::endpoint_t ep;
        YC_USE int thread_id;
    };


    // 하지만 해제가 느려진다면, io처리를 할 때 block 될 수도 있다. 
    // 다만 특정 io thread 에 들어오는 packet 이 많다면 send 속도가 느려질 수 있다.
    // 나중에 core 부분도 인덱스 버퍼를 사용하는 방식으로 최적화 해야 한다.


    template <size_t ThreadCount>
    void release_buf(nto_memory<ycnet::raw_packet_t, ThreadCount>& mem, ycnet::raw_packet_t* buf) { mem.read_end(buf); }

    template <size_t ThreadCount>
    std::vector<ycnet::raw_packet_t*> get_pkt_range(nto_memory<ycnet::raw_packet_t, ThreadCount>& mem) {
        return mem.get_read_ranges();
    }

    constexpr int max_client = 100;
    constexpr int io_thread = 4;

    class rudp_handler {
        yc_rudp::rudp_buffer_t rudp_buffs_;
        std::vector<yc_rudp::receive_packet_raw> no_ack_buffs_;
        int rtts_;
        int seq_;
        int end_;
        std::function<void(std::string)> log_func_;

    public:
        explicit rudp_handler(std::function<void(std::string)> log_func) : rudp_buffs_(1)
                                                                           , rtts_(100), seq_(0), end_(0),
                                                                           log_func_(std::move(log_func)) { }

        void recv(char* buf, size_t len) {
            if (yc_rudp::is_ack_packet(buf)) {
                if (set_send_complete(rudp_buffs_.send_buffer, rtts_, yc_rudp::get_seq(buf)) == -1) {
                    log_func_("set_send_complete() error");
                }
            }
            else {
                // 최근에 처리한 seq 번호보다 뒤에 있는 패킷이 오면 컷 하자.
                if (push_packet(rudp_buffs_.pkt_buffer, 0, 1, buf, len) == -1) {
                    log_func_(std::format("id : {} - push_packet failed!", "client_side"));
                }
            }
            auto [s,e] = get_read_range(rudp_buffs_.pkt_buffer, rudp_buffs_.receive_buffer, no_ack_buffs_, 1, end_);
            for (; s < e; ++s) {
                yc_rudp::make_seq(s);
                auto [size, pid, body] = yc_pack::unpack(rudp_buffs_.receive_buffer[yc_rudp::make_seq(s)].data);
                packet_events[pid](body, size, 0);
            }
            end_ = yc_rudp::make_seq(s);

            for (auto& [data, _] : no_ack_buffs_) {
                auto [size, pid, body] = yc_pack::unpack(data);
                packet_events[pid](body, size, 0);
            }
            no_ack_buffs_.clear();
        }
    };

    class server {
        nto_memory<ycnet::raw_packet_t, io_thread> packets_buffer_;
        ycnet::core::rio_udp_server_controller server_controller_;
        std::vector<ycnet::core::endpoint_t> endpoints_;
        // ReSharper disable once IdentifierTypo
        std::unordered_map<ycnet::core::endpoint_t, size_t> ids_;
        std::vector<size_t> id_index_q_;
        // ReSharper disable once IdentifierTypo
        std::vector<yc_rudp::rudp_buffer_t> rudp_buffs_;
        std::vector<std::vector<yc_rudp::receive_packet_raw>> no_ack_buffs_;
        // ReSharper disable once IdentifierTypo
        std::vector<int> rtts_;
        std::vector<int> seq_;
        std::vector<int> end_;

        // 극한의 퍼포먼스를 위해서는 Main Thread 에서 할당 해제된 패킷을 다시 반환하는 작업이 필요하다.
        // worker thread 에서 패킷 처리를 실행한다면 main thread 에게 사용한 패킷을 반납하는 절차가 필요하다.

        std::function<void(std::string)> log_func_;

        yc::err_opt_t<size_t> get_user_id(const ycnet::core::endpoint_t& ep) {
            if (!ids_.contains(ep)) return "user not found";
            return yc::err_opt_t(ids_[ep]);
        }

        void prc_pkt(const size_t& id) {
            auto [s,e] = get_read_range(rudp_buffs_[id].pkt_buffer, rudp_buffs_[id].receive_buffer, no_ack_buffs_[id],
                                        1, end_[id]);
            for (; s < e; ++s) {
                yc_rudp::make_seq(s);
                auto [size, pid, body] = yc_pack::unpack(rudp_buffs_[id].receive_buffer[yc_rudp::make_seq(s)].data);
                packet_events[pid](body, size, id);
            }
            end_[id] = yc_rudp::make_seq(s);

            for (auto& [data, len] : no_ack_buffs_[id]) {
                auto [size, pid, body] = yc_pack::unpack(data);
                packet_events[pid](body, size, id);
            }
            no_ack_buffs_[id].clear();
        }

    public:
        explicit server(std::function<void(std::string)> log_func): endpoints_(max_client)
                                                                    , no_ack_buffs_(max_client)
                                                                    , rtts_(max_client)
                                                                    , seq_(max_client)
                                                                    , end_(max_client)
                                                                    , log_func_(std::move(log_func)) {
            for (int i = max_client - 1; i >= 0; --i) id_index_q_.push_back(i);
            rudp_buffs_.reserve(max_client);
            for (int i = 0; i < max_client; ++i) { rudp_buffs_.emplace_back(1); }
            log_func_("Server Started");
        }

        // ReSharper disable once IdentifierTypo
        void intr_pkt() {
            std::unordered_map<size_t, int> packets;

            for (auto* i : get_pkt_range(packets_buffer_)) {
                auto opt_id = get_user_id(i->ep);
                size_t id = 0;
                if (!opt_id.has_value()) {
                    const auto new_id = id_index_q_.back();
                    id_index_q_.pop_back();
                    endpoints_[new_id] = i->ep;
                    rtts_[new_id] = 100;
                    ids_[i->ep] = new_id;
                    seq_[new_id] = 0;
                    end_[new_id] = 0;
                    id = new_id;
                }
                else { id = *opt_id; }

                if (yc_rudp::is_ack_packet(i->buf)) {
                    if (set_send_complete(rudp_buffs_[id].send_buffer, rtts_[id], yc_rudp::get_seq(i->buf)) == -1) {
                        log_func_("set_send_complete() error");
                    }
                }
                else {
                    // 최근에 처리한 seq 번호보다 뒤에 있는 패킷이 오면 컷 하자.
                    if (push_packet(rudp_buffs_[id].pkt_buffer, 0, 1, i->buf, i->len) == -1) {
                        log_func_(std::format("id : {} - push_packet failed!", id));
                    }
                    if (!packets.contains(id)) packets[id] = 0;
                    ++packets[id];
                    if (packets[id] > ACK_COUNTER_MAX / 2) {
                        packets[id] = 0;
                        prc_pkt(id);
                    }
                }
                release_buf(packets_buffer_, i);
            }
            std::ranges::for_each(packets | std::views::keys, [this](const auto& i) { prc_pkt(i); });
        }

        void run() {
            // ReSharper disable once CppTooWideScope
            const auto r = ycnet::core::udp_server_run(
                4,
                12345,
                [this](const char* data, const int size, const ycnet::core::endpoint_t& endpoint, const int thread_id) {
                    ycnet::raw_packet_t* packet = nullptr;
                    while (packets_buffer_.try_push(thread_id, packet)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    std::copy_n(data, size, packet->buf);
                    packet->len = size;
                    packet->ep = endpoint;
                    packets_buffer_.make_readable(packet);
                }, printf);

            if (r) server_controller_ = *r;
            else log_func_(r.err);
        }

        /**
         * \brief Client 고유 ID를 가져온다. thread safe 하지 않습니다.
         * \param endpoint 가져올 클라이언트의 endpoint
         * \return endpoint 에 해당하는 ID
         */
        yc::err_opt_t<size_t> user_id(const ycnet::core::endpoint_t& endpoint) {
            return ids_.contains(endpoint)
                       ? yc::err_opt_t(ids_[endpoint])
                       : "해당 endpoint는 존재하지 않습니다.";
        }

        [[nodiscard]] yc::err_opt_t<int> kill() const { return server_controller_.kill(); }
    };
}
