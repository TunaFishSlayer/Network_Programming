#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

// Force packed structs for all compilers
#pragma pack(push, 1)

#define MAX_USERNAME 50
#define MAX_EMAIL 100
#define MAX_PASSWORD 50
#define MAX_FILENAME 256
#define MAX_FILEPATH 1024
#define MAX_IP 16
#define MAX_HASH 65  // SHA256 = 64 chars + null terminator
#define CHUNK_SIZE 524288  // 512KB per chunk
#define BUFFER_SIZE 4096
#define MAX_BITMAP_SIZE 10000
#define SERVER_PORT 8888

// Mã lệnh giao thức Client-Server
typedef enum {
    CMD_REGISTER = 1,
    CMD_LOGIN = 2,
    CMD_SEARCH = 3,
    CMD_FIND = 4,           
    CMD_PUBLISH = 5,
    CMD_UNPUBLISH = 6,
    CMD_LOGOUT = 7,
    CMD_DOWNLOAD_STATUS = 8  // Báo cáo trạng thái download
} CommandCode;

// Mã phản hồi
typedef enum {
    RESP_SUCCESS = 100,
    RESP_FAIL = 101,
    RESP_USER_EXISTS = 102,
    RESP_INVALID_CRED = 103,
    RESP_NOT_FOUND = 104,
    RESP_INVALID_TOKEN = 105,
    RESP_UNAUTHORIZED = 106,
    RESP_FILE_NOT_OWNED = 107,
    RESP_INVALID_INPUT = 108
} ResponseCode;

// Mã lệnh P2P (Client-Client)
typedef enum {
    P2P_HANDSHAKE = 201,
    P2P_HANDSHAKE_RES = 202,
    P2P_BITMAP = 203,
    P2P_REQUEST_CHUNK = 204,
    P2P_CHUNK_DATA = 205,
    P2P_DISCONNECT = 206
} P2PCommand;

// Status cho handshake
typedef enum {
    HANDSHAKE_OK = 0,
    HANDSHAKE_NO_FILE = 1,
    HANDSHAKE_BUSY = 2
} HandshakeStatus;

// Thông điệp chính Client-Server
typedef struct {
    int command;
    char email[MAX_EMAIL];
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    char filename[MAX_FILENAME];
    char filehash[MAX_HASH];
    char ip[MAX_IP];
    int port;
    long file_size;
    int chunk_size;
    char access_token[64];
    int status;
    uint32_t request_id;          // ID của request để map response trong chế độ bất đồng bộ
    int original_request;         // Server sẽ echo lại request command gốc
    uint32_t validation_checksum; // Để validate data integrity
} Message;

// Thông tin file trong kết quả tìm kiếm
typedef struct {
    char filename[MAX_FILENAME];
    char filehash[MAX_HASH];
    long file_size;
    int chunk_size;
} SearchFileInfo;

// Phản hồi tìm kiếm file
typedef struct {
    int response_code;
    int count;
    SearchFileInfo files[100];
} SearchResponse;

// Thông tin peer
typedef struct {
    char ip[MAX_IP];
    int port;
} PeerInfo;

// Phản hồi FIND - danh sách peers có file
typedef struct {
    int response_code;
    int count;
    PeerInfo peers[50];
} FindResponse;

// Thông điệp P2P
typedef struct {
    int command;
    char filehash[MAX_HASH];
    int status;
    int chunk_index;
    int chunk_size;
    char bitmap[MAX_BITMAP_SIZE];
    int bitmap_size;
} P2PMessage;

#pragma pack(pop)

#endif