#include "sham.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <port> [--chat] [loss_rate]\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    float loss_rate = 0.0;
    int chat_mode_flag = 0;

    if (argc > 2 && strcmp(argv[2], "--chat") == 0) {
        chat_mode_flag = 1;
        if (argc > 3) {
            loss_rate = atof(argv[3]);
        }
    } else if (argc > 2) {
        loss_rate = atof(argv[2]);
    }

    int sockfd = create_socket(port);
    struct sockaddr_in client_addr;

    while (1) {
        /* Peek (don't consume) so three_way_handshake's own recv can read this exact
         * SYN for real - if we consumed it here instead, the handshake's role-detection
         * poll would find nothing waiting and misidentify itself as the initiator. */
        char peekbuf[16];
        struct sockaddr_in cand_addr;
        socklen_t cand_len = sizeof(cand_addr);
        ssize_t n = recvfrom(sockfd, peekbuf, sizeof(peekbuf), MSG_PEEK,
                              (struct sockaddr *)&cand_addr, &cand_len);
        if (n < 10) {
            if (n >= 0) {
                /* drain a too-short/garbage datagram so it doesn't spin forever */
                recvfrom(sockfd, peekbuf, sizeof(peekbuf), 0, NULL, NULL);
            }
            continue;
        }

        /* flags field is wire bytes [8:10), big-endian - only treat an actual SYN as a
         * new-connection attempt. Otherwise this is a stray leftover packet from an
         * already-closed session (e.g. a duplicate retransmitted FIN/ACK); drain and
         * ignore it instead of misidentifying it as a new client, which previously made
         * the server spuriously spend ~5s "initiating" a bogus connection and creating
         * an empty received_file_* artifact. */
        uint16_t peek_flags = (uint16_t)(((unsigned char)peekbuf[8] << 8) | (unsigned char)peekbuf[9]);
        if (!(peek_flags & FLAG_SYN)) {
            recvfrom(sockfd, peekbuf, sizeof(peekbuf), 0, NULL, NULL);
            continue;
        }

        client_addr = cand_addr;
        three_way_handshake(sockfd, &client_addr, loss_rate);

        if (chat_mode_flag) {
            /* chat_mode() handles its own teardown internally (on /quit or a
             * peer-initiated FIN), so no separate four_way_handshake() call here. */
            chat_mode(sockfd, &client_addr, loss_rate);
        } else {
            char filename[256];
            snprintf(filename, sizeof(filename), "received_file_%d", (int)time(NULL));
            receive_file(sockfd, &client_addr, filename, loss_rate);

            char md5_sum[33];
            calculate_md5(filename, md5_sum);
            printf("MD5: %s\n", md5_sum);
            fflush(stdout);

            four_way_handshake(sockfd, &client_addr, loss_rate);
        }
    }

    close(sockfd);
    return 0;
}
