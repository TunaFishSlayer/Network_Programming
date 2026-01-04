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

/* =========================SOCKET UTILS========================= */

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

/* =========================CONNECTION========================= */

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
            printf("Ket noi thanh cong voi %s:%d\n", peer_ip, peer_port);
            return sock;
        }
        
        close(sock);
        attempt++;
        printf("Ket noi that bai, thu lai... (%d/3)\n", attempt);
    }
    
    return -1;
}

/* =========================HANDSHAKE========================= */

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

/* =========================DOWNLOAD FILE========================= */


int download_file_chunked(const char* filehash, const char* filename,
                          long file_size, int chunk_size) {

    // 1. Tìm danh sách Peer đang giữ file
    FindResponse peers = find_peers_for_file(filehash);
    if (peers.count == 0) {
        printf("Khong tim thay peer nao giu file nay.\n");
        return 0;
    }

    // 2. Hiển thị danh sách để người dùng chọn
    printf("\n=== DANH SACH PEER GIU FILE ===\n");
    for (int i = 0; i < peers.count; i++) {
        printf("  [%d] IP: %s - Port: %d\n", i + 1, peers.peers[i].ip, peers.peers[i].port);
    }
    printf("===============================\n");
    
    int choice = 0;
    printf("Chon Peer de ket noi hoac 0 de huy: ");
    
    // Xử lý trôi lệnh: Chỉ xóa bộ nhớ đệm nếu có ký tự thừa
    // Lưu ý: scanf("%d") sẽ để lại \n, nên ta cần xóa nó trước khi đọc tiếp
    // Tuy nhiên, để an toàn, ta dùng scanf bỏ qua khoảng trắng
    scanf("%d", &choice); // Nếu bị trôi, bạn có thể thử: scanf(" %d", &choice);

    if (choice <= 0 || choice > peers.count) {
        printf("Huy tai file hoac lua chon khong hop le.\n");
        return 0;
    }

    // Lấy thông tin Peer được chọn
    PeerInfo target_peer = peers.peers[choice - 1];

    // 3. Bắt đầu logic tải file từ Peer đã chọn
    int total_chunks = (file_size + chunk_size - 1) / chunk_size;
    char* bitmap = calloc(total_chunks, 1);
    char* file_data = malloc(file_size);
    int downloaded = 0;
    int success = 0; 

    // Kết nối
    int sock = connect_to_peer_with_retry(target_peer.ip, target_peer.port);
    if (sock >= 0) {
        // Handshake
        if (handshake_with_peer(sock, filehash)) {
            
            // --- NHẬN BITMAP ---
            MessageHeader bitmap_hdr;
            if (recv_all(sock, &bitmap_hdr, sizeof(bitmap_hdr)) > 0 && 
                bitmap_hdr.command == P2P_BITMAP) {
                
                int bitmap_payload_size = total_chunks;
                char* bitmap_payload = malloc(bitmap_payload_size);
                
                if (recv_all(sock, bitmap_payload, bitmap_payload_size) > 0) {
                    printf("[P2P] Nhan bitmap tu Peer. Bat dau tai...\n");

                    // Vòng lặp tải từng Chunk
                    for (int i = 0; i < total_chunks; i++) {
                        if (i >= bitmap_payload_size || bitmap_payload[i] == 0) {
                            printf("[P2P] Peer khong co chunk %d. Dung tai.\n", i);
                            break; 
                        }

                        // Gửi yêu cầu lấy Chunk
                        P2PChunkRequest cr;
                        memset(&cr, 0, sizeof(cr));
                        cr.chunk_index = i;
                        
                        MessageHeader chunk_req_hdr;
                        chunk_req_hdr.command = P2P_REQUEST_CHUNK;
                        chunk_req_hdr.request_id = generate_request_id();

                        if (send_all(sock, &chunk_req_hdr, sizeof(chunk_req_hdr)) < 0) break;
                        if (send_all(sock, &cr, sizeof(cr)) < 0) break;

                        // Nhận Header của Chunk data
                        MessageHeader chunk_resp_hdr;
                        if (recv_all(sock, &chunk_resp_hdr, sizeof(chunk_resp_hdr)) < 0) break;

                        if (chunk_resp_hdr.command != P2P_CHUNK_DATA) break;

                        P2PChunkHeader ch;
                        if (recv_all(sock, &ch, sizeof(ch)) < 0) break;

                        // Nhận dữ liệu thực tế
                        int current_chunk_size = ch.chunk_size;
                        if (recv_all(sock, file_data + ch.chunk_index * chunk_size, current_chunk_size) < 0) break;

                        bitmap[i] = 1; 
                        downloaded++;
                        
                        printf("\r[P2P] Dang tai: %d/%d chunks...", downloaded, total_chunks);
                        fflush(stdout);
                    }
                    printf("\n");
                }
                free(bitmap_payload);
            }
        }
        close(sock);
    } else {
        printf("Khong the ket noi den Peer nay.\n");
    }

    // 4. Kiểm tra kết quả và ghi file
    // BÁO CÁO 1 LẦN DUY NHẤT TẠI ĐÂY
    if (downloaded == total_chunks) {
        char path[MAX_FILEPATH];
        snprintf(path, sizeof(path), "./downloads/%s", filename);
        FILE* fp = fopen(path, "wb");
        if (fp) {
            fwrite(file_data, 1, file_size, fp);
            fclose(fp);
            printf("Download HOAN TAT! File luu tai: %s\n", path);
            report_download_status(filehash, 1); // <--- Báo thành công
            success = 1;
        } else {
            printf("Loi ghi file xuong dia cung.\n");
            report_download_status(filehash, 0); // <--- Lỗi ghi file cũng tính là thất bại
        }
    } else {
        printf("Download THAT BAI (Tai duoc %d/%d chunks).\n", downloaded, total_chunks);
        report_download_status(filehash, 0); // <--- Báo thất bại
    }

    free(bitmap);
    free(file_data);
    return success;
}

/* =========================UPLOADER========================= */

void* handle_peer_download(void* arg) {
    int sock = *(int*)arg;
    free(arg);

    // Receive handshake header and request
    MessageHeader handshake_hdr;
    if (recv_all(sock, &handshake_hdr, sizeof(handshake_hdr)) < 0) {
        // Client kết nối nhưng không gửi gì hoặc ngắt ngay
        close(sock);
        return NULL;
    }

    if (handshake_hdr.command != P2P_HANDSHAKE) {
        printf("[P2P] Unexpected handshake command\n");
        close(sock);
        return NULL;
    }

    P2PHandshakeReq req;
    if (recv_all(sock, &req, sizeof(req)) < 0) {
        close(sock);
        return NULL;
    }

    printf("[P2P] Request FileHash: %.16s...\n", req.filehash);

    char filepath[MAX_FILEPATH] = "";
    DIR* dir = opendir(shared_dir);
    struct dirent* e;
    struct stat st;
    int found = 0;

    // Tìm file trong thư mục chia sẻ
    if (dir) {
        while ((e = readdir(dir))) {
            snprintf(filepath, sizeof(filepath), "%s%s", shared_dir, e->d_name);
            if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
                char h[MAX_HASH];
                calculate_file_hash(filepath, h);
                if (strcmp(h, req.filehash) == 0) {
                    found = 1;
                    break;
                }
            }
        }
        closedir(dir);
    }

    P2PHandshakeRes res;
    res.status = (found) ? HANDSHAKE_OK : HANDSHAKE_NO_FILE;

    MessageHeader resp_hdr;
    resp_hdr.command = P2P_HANDSHAKE_RES;
    resp_hdr.request_id = handshake_hdr.request_id;
    
    // Gửi phản hồi Handshake
    if (send_all(sock, &resp_hdr, sizeof(resp_hdr)) < 0 ||
        send_all(sock, &res, sizeof(res)) < 0) {
        close(sock);
        return NULL;
    }

    if (!found) {
        printf("[P2P] Handshake failed - File not found locally\n");
        close(sock);
        return NULL;
    }

    printf("[P2P] Handshake OK. Preparing transfer...\n");

    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        close(sock);
        return NULL;
    }
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    int total_chunks = (size + CHUNK_SIZE - 1) / CHUNK_SIZE;

    /* ---- SEND BITMAP ---- */
    MessageHeader bitmap_hdr;
    bitmap_hdr.command = P2P_BITMAP;
    bitmap_hdr.request_id = handshake_hdr.request_id;
    
    if (send_all(sock, &bitmap_hdr, sizeof(bitmap_hdr)) < 0) {
        fclose(fp);
        close(sock);
        return NULL;
    }

    // Send bitmap data (Full 1)
    char* bitmap_data = malloc(total_chunks);
    memset(bitmap_data, 1, total_chunks); 
    if (send_all(sock, bitmap_data, total_chunks) < 0) {
        free(bitmap_data);
        fclose(fp);
        close(sock);
        return NULL;
    }
    free(bitmap_data);
    printf("[P2P] Sent Bitmap (%d chunks)\n", total_chunks);

    /* ---- SEND CHUNKS LOOP ---- */
    while (1) {
        MessageHeader chunk_req_hdr;
        
        // SỬA LỖI: Kiểm tra giá trị trả về để không báo lỗi khi client ngắt kết nối
        int bytes_read = recv_all(sock, &chunk_req_hdr, sizeof(chunk_req_hdr));
        
        if (bytes_read <= 0) {
            // Đây là trường hợp bình thường khi client tải xong và đóng socket
            printf("[P2P] Client finished/disconnected.\n");
            break;
        }

        if (chunk_req_hdr.command != P2P_REQUEST_CHUNK) {
            printf("[P2P] Unknown command: %d\n", chunk_req_hdr.command);
            continue;
        }

        P2PChunkRequest cr;
        if (recv_all(sock, &cr, sizeof(cr)) <= 0) {
            break;
        }

        int idx = cr.chunk_index;
        if (idx >= total_chunks) continue;

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

        printf("[P2P] Sending chunk %d/%d (%d bytes)\n", idx + 1, total_chunks, csize);

        if (send_all(sock, &chunk_resp_hdr, sizeof(chunk_resp_hdr)) < 0 ||
            send_all(sock, &ch, sizeof(ch)) < 0 ||
            send_all(sock, buf, csize) < 0) {
            free(buf);
            break;
        }

        free(buf);
    }

    fclose(fp);
    close(sock);
    printf("========================================\n");
    printf("Transfer session ended: %s\n", filepath);
    printf("========================================\n");
    printf("\n=== MENU ===\n");
    printf("1. Xem danh sách file chia sẻ\n");
    printf("2. Tìm kiếm file\n");
    printf("3. Công bố file\n");
    printf("4. Hủy công bố file\n");
    printf("5. Thoát/Đăng xuất\n");
    printf("Chọn: ");
    return NULL;
}

/* =========================P2P SERVER========================= */

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
        if (*psock >= 0) {
            pthread_t tid;
            pthread_create(&tid, NULL, handle_peer_download, psock);
            pthread_detach(tid);
        } else {
            free(psock);
        }
    }
}