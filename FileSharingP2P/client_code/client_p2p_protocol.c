#include "client_p2p_protocol.h"
#include "client_utils.h"
#include "client_cs_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>

/* =========================
   SOCKET UTILS
   ========================= */

int send_all(int sock, const void* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(sock, (char*)buf + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return sent;
}

int recv_all(int sock, void* buf, int len) {
    int recvd = 0;
    while (recvd < len) {
        int n = recv(sock, (char*)buf + recvd, len - recvd, 0);
        if (n <= 0) return -1;
        recvd += n;
    }
    return recvd;
}

/* =========================
   CONNECT TO PEER
   ========================= */

int connect_to_peer_with_retry(const char* peer_ip, int peer_port) {
    for (int attempt = 1; attempt <= 3; attempt++) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        set_socket_timeout(sock, 5);

        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(peer_port)
        };
        inet_pton(AF_INET, peer_ip, &addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            printf("[P2P] Connected to %s:%d\n", peer_ip, peer_port);
            return sock;
        }

        close(sock);
        printf("[P2P] Retry connect (%d/3)\n", attempt);
    }
    return -1;
}

/* =========================
   HANDSHAKE
   ========================= */

int handshake_with_peer(int sock, const char* filehash) {
    P2PHeader hdr = { P2P_HANDSHAKE, sizeof(P2PHandshakeReq) };
    P2PHandshakeReq req;
    strcpy(req.filehash, filehash);

    send_all(sock, &hdr, sizeof(hdr));
    send_all(sock, &req, sizeof(req));
    printf("[SEND] HANDSHAKE %s\n", filehash);

    P2PHeader rh;
    if (recv_all(sock, &rh, sizeof(rh)) < 0) return 0;

    P2PHandshakeRes res;
    recv_all(sock, &res, sizeof(res));
    printf("[RECV] HANDSHAKE_RES status=%d\n", res.status);

    return res.status == HANDSHAKE_OK;
}

/* =========================
   DOWNLOAD FILE
   ========================= */

int download_file_chunked(const char* filehash, const char* filename,
                          long file_size, int chunk_size) {

    FindResponse peers = find_peers_for_file(filehash);
    if (peers.count == 0) {
        printf("Không tìm thấy peer\n");
        return 0;
    }

    int total_chunks = (file_size + chunk_size - 1) / chunk_size;
    char* bitmap = calloc(total_chunks, 1);
    char* file_data = malloc(file_size);
    int downloaded = 0;

    for (int p = 0; p < peers.count && downloaded < total_chunks; p++) {
        int sock = connect_to_peer_with_retry(peers.peers[p].ip,
                                              peers.peers[p].port);
        if (sock < 0) continue;

        if (!handshake_with_peer(sock, filehash)) {
            close(sock);
            continue;
        }

        /* ---- RECEIVE BITMAP ---- */
        P2PHeader hdr;
        recv_all(sock, &hdr, sizeof(hdr));

        int payload_size = hdr.length;
        char* payload = malloc(payload_size);
        recv_all(sock, payload, payload_size);

        P2PBitmap* bm = (P2PBitmap*)payload;
        char* bm_data = payload + sizeof(P2PBitmap);

        printf("[RECV] BITMAP chunks=%d\n", bm->total_chunks);

        for (int i = 0; i < total_chunks; i++) {
            if (bitmap[i] || bm_data[i] == 0) continue;

            /* ---- REQUEST CHUNK ---- */
            P2PHeader rq = { P2P_REQUEST_CHUNK, sizeof(P2PChunkRequest) };
            P2PChunkRequest cr = { .chunk_index = i };

            send_all(sock, &rq, sizeof(rq));
            send_all(sock, &cr, sizeof(cr));
            printf("[SEND] REQUEST CHUNK %d\n", i);

            /* ---- RECEIVE CHUNK HEADER ---- */
            recv_all(sock, &hdr, sizeof(hdr));
            P2PChunkHeader ch;
            recv_all(sock, &ch, sizeof(ch));

            /* ---- RECEIVE CHUNK DATA ---- */
            recv_all(sock,
                     file_data + ch.chunk_index * chunk_size,
                     ch.chunk_size);

            bitmap[i] = 1;
            downloaded++;
            printf("[RECV] CHUNK %d/%d size=%d\n",
                   i + 1, total_chunks, ch.chunk_size);
        }

        free(payload);
        close(sock);
    }

    if (downloaded == total_chunks) {
        char path[MAX_FILEPATH];
        snprintf(path, sizeof(path), "./downloads/%s", filename);
        FILE* fp = fopen(path, "wb");
        fwrite(file_data, 1, file_size, fp);
        fclose(fp);

        report_download_status(filehash, 1);
        printf("Download hoàn tất: %s\n", path);
        free(bitmap);
        free(file_data);
        return 1;
    }

    report_download_status(filehash, 0);
    free(bitmap);
    free(file_data);
    return 0;
}

/* =========================
   UPLOADER
   ========================= */

void* handle_peer_download(void* arg) {
    int sock = *(int*)arg;
    free(arg);

    P2PHeader hdr;
    recv_all(sock, &hdr, sizeof(hdr));

    P2PHandshakeReq req;
    recv_all(sock, &req, sizeof(req));

    char filepath[MAX_FILEPATH] = "";
    DIR* dir = opendir(shared_dir);
    struct dirent* e;
    struct stat st;

    while ((e = readdir(dir))) {
        snprintf(filepath, sizeof(filepath), "%s%s", shared_dir, e->d_name);
        if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
            char h[MAX_HASH];
            calculate_file_hash(filepath, h);
            if (strcmp(h, req.filehash) == 0) break;
        }
    }
    closedir(dir);

    P2PHandshakeRes res;
    res.status = (filepath[0]) ? HANDSHAKE_OK : HANDSHAKE_NO_FILE;

    hdr.command = P2P_HANDSHAKE_RES;
    hdr.length = sizeof(res);
    send_all(sock, &hdr, sizeof(hdr));
    send_all(sock, &res, sizeof(res));

    if (res.status != HANDSHAKE_OK) {
        close(sock);
        return NULL;
    }

    FILE* fp = fopen(filepath, "rb");
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    int total_chunks = (size + CHUNK_SIZE - 1) / CHUNK_SIZE;

    /* ---- SEND BITMAP ---- */
    int bm_size = total_chunks;
    hdr.command = P2P_BITMAP;
    hdr.length = sizeof(P2PBitmap) + bm_size;

    send_all(sock, &hdr, sizeof(hdr));

    P2PBitmap bm = { total_chunks, bm_size };
    send_all(sock, &bm, sizeof(bm));

    char* full = malloc(bm_size);
    memset(full, 1, bm_size);
    send_all(sock, full, bm_size);
    free(full);

    /* ---- SEND CHUNKS ---- */
    while (recv_all(sock, &hdr, sizeof(hdr)) > 0) {
        if (hdr.command != P2P_REQUEST_CHUNK) break;

        P2PChunkRequest cr;
        recv_all(sock, &cr, sizeof(cr));

        int idx = cr.chunk_index;
        int csize = (idx == total_chunks - 1)
                    ? size - idx * CHUNK_SIZE
                    : CHUNK_SIZE;

        char* buf = malloc(csize);
        fseek(fp, idx * CHUNK_SIZE, SEEK_SET);
        fread(buf, 1, csize, fp);

        hdr.command = P2P_CHUNK_DATA;
        hdr.length = sizeof(P2PChunkHeader);
        P2PChunkHeader ch = { idx, csize };

        send_all(sock, &hdr, sizeof(hdr));
        send_all(sock, &ch, sizeof(ch));
        send_all(sock, buf, csize);
        printf("[SEND] CHUNK %d size=%d\n", idx, csize);

        free(buf);
    }

    fclose(fp);
    printf("\n========================================\n");
    printf("Hoàn tất gửi file\n");
    printf("========================================\n");
    printf("\n=== MENU ===\n");
    printf("Người dùng: %s\n", current_username);
    printf("1. Xem danh sách file chia sẻ\n");
    printf("2. Tìm kiếm file\n");
    printf("3. Công bố file\n");
    printf("4. Hủy công bố file\n");
    printf("5. Thoát/Đăng xuất\n");
    printf("Chọn: ");
    fflush(stdout);
    close(sock);
    return NULL;
}

/* =========================
   P2P SERVER
   ========================= */

void* p2p_server(void* arg) {
    (void)arg;
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = 0
    };

    bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(sock, 5);

    socklen_t len = sizeof(addr);
    getsockname(sock, (struct sockaddr*)&addr, &len);
    p2p_listening_port = ntohs(addr.sin_port);

    printf("[P2P] Server listening on %d\n", p2p_listening_port);

    while (1) {
        int* psock = malloc(sizeof(int));
        *psock = accept(sock, NULL, NULL);
        pthread_t tid;
        pthread_create(&tid, NULL, handle_peer_download, psock);
        pthread_detach(tid);
    }
}
