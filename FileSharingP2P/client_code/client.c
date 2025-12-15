#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>

#include "../protocol.h"
#include "client_utils.h"
#include "client_cs_protocol.h"
#include "client_p2p_protocol.h"

// Biến trạng thái mới
int logged_in = 0; 

// Hàm hiển thị menu
void display_menu() {
    printf("\n=== MENU ===\n");
    if (logged_in) {
        printf("Người dùng: %s\n", current_username);
        printf("1. Tìm kiếm file\n");
        printf("2. Công bố file\n");
        printf("3. Hủy công bố file\n");
        printf("4. Thoát/Đăng xuất\n");
    } else {
        printf("1. Đăng ký\n");
        printf("2. Đăng nhập\n");
        printf("3. Thoát\n");
    }
    printf("Chọn: ");
}


int main() {
    char server_ip[MAX_IP];
    int choice;
    char email[MAX_EMAIL], username[MAX_USERNAME], password[MAX_PASSWORD];
    char keyword[MAX_FILENAME], filename[MAX_FILENAME];
    pthread_t p2p_thread;
    
    // Khởi tạo thư mục chia sẻ
    mkdir(shared_dir, 0777);
    mkdir("./downloads", 0777);
    
    printf("=== Ứng dụng P2P File Sharing ===\n");
    
    printf("Đang khởi động P2P server...\n");
    pthread_create(&p2p_thread, NULL, p2p_server, NULL);
    pthread_detach(p2p_thread);
    sleep(1);
    
    if (p2p_listening_port == 0) {
        printf("CẢNH BÁO: P2P server chưa sẵn sàng!\n");
        return 1;
    }
    
    printf("P2P server đã sẵn sàng trên port: %d\n", p2p_listening_port);
    
    printf("Nhập IP server: ");
    scanf("%s", server_ip);
    
    if (!connect_to_server(server_ip)) {
        printf("Không thể kết nối server!\n");
        return 1;
    }
    
    printf("✓ Đã kết nối tới server: %s:%d\n", server_ip, SERVER_PORT);
    
    strcpy(client_ip, "127.0.0.1"); 
    printf("Sử dụng IP P2P: %s để công bố file.\n", client_ip);
    
    while (1) {
        display_menu();
        
        if (scanf("%d", &choice) != 1) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            printf("Lựa chọn không hợp lệ.\n");
            continue;
        }
        
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        
        if (!logged_in) {
            // --- XỬ LÝ KHI CHƯA ĐĂNG NHẬP ---
            switch (choice) {
                case 1: // Đăng ký
                    printf("Email: ");
                    scanf("%s", email);
                    printf("Username: ");
                    scanf("%s", username);
                    printf("Password: ");
                    scanf("%s", password);
                    if (register_user(email, username, password)) {
                        printf("Đăng ký thành công!\n");
                    } else {
                        printf("Đăng ký thất bại! (Email đã tồn tại?)\n");
                    }
                    break;
                    
                case 2: // Đăng nhập
                    printf("Email: ");
                    scanf("%s", email);
                    printf("Password: ");
                    scanf("%s", password);
                    if (login_user(email, password)) {
                        printf("Đăng nhập thành công!\n");
                        logged_in = 1; // Cập nhật trạng thái
                    } else {
                        printf("Đăng nhập thất bại!\n");
                    }
                    break;
                    
                case 3: // Thoát
                    printf("Tạm biệt!\n");
                    close(server_sock);
                    return 0;
                    
                default:
                    printf("Lựa chọn không hợp lệ. Vui lòng Đăng ký hoặc Đăng nhập.\n");
                    break;
            }
        } else {
            // --- XỬ LÝ KHI ĐÃ ĐĂNG NHẬP
            switch (choice) {
                case 1: { 
                    printf("Từ khóa tìm kiếm: ");
                    scanf("%s", keyword);
                    SearchResponse resp = search_file(keyword);
                    
                    if (resp.count == 0) {
                        printf("Không tìm thấy file nào khớp với từ khóa!\n");
                        break;
                    }
                    
                    printf("\n=== %d file được tìm thấy. Chọn file để tải ===\n", resp.count);
                    for (int i = 0; i < resp.count; i++) {
                        printf("%d. %s (%ld bytes)\n", i + 1,
                               resp.files[i].filename,
                               resp.files[i].file_size);
                        printf("   Hash: %.16s...\n", resp.files[i].filehash);
                    }
                    
                    int sel;
                    printf("Chọn (0=hủy): ");
                    scanf("%d", &sel);
                    
                    if (sel > 0 && sel <= resp.count) {
                        sel--;
                        download_file_chunked(resp.files[sel].filehash,
                                            resp.files[sel].filename,
                                            resp.files[sel].file_size,
                                            resp.files[sel].chunk_size);
                    }
                    break;
                }
                    
                case 2: 
                    printf("Tên file (trong %s): ", shared_dir);
                    scanf("%s", filename);
                    publish_file(filename);
                    break;
                    
                case 3: 
                    printf("Tên file: ");
                    scanf("%s", filename);
                    unpublish_file(filename);
                    break;
                    
                case 4: 
                    logout_user();
                    logged_in = 0; // Đặt trạng thái về chưa đăng nhập
                    printf("Đã đăng xuất!\n");
                    break;
                    
                default:
                    printf("Lựa chọn không hợp lệ.\n");
                    break;
            }
        }
    }
    return 0;
}