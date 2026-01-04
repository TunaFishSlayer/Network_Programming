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
#define SERVER_PORT 18888

// Command codes
typedef enum {
    CMD_REGISTER = 1,
    CMD_LOGIN = 2,
    CMD_SEARCH = 3,
    CMD_FIND = 4,
    CMD_PUBLISH = 5,
    CMD_UNPUBLISH = 6,
    CMD_LOGOUT = 7,
    CMD_DOWNLOAD_STATUS = 8,
    CMD_BROWSE_FILES = 9       
} CommandCode;

// Response codes
typedef enum {
    RESP_SUCCESS = 100,
    RESP_FAIL = 101,
    RESP_USER_EXISTS = 102,
    RESP_INVALID_CRED = 103,
    RESP_NOT_FOUND = 104,
    RESP_INVALID_TOKEN = 105,
    RESP_UNAUTHORIZED = 106,
    RESP_FILE_NOT_OWNED = 107,
    RESP_INVALID_INPUT = 108,
    RESP_ALREADY_LOGGED_IN = 109
} ResponseCode;

// P2P Command codes
typedef enum {
    P2P_HANDSHAKE = 201,
    P2P_HANDSHAKE_RES = 202,
    P2P_BITMAP = 203,
    P2P_REQUEST_CHUNK = 204,
    P2P_CHUNK_DATA = 205,
    P2P_DISCONNECT = 206
} P2PCommand;

// Handshake status
typedef enum {
    HANDSHAKE_OK = 0,
    HANDSHAKE_NO_FILE = 1,
    HANDSHAKE_BUSY = 2
} HandshakeStatus;

// ============================================================================
// SEPARATED MESSAGE STRUCTURES FOR CLIENT-SERVER COMMUNICATION
// ============================================================================

// Common header for all messages
typedef struct {
    int command;
    uint32_t request_id;
} MessageHeader;

// --- REGISTER ---
typedef struct {
    MessageHeader header;
    char email[MAX_EMAIL];
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
} RegisterRequest;

typedef struct {
    MessageHeader header;
    int status;  // RESP_SUCCESS or RESP_USER_EXISTS
} RegisterResponse;

// --- LOGIN ---
typedef struct {
    MessageHeader header;
    char email[MAX_EMAIL];
    char password[MAX_PASSWORD];
    int port;  // P2P listening port
} LoginRequest;

typedef struct {
    MessageHeader header;
    int status;  // RESP_SUCCESS or RESP_INVALID_CRED
    char username[MAX_USERNAME];
    char access_token[64];
} LoginResponse;

// --- SEARCH ---
typedef struct {
    MessageHeader header;
    char email[MAX_EMAIL];
    char access_token[64];
    char keyword[MAX_FILENAME];
} SearchRequest;

typedef struct {
    char filename[MAX_FILENAME];
    char filehash[MAX_HASH];
    long file_size;
    int chunk_size;
} SearchFileInfo;

typedef struct {
    MessageHeader header;
    int status;  // RESP_SUCCESS or RESP_NOT_FOUND
    int count;
    SearchFileInfo files[100];
} SearchResponse;


// --- BROWSE FILES ---
typedef struct {
    MessageHeader header;
    char email[MAX_EMAIL];
    char access_token[64];
} BrowseFilesRequest;

typedef struct {
    MessageHeader header;
    int status;      // RESP_SUCCESS or RESP_INVALID_TOKEN
    int count;
    SearchFileInfo files[100];
} BrowseFilesResponse;

// --- FIND PEERS ---
typedef struct {
    MessageHeader header;
    char email[MAX_EMAIL];
    char access_token[64];
    char filehash[MAX_HASH];
} FindRequest;

typedef struct {
    char ip[MAX_IP];
    int port;
} PeerInfo;

typedef struct {
    MessageHeader header;
    int status;  // RESP_SUCCESS or RESP_NOT_FOUND
    int count;
    PeerInfo peers[50];
} FindResponse;

// --- PUBLISH ---
typedef struct {
    MessageHeader header;
    char email[MAX_EMAIL];
    char access_token[64];
    char filename[MAX_FILENAME];
    char filehash[MAX_HASH];
    char ip[MAX_IP];
    int port;
    long file_size;
    int chunk_size;
} PublishRequest;

typedef struct {
    MessageHeader header;
    int status;  // RESP_SUCCESS or error codes
} PublishResponse;

// --- UNPUBLISH ---
typedef struct {
    MessageHeader header;
    char email[MAX_EMAIL];
    char access_token[64];
    char filehash[MAX_HASH];
} UnpublishRequest;

typedef struct {
    MessageHeader header;
    int status;  // RESP_SUCCESS or error codes
} UnpublishResponse;

// --- LOGOUT ---
typedef struct {
    MessageHeader header;
    char email[MAX_EMAIL];
    char access_token[64];
} LogoutRequest;

typedef struct {
    MessageHeader header;
    int status;  // RESP_SUCCESS
} LogoutResponse;

// --- DOWNLOAD STATUS ---
typedef struct {
    MessageHeader header;
    char email[MAX_EMAIL];
    char access_token[64];
    char filehash[MAX_HASH];
    int download_success;  // 1 = success, 0 = failed
} DownloadStatusRequest;

typedef struct {
    MessageHeader header;
    int status;  // RESP_SUCCESS
} DownloadStatusResponse;

// ============================================================================
// P2P PROTOCOL STRUCTURES
// ============================================================================

// ---- HANDSHAKE ----
typedef struct {
    MessageHeader header;
    char filehash[MAX_HASH];
} P2PHandshakeReq;

typedef struct {
    MessageHeader header;
    int status; // HANDSHAKE_OK / HANDSHAKE_NO_FILE
} P2PHandshakeRes;

// ---- BITMAP ----
typedef struct {
    MessageHeader header;
    int total_chunks;
    int bitmap_size;
    char bitmap[];   // flexible array
} P2PBitmap;

// ---- REQUEST CHUNK ----
typedef struct {
    MessageHeader header;
    int chunk_index;
} P2PChunkRequest;

// ---- CHUNK HEADER ----
typedef struct {
    MessageHeader header;
    int chunk_index;
    int chunk_size;
} P2PChunkHeader;


#pragma pack(pop)

// ============================================================================
// HELPER FUNCTIONS FOR REQUEST ID GENERATION
// ============================================================================

static inline uint32_t generate_request_id(void) {
    static uint32_t counter = 0;
    return __sync_add_and_fetch(&counter, 1);
}

#endif