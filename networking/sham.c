#include "sham.h"
#include <stdarg.h>
#include <sys/select.h>

#ifndef SHAM_ROLE
#define SHAM_ROLE "client"
#endif

/* ---------------- Logging ---------------- */

static FILE *g_log_fp = NULL;
static int g_log_inited = 0;

static void sham_log_init(void) {
    if (g_log_inited) return;
    g_log_inited = 1;
    const char *env = getenv("RUDP_LOG");
    if (env && strcmp(env, "1") == 0) {
        char fname[64];
        snprintf(fname, sizeof(fname), "%s_log.txt", SHAM_ROLE);
        g_log_fp = fopen(fname, "a");
    }
}

static void sham_log(const char *fmt, ...) {
    if (!g_log_fp) return;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t curtime = tv.tv_sec;
    char time_buffer[32];
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", localtime(&curtime));
    fprintf(g_log_fp, "[%s.%06ld] [LOG] ", time_buffer, (long)tv.tv_usec);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log_fp, fmt, ap);
    va_end(ap);
    fprintf(g_log_fp, "\n");
    fflush(g_log_fp);
}

/* ---------------- Wire format ---------------- */

struct wire_header {
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t flags;
    uint16_t window_size;
} __attribute__((packed));

static void serialize_packet(const Packet *pkt, char *buf, size_t *out_len) {
    struct wire_header wh;
    wh.seq_num = htonl(pkt->header.seq_num);
    wh.ack_num = htonl(pkt->header.ack_num);
    wh.flags = htons(pkt->header.flags);
    wh.window_size = htons(pkt->header.window_size);
    memcpy(buf, &wh, sizeof(wh));
    if (pkt->data_len > 0) {
        memcpy(buf + sizeof(wh), pkt->data, pkt->data_len);
    }
    *out_len = sizeof(wh) + pkt->data_len;
}

static void deserialize_packet(const char *buf, size_t len, Packet *pkt) {
    struct wire_header wh;
    memcpy(&wh, buf, sizeof(wh));
    pkt->header.seq_num = ntohl(wh.seq_num);
    pkt->header.ack_num = ntohl(wh.ack_num);
    pkt->header.flags = ntohs(wh.flags);
    pkt->header.window_size = ntohs(wh.window_size);
    size_t payload_len = (len > sizeof(wh)) ? (len - sizeof(wh)) : 0;
    if (payload_len > DATA_SIZE) {
        payload_len = DATA_SIZE;
    }
    pkt->data_len = payload_len;
    if (payload_len > 0) {
        memcpy(pkt->data, buf + sizeof(wh), payload_len);
    }
}

/* ---------------- Core socket / packet primitives ---------------- */

int create_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 50000; /* 50ms poll granularity for RTO/select-driven loops */
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sham_log_init();
    srand48((long)time(NULL) ^ (long)getpid());

    return sockfd;
}

Packet create_packet(uint32_t seq, uint32_t ack, uint16_t flags, uint16_t win) {
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.seq_num = seq;
    pkt.header.ack_num = ack;
    pkt.header.flags = flags;
    pkt.header.window_size = win;
    pkt.data_len = 0;
    return pkt;
}

int send_packet(int sockfd, struct sockaddr_in *addr, Packet *pkt, float loss_rate) {
    (void)loss_rate; /* loss is simulated on the receive side only (see recv_packet) */
    char buf[sizeof(struct wire_header) + DATA_SIZE];
    size_t len;
    serialize_packet(pkt, buf, &len);
    ssize_t n = sendto(sockfd, buf, len, 0, (struct sockaddr *)addr, sizeof(*addr));
    return (n < 0) ? -1 : (int)n;
}

/* Drops incoming DATA packets (never control SYN/FIN packets, or handshakes would
 * flake unpredictably) with probability loss_rate. Implemented as a retry loop so a
 * caller's single recv_packet() call transparently "sees" the next surviving packet,
 * exactly like real packet loss looks from the receiver's point of view. */
int recv_packet(int sockfd, struct sockaddr_in *addr, Packet *pkt, float loss_rate) {
    char buf[sizeof(struct wire_header) + DATA_SIZE];
    socklen_t addrlen = sizeof(*addr);

    for (;;) {
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0, (struct sockaddr *)addr, &addrlen);
        if (n < 0) {
            return -1; /* EAGAIN/EWOULDBLOCK on socket timeout, or a real error */
        }
        deserialize_packet(buf, (size_t)n, pkt);

        int is_control = (pkt->header.flags & (FLAG_SYN | FLAG_FIN)) != 0;
        if (!is_control && pkt->data_len > 0 && loss_rate > 0.0f) {
            if (drand48() < (double)loss_rate) {
                sham_log("DROP DATA SEQ=%u", pkt->header.seq_num);
                continue;
            }
        }
        return (pkt->data_len > 0) ? (int)pkt->data_len : 1;
    }
}

/* Polls recv_packet (itself non-blocking past the socket's 50ms timeout) until either
 * a packet arrives or timeout_ms of wall-clock time has elapsed. */
static int try_recv_with_timeout(int sockfd, struct sockaddr_in *addr, Packet *pkt,
                                  float loss_rate, int timeout_ms) {
    struct timeval start, now;
    gettimeofday(&start, NULL);
    for (;;) {
        int r = recv_packet(sockfd, addr, pkt, loss_rate);
        if (r > 0) return r;
        gettimeofday(&now, NULL);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                          (now.tv_usec - start.tv_usec) / 1000;
        if (elapsed_ms >= timeout_ms) return -1;
    }
}

/* ---------------- Connection management ---------------- */

/* Frozen signature is used identically by both client.c (always the initiator - it
 * calls this right after opening its socket, before any datagram has been exchanged)
 * and server.c (always the responder - server.c only calls this after MSG_PEEK has
 * confirmed a SYN is already sitting in the socket buffer for a given client_addr).
 * We disambiguate roles by a short poll: if a SYN is already waiting, act as the
 * responder; if nothing arrives within that short window, act as the initiator. This
 * matches both call sites' real timing without needing a role parameter. */
void three_way_handshake(int sockfd, struct sockaddr_in *addr, float loss_rate) {
    Packet pkt;
    int got = try_recv_with_timeout(sockfd, addr, &pkt, loss_rate, 150);

    if (got > 0 && (pkt.header.flags & FLAG_SYN) && !(pkt.header.flags & FLAG_ACK)) {
        /* ---- Responder (server) ---- */
        uint32_t client_seq = pkt.header.seq_num;
        sham_log("RCV SYN SEQ=%u", client_seq);
        uint32_t my_seq = 5000 + (uint32_t)(getpid() % 1000);

        for (int attempt = 0; attempt < 10; attempt++) {
            Packet synack = create_packet(my_seq, client_seq + 1, FLAG_SYN | FLAG_ACK,
                                           WINDOW_SIZE * DATA_SIZE);
            send_packet(sockfd, addr, &synack, loss_rate);
            sham_log("SND SYN-ACK SEQ=%u ACK=%u", my_seq, client_seq + 1);

            Packet ackpkt;
            int r = try_recv_with_timeout(sockfd, addr, &ackpkt, loss_rate, TIMEOUT_MS);
            if (r > 0 && (ackpkt.header.flags & FLAG_ACK) && !(ackpkt.header.flags & FLAG_SYN)) {
                sham_log("RCV ACK FOR SYN");
                return;
            }
            /* peer's original SYN or our SYN-ACK may have been lost; retry */
        }
        return;
    }

    /* ---- Initiator (client) ---- */
    uint32_t my_seq = 100;
    for (int attempt = 0; attempt < 10; attempt++) {
        Packet syn = create_packet(my_seq, 0, FLAG_SYN, WINDOW_SIZE * DATA_SIZE);
        send_packet(sockfd, addr, &syn, loss_rate);
        sham_log("SND SYN SEQ=%u", my_seq);

        Packet synack;
        int r = try_recv_with_timeout(sockfd, addr, &synack, loss_rate, TIMEOUT_MS);
        if (r > 0 && (synack.header.flags & FLAG_SYN) && (synack.header.flags & FLAG_ACK) &&
            synack.header.ack_num == my_seq + 1) {
            uint32_t server_seq = synack.header.seq_num;
            Packet finalack = create_packet(my_seq + 1, server_seq + 1, FLAG_ACK,
                                             WINDOW_SIZE * DATA_SIZE);
            send_packet(sockfd, addr, &finalack, loss_rate);
            return;
        }
    }
}

/* Symmetric / simultaneous-close-capable: both call sites use the same signature with
 * no initiator/responder distinction, so this sends our own FIN (retrying on timeout),
 * separately waits for the peer's FIN (which may already be in flight if the peer
 * called this around the same time), and ACKs it once seen. Handles the ordinary
 * sequential case and the simultaneous-close case with the same logic. */
void four_way_handshake(int sockfd, struct sockaddr_in *addr, float loss_rate) {
    uint32_t my_fin_seq = 9000 + (uint32_t)(getpid() % 1000);
    int got_my_ack = 0;
    int got_peer_fin = 0;

    for (int attempt = 0; attempt < 10 && (!got_my_ack || !got_peer_fin); attempt++) {
        if (!got_my_ack) {
            Packet fin = create_packet(my_fin_seq, 0, FLAG_FIN, 0);
            send_packet(sockfd, addr, &fin, loss_rate);
            sham_log("SND FIN SEQ=%u", my_fin_seq);
        }

        Packet in;
        int r = try_recv_with_timeout(sockfd, addr, &in, loss_rate, TIMEOUT_MS);
        if (r > 0) {
            if ((in.header.flags & FLAG_ACK) && !(in.header.flags & FLAG_FIN) &&
                in.header.ack_num == my_fin_seq + 1) {
                sham_log("RCV ACK=%u", in.header.ack_num);
                got_my_ack = 1;
            } else if (in.header.flags & FLAG_FIN) {
                sham_log("RCV FIN SEQ=%u", in.header.seq_num);
                Packet ack = create_packet(0, in.header.seq_num + 1, FLAG_ACK, 0);
                send_packet(sockfd, addr, &ack, loss_rate);
                sham_log("SND ACK FOR FIN");
                got_peer_fin = 1;
            }
        }
    }
}

/* ---------------- Data transfer ---------------- */

typedef struct {
    Packet pkt;
    struct timeval sent_time;
    int in_flight;
} SendSlot;

void send_file(int sockfd, struct sockaddr_in *addr, const char *filename, float loss_rate) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "send_file: cannot open %s\n", filename);
        return;
    }

    SendSlot window[WINDOW_SIZE];
    memset(window, 0, sizeof(window));

    uint32_t next_seq = 1; /* data phase re-sequences from 1, independent of handshake ISNs */
    uint32_t base_seq = 1;
    int eof_reached = 0;
    uint32_t peer_window = WINDOW_SIZE * DATA_SIZE;

    /* Fast retransmit: 3 duplicate ACKs for the same ack_num (receiver repeatedly
     * saying "still waiting for the same byte") is a strong enough signal that the
     * base-of-window packet is lost that we don't need to wait for its RTO timer -
     * this is what keeps a single loss from stalling/timing-out the whole window
     * behind it. */
    uint32_t last_ack_num = 0;
    int dup_ack_count = 0;

    /* Adaptive (Jacobson/Karn, RFC 6298) RTO estimation, replacing the fixed TIMEOUT_MS
     * default. srtt/rttvar are unseeded until the first real sample arrives - blending
     * a real localhost RTT (microseconds) against a 500ms static seed via the normal
     * EWMA would itself transiently spike rto_ms far above 500ms on convergence, so the
     * first sample bootstraps directly (SRTT=R, RTTVAR=R/2) per RFC 6298, and only
     * subsequent samples use the smoothing formula. */
    long srtt_us = 0;
    long rttvar_us = 0;
    int have_rtt_sample = 0;
    long rto_ms = TIMEOUT_MS;

    while (1) {
        for (int i = 0; i < WINDOW_SIZE && !eof_reached; i++) {
            if (window[i].in_flight) continue;
            if ((next_seq - base_seq) + DATA_SIZE > peer_window) break; /* flow control */

            char buf[DATA_SIZE];
            size_t rd = fread(buf, 1, DATA_SIZE, fp);
            if (rd == 0) {
                eof_reached = 1;
                break;
            }

            Packet pkt = create_packet(next_seq, 0, 0, WINDOW_SIZE * DATA_SIZE);
            memcpy(pkt.data, buf, rd);
            pkt.data_len = rd;

            window[i].pkt = pkt;
            gettimeofday(&window[i].sent_time, NULL);
            window[i].in_flight = 1;

            send_packet(sockfd, addr, &pkt, loss_rate);
            sham_log("SND DATA SEQ=%u LEN=%zu", next_seq, rd);
            next_seq += (uint32_t)rd;
        }

        int any_in_flight = 0;
        for (int i = 0; i < WINDOW_SIZE; i++) {
            if (window[i].in_flight) any_in_flight = 1;
        }
        if (eof_reached && !any_in_flight) break;

        Packet ackpkt;
        int r = recv_packet(sockfd, addr, &ackpkt, loss_rate);
        if (r > 0 && (ackpkt.header.flags & FLAG_ACK)) {
            sham_log("RCV ACK=%u", ackpkt.header.ack_num);
            peer_window = ackpkt.header.window_size;
            uint32_t ack_num = ackpkt.header.ack_num;

            struct timeval now;
            gettimeofday(&now, NULL);

            for (int i = 0; i < WINDOW_SIZE; i++) {
                if (!window[i].in_flight) continue;
                uint32_t covers = window[i].pkt.header.seq_num + (uint32_t)window[i].pkt.data_len;
                if (covers <= ack_num) {
                    long sample_us = (now.tv_sec - window[i].sent_time.tv_sec) * 1000000L +
                                      (now.tv_usec - window[i].sent_time.tv_usec);
                    if (sample_us > 0) {
                        if (!have_rtt_sample) {
                            srtt_us = sample_us;
                            rttvar_us = sample_us / 2;
                            have_rtt_sample = 1;
                        } else {
                            long err = sample_us - srtt_us;
                            srtt_us += err / 8;
                            rttvar_us += (labs(err) - rttvar_us) / 4;
                        }
                        rto_ms = (srtt_us + 4 * rttvar_us) / 1000;
                        if (rto_ms < 100) rto_ms = 100;
                        if (rto_ms > 2000) rto_ms = 2000;
                    }
                    window[i].in_flight = 0;
                }
            }

            if (ack_num > base_seq) {
                base_seq = ack_num;
                last_ack_num = ack_num;
                dup_ack_count = 0;
            } else if (ack_num == last_ack_num) {
                dup_ack_count++;
                if (dup_ack_count == 3) {
                    for (int i = 0; i < WINDOW_SIZE; i++) {
                        if (window[i].in_flight && window[i].pkt.header.seq_num == ack_num) {
                            sham_log("FAST RETX DATA SEQ=%u LEN=%zu", window[i].pkt.header.seq_num,
                                      window[i].pkt.data_len);
                            send_packet(sockfd, addr, &window[i].pkt, loss_rate);
                            gettimeofday(&window[i].sent_time, NULL);
                            break;
                        }
                    }
                    dup_ack_count = 0; /* avoid re-triggering on every subsequent dup */
                }
            } else {
                last_ack_num = ack_num;
                dup_ack_count = 0;
            }
        }

        struct timeval now;
        gettimeofday(&now, NULL);
        for (int i = 0; i < WINDOW_SIZE; i++) {
            if (!window[i].in_flight) continue;
            long elapsed_ms = (now.tv_sec - window[i].sent_time.tv_sec) * 1000 +
                               (now.tv_usec - window[i].sent_time.tv_usec) / 1000;
            if (elapsed_ms >= rto_ms) {
                sham_log("TIMEOUT SEQ=%u", window[i].pkt.header.seq_num);
                send_packet(sockfd, addr, &window[i].pkt, loss_rate);
                gettimeofday(&window[i].sent_time, NULL);
                sham_log("RETX DATA SEQ=%u LEN=%zu", window[i].pkt.header.seq_num,
                          window[i].pkt.data_len);
            }
        }
    }

    fclose(fp);
}

typedef struct {
    uint32_t seq_num;
    char data[DATA_SIZE];
    size_t data_len;
    int occupied;
} RecvSlot;

void receive_file(int sockfd, struct sockaddr_in *addr, const char *filename, float loss_rate) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "receive_file: cannot open %s for writing\n", filename);
        return;
    }

    RecvSlot buf_slots[WINDOW_SIZE];
    memset(buf_slots, 0, sizeof(buf_slots));

    uint32_t expected_seq = 1;
    int last_reported_window = -1;
    time_t last_activity = time(NULL);

    while (1) {
        Packet pkt;
        int r = recv_packet(sockfd, addr, &pkt, loss_rate);
        if (r <= 0) {
            if (time(NULL) - last_activity > 15) break; /* safety net if FIN itself is lost repeatedly */
            continue;
        }
        last_activity = time(NULL);

        if (pkt.header.flags & FLAG_FIN) {
            /* Sender is done; let the caller's four_way_handshake() do the real teardown. */
            break;
        }
        if (pkt.data_len == 0) {
            continue;
        }

        sham_log("RCV DATA SEQ=%u LEN=%zu", pkt.header.seq_num, pkt.data_len);

        if (pkt.header.seq_num == expected_seq) {
            fwrite(pkt.data, 1, pkt.data_len, fp);
            expected_seq += (uint32_t)pkt.data_len;

            int progressed = 1;
            while (progressed) {
                progressed = 0;
                for (int i = 0; i < WINDOW_SIZE; i++) {
                    if (buf_slots[i].occupied && buf_slots[i].seq_num == expected_seq) {
                        fwrite(buf_slots[i].data, 1, buf_slots[i].data_len, fp);
                        expected_seq += (uint32_t)buf_slots[i].data_len;
                        buf_slots[i].occupied = 0;
                        progressed = 1;
                    }
                }
            }
        } else if (pkt.header.seq_num > expected_seq) {
            int already = 0;
            for (int i = 0; i < WINDOW_SIZE; i++) {
                if (buf_slots[i].occupied && buf_slots[i].seq_num == pkt.header.seq_num) already = 1;
            }
            if (!already) {
                for (int i = 0; i < WINDOW_SIZE; i++) {
                    if (!buf_slots[i].occupied) {
                        buf_slots[i].seq_num = pkt.header.seq_num;
                        memcpy(buf_slots[i].data, pkt.data, pkt.data_len);
                        buf_slots[i].data_len = pkt.data_len;
                        buf_slots[i].occupied = 1;
                        break;
                    }
                }
            }
        }
        /* pkt.header.seq_num < expected_seq: duplicate of already-written data, ignore */

        int occ = 0;
        for (int i = 0; i < WINDOW_SIZE; i++) {
            if (buf_slots[i].occupied) occ++;
        }
        uint16_t avail_window = (uint16_t)((WINDOW_SIZE - occ) * DATA_SIZE);

        Packet ackpkt = create_packet(0, expected_seq, FLAG_ACK, avail_window);
        send_packet(sockfd, addr, &ackpkt, loss_rate);
        sham_log("SND ACK=%u WIN=%u", expected_seq, avail_window);

        if ((int)avail_window != last_reported_window) {
            sham_log("FLOW WIN UPDATE=%u", avail_window);
            last_reported_window = avail_window;
        }
    }

    fclose(fp);
}

/* ---------------- Chat mode ---------------- */

void chat_mode(int sockfd, struct sockaddr_in *addr, float loss_rate) {
    uint32_t my_seq = 1;
    int has_pending = 0;
    Packet pending;
    struct timeval pending_sent;
    char line[DATA_SIZE];
    int stdin_eof = 0;
    time_t last_activity = time(NULL);

    printf("Chat mode. Type /quit to exit.\n");
    fflush(stdout);

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        if (!stdin_eof) FD_SET(STDIN_FILENO, &rfds);
        FD_SET(sockfd, &rfds);
        int maxfd = (sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO) + 1;
        struct timeval tv = {0, 50000}; /* bounded timeout: also need to service RTO while idle */
        int sr = select(maxfd, &rfds, NULL, NULL, &tv);

        /* If stdin has nothing more to give (e.g. EOF from a non-interactive/piped or
         * /dev/null stdin, common when one side is scripted or run headless) we must
         * NOT just exit the whole session - the socket side still needs servicing.
         * Only give up entirely if the peer has also gone quiet for a while. */
        if (stdin_eof && !has_pending && time(NULL) - last_activity > 30) {
            break;
        }

        if (has_pending) {
            struct timeval now;
            gettimeofday(&now, NULL);
            long elapsed_ms = (now.tv_sec - pending_sent.tv_sec) * 1000 +
                               (now.tv_usec - pending_sent.tv_usec) / 1000;
            if (elapsed_ms >= TIMEOUT_MS) {
                sham_log("TIMEOUT SEQ=%u", pending.header.seq_num);
                send_packet(sockfd, addr, &pending, loss_rate);
                gettimeofday(&pending_sent, NULL);
                sham_log("RETX DATA SEQ=%u LEN=%zu", pending.header.seq_num, pending.data_len);
            }
        }

        if (!stdin_eof && sr > 0 && FD_ISSET(STDIN_FILENO, &rfds) && !has_pending) {
            if (fgets(line, sizeof(line), stdin) == NULL) {
                stdin_eof = 1;
                continue; /* keep servicing the socket instead of exiting outright */
            }
            last_activity = time(NULL);
            line[strcspn(line, "\n")] = '\0';

            if (strcmp(line, "/quit") == 0) {
                four_way_handshake(sockfd, addr, loss_rate);
                break;
            }

            size_t len = strlen(line);
            if (len > DATA_SIZE) len = DATA_SIZE;

            Packet pkt = create_packet(my_seq, 0, 0, WINDOW_SIZE * DATA_SIZE);
            memcpy(pkt.data, line, len);
            pkt.data_len = len;

            send_packet(sockfd, addr, &pkt, loss_rate);
            sham_log("SND DATA SEQ=%u LEN=%zu", my_seq, len);

            pending = pkt;
            gettimeofday(&pending_sent, NULL);
            has_pending = 1;
        }

        if (sr > 0 && FD_ISSET(sockfd, &rfds)) {
            Packet in;
            int r = recv_packet(sockfd, addr, &in, loss_rate);
            if (r > 0) {
                last_activity = time(NULL);
                if (in.header.flags & FLAG_FIN) {
                    sham_log("RCV FIN SEQ=%u", in.header.seq_num);
                    Packet finack = create_packet(0, in.header.seq_num + 1, FLAG_ACK, 0);
                    send_packet(sockfd, addr, &finack, loss_rate);
                    sham_log("SND ACK FOR FIN");

                    uint32_t my_fin_seq = my_seq + 100000;
                    Packet myfin = create_packet(my_fin_seq, 0, FLAG_FIN, 0);
                    send_packet(sockfd, addr, &myfin, loss_rate);
                    sham_log("SND FIN SEQ=%u", my_fin_seq);
                    break;
                } else if (in.header.flags & FLAG_ACK) {
                    if (has_pending && in.header.ack_num == pending.header.seq_num + pending.data_len) {
                        my_seq += (uint32_t)pending.data_len;
                        has_pending = 0;
                    }
                } else if (in.data_len > 0) {
                    printf("peer: %.*s\n", (int)in.data_len, in.data);
                    fflush(stdout);
                    uint32_t new_expected = in.header.seq_num + (uint32_t)in.data_len;
                    Packet ackpkt = create_packet(0, new_expected, FLAG_ACK, WINDOW_SIZE * DATA_SIZE);
                    send_packet(sockfd, addr, &ackpkt, loss_rate);
                    sham_log("SND ACK=%u WIN=%u", new_expected, (uint16_t)(WINDOW_SIZE * DATA_SIZE));
                }
            }
        }
    }
}

/* ---------------- MD5 ---------------- */

void calculate_md5(const char *filename, char *md5_sum) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        md5_sum[0] = '\0';
        return;
    }

    MD5_CTX ctx;
    MD5_Init(&ctx);
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        MD5_Update(&ctx, buf, n);
    }
    fclose(fp);

    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &ctx);

    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        snprintf(md5_sum + i * 2, 3, "%02x", digest[i]);
    }
    md5_sum[MD5_DIGEST_LENGTH * 2] = '\0';
}
