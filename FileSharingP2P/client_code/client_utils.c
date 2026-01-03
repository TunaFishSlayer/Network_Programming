#include "client_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <openssl/evp.h>

#define MAX_SHARED_DIR_PATH 64

char current_email[MAX_EMAIL];
char current_username[MAX_USERNAME];
char current_token[64];
char shared_dir[64] = "./files_to_share/";
int server_sock = -1;
int p2p_listening_port = 0;
char client_ip[MAX_IP];

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

#include <ifaddrs.h>

// Add this function to detect LAN IP automatically
int get_local_ip(char* buffer, size_t buflen) {
    struct ifaddrs *ifaddr, *ifa;
    int family;
    
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return 0;
    }
    
    // Look for non-loopback IPv4 address
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
        
        family = ifa->ifa_addr->sa_family;
        
        if (family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr->sin_addr, ip, INET_ADDRSTRLEN);
            
            // Skip loopback and link-local addresses
            if (strcmp(ip, "127.0.0.1") != 0 && strncmp(ip, "169.254", 7) != 0) {
                strncpy(buffer, ip, buflen - 1);
                buffer[buflen - 1] = '\0';
                freeifaddrs(ifaddr);
                return 1;
            }
        }
    }
    
    freeifaddrs(ifaddr);
    return 0;
}