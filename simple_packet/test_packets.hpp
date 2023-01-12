#pragma once
#include "yc_packet.hpp"

#pragma pack(push, 1)

APACKET(
    chat,
    char message[256];
);

//PACKET(player_movement_start, player_movement_start_t move_data;)
//PACKET_VAR(players_location, player_movement_t, player_movements[50];);
//PACKET_VAR(players_spawn, player_spawn_t, spawn_data[10];);

#pragma pack(pop)