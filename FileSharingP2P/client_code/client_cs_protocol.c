#include "client_cs_protocol.h"
#include "client_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <errno.h>

// Helper function to receive full struct
static int recv_full(int sock, void* buffer, size_t size) {
    char* buf = (char*)buffer;
    size_t total = 0;
    
    while (total < size) {
        int bytes = recv(sock, buf + total, size - total, 0);
        if (bytes <= 0) {
            return bytes;
        }
        total += bytes;
    }
    return total;
}

// Kết nối đến server
int connect_to_server(const char* server_ip) {
    struct sockaddr_in server_addr;
    
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        return 0;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    
    if (connect(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        return 0;
    }
    
    return 1;
}

// Đăng ký với email
int register_user(const char* email, const char* username, const char* password) {
    RegisterRequest req;
    RegisterResponse resp;
    
    memset(&req, 0, sizeof(RegisterRequest));
    
    req.header.command = CMD_REGISTER;
    req.header.request_id = generate_request_id();
    strcpy(req.email, email);
    strcpy(req.username, username);
    strcpy(req.password, password);
    
    printf("[DEBUG] Sending REGISTER request (request_id: %u)\n", req.header.request_id);
    printf("        Email: %s, Username: %s\n", email, username);
    
    if (send(server_sock, &req, sizeof(RegisterRequest), 0) < 0) {
        perror("Send register request failed");
        return 0;
    }
    
    if (recv_full(server_sock, &resp, sizeof(RegisterResponse)) <= 0) {
        perror("Receive register response failed");
        return 0;
    }
    
    printf("[DEBUG] Received REGISTER response (status: %d)\n", resp.status);
    
    return (resp.status == RESP_SUCCESS);
}

// Đăng nhập với email
int login_user(const char* email, const char* password) {
    LoginRequest req;
    LoginResponse resp;
    
    memset(&req, 0, sizeof(LoginRequest));
    
    req.header.command = CMD_LOGIN;
    req.header.request_id = generate_request_id();
    strcpy(req.email, email);
    strcpy(req.password, password);
    req.port = p2p_listening_port;
    
    printf("[DEBUG] Sending LOGIN request (request_id: %u)\n", req.header.request_id);
    printf("        Email: %s, P2P Port: %d\n", email, p2p_listening_port);
    
    if (send(server_sock, &req, sizeof(LoginRequest), 0) < 0) {
        perror("Send login request failed");
        return 0;
    }
    
    if (recv_full(server_sock, &resp, sizeof(LoginResponse)) <= 0) {
        perror("Receive login response failed");
        return 0;
    }
    
    printf("[DEBUG] Received LOGIN response (status: %d)\n", resp.status);
    
    if (resp.status == RESP_SUCCESS) {
        strcpy(current_email, email);
        strcpy(current_username, resp.username);
        strcpy(current_token, resp.access_token);
        printf("[DEBUG] Login successful. Username: %s\n", current_username);
        return 1;
    } else if (resp.status == RESP_ALREADY_LOGGED_IN) {
        printf("[ERROR] Tài khoản này đã đăng nhập từ vị trí khác!\n");
        printf("        Vui lòng đăng xuất phiên làm việc cũ trước.\n");
        return 0;
    }
    
    return 0;
}

BrowseFilesResponse browse_files(void) {
    BrowseFilesRequest req;
    BrowseFilesResponse resp;

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    req.header.command = CMD_BROWSE_FILES;
    req.header.request_id = generate_request_id();
    strcpy(req.email, current_email);
    strcpy(req.access_token, current_token);

    printf("[DEBUG] Sending BROWSE request (request_id: %u)\n",
           req.header.request_id);

    if (send(server_sock, &req, sizeof(req), 0) < 0) {
        perror("Send browse request failed");
        return resp;
    }

    if (recv_full(server_sock, &resp, sizeof(resp)) <= 0) {
        perror("Receive browse response failed");
        return resp;
    }

    printf("[DEBUG] Browse result: %d file(s)\n", resp.count);

    for (int i = 0; i < resp.count; i++) {
        printf("  [%d] %s | %.16s... | %ld bytes\n",
               i + 1,
               resp.files[i].filename,
               resp.files[i].filehash,
               resp.files[i].file_size);
    }

    return resp;
}

// Tìm kiếm file
SearchResponse search_file(const char* keyword) {
    SearchRequest req;
    SearchResponse resp;
    
    memset(&req, 0, sizeof(SearchRequest));
    memset(&resp, 0, sizeof(SearchResponse));
    
    req.header.command = CMD_SEARCH;
    req.header.request_id = generate_request_id();
    strcpy(req.email, current_email);
    strcpy(req.access_token, current_token);
    strcpy(req.keyword, keyword);
    
    printf("[DEBUG] Sending SEARCH request (request_id: %u)\n", req.header.request_id);
    printf("        Keyword: %s\n", keyword);
    
    if (send(server_sock, &req, sizeof(SearchRequest), 0) < 0) {
        perror("Send search request failed");
        resp.count = 0;
        return resp;
    }
    
    // Set receive timeout
    struct timeval tv = {10, 0}; // 10 seconds timeout
    setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    if (recv_full(server_sock, &resp, sizeof(SearchResponse)) <= 0) {
        perror("Receive search response failed");
        resp.count = 0;
        return resp;
    }
    
    printf("[DEBUG] Received SEARCH response (status: %d, count: %d)\n", 
           resp.status, resp.count);
    
    for (int i = 0; i < resp.count && i < 5; i++) {
        printf("        [%d] %s (%.16s...) %ld bytes\n", 
               i+1, resp.files[i].filename, 
               resp.files[i].filehash, resp.files[i].file_size);
    }
    
    return resp;
}

// Tìm peers có file hash cụ thể
FindResponse find_peers_for_file(const char* filehash) {
    FindRequest req;
    FindResponse resp;
    
    memset(&req, 0, sizeof(FindRequest));
    memset(&resp, 0, sizeof(FindResponse));
    
    req.header.command = CMD_FIND;
    req.header.request_id = generate_request_id();
    strcpy(req.email, current_email);
    strcpy(req.access_token, current_token);
    strcpy(req.filehash, filehash);
    
    printf("[DEBUG] Sending FIND request (request_id: %u)\n", req.header.request_id);
    printf("        Filehash: %.16s...\n", filehash);
    
    if (send(server_sock, &req, sizeof(FindRequest), 0) < 0) {
        perror("Send find request failed");
        resp.count = 0;
        return resp;
    }
    
    // Set receive timeout
    struct timeval tv = {10, 0}; // 10 seconds timeout
    setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    if (recv_full(server_sock, &resp, sizeof(FindResponse)) <= 0) {
        perror("Receive find response failed");
        resp.count = 0;
        return resp;
    }
    
    printf("[DEBUG] Received FIND response (status: %d, count: %d)\n", 
           resp.status, resp.count);
    
    for (int i = 0; i < resp.count && i < 5; i++) {
        printf("        [%d] Peer: %s:%d\n", 
               i+1, resp.peers[i].ip, resp.peers[i].port);
    }
    
    return resp;
}

// Công bố file với hash và chunk_size
void publish_file(const char* filename) {
    PublishRequest req;
    PublishResponse resp;
    char filepath[MAX_FILEPATH];
    struct stat st;
    
    snprintf(filepath, sizeof(filepath), "%s%s", shared_dir, filename);
    
    if (stat(filepath, &st) != 0) {
        printf("File không tồn tại!\n");
        return;
    }
    
    // Tính hash
    char filehash[MAX_HASH];
    printf("Đang tính hash...\n");
    calculate_file_hash(filepath, filehash);
    
    if (filehash[0] == '\0') {
        printf("Không thể tính hash!\n");
        return;
    }
    
    printf("DEBUG CLIENT: Calculated hash: %s\n", filehash);
    
    memset(&req, 0, sizeof(PublishRequest));
    
    req.header.command = CMD_PUBLISH;
    req.header.request_id = generate_request_id();
    strcpy(req.email, current_email);
    strcpy(req.access_token, current_token);
    strcpy(req.filename, filename);
    strcpy(req.filehash, filehash);
    strcpy(req.ip, client_ip);
    req.port = p2p_listening_port;
    req.file_size = st.st_size;
    req.chunk_size = CHUNK_SIZE;
    
    printf("[DEBUG] Sending PUBLISH request (request_id: %u)\n", req.header.request_id);
    printf("        Filename: %s, Size: %ld bytes\n", filename, st.st_size);
    printf("        Hash: %.16s...\n", filehash);
    
    if (send(server_sock, &req, sizeof(PublishRequest), 0) < 0) {
        perror("Send publish request failed");
        return;
    }
    
    if (recv_full(server_sock, &resp, sizeof(PublishResponse)) <= 0) {
        perror("Receive publish response failed");
        return;
    }
    
    printf("[DEBUG] Received PUBLISH response (status: %d)\n", resp.status);
    
    if (resp.status == RESP_SUCCESS) {
        printf("Công bố file: %s\n", filename);
        printf("  Hash: %s\n", filehash);
        printf("  Size: %ld bytes\n", st.st_size);
    } else {
        printf("Công bố file thất bại! (Status: %d)\n", resp.status);
    }
}

// Hủy công bố
void unpublish_file(const char* filename) {
    UnpublishRequest req;
    UnpublishResponse resp;
    char filepath[MAX_FILEPATH];
    
    snprintf(filepath, sizeof(filepath), "%s%s", shared_dir, filename);
    
    char filehash[MAX_HASH];
    calculate_file_hash(filepath, filehash);
    
    if (filehash[0] == '\0') {
        printf("Không thể tính hash!\n");
        return;
    }
    
    memset(&req, 0, sizeof(UnpublishRequest));
    
    req.header.command = CMD_UNPUBLISH;
    req.header.request_id = generate_request_id();
    strcpy(req.email, current_email);
    strcpy(req.access_token, current_token);
    strcpy(req.filehash, filehash);
    
    printf("[DEBUG] Sending UNPUBLISH request (request_id: %u)\n", req.header.request_id);
    printf("        Filehash: %.16s...\n", filehash);
    
    if (send(server_sock, &req, sizeof(UnpublishRequest), 0) < 0) {
        perror("Send unpublish request failed");
        return;
    }
    
    if (recv_full(server_sock, &resp, sizeof(UnpublishResponse)) <= 0) {
        perror("Receive unpublish response failed");
        return;
    }
    
    printf("[DEBUG] Received UNPUBLISH response (status: %d)\n", resp.status);
    
    if (resp.status == RESP_SUCCESS) {
        printf("Hủy công bố file thành công!\n");
    } else if (resp.status == RESP_FILE_NOT_OWNED) {
        printf("Hủy công bố file thất bại! Bạn không sở hữu file này.\n");
    } else if (resp.status == RESP_INVALID_TOKEN) {
        printf("Hủy công bố file thất bại! Token không hợp lệ.\n");
    } else {
        printf("Hủy công bố file thất bại! (Status: %d)\n", resp.status);
    }
}

// Đăng xuất
void logout_user(void) {
    LogoutRequest req;
    LogoutResponse resp;
    
    memset(&req, 0, sizeof(LogoutRequest));
    
    req.header.command = CMD_LOGOUT;
    req.header.request_id = generate_request_id();
    strcpy(req.email, current_email);
    strcpy(req.access_token, current_token);
    
    printf("[DEBUG] Sending LOGOUT request (request_id: %u)\n", req.header.request_id);
    
    if (send(server_sock, &req, sizeof(LogoutRequest), 0) < 0) {
        perror("Send logout request failed");
        return;
    }
    
    if (recv_full(server_sock, &resp, sizeof(LogoutResponse)) <= 0) {
        perror("Receive logout response failed");
        return;
    }
    
    printf("[DEBUG] Received LOGOUT response (status: %d)\n", resp.status);
    
    // Clear token và email
    memset(current_token, 0, sizeof(current_token));
    memset(current_email, 0, sizeof(current_email));
    memset(current_username, 0, sizeof(current_username));
}

// Báo cáo trạng thái download
void report_download_status(const char* filehash, int success) {
    DownloadStatusRequest req;
    DownloadStatusResponse resp;
    
    memset(&req, 0, sizeof(DownloadStatusRequest));
    
    req.header.command = CMD_DOWNLOAD_STATUS;
    req.header.request_id = generate_request_id();
    strcpy(req.email, current_email);
    strcpy(req.access_token, current_token);
    strcpy(req.filehash, filehash);
    req.download_success = success;
    
    printf("[DEBUG] Sending DOWNLOAD_STATUS request (request_id: %u)\n", req.header.request_id);
    printf("        Status: %s\n", success ? "SUCCESS" : "FAILED");
    
    if (send(server_sock, &req, sizeof(DownloadStatusRequest), 0) < 0) {
        perror("Send download status request failed");
        return;
    }
    
    if (recv_full(server_sock, &resp, sizeof(DownloadStatusResponse)) <= 0) {
        perror("Receive download status response failed");
        return;
    }
    
    printf("[DEBUG] Download status reported to server (response status: %d)\n", resp.status);
}