#pragma once
#include "yc_packet.hpp"

#pragma pack(push, 1)

APACKET(
    chat,
    char message[256];
);

APACKET_VAR(
  chat_var,
  char, message[512];
);

#pragma pack(pop)