#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include "../protocol.h"
#include <pthread.h>

// Định nghĩa cấu trúc dữ liệu cần quản lý
typedef struct User {
    char email[MAX_EMAIL];
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    struct User* next;
} User;

typedef struct SharedFile {
    char filename[MAX_FILENAME];
    char filehash[MAX_HASH];
    char owner_email[MAX_EMAIL];
    long file_size;
    int chunk_size;
    struct SharedFile* next;
} SharedFile;

typedef struct ConnectedUser {
    char email[MAX_EMAIL];
    char ip[MAX_IP];
    int port;
    time_t connect_time;
    struct ConnectedUser* next;
} ConnectedUser;

typedef struct Session {
    char token[64];
    char email[MAX_EMAIL];
    time_t login_time;
    struct Session* next;
} Session;

// Các biến global (sẽ được định nghĩa trong data_manager.c)
extern User* users;
extern SharedFile* files;
extern Session* sessions;
extern ConnectedUser* connected_users;
extern pthread_mutex_t users_mutex;
extern pthread_mutex_t files_mutex;
extern pthread_mutex_t sessions_mutex;
extern pthread_mutex_t connected_users_mutex;

// --- Khai báo hàm Lưu/Tải ---
void load_data();
void save_users();
void save_shared_files();
void save_connected_users();

// --- Connected Users Management ---
void add_connected_user(const char* email, const char* ip, int port);
void remove_connected_user(const char* email);
void cleanup_disconnected_users();

// --- Khai báo các hàm Logic Server ---
int add_user(const char* email, const char* username, const char* password);
int authenticate(const char* email, const char* password);
void publish_file(const char* filename, const char* filehash, const char* owner_email, long file_size, int chunk_size);
int unpublish_file(const char* filehash, const char* owner_email);
SearchResponse browse_all_files(void);
SearchResponse search_files(const char* keyword);
FindResponse find_peers(const char* filehash);
void ensure_data_files_exist();
int get_username_by_email(const char* email, char* username_out);
int get_file_owner_info(const char* filehash, char* ip, int* port);
int is_user_already_connected(const char* email);


// --- Khai báo các hàm Session/Verification ---
char* create_session(const char* email);
int verify_token(const char* token, const char* email);
void destroy_session(const char* token);
int is_file_owner(const char* filehash, const char* email);
int validate_email(const char* email);
int validate_filename(const char* filename);

#endif