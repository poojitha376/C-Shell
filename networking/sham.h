#ifndef SHAM_H
#define SHAM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <openssl/md5.h>

#define MAX_PACKET_SIZE 1500
#define DATA_SIZE 1024
#define WINDOW_SIZE 10
#define TIMEOUT_MS 500

struct sham_header {
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t flags;
    uint16_t window_size;
};

#define FLAG_SYN 0x1
#define FLAG_ACK 0x2
#define FLAG_FIN 0x4

typedef struct {
    struct sham_header header;
    char data[DATA_SIZE];
    size_t data_len;
} Packet;

// Function declarations
int create_socket(int port);
Packet create_packet(uint32_t seq, uint32_t ack, uint16_t flags, uint16_t win);
int send_packet(int sockfd, struct sockaddr_in *addr, Packet *pkt, float loss_rate);
int recv_packet(int sockfd, struct sockaddr_in *addr, Packet *pkt, float loss_rate);
void three_way_handshake(int sockfd, struct sockaddr_in *addr, float loss_rate);
void four_way_handshake(int sockfd, struct sockaddr_in *addr, float loss_rate);
void send_file(int sockfd, struct sockaddr_in *addr, const char *filename, float loss_rate);
void receive_file(int sockfd, struct sockaddr_in *addr, const char *filename, float loss_rate);
void chat_mode(int sockfd, struct sockaddr_in *addr, float loss_rate);
void calculate_md5(const char *filename, char *md5_sum);

#endif