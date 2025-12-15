#ifndef CLIENT_P2P_PROTOCOL_H
#define CLIENT_P2P_PROTOCOL_H

#include <pthread.h>

// Hàm P2P
int connect_to_peer_with_retry(const char* peer_ip, int peer_port);
int handshake_with_peer(int peer_sock, const char* filehash);
int download_file_chunked(const char* filehash, const char* filename, 
                         long file_size, int chunk_size);
void* handle_peer_download(void* arg);
void* p2p_server(void* arg);

#endif