#ifndef CLIENT_CS_PROTOCOL_H
#define CLIENT_CS_PROTOCOL_H

#include "../protocol.h"

// Hàm Client-Server
int connect_to_server(const char* server_ip);
int register_user(const char* email, const char* username, const char* password);
int login_user(const char* email, const char* password);
SearchResponse search_file(const char* keyword);
FindResponse find_peers_for_file(const char* filehash);
void publish_file(const char* filename);
void unpublish_file(const char* filename);
void logout_user(void);
void report_download_status(const char* filehash, int success);

#endif