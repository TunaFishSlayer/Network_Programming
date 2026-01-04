#include "client_p2p_protocol.h"
#include "client_utils.h"
#include "client_cs_protocol.h"
#include "../protocol.h"

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
        int bytes = send(sock, buf + sent, len - sent, 0);
        if (bytes <= 0) return -1;
        sent += bytes;
    }
    return sent;
}

int recv_all(int sock, void* buf, int len) {
    int received = 0;
    while (received < len) {
        int bytes = recv(sock, buf + received, len - received, 0);
        if (bytes <= 0) return -1;
        received += bytes;
    }
    return received;
}

/* =========================
   CONNECTION
   ========================= */

int connect_to_peer_with_retry(const char* peer_ip, int peer_port) {
    int attempt = 0;
    
    while (attempt < 3) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            attempt++;
            continue;
        }
        
        // Set timeout 5 giây
        set_socket_timeout(sock, 5);
        
        struct sockaddr_in peer_addr;
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(peer_port);
        inet_pton(AF_INET, peer_ip, &peer_addr.sin_addr);
        
        if (connect(sock, (struct sockaddr*)&peer_addr, sizeof(peer_addr)) == 0) {
            printf("Kết nối thành công với %s:%d\n", peer_ip, peer_port);
            return sock;
        }
        
        close(sock);
        attempt++;
        printf("Kết nối thất bại, thử lại... (%d/3)\n", attempt);
    }
    
    return -1;
}

/* =========================
   HANDSHAKE
   ========================= */

int handshake_with_peer(int sock, const char* filehash) {
    P2PHandshakeReq req;
    memset(&req, 0, sizeof(req));
    strcpy(req.filehash, filehash);
    
    printf("[P2P] Connecting to peer...\n");
    printf("[P2P] Filehash: %s\n", filehash);
    
    // Send header first
    MessageHeader hdr;
    hdr.command = P2P_HANDSHAKE;
    hdr.request_id = generate_request_id();
    
    if (send_all(sock, &hdr, sizeof(hdr)) < 0) return 0;
    if (send_all(sock, &req, sizeof(req)) < 0) return 0;
    
    printf("[P2P] Waiting for handshake response...\n");
    
    // Receive response header
    MessageHeader resp_hdr;
    if (recv_all(sock, &resp_hdr, sizeof(resp_hdr)) < 0) return 0;
    
    P2PHandshakeRes res;
    if (recv_all(sock, &res, sizeof(res)) < 0) return 0;
    
    printf("[P2P] Received handshake response\n");
    printf("[P2P] Command: %d, Status: %d\n", resp_hdr.command, res.status);
    
    if (resp_hdr.command == P2P_HANDSHAKE_RES && res.status == HANDSHAKE_OK) {
        printf("[P2P] Handshake successful - peer has file\n");
    } else if (resp_hdr.command == P2P_HANDSHAKE_RES && res.status == HANDSHAKE_NO_FILE) {
        printf("[P2P] Handshake failed - peer does not have file\n");
    } else {
        printf("[P2P] Unexpected handshake response - cmd=%d, status=%d\n", resp_hdr.command, res.status);
    }
    
    return (resp_hdr.command == P2P_HANDSHAKE_RES && res.status == HANDSHAKE_OK);
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
        MessageHeader bitmap_hdr;
        if (recv_all(sock, &bitmap_hdr, sizeof(bitmap_hdr)) < 0) {
            close(sock);
            continue;
        }

        if (bitmap_hdr.command != P2P_BITMAP) {
            close(sock);
            continue;
        }

        // Calculate bitmap payload size
        int bitmap_payload_size = total_chunks;
        char* bitmap_payload = malloc(bitmap_payload_size);
        if (recv_all(sock, bitmap_payload, bitmap_payload_size) < 0) {
            free(bitmap_payload);
            close(sock);
            continue;
        }

        printf("[P2P] Received bitmap from peer\n");
        printf("[P2P] Bitmap: ");
        for (int i = 0; i < bitmap_payload_size; i++) {
            printf("%d", bitmap_payload[i]);
        }
        printf("\n");
        printf("[P2P] Bitmap size: %d, Available chunks: %d\n", bitmap_payload_size, available_chunks);
        
        int available_chunks = 0;
        for (int i = 0; i < bitmap_payload_size; i++) {
            if (bitmap_payload[i] == 1) available_chunks++;
        }        
        for (int i = 0; i < total_chunks; i++) {
            if (bitmap[i] == 1) {
                printf("[P2P] Chunk %d is already downloaded\n", i);
                continue;
            }
            
            if (i >= bitmap_payload_size || bitmap_payload[i] == 0) {
                printf("[P2P] Chunk %d is not available from this peer\n", i);
                continue;
            }
            
            printf("[P2P] Chunk %d is available from peer\n", i);

            /* ---- REQUEST CHUNK ---- */
            P2PChunkRequest cr;
            memset(&cr, 0, sizeof(cr));
            cr.chunk_index = i;
            
            MessageHeader chunk_req_hdr;
            chunk_req_hdr.command = P2P_REQUEST_CHUNK;
            chunk_req_hdr.request_id = generate_request_id();

            printf("[P2P] Requesting chunk %d from peer...\n", i);
            printf("[P2P] Chunk %d/%d (size: %d bytes)\n", i + 1, total_chunks, chunk_size);

            if (send_all(sock, &chunk_req_hdr, sizeof(chunk_req_hdr)) < 0) break;
            if (send_all(sock, &cr, sizeof(cr)) < 0) break;

            /* ---- RECEIVE CHUNK HEADER ---- */
            MessageHeader chunk_resp_hdr;
            if (recv_all(sock, &chunk_resp_hdr, sizeof(chunk_resp_hdr)) < 0) break;

            if (chunk_resp_hdr.command != P2P_CHUNK_DATA) {
                printf("[P2P] Unexpected chunk response - cmd=%d (expected P2P_CHUNK_DATA=%d)\n", 
                       chunk_resp_hdr.command, P2P_CHUNK_DATA);
                continue;
            }

            P2PChunkHeader ch;
            if (recv_all(sock, &ch, sizeof(ch)) < 0) break;

            /* ---- RECEIVE CHUNK DATA ---- */
            int chunk_data_size = ch.chunk_size;
            if (recv_all(sock, file_data + ch.chunk_index * chunk_size, chunk_data_size) < 0) break;

            bitmap[i] = 1;
            downloaded++;
            printf("[P2P] Received chunk %d/%d (size: %d bytes, total: %d/%d)\n", 
                   i + 1, total_chunks, chunk_data_size, downloaded, total_chunks);
        }

        free(bitmap_payload);
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

    // Receive handshake header and request
    MessageHeader handshake_hdr;
    if (recv_all(sock, &handshake_hdr, sizeof(handshake_hdr)) < 0) {
        printf("[P2P] Error receiving handshake header\n");
        close(sock);
        return NULL;
    }

    if (handshake_hdr.command != P2P_HANDSHAKE) {
        printf("[P2P] Unexpected handshake command - cmd=%d (expected P2P_HANDSHAKE=%d)\n", 
               handshake_hdr.command, P2P_HANDSHAKE);
        close(sock);
        return NULL;
    }

    P2PHandshakeReq req;
    if (recv_all(sock, &req, sizeof(req)) < 0) {
        printf("[P2P] Error receiving handshake request\n");
        close(sock);
        return NULL;
    }

    printf("[P2P] Received handshake request for filehash: %.16s...\n", req.filehash);

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

    MessageHeader resp_hdr;
    resp_hdr.command = P2P_HANDSHAKE_RES;
    resp_hdr.request_id = handshake_hdr.request_id;
    
    printf("[P2P] Sending handshake response - status: %d\n", res.status);
    
    if (send_all(sock, &resp_hdr, sizeof(resp_hdr)) < 0) {
        close(sock);
        return NULL;
    }
    if (send_all(sock, &res, sizeof(res)) < 0) {
        close(sock);
        return NULL;
    }

    if (res.status == HANDSHAKE_NO_FILE) {
        printf("[P2P] Handshake failed - file not found\n");
        close(sock);
        return NULL;
    }

    printf("[P2P] Handshake successful - file found\n");

    if (res.status != HANDSHAKE_OK) {
        close(sock);
        return NULL;
    }

    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        printf("[P2P] Error opening file\n");
        close(sock);
        return NULL;
    }
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    int total_chunks = (size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    printf("[P2P] Starting file transfer: %s (%d chunks, %ld bytes)\n", 
           filepath, total_chunks, size);

    /* ---- SEND BITMAP ---- */
    MessageHeader bitmap_hdr;
    bitmap_hdr.command = P2P_BITMAP;
    bitmap_hdr.request_id = handshake_hdr.request_id;
    
    printf("[P2P] Sending bitmap to peer\n");
    
    if (send_all(sock, &bitmap_hdr, sizeof(bitmap_hdr)) < 0) {
        fclose(fp);
        close(sock);
        return NULL;
    }

    // Send bitmap data (all chunks available)
    char* bitmap_data = malloc(total_chunks);
    memset(bitmap_data, 1, total_chunks); // All chunks available
    if (send_all(sock, bitmap_data, total_chunks) < 0) {
        free(bitmap_data);
        fclose(fp);
        close(sock);
        return NULL;
    }
    free(bitmap_data);
    printf("[P2P] Sent bitmap to peer\n");

    /* ---- SEND CHUNKS ---- */
    while (1) {
        MessageHeader chunk_req_hdr;
        if (recv_all(sock, &chunk_req_hdr, sizeof(chunk_req_hdr)) <= 0) {
            printf("[P2P] Error receiving chunk request\n");
            break;
        }

        if (chunk_req_hdr.command != P2P_REQUEST_CHUNK) {
            if (chunk_req_hdr.command == P2P_DISCONNECT) {
                printf("[P2P] Peer disconnected\n");
                break;
            }
            printf("[P2P] Unexpected command: %d\n", chunk_req_hdr.command);
            continue;
        }

        P2PChunkRequest cr;
        if (recv_all(sock, &cr, sizeof(cr)) <= 0) {
            printf("[P2P] Error receiving chunk request details\n");
            break;
        }

        int idx = cr.chunk_index;
        if (idx >= total_chunks) {
            printf("[P2P] Invalid chunk index - chunk_idx=%d (expected 0-%d)\n", idx, total_chunks - 1);
            continue;
        }

        int csize = (idx == total_chunks - 1) ? (size - idx * CHUNK_SIZE) : CHUNK_SIZE;

        char* buf = malloc(csize);
        fseek(fp, idx * CHUNK_SIZE, SEEK_SET);
        fread(buf, 1, csize, fp);

        /* ---- SEND CHUNK HEADER ---- */
        MessageHeader chunk_resp_hdr;
        chunk_resp_hdr.command = P2P_CHUNK_DATA;
        chunk_resp_hdr.request_id = chunk_req_hdr.request_id;

        P2PChunkHeader ch;
        ch.chunk_index = idx;
        ch.chunk_size = csize;

        printf("[P2P] Sending chunk %d/%d to peer (size: %d bytes)\n", 
               idx + 1, total_chunks, csize);

        if (send_all(sock, &chunk_resp_hdr, sizeof(chunk_resp_hdr)) < 0) {
            free(buf);
            break;
        }
        if (send_all(sock, &ch, sizeof(ch)) < 0) {
            free(buf);
            break;
        }
        if (send_all(sock, buf, csize) < 0) {
            free(buf);
            break;
        }

        printf("[P2P] Sent chunk %d\n", idx + 1);
        free(buf);
    }

    fclose(fp);
    close(sock);
    printf("\n========================================\n");
    printf("File transfer completed: %s\n", filepath);
    printf("========================================\n");
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
