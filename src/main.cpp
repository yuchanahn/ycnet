#include "ycnet_core.hpp"
#include "yc_rudp.hpp"

#include "clnt.hpp"
#include "svr.hpp"

constexpr int number_server = 2;
constexpr int number_client = 1;

int main() {
    int clnt_or_svr = 0;
    std::cout << "1. client, 2. server : ";
    std::cin >> clnt_or_svr;

    if (clnt_or_svr == number_server) srv();
    if (clnt_or_svr == number_client) clnt();
    printf("wrong number\n");

    return 0;
}
