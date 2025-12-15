#include "client_cs_protocol.h"
#include "client_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include "../serialize_helper.h"
#include <errno.h>

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
    Message msg, response;
    memset(&msg, 0, sizeof(Message));
    
    msg.command = CMD_REGISTER;
    strcpy(msg.email, email);
    strcpy(msg.username, username);
    strcpy(msg.password, password);
    
    send(server_sock, &msg, sizeof(Message), 0);
    recv(server_sock, &response, sizeof(Message), 0);
    
    return (response.command == RESP_SUCCESS);
}

// Đăng nhập với email
int login_user(const char* email, const char* password) {
    Message msg, response;
    memset(&msg, 0, sizeof(Message));
    
    msg.command = CMD_LOGIN;
    strcpy(msg.email, email);
    strcpy(msg.password, password);
    
    msg.port = p2p_listening_port;
    
    printf("[DEBUG] Logging in with P2P port: %d\n", p2p_listening_port);
    
    send(server_sock, &msg, sizeof(Message), 0);
    recv(server_sock, &response, sizeof(Message), 0);
    
    if (response.command == RESP_SUCCESS) {
        strcpy(current_email, email);
        strcpy(current_username, response.username);
        strcpy(current_token, response.access_token);
        printf("[DEBUG] Login successful. Server now knows P2P port: %d\n", p2p_listening_port);
        return 1;
    }
    return 0;
}

// Tìm kiếm file
SearchResponse search_file(const char* keyword) {
    Message msg, response;
    SearchResponse search_resp;
    memset(&msg, 0, sizeof(Message));
    memset(&response, 0, sizeof(Message));
    memset(&search_resp, 0, sizeof(SearchResponse));
    
    // Prepare search request
    msg.command = CMD_SEARCH;
    strcpy(msg.filename, keyword);
    strcpy(msg.email, current_email);
    strcpy(msg.access_token, current_token);
    
    printf("[DEBUG] Sending search request for: %s\n", keyword);
    
    // Send search request with error checking
    if (send(server_sock, &msg, sizeof(Message), 0) < 0) {
        perror("[ERROR] send search request failed");
        search_resp.count = 0;
        return search_resp;
    }
    
    // Set receive timeout
    struct timeval tv = {10, 0}; // 10 seconds timeout
    setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // 1. First receive the Message response
    printf("[DEBUG] Waiting for search response...\n");
    int bytes_received = recv(server_sock, &response, sizeof(Message), 0);
    if (bytes_received <= 0) {
        perror("[ERROR] receive search response failed");
       // printf("[DEBUG] Received %d bytes (expected %zu)\n", bytes_received, sizeof(Message));
        search_resp.count = 0;
        return search_resp;
    }
    
    printf("[DEBUG] Received response: command=%d, status=%d\n", response.command, response.status);
    
    // 2. Then receive the SearchResponse
    printf("[DEBUG] Receiving search results...\n");
    char* resp_ptr = (char*)&search_resp;
    int total_bytes = 0;
    int remaining = sizeof(SearchResponse);
    
    while (remaining > 0) {
        int bytes = recv(server_sock, resp_ptr + total_bytes, remaining, 0);
        if (bytes <= 0) {
            perror("[ERROR] receive search results failed");
            //printf("[DEBUG] Received %d/%zu bytes of search results\n",  total_bytes, sizeof(SearchResponse));
            search_resp.count = 0;
            return search_resp;
        }
        total_bytes += bytes;
        remaining -= bytes;
        //printf("[DEBUG] Received %d bytes of search results (%d/%zu total)\n", bytes, total_bytes, sizeof(SearchResponse));
    }
    printf("[DEBUG] Search completed. Found %d results\n", search_resp.count);
    return search_resp;
}

// Tìm peers có file hash cụ thể
FindResponse find_peers_for_file(const char* filehash) {
    Message msg, response;
    FindResponse find_resp;
    memset(&msg, 0, sizeof(Message));
    memset(&response, 0, sizeof(Message));
    memset(&find_resp, 0, sizeof(FindResponse));
    
    // Prepare find peers request
    msg.command = CMD_FIND;
    strcpy(msg.filehash, filehash);
    strcpy(msg.email, current_email);
    strcpy(msg.access_token, current_token);
    
    printf("[DEBUG] Sending find peers request for filehash: %s\n", filehash);
    
    // Send find peers request with error checking
    if (send(server_sock, &msg, sizeof(Message), 0) < 0) {
        perror("[ERROR] send find peers request failed");
        find_resp.count = 0;
        return find_resp;
    }
    
    // Set receive timeout
    struct timeval tv = {10, 0}; // 10 seconds timeout
    setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // 1. First receive the Message response
    printf("[DEBUG] Waiting for find peers response...\n");
    int bytes_received = recv(server_sock, &response, sizeof(Message), 0);
    if (bytes_received <= 0) {
        perror("[ERROR] receive find peers response failed");
        //printf("[DEBUG] Received %d bytes (expected %zu)\n", bytes_received, sizeof(Message));
        find_resp.count = 0;
        return find_resp;
    }
    
    printf("[DEBUG] Received response: command=%d, status=%d\n", 
          response.command, response.status);
    
    if (response.command != RESP_SUCCESS) {
        printf("[ERROR] Server returned error status: %d\n", response.status);
        find_resp.count = 0;
        return find_resp;
    }
    
    // 2. Then receive the FindResponse
    printf("[DEBUG] Receiving find peers results...\n");
    char buffer[4096]; // Adjust size as needed
    bytes_received = recv(server_sock, buffer, sizeof(buffer), 0);
    if (bytes_received <= 0) {
        perror("[ERROR] receive find peers results failed");
        find_resp.count = 0;
        return find_resp;
    }
    
    // Deserialize the response
    deserialize_find_response(buffer, &find_resp);
    printf("[DEBUG] Find peers completed. Found %d peers\n", find_resp.count);
    
    return find_resp;
}

// Công bố file với hash và chunk_size
void publish_file(const char* filename) {
    Message msg, response;
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
    
    memset(&msg, 0, sizeof(Message));
    msg.command = CMD_PUBLISH;
    strcpy(msg.filename, filename);
    strcpy(msg.filehash, filehash);
    strcpy(msg.email, current_email);
    strcpy(msg.access_token, current_token);  // Gửi token
    strcpy(msg.ip, "127.0.0.1");
    msg.port = p2p_listening_port;
    msg.file_size = st.st_size;
    msg.chunk_size = CHUNK_SIZE;
    
    send(server_sock, &msg, sizeof(Message), 0);
    recv(server_sock, &response, sizeof(Message), 0);
    
    if (response.command == RESP_SUCCESS) {
        printf("✓ Công bố file: %s\n", filename);
        printf("  Hash: %s\n", filehash);
        printf("  Size: %ld bytes\n", st.st_size);
    } else {
        printf("✗ Công bố file thất bại!\n");
    }
}

// Hủy công bố
void unpublish_file(const char* filename) {
    char filepath[MAX_FILEPATH];
    snprintf(filepath, sizeof(filepath), "%s%s", shared_dir, filename);
    
    char filehash[MAX_HASH];
    calculate_file_hash(filepath, filehash);
    
    if (filehash[0] == '\0') {
        printf("Không thể tính hash!\n");
        return;
    }
    
    Message msg, response;
    memset(&msg, 0, sizeof(Message));
    
    msg.command = CMD_UNPUBLISH;
    strcpy(msg.filehash, filehash);
    strcpy(msg.email, current_email);
    strcpy(msg.access_token, current_token);  // Gửi token
    
    send(server_sock, &msg, sizeof(Message), 0);
    recv(server_sock, &response, sizeof(Message), 0);
    
    if (response.command == RESP_SUCCESS) {
        printf("Hủy công bố file thành công!\n");
    } else {
        printf("Hủy công bố file thất bại!\n");
    }
}

// Đăng xuất
void logout_user(void) {
    Message msg, response;
    memset(&msg, 0, sizeof(Message));
    
    msg.command = CMD_LOGOUT;
    strcpy(msg.email, current_email);
    strcpy(msg.access_token, current_token);
    
    send(server_sock, &msg, sizeof(Message), 0);
    recv(server_sock, &response, sizeof(Message), 0);
    
    // Clear token và email
    memset(current_token, 0, sizeof(current_token));
    memset(current_email, 0, sizeof(current_email));
    memset(current_username, 0, sizeof(current_username));
}

// Báo cáo trạng thái download
void report_download_status(const char* filehash, int success) {
    Message msg, response;
    memset(&msg, 0, sizeof(Message));
    
    msg.command = CMD_DOWNLOAD_STATUS;
    strcpy(msg.filehash, filehash);
    strcpy(msg.email, current_email);
    strcpy(msg.access_token, current_token);
    msg.status = success ? 1 : 0;  // 1 = success, 0 = failed
    
    send(server_sock, &msg, sizeof(Message), 0);
    recv(server_sock, &response, sizeof(Message), 0);
    
    printf("DEBUG: Download status reported to server (status: %d)\n", success);
}