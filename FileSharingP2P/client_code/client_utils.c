#include "client_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <openssl/evp.h>
#include <unistd.h>

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

int get_local_ip_via_connect(char* buffer, size_t buflen) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return 0;
    }
    
    // Connect to Google DNS (no actual data sent)
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr("8.8.8.8");
    serv.sin_port = htons(53);
    
    if (connect(sock, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        close(sock);
        return 0;
    }
    
    // Get local address that would be used for this connection
    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    if (getsockname(sock, (struct sockaddr*)&name, &namelen) < 0) {
        close(sock);
        return 0;
    }
    
    char* ip = inet_ntoa(name.sin_addr);
    strncpy(buffer, ip, buflen - 1);
    buffer[buflen - 1] = '\0';
    
    close(sock);
    
    // Don't use 127.0.0.1
    if (strcmp(buffer, "127.0.0.1") == 0) {
        return 0;
    }
    
    return 1;
}

// Method 2: Enhanced getifaddrs with better filtering
int get_local_ip_via_ifaddrs(char* buffer, size_t buflen) {
    struct ifaddrs *ifaddr, *ifa;
    int family;
    char best_ip[INET_ADDRSTRLEN] = "";
    int found_eth = 0;
    
    if (getifaddrs(&ifaddr) == -1) {
        return 0;
    }
    
    // Priority order: eth0 > wlan0 > other non-loopback
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
        
        family = ifa->ifa_addr->sa_family;
        
        if (family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr->sin_addr, ip, INET_ADDRSTRLEN);
            
            // Skip loopback and link-local
            if (strcmp(ip, "127.0.0.1") == 0 || 
                strncmp(ip, "169.254", 7) == 0 ||
                strncmp(ip, "127.", 4) == 0) {
                continue;
            }
            
            // Prioritize eth0 (Ethernet)
            if (strncmp(ifa->ifa_name, "eth", 3) == 0) {
                strncpy(buffer, ip, buflen - 1);
                buffer[buflen - 1] = '\0';
                found_eth = 1;
                break;
            }
            
            // Second priority: wlan (WiFi)
            if (!found_eth && strncmp(ifa->ifa_name, "wlan", 4) == 0) {
                strncpy(buffer, ip, buflen - 1);
                buffer[buflen - 1] = '\0';
                continue;
            }
            
            // Keep first valid IP as fallback
            if (best_ip[0] == '\0') {
                strncpy(best_ip, ip, INET_ADDRSTRLEN - 1);
            }
        }
    }
    
    freeifaddrs(ifaddr);
    
    // If found eth0, return
    if (found_eth) {
        return 1;
    }
    
    // Use fallback
    if (buffer[0] != '\0') {
        return 1;
    }
    
    if (best_ip[0] != '\0') {
        strncpy(buffer, best_ip, buflen - 1);
        buffer[buflen - 1] = '\0';
        return 1;
    }
    
    return 0;
}

// Combined method: Try connect method first (most reliable), fallback to ifaddrs
int get_local_ip(char* buffer, size_t buflen) {
    // Method 1: Use UDP connect trick (works best with WSL mirrored)
    if (get_local_ip_via_connect(buffer, buflen)) {
        printf("[DEBUG] Detected IP via connect: %s\n", buffer);
        return 1;
    }
    
    // Method 2: Fallback to ifaddrs with better filtering
    if (get_local_ip_via_ifaddrs(buffer, buflen)) {
        printf("[DEBUG] Detected IP via ifaddrs: %s\n", buffer);
        return 1;
    }
    
    return 0;
}