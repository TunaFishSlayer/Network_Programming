#ifndef SERIALIZE_HELPER_H
#define SERIALIZE_HELPER_H

#include "protocol.h"
#include <string.h>

#define FIND_RESPONSE_BUFFER_SIZE 1008  
// Serialize FindResponse thành byte array
static inline void serialize_find_response(const FindResponse* resp, char* buffer) {
    int offset = 0;
    
    // response_code (4 bytes)
    memcpy(buffer + offset, &resp->response_code, sizeof(int));
    offset += sizeof(int);
    
    // count (4 bytes)
    memcpy(buffer + offset, &resp->count, sizeof(int));
    offset += sizeof(int);
    
    // peers array - serialize từng peer
    for (int i = 0; i < 50; i++) {
        // ip (16 bytes - copy đúng MAX_IP bytes)
        memcpy(buffer + offset, resp->peers[i].ip, 16);
        offset += 16;
        
        // port (4 bytes)
        memcpy(buffer + offset, &resp->peers[i].port, sizeof(int));
        offset += sizeof(int);
    }
}

// Deserialize byte array thành FindResponse
static inline void deserialize_find_response(const char* buffer, FindResponse* resp) {
    int offset = 0;
    
    // response_code (4 bytes)
    memcpy(&resp->response_code, buffer + offset, sizeof(int));
    offset += sizeof(int);
    
    // count (4 bytes)
    memcpy(&resp->count, buffer + offset, sizeof(int));
    offset += sizeof(int);
    
    // peers array - deserialize từng peer
    for (int i = 0; i < 50; i++) {
        // ip (16 bytes)
        memcpy(resp->peers[i].ip, buffer + offset, 16);
        resp->peers[i].ip[15] = '\0';  // Đảm bảo null-terminated
        offset += 16;
        
        // port (4 bytes)
        memcpy(&resp->peers[i].port, buffer + offset, sizeof(int));
        offset += sizeof(int);
    }
}

#endif