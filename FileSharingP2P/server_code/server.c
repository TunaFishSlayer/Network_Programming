#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <pthread.h>
#include "../protocol.h"
#include "data_manager.h"

void cleanup_client_connection(int sock) {
    if (sock >= 0) {
        close(sock);
    }
}

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
        case CMD_BROWSE_FILES: return "CMD_BROWSE_FILES";
        case RESP_SUCCESS: return "RESP_SUCCESS";
        case RESP_USER_EXISTS: return "RESP_USER_EXISTS";
        case RESP_INVALID_CRED: return "RESP_INVALID_CRED";
        case RESP_NOT_FOUND: return "RESP_NOT_FOUND";
        case RESP_INVALID_TOKEN: return "RESP_INVALID_TOKEN";
        case RESP_UNAUTHORIZED: return "RESP_UNAUTHORIZED";
        case RESP_FILE_NOT_OWNED: return "RESP_FILE_NOT_OWNED";
        case RESP_INVALID_INPUT: return "RESP_INVALID_INPUT";
        case RESP_ALREADY_LOGGED_IN: return "RESP_ALREADY_LOGGED_IN"; 
        default: return "UNKNOWN_CMD";
    }
}

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

void* handle_client(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);
    
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    getpeername(client_sock, (struct sockaddr*)&client_addr, &addr_len);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);
    
    char current_email[MAX_EMAIL] = {0};
    
    struct timeval tv;
    tv.tv_sec = 300; 
    tv.tv_usec = 0;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    while (1) {
        // First, peek at the header to determine message type
        MessageHeader header;
        int bytes = recv(client_sock, &header, sizeof(MessageHeader), MSG_PEEK);
        
        if (bytes <= 0) {
            if (bytes == 0) {
                printf("[INFO] Client disconnected gracefully.\n");
            } else {
                printf("[ERROR] recv failed (timeout or error).\n");
            }
            break;
        }
        
        printf("\n[RECV] Command: %s (request_id: %u)\n", 
               cmd_name(header.command), header.request_id);
        
        switch (header.command) {
            case CMD_REGISTER: {
                RegisterRequest req;
                if (recv_full(client_sock, &req, sizeof(RegisterRequest)) <= 0) break;
                
                printf("  === REGISTER REQUEST ===\n");
                printf("  Email: %s\n", req.email);
                printf("  Username: %s\n", req.username);
                printf("  Request ID: %u\n", req.header.request_id);
                
                RegisterResponse resp;
                memset(&resp, 0, sizeof(RegisterResponse));
                resp.header.command = CMD_REGISTER;
                resp.header.request_id = req.header.request_id;
                
                if (add_user(req.email, req.username, req.password)) {
                    resp.status = RESP_SUCCESS;
                    printf("[REGISTER] Success: %s (%s)\n", req.username, req.email);
                } else {
                    resp.status = RESP_USER_EXISTS;
                    printf("[REGISTER] Failed: %s (email exists)\n", req.email);
                }
                
                printf("[SEND] Response: %s\n", cmd_name(resp.status));
                printf("  Status: %s\n", cmd_name(resp.status));
                printf("  Request ID: %u\n", resp.header.request_id);
                send(client_sock, &resp, sizeof(RegisterResponse), 0);
                break;
            }
            
            case CMD_LOGIN: {
                LoginRequest req;
                if (recv_full(client_sock, &req, sizeof(LoginRequest)) <= 0) break;
                
                printf("  === LOGIN REQUEST ===\n");
                printf("  Email: %s\n", req.email);
                printf("  Password: %s\n", req.password);
                printf("  P2P Port: %d\n", req.port);
                printf("  Request ID: %u\n", req.header.request_id);
                
                LoginResponse resp;
                memset(&resp, 0, sizeof(LoginResponse));
                resp.header.command = CMD_LOGIN;
                resp.header.request_id = req.header.request_id;
                
                // Check if user is already logged in
                if (is_user_already_connected(req.email)) {
                    resp.status = RESP_ALREADY_LOGGED_IN;
                    printf("[LOGIN] Failed: %s is already logged in from another location\n", req.email);
                    printf("[SEND] Response: %s\n", cmd_name(resp.status));
                    printf("  Status: %s\n", cmd_name(resp.status));
                    printf("  Request ID: %u\n", resp.header.request_id);
                    send(client_sock, &resp, sizeof(LoginResponse), 0);
                    break;
                }
                
                if (authenticate(req.email, req.password)) {
                    resp.status = RESP_SUCCESS;
                    char* token = create_session(req.email);
                    strcpy(resp.access_token, token);
                    free(token);
                    
                    if (!get_username_by_email(req.email, resp.username)) {
                        strcpy(resp.username, "Unknown");
                    }
                    
                    strncpy(current_email, req.email, MAX_EMAIL - 1);
                    
                    int p2p_port = req.port;
                    if (p2p_port == 0) {
                        p2p_port = client_port;
                    }
                    
                    add_connected_user(current_email, client_ip, p2p_port);
                    
                    printf("[LOGIN] Success: %s (%s) from %s:%d (P2P port: %d)\n", 
                        resp.username, req.email, client_ip, client_port, p2p_port);
                } else {
                    resp.status = RESP_INVALID_CRED;
                    printf("[LOGIN] Failed: %s (invalid credentials)\n", req.email);
                }
                
                printf("[SEND] Response: %s\n", cmd_name(resp.status));
                printf("  Status: %s\n", cmd_name(resp.status));
                printf("  Request ID: %u\n", resp.header.request_id);
                if (resp.status == RESP_SUCCESS) {
                    printf("  Username: %s\n", resp.username);
                    printf("  Access Token: %s\n", resp.access_token);
                }
                send(client_sock, &resp, sizeof(LoginResponse), 0);
                break;
            }
            
            case CMD_BROWSE_FILES: {
                BrowseFilesRequest req;
                if (recv_full(client_sock, &req, sizeof(BrowseFilesRequest)) <= 0) break;

                printf("  === BROWSE FILES REQUEST ===\n");
                printf("  Email: %s\n", req.email);
                printf("  Access Token: %s\n", req.access_token);

                BrowseFilesResponse resp;
                memset(&resp, 0, sizeof(BrowseFilesResponse));
                resp.header.command = CMD_BROWSE_FILES;
                resp.header.request_id = req.header.request_id;

                if (!verify_token(req.access_token, req.email)) {
                    resp.status = RESP_INVALID_TOKEN;
                    resp.count = 0;
                    printf("[BROWSE] Invalid token\n");
                } else {
                    SearchResponse data = browse_all_files();
                    resp.status = data.status;
                    resp.count = data.count;
                    memcpy(resp.files, data.files, sizeof(resp.files));

                    printf("[BROWSE] %d shared file(s)\n", resp.count);
                }

                send(client_sock, &resp, sizeof(BrowseFilesResponse), 0);
                break;
            }
            case CMD_SEARCH: {
                SearchRequest req;
                if (recv_full(client_sock, &req, sizeof(SearchRequest)) <= 0) break;
                
                printf("  === SEARCH REQUEST ===\n");
                printf("  Email: %s\n", req.email);
                printf("  Access Token: %s\n", req.access_token);
                printf("  Keyword: %s\n", req.keyword);
                printf("  Request ID: %u\n", req.header.request_id);
                
                SearchResponse resp;
                memset(&resp, 0, sizeof(SearchResponse));
                resp.header.command = CMD_SEARCH;
                resp.header.request_id = req.header.request_id;
                
                if (!verify_token(req.access_token, req.email)) {
                    resp.status = RESP_INVALID_TOKEN;
                    resp.count = 0;
                    printf("[SEARCH] Failed: Invalid token for %s\n", req.email);
                } else {
                    // Get search results from data manager
                    SearchResponse search_data = search_files(req.keyword);
                    resp.status = (search_data.count > 0) ? RESP_SUCCESS : RESP_NOT_FOUND;
                    resp.count = search_data.count;
                    memcpy(resp.files, search_data.files, sizeof(resp.files));
                    
                    printf("[SEARCH] Keyword '%s': %d file(s) found\n", 
                          req.keyword, resp.count);
                    
                    for (int i = 0; i < resp.count && i < 5; i++) {
                        printf("  #%d: %s (hash: %.16s...) size=%ld\n", 
                              i+1, resp.files[i].filename, 
                              resp.files[i].filehash, resp.files[i].file_size);
                    }
                }
                
                printf("[SEND] Response: %s\n", cmd_name(resp.status));
                printf("  Status: %s\n", cmd_name(resp.status));
                printf("  Request ID: %u\n", resp.header.request_id);
                if (resp.status == RESP_SUCCESS) {
                    printf("  Files Found: %d\n", resp.count);
                } else if (resp.status == RESP_INVALID_TOKEN) {
                    printf("  Error: Invalid access token\n");
                }
                printf("  Access Token: %s\n", req.access_token);
                send(client_sock, &resp, sizeof(SearchResponse), 0);
                break;
            }
            
            case CMD_FIND: {
                FindRequest req;
                if (recv_full(client_sock, &req, sizeof(FindRequest)) <= 0) break;
                
                printf("  === FIND PEERS REQUEST ===\n");
                printf("  Email: %s\n", req.email);
                printf("  Access Token: %s\n", req.access_token);
                printf("  Filehash: %.16s...\n", req.filehash);
                printf("  Request ID: %u\n", req.header.request_id);
                
                FindResponse resp;
                memset(&resp, 0, sizeof(FindResponse));
                resp.header.command = CMD_FIND;
                resp.header.request_id = req.header.request_id;
                
                if (!verify_token(req.access_token, req.email)) {
                    resp.status = RESP_INVALID_TOKEN;
                    resp.count = 0;
                    printf("[FIND] Failed: Invalid token for %s\n", req.email);
                } else {
                    // Get peer list from data manager
                    FindResponse find_data = find_peers(req.filehash);
                    resp.status = (find_data.count > 0) ? RESP_SUCCESS : RESP_NOT_FOUND;
                    resp.count = find_data.count;
                    memcpy(resp.peers, find_data.peers, sizeof(resp.peers));
                    
                    printf("[FIND] Hash '%.16s...': %d peer(s) found\n", 
                          req.filehash, resp.count);
                    
                    for (int i = 0; i < resp.count && i < 5; i++) {
                        printf("  #%d: peer %s:%d\n", 
                              i+1, resp.peers[i].ip, resp.peers[i].port);
                    }
                }
                
                printf("[SEND] Response: %s\n", cmd_name(resp.status));
                printf("  Status: %s\n", cmd_name(resp.status));
                printf("  Request ID: %u\n", resp.header.request_id);
                if (resp.status == RESP_SUCCESS) {
                    printf("  Peers Found: %d\n", resp.count);
                } else if (resp.status == RESP_INVALID_TOKEN) {
                    printf("  Error: Invalid access token\n");
                }
                printf("  Access Token: %s\n", req.access_token);
                send(client_sock, &resp, sizeof(FindResponse), 0);
                break;
            }
            
            case CMD_PUBLISH: {
                PublishRequest req;
                if (recv_full(client_sock, &req, sizeof(PublishRequest)) <= 0) break;
                
                printf("  === PUBLISH FILE REQUEST ===\n");
                printf("  Email: %s\n", req.email);
                printf("  Access Token: %s\n", req.access_token);
                printf("  Filename: %s\n", req.filename);
                printf("  Filehash: %.16s...\n", req.filehash);
                printf("  File Size: %ld bytes\n", req.file_size);
                printf("  Chunk Size: %d\n", req.chunk_size);
                printf("  Request ID: %u\n", req.header.request_id);
                
                PublishResponse resp;
                memset(&resp, 0, sizeof(PublishResponse));
                resp.header.command = CMD_PUBLISH;
                resp.header.request_id = req.header.request_id;
                
                if (!verify_token(req.access_token, req.email)) {
                    resp.status = RESP_INVALID_TOKEN;
                    printf("[PUBLISH] Failed: Invalid token for %s\n", req.email);
                } else if (!validate_filename(req.filename)) {
                    resp.status = RESP_INVALID_INPUT;
                    printf("[PUBLISH] Failed: Invalid filename for %s\n", req.email);
                } else {
                    publish_file(req.filename, req.filehash, req.email, 
                               req.file_size, req.chunk_size);
                    resp.status = RESP_SUCCESS;
                    printf("[PUBLISH] File: %s (hash: %.16s...) by %s\n", 
                          req.filename, req.filehash, req.email);
                }
                
                printf("[SEND] Response: %s\n", cmd_name(resp.status));
                printf("  Status: %s\n", cmd_name(resp.status));
                printf("  Request ID: %u\n", resp.header.request_id);
                send(client_sock, &resp, sizeof(PublishResponse), 0);
                break;
            }
            
            case CMD_UNPUBLISH: {
                UnpublishRequest req;
                if (recv_full(client_sock, &req, sizeof(UnpublishRequest)) <= 0) break;
                
                printf("  === UNPUBLISH FILE REQUEST ===\n");
                printf("  Email: %s\n", req.email);
                printf("  Access Token: %s\n", req.access_token);
                printf("  Filehash: %.16s...\n", req.filehash);
                printf("  Request ID: %u\n", req.header.request_id);
                
                UnpublishResponse resp;
                memset(&resp, 0, sizeof(UnpublishResponse));
                resp.header.command = CMD_UNPUBLISH;
                resp.header.request_id = req.header.request_id;
                
                if (!verify_token(req.access_token, req.email)) {
                    resp.status = RESP_INVALID_TOKEN;
                    printf("[UNPUBLISH] Failed: Invalid token for %s\n", req.email);
                } else if (!is_file_owner(req.filehash, req.email)) {
                    resp.status = RESP_FILE_NOT_OWNED;
                    printf("[UNPUBLISH] Failed: File %.16s... not owned by %s\n", 
                          req.filehash, req.email);
                } else {
                    unpublish_file(req.filehash, req.email);
                    resp.status = RESP_SUCCESS;
                    printf("[UNPUBLISH] Hash %.16s... by %s\n", 
                          req.filehash, req.email);
                }
                
                printf("[SEND] Response: %s\n", cmd_name(resp.status));
                printf("  Status: %s\n", cmd_name(resp.status));
                printf("  Request ID: %u\n", resp.header.request_id);
                send(client_sock, &resp, sizeof(UnpublishResponse), 0);
                break;
            }
            
            case CMD_LOGOUT: {
                LogoutRequest req;
                if (recv_full(client_sock, &req, sizeof(LogoutRequest)) <= 0) break;
                
                printf("  === LOGOUT REQUEST ===\n");
                printf("  Email: %s\n", req.email);
                printf("  Access Token: %s\n", req.access_token);
                printf("  Request ID: %u\n", req.header.request_id);
                
                destroy_session(req.access_token);
                remove_connected_user(req.email);  
                
                LogoutResponse resp;
                memset(&resp, 0, sizeof(LogoutResponse));
                resp.header.command = CMD_LOGOUT;
                resp.header.request_id = req.header.request_id;
                resp.status = RESP_SUCCESS;
                
                printf("[LOGOUT] %s\n", req.email);
                printf("[SEND] Response: %s\n", cmd_name(resp.status));
                printf("  Status: %s\n", cmd_name(resp.status));
                printf("  Request ID: %u\n", resp.header.request_id);
                send(client_sock, &resp, sizeof(LogoutResponse), 0);
                
                close(client_sock);
                return NULL;
            }
            
            case CMD_DOWNLOAD_STATUS: {
                DownloadStatusRequest req;
                if (recv_full(client_sock, &req, sizeof(DownloadStatusRequest)) <= 0) break;
                
                printf("  === DOWNLOAD STATUS REQUEST ===\n");
                printf("  Email: %s\n", req.email);
                printf("  Access Token: %s\n", req.access_token);
                printf("  Filehash: %.16s...\n", req.filehash);
                printf("  Download Success: %d\n", req.download_success);
                printf("  Request ID: %u\n", req.header.request_id);
                
                DownloadStatusResponse resp;
                memset(&resp, 0, sizeof(DownloadStatusResponse));
                resp.header.command = CMD_DOWNLOAD_STATUS;
                resp.header.request_id = req.header.request_id;
                
                if (!verify_token(req.access_token, req.email)) {
                    resp.status = RESP_INVALID_TOKEN;
                    printf("[DOWNLOAD_STATUS] Failed: Invalid token\n");
                } else {
                    resp.status = RESP_SUCCESS;
                    if (req.download_success == 1) {
                        printf("[DOWNLOAD_STATUS] Success: %s downloaded file (hash: %.16s...)\n", 
                              req.email, req.filehash);
                    } else {
                        printf("[DOWNLOAD_STATUS] Failed: %s failed to download file (hash: %.16s...)\n", 
                              req.email, req.filehash);
                    }
                }
                
                printf("[SEND] Response: %s\n", cmd_name(resp.status));
                printf("  Status: %s\n", cmd_name(resp.status));
                printf("  Request ID: %u\n", resp.header.request_id);
                send(client_sock, &resp, sizeof(DownloadStatusResponse), 0);
                break;
            }
            
            default:
                printf("[ERROR] Unknown command: %d\n", header.command);
                close(client_sock);
                return NULL;
        }
    }
    
    if (current_email[0] != '\0') {
        remove_connected_user(current_email);
    }
    
    close(client_sock);
    return NULL;
}

int main() {
    int server_sock;  
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    printf("=== P2P File Sharing Server ===\n");
    printf("Initializing...\n\n");
    
    // Load data on startup
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