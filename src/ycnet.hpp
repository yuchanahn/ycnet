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

    template <size_t ThreadCount>
    void release_buf(nto_memory<raw_packet_t, ThreadCount>& mem, raw_packet_t* buf) { mem.read_end(buf); }

    template <size_t ThreadCount>
    std::vector<raw_packet_t*> get_pkt_range(nto_memory<raw_packet_t, ThreadCount>& mem) {
        return mem.get_read_ranges();
    }

    inline packet_ack_type make_ack_pkt(const int seq) {
        yc_pack::udp::convert_ack seq_packet;
        seq_packet.is_ack_packet = true;
        seq_packet.use_ack = true;
        seq_packet.counter = seq;
        return seq_packet.to_ack();
    }
    
    // ReSharper disable once IdentifierTypo
    class rudp_handler {
        // ReSharper disable once IdentifierTypo
        yc_rudp::rudp_buffer_t rudp_buffs_;
        std::vector<int> resend_buff_;
        std::vector<yc_rudp::receive_packet_raw> no_ack_buffs_;
        // ReSharper disable once IdentifierTypo
        int rtts_;
        int seq_;
        int send_seq_;
        int end_;
        std::function<void(char*, size_t)> send_func_;
        std::function<void(std::string)> log_func_;
        char send_packet_build_buff_[1024] = {};
        
        void send_ack(const int seq, const int thread_num = 0) const {
            char pkt = make_ack_pkt(seq);
            send_func_(&pkt, 1);
        }

        int build_send_buffer(char* target_buf, const yc_pack::raw_packet& pkt_data) const {
            *reinterpret_cast<packet_size_type*>(target_buf) = pkt_data.size;
            *reinterpret_cast<packet_id_type*>(target_buf + sizeof packet_size_type) = pkt_data.id;
            std::copy_n(pkt_data.body, pkt_data.size - yc_pack::HEADER_SIZE, target_buf + yc_pack::HEADER_SIZE);
            return pkt_data.size;
        }
    public:
        // ReSharper disable once IdentifierTypo
        explicit rudp_handler(std::function<void(std::string)> log_func,
                              std::function<void(char*, size_t)> send_func)
        : rudp_buffs_(1)
        , rtts_(100)
        , seq_(0)
        , send_seq_(0)
        , end_(0)
        , send_func_(std::move(send_func))
        , log_func_(std::move(log_func)) { }

        // ReSharper disable once IdentifierTypo
        void on_recv(char* buf, const size_t len) {
            if(!yc_pack::pkt_vrfct(buf, len)) {
                log_func_("packet verification failed");
                return;
            }
            if (yc_rudp::is_ack_packet(buf)) {
                if (set_send_complete(rudp_buffs_.send_buffer, resend_buff_, rtts_, yc_rudp::get_seq(buf)) == -1) {
                    log_func_("set_send_complete() error");
                }
            }
            else {
                if (push_packet(rudp_buffs_.pkt_buffer, 0, 1, buf, len) == -1) {
                    log_func_(std::format("id : {} - push_packet failed!", "client_side"));
                }
            }
            auto [s,e] = get_read_range(rudp_buffs_.pkt_buffer, rudp_buffs_.receive_buffer, no_ack_buffs_, 1, end_);
            for (; s < e; ++s) {
                yc_rudp::make_seq(s);
                send_ack(yc_rudp::make_seq(s));
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

        // send packet
        void send(is_ack_packet auto& packet) {
            auto pkt = yc_pack::pack(packet);
            auto size = build_send_buffer(send_packet_build_buff_, pkt);
            const auto seq = yc_rudp::ready_to_send(rudp_buffs_.send_buffer, resend_buff_, send_packet_build_buff_, size, true, send_seq_);
            auto& send_buf = rudp_buffs_.send_buffer[seq];
            send_func_(send_buf.data, send_buf.len);
            send_seq_ = yc_rudp::get_next_seq(send_seq_);
        }

        // send packet without ack
        void send(is_no_ack_packet auto& packet) {
            auto pkt = yc_pack::pack(packet);
            auto size = build_send_buffer(send_packet_build_buff_, pkt);
            const auto seq = yc_rudp::ready_to_send(rudp_buffs_.send_buffer, send_packet_build_buff_, size, false, 0);
            auto& send_buf = rudp_buffs_.send_buffer[seq];
            send_func_(send_buf.data, send_buf.len);
        }
        
        //ack 패킷 수신에 실패 했을 경우 패킷을 재전송 합니다.
        void prc_resend() {
            auto r = get_resend_packets(rudp_buffs_.send_buffer, resend_buff_, rtts_, 1000);
            for(auto& i : r) {
                log_func_(std::format("resend to {}", i));
                auto& pkt = rudp_buffs_.send_buffer[i];
                send_func_(pkt.data, pkt.len);
            }
        }
    };

    template <size_t MaxClient, size_t IoThreadCount, int Port = 51234>
    // ReSharper disable once IdentifierTypo
    class rudp_server {
        nto_memory<raw_packet_t, IoThreadCount> packets_buffer_;
        core::rio_udp_server_controller server_controller_;
        std::vector<core::endpoint_t> endpoints_;
        // ReSharper disable once IdentifierTypo
        std::unordered_map<core::endpoint_t, size_t> ids_;
        std::vector<size_t> id_index_q_;
        // ReSharper disable once IdentifierTypo
        std::vector<yc_rudp::rudp_buffer_t> rudp_buffs_;
        std::vector<std::vector<int>> resend_index_buffs_;
        std::vector<std::vector<yc_rudp::receive_packet_raw>> no_ack_buffs_;
        // ReSharper disable once IdentifierTypo
        std::vector<int> rtts_;
        std::vector<int> seq_;
        std::vector<int> end_;
        std::vector<int> send_seq_;
        std::function<void(std::string)> log_func_;
        std::function<void(core::endpoint_t, size_t)> time_out_;
        std::function<void(core::endpoint_t, size_t)> connect_;
        int time_out_rtt_;
        
        yc::err_opt_t<size_t> get_user_id(const core::endpoint_t& ep) {
            if (!ids_.contains(ep)) return "user not found";
            return yc::err_opt_t(ids_[ep]);
        }
        void send_ack(const size_t& id, const int seq, const int thread_num = 0) const {
            const char pkt = make_ack_pkt(seq);
            send_udp(&pkt, 1, endpoints_[id], thread_num);
        }
        void prc_pkt(const size_t& id) {
            auto [s,e] = get_read_range(rudp_buffs_[id].pkt_buffer, rudp_buffs_[id].receive_buffer, no_ack_buffs_[id],
                                        1, end_[id]);
            for (; s < e; ++s) {
                send_ack(id, yc_rudp::make_seq(s));
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
        int build_send_buffer(char* target_buf, const yc_pack::raw_packet& pkt_data) const {
            *reinterpret_cast<packet_size_type*>(target_buf) = pkt_data.size;
            *reinterpret_cast<packet_id_type*>(target_buf + sizeof packet_size_type) = pkt_data.id;
            std::copy_n(pkt_data.body, pkt_data.size - yc_pack::HEADER_SIZE, target_buf + yc_pack::HEADER_SIZE);
            return pkt_data.size;
        }
        bool reset_connection(const size_t& id) {
            rtts_[id] = 100;
            seq_[id] = 0;
            end_[id] = 0;
            send_seq_[id] = 0;
            id_index_q_.push_back(id);
            resend_index_buffs_[id].clear();
            rudp_buffs_[id].clear();
            return true;
        }

    public:
        // ReSharper disable once IdentifierTypo
        explicit rudp_server(std::function<void(std::string)> log_func): endpoints_(MaxClient)
                                                                         , resend_index_buffs_(MaxClient)
                                                                         , no_ack_buffs_(MaxClient)
                                                                         , rtts_(MaxClient)
                                                                         , seq_(MaxClient)
                                                                         , end_(MaxClient)
                                                                         , send_seq_(MaxClient)
                                                                         , log_func_(std::move(log_func))
                                                                         , time_out_rtt_(1000) {
            for (int i = MaxClient - 1; i >= 0; --i) id_index_q_.push_back(i);
            rudp_buffs_.reserve(MaxClient);
            for (size_t i = 0; i < MaxClient; ++i) resend_index_buffs_[i].reserve(32);
            for (size_t i = 0; i < MaxClient; ++i) { rudp_buffs_.emplace_back(1); }
            log_func_("Server Started");
        }

        void set_time_out_rtt(const int rtt) { time_out_rtt_ = rtt; }
        void set_time_out_func(std::function<void(core::endpoint_t, size_t)> func) { time_out_ = std::move(func); }
        void set_connect_func(std::function<void(core::endpoint_t, size_t)> func) { connect_ = std::move(func); }
        
        /**
         * \brief packet을 읽어드립니다.
         * 이 함수는 Thread Safe 하지 않습니다.
         */
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
                    connect_(i->ep, id);
                }
                else { id = *opt_id; }
                if (yc_rudp::is_ack_packet(i->buf)) {
                    if (set_send_complete(rudp_buffs_[id].send_buffer, resend_index_buffs_[id], rtts_[id], yc_rudp::get_seq(i->buf)) == -1) {
                        log_func_("set_send_complete() error");
                    }
                }
                else {
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

        void send(is_ack_packet auto& packet, const size_t& id, const int thread_num = 0) {
            auto pkt = yc_pack::pack(packet);
            char buf[1024] = {};
            auto size = build_send_buffer(buf, pkt);
            const auto seq = yc_rudp::ready_to_send(rudp_buffs_[id].send_buffer, resend_index_buffs_[id], buf, size, true, send_seq_[id]);
            auto& send_buf = rudp_buffs_[id].send_buffer[seq];
            core::send_udp(send_buf.data, send_buf.len, endpoints_[id], thread_num);
            send_seq_[id] = yc_rudp::get_next_seq(send_seq_[id]);
        }

        void send(is_no_ack_packet auto& packet, const size_t& id, const int thread_num = 0) {
            auto pkt = yc_pack::pack(packet);
            char buf[1024] = {};
            auto size = build_send_buffer(buf, pkt);
            const auto seq = yc_rudp::ready_to_send(rudp_buffs_[id].send_buffer, resend_index_buffs_[id], buf, size, false, 0);
            auto& send_buf = rudp_buffs_[id].send_buffer[seq];
            core::send_udp(send_buf.data, send_buf.len, endpoints_[id], thread_num);
        }

        void prc_resend(const size_t& id, const int thread_num = 0) {
            const auto r = get_resend_packets(rudp_buffs_[id].send_buffer, resend_index_buffs_[id], rtts_[id], 1000);
            if(r.empty() && rtts_[id] == -1) {
                time_out_(endpoints_[id], id);
                reset_connection(id);
                return;
            }
            for(auto& i : r) {
                log_func_(std::format("resend to {}", id));
                const auto& pkt = rudp_buffs_[id].send_buffer[i];
                send_udp(pkt.data, pkt.len, endpoints_[id], thread_num);
            }
        }
        
        void io_service_run() {
            // ReSharper disable once CppTooWideScope
            const auto r = core::udp_server_run(
                IoThreadCount,
                Port,
                [this](const char* data, const int size, const core::endpoint_t& endpoint, const int thread_id) {
                    log_func_(std::format("recv from {} - {}", endpoint.to_string(), size));
                    if(!yc_pack::pkt_vrfct(const_cast<char*>(data), size)) {
                        log_func_("packet verification failed");
                        return;
                    }
                    raw_packet_t* packet = nullptr;
                    while (!packets_buffer_.try_push(thread_id, packet)) {
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
        yc::err_opt_t<size_t> user_id(const core::endpoint_t& endpoint) {
            return ids_.contains(endpoint)
                       ? yc::err_opt_t(ids_[endpoint])
                       : "해당 endpoint는 존재하지 않습니다.";
        }

        [[nodiscard]] yc::err_opt_t<int> kill() const { return server_controller_.kill(); }
    };
}
