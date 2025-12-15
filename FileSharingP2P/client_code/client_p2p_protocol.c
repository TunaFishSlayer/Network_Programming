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

// Thử kết nối với peer (timeout 5s, retry 3 lần)
int connect_to_peer_with_retry(const char* peer_ip, int peer_port) {
    int attempt = 0;
    int peer_sock;
    
    while (attempt < 3) {
        peer_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (peer_sock < 0) {
            attempt++;
            continue;
        }
        
        // Set timeout 5 giây
        set_socket_timeout(peer_sock, 5);
        
        struct sockaddr_in peer_addr;
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(peer_port);
        inet_pton(AF_INET, peer_ip, &peer_addr.sin_addr);
        
        if (connect(peer_sock, (struct sockaddr*)&peer_addr, sizeof(peer_addr)) == 0) {
            printf("Kết nối thành công với %s:%d\n", peer_ip, peer_port);
            return peer_sock;
        }
        
        close(peer_sock);
        attempt++;
        printf("Kết nối thất bại, thử lại... (%d/3)\n", attempt);
    }
    
    printf("Không thể kết nối với %s:%d sau 3 lần thử\n", peer_ip, peer_port);
    return -1;
}

// Handshake với peer
int handshake_with_peer(int peer_sock, const char* filehash) {
    P2PMessage msg, response;
    memset(&msg, 0, sizeof(P2PMessage));
    
    msg.command = P2P_HANDSHAKE;
    strcpy(msg.filehash, filehash);
    
    printf("[SEND] Handshake - Filehash: %s\n", filehash);
    send(peer_sock, &msg, sizeof(P2PMessage), 0);
    
    // Set timeout để đợi phản hồi
    set_socket_timeout(peer_sock, 5);
    
    if (recv(peer_sock, &response, sizeof(P2PMessage), 0) <= 0) {
        printf("[RECV] Handshake response: Timeout or error\n");
        return 0;
    }
    
    printf("[RECV] Handshake response - Command: %d, Status: %d\n", response.command, response.status);
    
    return (response.command == P2P_HANDSHAKE_RES && 
            response.status == HANDSHAKE_OK);
}

// Download file với chunk và bitmap
int download_file_chunked(const char* filehash, const char* filename, 
                         long file_size, int chunk_size) {
    
    FindResponse peers = find_peers_for_file(filehash);
    
    if (peers.count == 0) {
        printf("Không tìm thấy peer nào có file này!\n");
        return 0;
    }
    
    printf("Tìm thấy %d peer(s)\n", peers.count);
    
    int total_chunks = (file_size + chunk_size - 1) / chunk_size;
    char* bitmap = calloc(total_chunks, 1);
    char* file_data = malloc(file_size);
    
    printf("File size: %ld bytes, Chunks: %d, Chunk size: %d\n", 
           file_size, total_chunks, chunk_size);
    
    int total_downloaded_chunks = 0;
    
    for (int p = 0; p < peers.count && p < 10; p++) {
        if (total_downloaded_chunks == total_chunks) break;

        printf("\n--- Thử peer %d: %s:%d ---\n", 
               p + 1, peers.peers[p].ip, peers.peers[p].port);
        
        int peer_sock = connect_to_peer_with_retry(peers.peers[p].ip, 
                                                   peers.peers[p].port);
        if (peer_sock < 0) continue;
        
        if (!handshake_with_peer(peer_sock, filehash)) {
            printf("Handshake thất bại\n");
            close(peer_sock);
            continue;
        }
        
        P2PMessage peer_msg;
        memset(&peer_msg, 0, sizeof(P2PMessage));
        
        // Nhận P2PMessage đầy đủ
        char* msg_ptr = (char*)&peer_msg;
        int total_bytes = 0;
        int remaining = sizeof(P2PMessage);
        
        while (remaining > 0 && total_bytes < (int)sizeof(P2PMessage)) {
            int bytes = recv(peer_sock, msg_ptr + total_bytes, remaining, 0);
            if (bytes <= 0) {
                printf("[RECV] Error receiving message: %d bytes received\n", total_bytes);
                break;
            }
            total_bytes += bytes;
            remaining -= bytes;
        }
        
        if (total_bytes == 0) {
            close(peer_sock);
            continue;
        }
        
        if (peer_msg.command != P2P_BITMAP) {
            close(peer_sock);
            continue;
        }
        
        printf("Nhận bitmap từ peer (size: %d)\n", peer_msg.bitmap_size);
        
        for (int i = 0; i < total_chunks; i++) {
            if (bitmap[i] == 1) continue;
            
            if (i >= peer_msg.bitmap_size || peer_msg.bitmap[i] == 0) {
                continue;
            }
            
            int retry_count = 0;
            int chunk_downloaded = 0;
            
            while (retry_count < 3 && !chunk_downloaded) {
                P2PMessage req;
                memset(&req, 0, sizeof(P2PMessage));
                req.command = P2P_REQUEST_CHUNK;
                req.chunk_index = i;
                
                printf("[SEND] Request chunk %d\n", i);
                send(peer_sock, &req, sizeof(P2PMessage), 0);
                
                set_socket_timeout(peer_sock, 60);
                
                P2PMessage chunk_msg;
                memset(&chunk_msg, 0, sizeof(P2PMessage));
                
                // Nhận P2PMessage đầy đủ
                char* chunk_ptr = (char*)&chunk_msg;
                int chunk_total = 0;
                int chunk_remaining = sizeof(P2PMessage);
                
                while (chunk_remaining > 0 && chunk_total < (int)sizeof(P2PMessage)) {
                    int bytes = recv(peer_sock, chunk_ptr + chunk_total, chunk_remaining, 0);
                    if (bytes <= 0) break;
                    chunk_total += bytes;
                    chunk_remaining -= bytes;
                }
                
                if (chunk_total == 0) {
                    retry_count++;
                    printf("Timeout/Error chunk %d, retry %d/3\n", i, retry_count);
                    continue;
                }
                
                if (chunk_msg.command != P2P_CHUNK_DATA) {
                    printf("[RECV] Unexpected response: Command=%d (expected P2P_CHUNK_DATA=%d)\n", 
                           chunk_msg.command, P2P_CHUNK_DATA);
                    retry_count++;
                    continue;
                }
                
                int this_chunk_size = (i == total_chunks - 1) ? 
                    (file_size - i * chunk_size) : chunk_size;
                
                char* chunk_buffer = malloc(this_chunk_size);
                int total_received = 0;
                
                while (total_received < this_chunk_size) {
                    int received = recv(peer_sock, chunk_buffer + total_received,
                                      this_chunk_size - total_received, 0);
                    if (received <= 0) break;
                    total_received += received;
                }
                
                if (total_received == this_chunk_size) {
                    // Chép dữ liệu vào vị trí đúng
                    memcpy(file_data + i * chunk_size, chunk_buffer, this_chunk_size);
                    bitmap[i] = 1;
                    chunk_downloaded = 1;
                    total_downloaded_chunks++;
                    printf("[RECV] Received chunk %d/%d (size: %d bytes, total: %d/%d)\n", 
                           i + 1, total_chunks, this_chunk_size, total_downloaded_chunks, total_chunks);
                } else {
                    retry_count++;
                    printf("Chunk %d không đầy đủ, retry %d/3\n", i, retry_count);
                }
                
                free(chunk_buffer);
            }
            
            if (retry_count >= 3) {
                printf("Chunk %d thất bại sau 3 lần thử, chuyển peer...\n", i);
                break;
            }
        }
        
        close(peer_sock);
    }
    
    // Kiểm tra đã download đủ chưa
    if (total_downloaded_chunks == total_chunks) {
        // Lưu file vào thư mục downloads
        char filepath[MAX_FILEPATH];
        snprintf(filepath, sizeof(filepath), "./downloads/%s", filename);
        
        FILE* fp = fopen(filepath, "wb");
        if (fp) {
            fwrite(file_data, 1, file_size, fp);
            fclose(fp);
            printf("\nDownload hoàn tất: %s\n", filename);
            printf("  Lưu tại: %s\n", filepath);
            
            // Báo cáo server rằng download xong
            report_download_status(filehash, 1);
            
            free(bitmap);
            free(file_data);
            return 1;
        }
    }
    
    // Download thất bại - báo server
    report_download_status(filehash, 0);
    free(bitmap);
    free(file_data);
    printf("\nDownload thất bại!\n");
    return 0;
}

// Xử lý yêu cầu P2P từ peer khác (sender)
void* handle_peer_download(void* arg) {
    int peer_sock = *(int*)arg;
    free(arg);
    
    P2PMessage msg;
    
    // Nhận handshake
    if (recv(peer_sock, &msg, sizeof(P2PMessage), 0) <= 0) {
        close(peer_sock);
        return NULL;
    }
    
    if (msg.command != P2P_HANDSHAKE) {
        close(peer_sock);
        return NULL;
    }
    
    // Tìm file với hash
    char filepath[MAX_FILEPATH];
    DIR* dir = opendir(shared_dir);
    struct dirent* entry;
    char found_file[MAX_FILENAME] = "";
    struct stat st;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(filepath, sizeof(filepath), "%s%s", shared_dir, entry->d_name);
        
        if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
            char file_hash[MAX_HASH];
            calculate_file_hash(filepath, file_hash);
            
            if (strcmp(file_hash, msg.filehash) == 0) {
                strcpy(found_file, entry->d_name);
                break;
            }
        }
    }
    closedir(dir);
    
    // Phản hồi handshake
    P2PMessage response;
    memset(&response, 0, sizeof(P2PMessage));
    response.command = P2P_HANDSHAKE_RES;
    
    if (found_file[0] == '\0') {
        response.status = HANDSHAKE_NO_FILE;
        send(peer_sock, &response, sizeof(P2PMessage), 0);
        close(peer_sock);
        return NULL;
    }
    
    response.status = HANDSHAKE_OK;
    send(peer_sock, &response, sizeof(P2PMessage), 0);
    
    snprintf(filepath, sizeof(filepath), "%s%s", shared_dir, found_file);
    
    // Mở file
    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        close(peer_sock);
        return NULL;
    }
    
    // Tính số chunk và tạo bitmap
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    int total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    // Gửi bitmap (giả sử có đầy đủ tất cả chunks)
    P2PMessage bitmap_msg;
    memset(&bitmap_msg, 0, sizeof(P2PMessage));
    bitmap_msg.command = P2P_BITMAP;
    bitmap_msg.bitmap_size = total_chunks;
    memset(bitmap_msg.bitmap, 1, total_chunks); // Có tất cả chunks
    
    send(peer_sock, &bitmap_msg, sizeof(P2PMessage), 0);
    
    printf("Bắt đầu gửi file: %s (%d chunks)\n", found_file, total_chunks);
    
    // Nhận và xử lý request chunks
    while (1) {
        P2PMessage req;
        if (recv(peer_sock, &req, sizeof(P2PMessage), 0) <= 0) {
            break;
        }
        
        if (req.command == P2P_REQUEST_CHUNK) {
            int chunk_idx = req.chunk_index;
            
            if (chunk_idx >= total_chunks) continue;
            
            // Đọc chunk
            int this_chunk_size = (chunk_idx == total_chunks - 1) ?
                (file_size - chunk_idx * CHUNK_SIZE) : CHUNK_SIZE;
            
            char* chunk_data = malloc(this_chunk_size);
            fseek(fp, chunk_idx * CHUNK_SIZE, SEEK_SET);
            fread(chunk_data, 1, this_chunk_size, fp);
            
            // Gửi phản hồi
            P2PMessage chunk_msg;
            memset(&chunk_msg, 0, sizeof(P2PMessage));
            chunk_msg.command = P2P_CHUNK_DATA;
            chunk_msg.chunk_index = chunk_idx;
            chunk_msg.chunk_size = this_chunk_size;
            
            send(peer_sock, &chunk_msg, sizeof(P2PMessage), 0);
            send(peer_sock, chunk_data, this_chunk_size, 0);
            
            printf("Gửi chunk %d/%d\n", chunk_idx + 1, total_chunks);
            
            free(chunk_data);
        } else if (req.command == P2P_DISCONNECT) {
            break;
        }
    }
    
    fclose(fp);
    close(peer_sock);
    printf("\n========================================\n");
    printf("✓ Hoàn tất gửi file: %s\n", found_file);
    printf("========================================\n");
    printf("\n=== MENU ===\n");
    printf("Người dùng: %s\n", current_username);
    printf("1. Tìm kiếm file\n");
    printf("2. Công bố file\n");
    printf("3. Hủy công bố file\n");
    printf("4. Thoát/Đăng xuất\n");
    printf("Chọn: ");
    fflush(stdout);  
    
    return NULL;
}

// P2P Server lắng nghe
void* p2p_server(void* arg) {
    (void)arg;
    struct sockaddr_in server_addr, peer_addr;
    socklen_t addr_len = sizeof(peer_addr);
    pthread_t thread_id;
    
    int p2p_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(p2p_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = 0; // Auto-assign port
    
    bind(p2p_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    // Lấy port được gán
    socklen_t len = sizeof(server_addr);
    getsockname(p2p_sock, (struct sockaddr*)&server_addr, &len);
    p2p_listening_port = ntohs(server_addr.sin_port);
    
    listen(p2p_sock, 5);
    
    printf("P2P server lắng nghe trên cổng %d\n", p2p_listening_port);
    
    while (1) {
        int* peer_sock = malloc(sizeof(int));
        *peer_sock = accept(p2p_sock, (struct sockaddr*)&peer_addr, &addr_len);
        
        if (*peer_sock < 0) {
            free(peer_sock);
            continue;
        }
        
        pthread_create(&thread_id, NULL, handle_peer_download, peer_sock);
        pthread_detach(thread_id);
    }
    
    return NULL;
}