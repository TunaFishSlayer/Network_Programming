#include "client_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <openssl/evp.h>

#define MAX_SHARED_DIR_PATH 64

char current_email[MAX_EMAIL];
char current_username[MAX_USERNAME];
char current_token[64];
char shared_dir[64] = "./files_to_share/";
int server_sock = -1;
int p2p_listening_port = 0;
char client_ip[MAX_IP];

// Simple request id generator (atomic via GCC builtin)
static uint32_t req_counter = 0;

uint32_t get_next_request_id(void) {
    return (uint32_t)__sync_add_and_fetch(&req_counter, 1);
}

// Tính SHA256 hash của file
void calculate_file_hash(const char* filepath, char* hash_output) {
    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        hash_output[0] = '\0';
        return;
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        fclose(fp);
        hash_output[0] = '\0';
        return;
    }

    EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
    
    unsigned char buffer[BUFFER_SIZE];
    int bytes_read;
    
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
        EVP_DigestUpdate(mdctx, buffer, bytes_read);
    }
    
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    
    EVP_DigestFinal_ex(mdctx, hash, &hash_len);
    EVP_MD_CTX_free(mdctx);
    fclose(fp);

    // Convert to hex string
    for (unsigned int i = 0; i < hash_len; i++) {
        sprintf(hash_output + (i * 2), "%02x", hash[i]);
    }
    hash_output[64] = '\0';
}

// Tính hash của một chunk 
void calculate_chunk_hash(const char* data, int size, char* hash_output) {
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        hash_output[0] = '\0';
        return;
    }

    EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(mdctx, data, size);
    
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    
    EVP_DigestFinal_ex(mdctx, hash, &hash_len);
    EVP_MD_CTX_free(mdctx);
    
    // Convert to hex string
    for (unsigned int i = 0; i < hash_len; i++) {
        sprintf(hash_output + (i * 2), "%02x", hash[i]);
    }
    hash_output[64] = '\0';
}

// Set timeout cho socket
int set_socket_timeout(int sock, int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}