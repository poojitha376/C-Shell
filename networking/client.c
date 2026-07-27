#include "sham.h"

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <server_ip> <server_port> <input_file> <output_file_name> [loss_rate]\n", argv[0]);
        printf("Or: %s <server_ip> <server_port> --chat [loss_rate]\n", argv[0]);
        return 1;
    }
    
    char *server_ip = argv[1];
    int server_port = atoi(argv[2]);
    float loss_rate = 0.0;
    
    if (argc > 5 && strcmp(argv[3], "--chat") != 0) {
        loss_rate = atof(argv[5]);
    } else if (argc > 4 && strcmp(argv[3], "--chat") == 0) {
        loss_rate = atof(argv[4]);
    }
    
    int sockfd = create_socket(0);
    struct sockaddr_in server_addr;
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    
    if (strcmp(argv[3], "--chat") == 0) {
        /* Chat mode must also establish the connection first - the original code
         * skipped straight to chat_mode() with no handshake at all. */
        three_way_handshake(sockfd, &server_addr, loss_rate);
        chat_mode(sockfd, &server_addr, loss_rate);
    } else {
        char *input_file = argv[3];
        char *output_file = argv[4];
        (void)output_file; /* sham_header has no filename field, so the server always
                             * names the received file itself (received_file_<ts>) -
                             * the protocol as specified has no channel to pass this
                             * through. Kept here only so the CLI matches the spec. */

        three_way_handshake(sockfd, &server_addr, loss_rate);
        send_file(sockfd, &server_addr, input_file, loss_rate);
        four_way_handshake(sockfd, &server_addr, loss_rate);
    }
    
    close(sockfd);
    return 0;
}