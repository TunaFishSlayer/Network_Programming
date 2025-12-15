#ifndef CLIENT_UTILS_H
#define CLIENT_UTILS_H

#include "../protocol.h"
#include <sys/time.h>
#include <openssl/sha.h>

// Khai báo các biến global được sử dụng ở nhiều nơi
extern char current_email[MAX_EMAIL];
extern char current_username[MAX_USERNAME];
extern char current_token[64];  // Token lưu sau login
extern char shared_dir[];
extern int server_sock;
extern int p2p_listening_port;
extern char client_ip[MAX_IP];

// Hàm tiện ích
void calculate_file_hash(const char* filepath, char* hash_output);
void calculate_chunk_hash(const char* data, int size, char* hash_output); // Chưa dùng, giữ lại cho tính toàn vẹn
int set_socket_timeout(int sock, int seconds);

// Request id generator for concurrent requests
uint32_t get_next_request_id(void);

#endif