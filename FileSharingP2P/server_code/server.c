#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <pthread.h>
#include "../protocol.h"
#include "data_manager.h"
#include "../serialize_helper.h"

void cleanup_client_connection(int sock) {
    if (sock >= 0) {
        close(sock);
    }
}

#include <inttypes.h>

// Map command/response code to human-readable name
static const char* cmd_name(int code) {
    switch (code) {
        case CMD_REGISTER: return "CMD_REGISTER";
        case CMD_LOGIN: return "CMD_LOGIN";
        case CMD_SEARCH: return "CMD_SEARCH";
        case CMD_FIND: return "CMD_FIND";
        case CMD_PUBLISH: return "CMD_PUBLISH";
        case CMD_UNPUBLISH: return "CMD_UNPUBLISH";
        case CMD_LOGOUT: return "CMD_LOGOUT";
        case CMD_DOWNLOAD_STATUS: return "CMD_DOWNLOAD_STATUS";
        case RESP_SUCCESS: return "RESP_SUCCESS";
        case RESP_USER_EXISTS: return "RESP_USER_EXISTS";
        case RESP_INVALID_CRED: return "RESP_INVALID_CRED";
        case RESP_NOT_FOUND: return "RESP_NOT_FOUND";
        case RESP_INVALID_TOKEN: return "RESP_INVALID_TOKEN";
        case RESP_UNAUTHORIZED: return "RESP_UNAUTHORIZED";
        case RESP_FILE_NOT_OWNED: return "RESP_FILE_NOT_OWNED";
        case RESP_INVALID_INPUT: return "RESP_INVALID_INPUT";
        default: return "UNKNOWN_CMD";
    }
}

// Print a Message struct (mask token) with direction (RECV/SEND)
static void print_message(const Message* m, const char* direction) {
    if (!m) return;
    printf("[%s] Message: cmd=%s\n",
           direction, cmd_name(m->command));
    if (m->email[0]) printf("  email: %s\n", m->email);
    if (m->username[0]) printf("  username: %s\n", m->username);
    if (m->access_token[0]) {
        printf("  access_token: %s\n", m->access_token);
    }
    if (m->filename[0]) printf("  filename: %s\n", m->filename);
    if (m->filehash[0]) printf("  filehash: %s\n", m->filehash);
    if (m->ip[0]) printf("  ip: %s\n", m->ip);
    if (m->port) printf("  port: %d\n", m->port);
    if (m->file_size) printf("  file_size: %ld\n", m->file_size);
    if (m->chunk_size) printf("  chunk_size: %d\n", m->chunk_size);
    if (m->status) printf("  status: %d\n", m->status);
    if (m->validation_checksum) printf("  validation_checksum: %u\n", m->validation_checksum);
}

static void print_search_response(const SearchResponse* sr) {
    if (!sr) return;
    printf("[PAYLOAD] SearchResponse: count=%d\n", sr->count);
    for (int i = 0; i < sr->count && i < 10; i++) {
        printf("  #%d: %s (hash:%s) size=%ld chunk=%d\n", i+1,
               sr->files[i].filename, sr->files[i].filehash,
               sr->files[i].file_size, sr->files[i].chunk_size);
    }
}

static void print_find_response(const FindResponse* fr) {
    if (!fr) return;
    printf("[PAYLOAD] FindResponse: count=%d\n", fr->count);
    for (int i = 0; i < fr->count && i < 10; i++) {
        printf("  #%d: peer %s:%d\n", i+1, fr->peers[i].ip, fr->peers[i].port);
    }
}

void* handle_client(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);
    
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    getpeername(client_sock, (struct sockaddr*)&client_addr, &addr_len);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);
    
    Message msg;
    int bytes_read;
    char current_email[MAX_EMAIL] = {0};
    
    struct timeval tv;
    tv.tv_sec = 300; 
    tv.tv_usec = 0;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    while (1) {
        // Read full Message struct with recv loop
        char* buf = (char*)&msg;
        int total = 0;
        int expected = (int)sizeof(Message);
        
        while (total < expected) {
            bytes_read = recv(client_sock, buf + total, expected - total, 0);
            if (bytes_read < 0) {
                // recv error or timeout
                printf("[ERROR] recv failed (timeout or error). Closing connection.\n");
                close(client_sock);
                return NULL;
            }
            if (bytes_read == 0) {
                // Client disconnected gracefully
                printf("[INFO] Client disconnected (recv returned 0).\n");
                close(client_sock);
                return NULL;
            }
            total += bytes_read;
        }
        
        // Full Message received; process it
        Message response;
        memset(&response, 0, sizeof(Message));
        print_message(&msg, "RECV");
        
        switch (msg.command) {
            case CMD_REGISTER:
                if (add_user(msg.email, msg.username, msg.password)) {
                    response.command = RESP_SUCCESS;
                    printf("[REGISTER] Success: %s (%s)\n", msg.username, msg.email);
                } else {
                    response.command = RESP_USER_EXISTS;
                    printf("[REGISTER] Failed: %s (email exists)\n", msg.email);
                }
                response.request_id = msg.request_id;
                response.original_request = msg.command;
                print_message(&response, "SEND");
                send(client_sock, &response, sizeof(Message), 0);
                break;
                
            case CMD_LOGIN:
                if (authenticate(msg.email, msg.password)) {
                    response.command = RESP_SUCCESS;
                    char* token = create_session(msg.email);
                    strcpy(response.access_token, token);
                    free(token);
                    
                    if (!get_username_by_email(msg.email, response.username)) {
                        strcpy(response.username, "Unknown");
                    }
                    
                    strncpy(current_email, msg.email, MAX_EMAIL - 1);
                    
                    int p2p_port = msg.port;
                    if (p2p_port == 0) {
                        p2p_port = client_port;
                    }
                    
                    add_connected_user(current_email, client_ip, p2p_port);
                    
                    printf("[LOGIN] Success: %s (%s) from %s:%d (P2P port: %d)\n", 
                        response.username, msg.email, client_ip, client_port, p2p_port);
                    save_users();
                } else {
                    response.command = RESP_INVALID_CRED;
                    printf("[LOGIN] Failed: %s\n", msg.email);
                }
                response.request_id = msg.request_id;
                response.original_request = msg.command;
                print_message(&response, "SEND");
                send(client_sock, &response, sizeof(Message), 0);
                break;
                
            case CMD_SEARCH: {
                if (!verify_token(msg.access_token, msg.email)) {
                    response.command = RESP_INVALID_TOKEN;
                    response.request_id = msg.request_id;
                    response.original_request = msg.command;
                    print_message(&response, "SEND");
                    send(client_sock, &response, sizeof(Message), 0);
                    printf("[SEARCH] Failed: Invalid token for %s\n", msg.email);
                    break;
                }
                
                SearchResponse search_resp = search_files(msg.filename);
                // First send a small Message header echoing request id and result
                  response.request_id = msg.request_id;
                  response.original_request = msg.command;
                  response.command = (search_resp.count > 0) ? RESP_SUCCESS : RESP_NOT_FOUND;
                  print_message(&response, "SEND");
                  send(client_sock, &response, sizeof(Message), 0);

                  // Then send the SearchResponse payload
                  print_search_response(&search_resp);
                  send(client_sock, &search_resp, sizeof(SearchResponse), 0);
                  printf("[SEARCH] Keyword '%s': %d file(s) found\n", 
                      msg.filename, search_resp.count);
                break;
            }
            
            case CMD_FIND: {
                if (!verify_token(msg.access_token, msg.email)) {
                    response.command = RESP_INVALID_TOKEN;
                    response.request_id = msg.request_id;
                    response.original_request = msg.command;
                    print_message(&response, "SEND");
                    send(client_sock, &response, sizeof(Message), 0);
                    printf("[FIND] Failed: Invalid token for %s\n", msg.email);
                    break;
                }
                
                FindResponse find_resp = find_peers(msg.filehash);
                if (find_resp.count > 0) {
                    printf("DEBUG SERVER: First peer - IP: %s, Port: %d\n",
                        find_resp.peers[0].ip, find_resp.peers[0].port);
                }
                
                // First send a Message header with request id and response code
                response.request_id = msg.request_id;
                response.original_request = msg.command;
                response.command = (find_resp.count > 0) ? RESP_SUCCESS : RESP_NOT_FOUND;
                print_message(&response, "SEND");
                send(client_sock, &response, sizeof(Message), 0);

                // Serialize payload and send
                char buffer[FIND_RESPONSE_BUFFER_SIZE];
                memset(buffer, 0, FIND_RESPONSE_BUFFER_SIZE);
                serialize_find_response(&find_resp, buffer);
                // Print structured find response
                print_find_response(&find_resp);
                (void)send(client_sock, buffer, FIND_RESPONSE_BUFFER_SIZE, 0);

                printf("[FIND] Hash '%.16s...': %d peer(s) found\n", 
                    msg.filehash, find_resp.count);
                break;
            }
                
            case CMD_PUBLISH:
                if (!verify_token(msg.access_token, msg.email)) {
                    response.command = RESP_INVALID_TOKEN;
                    response.request_id = msg.request_id;
                    response.original_request = msg.command;
                    print_message(&response, "SEND");
                    send(client_sock, &response, sizeof(Message), 0);
                    printf("[PUBLISH] Failed: Invalid token for %s\n", msg.email);
                    break;
                }
                
                // Validate input
                if (!validate_filename(msg.filename)) {
                    response.command = RESP_INVALID_INPUT;
                    response.request_id = msg.request_id;
                    response.original_request = msg.command;
                    print_message(&response, "SEND");
                    send(client_sock, &response, sizeof(Message), 0);
                    printf("[PUBLISH] Failed: Invalid filename for %s\n", msg.email);
                    break;
                }
                
                publish_file(msg.filename, msg.filehash, msg.email, 
                        msg.file_size, msg.chunk_size);
                
                response.command = RESP_SUCCESS;
                response.request_id = msg.request_id;
                response.original_request = msg.command;
                print_message(&response, "SEND");
                send(client_sock, &response, sizeof(Message), 0);
                
                printf("[PUBLISH] File: %s (hash: %.16s...) by %s\n", 
                    msg.filename, msg.filehash, msg.email);
                break;
                
            case CMD_UNPUBLISH:
                if (!verify_token(msg.access_token, msg.email)) {
                    response.command = RESP_INVALID_TOKEN;
                    response.request_id = msg.request_id;
                    response.original_request = msg.command;
                    print_message(&response, "SEND");
                    send(client_sock, &response, sizeof(Message), 0);
                    printf("[UNPUBLISH] Failed: Invalid token for %s\n", msg.email);
                    break;
                }
                
                // Check if user owns the file
                if (!is_file_owner(msg.filehash, msg.email)) {
                    response.command = RESP_FILE_NOT_OWNED;
                    response.request_id = msg.request_id;
                    response.original_request = msg.command;
                    print_message(&response, "SEND");
                    send(client_sock, &response, sizeof(Message), 0);
                    printf("[UNPUBLISH] Failed: File %s not owned by %s\n", 
                           msg.filehash, msg.email);
                    break;
                }
                
                unpublish_file(msg.filehash, msg.email);
                response.command = RESP_SUCCESS;
                response.request_id = msg.request_id;
                response.original_request = msg.command;
                print_message(&response, "SEND");
                send(client_sock, &response, sizeof(Message), 0);
                printf("[UNPUBLISH] Hash %s by %s\n", msg.filehash, msg.email);
                break;
                
            case CMD_LOGOUT:
                destroy_session(msg.access_token);
                printf("[LOGOUT] %s\n", msg.email);
                response.command = RESP_SUCCESS;
                response.request_id = msg.request_id;
                response.original_request = msg.command;
                print_message(&response, "SEND");
                send(client_sock, &response, sizeof(Message), 0);
                close(client_sock);
                return NULL;
                
            case CMD_DOWNLOAD_STATUS:
                if (!verify_token(msg.access_token, msg.email)) {
                    response.command = RESP_INVALID_TOKEN;
                    response.request_id = msg.request_id;
                    response.original_request = msg.command;
                    print_message(&response, "SEND");
                    send(client_sock, &response, sizeof(Message), 0);
                    break;
                }

                response.command = RESP_SUCCESS;
                response.request_id = msg.request_id;
                response.original_request = msg.command;
                print_message(&response, "SEND");
                send(client_sock, &response, sizeof(Message), 0);
                
                if (msg.status == 1) {
                    printf("[DOWNLOAD_STATUS] Success: %s downloaded file (hash: %s)\n", 
                           msg.email, msg.filehash);
                } else {
                    printf("[DOWNLOAD_STATUS] Failed: %s failed to download file (hash: %s)\n", 
                           msg.email, msg.filehash);
                }
                break;
        }
    }
    
    return NULL;
}

int main() {
    int server_sock;  
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    printf("=== P2P File Sharing Server ===\n");
    printf("Initializing...\n\n");
    
    // TẢI DỮ LIỆU KHI KHỞI ĐỘNG
    load_data(); 
    
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    
    if (listen(server_sock, 10) < 0) {
        perror("Listen failed");
        exit(1);
    }
    
    printf("Server running on port %d...\n", SERVER_PORT);
    printf("Waiting for connections...\n\n");
    
    while (1) {
        int* client_sock = malloc(sizeof(int));  
        *client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
        
        if (*client_sock < 0) {
            perror("Accept failed");
            free(client_sock);
            continue;
        }
        
        printf("[CONNECT] New connection from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), 
               ntohs(client_addr.sin_port));
        
        pthread_t thread_id;  
        if (pthread_create(&thread_id, NULL, handle_client, client_sock) != 0) {
            perror("Could not create thread");
            close(*client_sock);  
            free(client_sock);
            continue;
        }
        pthread_detach(thread_id);
    }
    
    close(server_sock);
    return 0;
}