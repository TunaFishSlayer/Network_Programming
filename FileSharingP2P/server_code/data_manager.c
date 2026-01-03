#include "data_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Định nghĩa file lưu trữ
#define USER_FILE "users.txt"
#define FILES_FILE "shared_files.txt"
#define CONNECTED_USERS_FILE "connected_users.txt"
#define SESSION_TIMEOUT 3600  // 1 giờ

// Định nghĩa các biến global
User* users = NULL;
SharedFile* files = NULL;
Session* sessions = NULL;
ConnectedUser* connected_users = NULL;
pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t files_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t connected_users_mutex = PTHREAD_MUTEX_INITIALIZER;

void ensure_data_files_exist() {
    FILE* fp;

    // Kiểm tra và tạo file users.txt nếu không tồn tại
    fp = fopen(USER_FILE, "r");
    if (!fp) {
        fp = fopen(USER_FILE, "w");
        if (fp) {
            fclose(fp);
        }
    } else {
        fclose(fp);
    }

    // Kiểm tra và tạo file shared_files.txt nếu không tồn tại
    fp = fopen(FILES_FILE, "r");
    if (!fp) {
        fp = fopen(FILES_FILE, "w");
        if (fp) {
            fclose(fp);
        }
    } else {
        fclose(fp);
    }
}

// ----------------------------------------------------------------
//                          CHỨC NĂNG LƯU DỮ LIỆU
// ----------------------------------------------------------------

void save_users() {
    pthread_mutex_lock(&users_mutex);
    FILE* fp = fopen(USER_FILE, "w");
    if (!fp) {
        perror("Lỗi khi mở users.txt để ghi");
        pthread_mutex_unlock(&users_mutex);
        return;
    }

    fprintf(fp, "email|username|password\n");

    User* current = users;
    while (current) {
        fprintf(fp, "%s|%s|%s\n", current->email, current->username, current->password);
        current = current->next;
    }

    fclose(fp);
    pthread_mutex_unlock(&users_mutex);
}

void save_shared_files() {
    FILE* fp = fopen(FILES_FILE, "w");
    if (!fp) return;
    
    pthread_mutex_lock(&files_mutex);
    
    fprintf(fp, "filename|filehash|email|filesize|chunksize\n");
    
    SharedFile* current = files;
    while (current) {
        fprintf(fp, "%s|%s|%s|%ld|%d\n", 
                current->filename, 
                current->filehash, 
                current->owner_email,
                current->file_size,
                current->chunk_size);
        current = current->next;
    }
    pthread_mutex_unlock(&files_mutex);
    
    fclose(fp);
}

void save_connected_users() {
    FILE* fp = fopen(CONNECTED_USERS_FILE, "w");
    if (!fp) {
        perror("Failed to save connected users");
        return;
    }
    
    pthread_mutex_lock(&connected_users_mutex);
    
    fprintf(fp, "email|ip|port|connecttime\n");  
    
    ConnectedUser* current = connected_users;
    while (current) {
        fprintf(fp, "%s|%s|%d|%ld\n", 
               current->email, 
               current->ip, 
               current->port, 
               current->connect_time);
        current = current->next;
    }
    pthread_mutex_unlock(&connected_users_mutex);
    fclose(fp);
}

// ----------------------------------------------------------------
//                          CHỨC NĂNG TẢI DỮ LIỆU
// ----------------------------------------------------------------

void load_users() {
    pthread_mutex_lock(&users_mutex);
    FILE* fp = fopen(USER_FILE, "r");
    if (!fp) {
        pthread_mutex_unlock(&users_mutex);
        return;
    }

    char line[MAX_EMAIL + MAX_USERNAME + MAX_PASSWORD + 5];
    int line_num = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        
        // Skip header row (first line)
        if (line_num == 1) {
            continue;
        }
        
        line[strcspn(line, "\n")] = 0;

        char* email = strtok(line, "|");
        char* username = strtok(NULL, "|");
        char* password = strtok(NULL, "|");

        if (email && username && password) {
            User* new_user = (User*)malloc(sizeof(User));
            strcpy(new_user->email, email);
            strcpy(new_user->username, username);
            strcpy(new_user->password, password);
            new_user->next = users;
            users = new_user;
        }
    }

    fclose(fp);
    pthread_mutex_unlock(&users_mutex);
}

void load_shared_files() {
    FILE* fp = fopen(FILES_FILE, "r");
    if (!fp) return;
    
    pthread_mutex_lock(&files_mutex);
    
    char line[2048];
    int line_num = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        
        if (line_num == 1) {
            continue;
        }
        
        char filename[MAX_FILENAME] = {0};
        char filehash[MAX_HASH] = {0};
        char email[MAX_EMAIL] = {0};
        long file_size = 0;
        int chunk_size = 0;
        
        if (sscanf(line, "%[^|]|%[^|]|%[^|]|%ld|%d", 
                  filename, filehash, email, &file_size, &chunk_size) == 5) {
            
            SharedFile* new_file = (SharedFile*)malloc(sizeof(SharedFile));
            if (new_file) {
                strncpy(new_file->filename, filename, MAX_FILENAME - 1);
                new_file->filename[MAX_FILENAME - 1] = '\0';
                strncpy(new_file->filehash, filehash, MAX_HASH - 1);
                new_file->filehash[MAX_HASH - 1] = '\0';
                strncpy(new_file->owner_email, email, MAX_EMAIL - 1);
                new_file->owner_email[MAX_EMAIL - 1] = '\0';
                new_file->file_size = file_size;
                new_file->chunk_size = chunk_size;
                new_file->next = files;
                files = new_file;
            }
        }
    }
    
    pthread_mutex_unlock(&files_mutex);
    fclose(fp);
}

void load_data() {
    load_users();
    load_shared_files();
    connected_users = NULL;
}

// ----------------------------------------------------------------
//                     QUẢN LÝ NGƯỜI DÙNG ĐANG KẾT NỐI
// ----------------------------------------------------------------

void add_connected_user(const char* email, const char* ip, int port) {
    if (!email || !ip) return;
    
    // First remove if user already exists (update case)
    remove_connected_user(email);
    
    // Create new connected user
    ConnectedUser* new_user = (ConnectedUser*)malloc(sizeof(ConnectedUser));
    if (!new_user) return;
    
    strncpy(new_user->email, email, MAX_EMAIL - 1);
    new_user->email[MAX_EMAIL - 1] = '\0';
    strncpy(new_user->ip, ip, MAX_IP - 1);
    new_user->ip[MAX_IP - 1] = '\0';
    new_user->port = port;
    new_user->connect_time = time(NULL);
    
    // Add to the beginning of the list
    pthread_mutex_lock(&connected_users_mutex);
    new_user->next = connected_users;
    connected_users = new_user;
    pthread_mutex_unlock(&connected_users_mutex);
    
    printf("[INFO] User %s connected from %s:%d\n", email, ip, port);
    save_connected_users();
}

void remove_connected_user(const char* email) {
    if (!email) return;
    
    pthread_mutex_lock(&connected_users_mutex);
    
    ConnectedUser* current = connected_users;
    ConnectedUser* prev = NULL;
    
    while (current) {
        if (strcmp(current->email, email) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                connected_users = current->next;
            }
            printf("[INFO] User %s disconnected\n", email);
            free(current);
            break;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&connected_users_mutex);
    save_connected_users();
}

void cleanup_disconnected_users() {
    time_t now = time(NULL);
    pthread_mutex_lock(&connected_users_mutex);
    
    ConnectedUser* current = connected_users;
    ConnectedUser* prev = NULL;
    
    while (current) {
        if (now - current->connect_time > 3600) { // 1 hour timeout
            printf("[INFO] Cleaning up stale connection for %s (last seen: %lds ago)\n", 
                  current->email, now - current->connect_time);
                  
            ConnectedUser* to_delete = current;
            if (prev) {
                prev->next = current->next;
                current = current->next;
            } else {
                connected_users = current->next;
                current = connected_users;
            }
            free(to_delete);
        } else {
            prev = current;
            current = current->next;
        }
    }
    
    pthread_mutex_unlock(&connected_users_mutex);
}

int is_user_already_connected(const char* email) {
    if (!email) return 0;
    
    pthread_mutex_lock(&connected_users_mutex);
    
    ConnectedUser* current = connected_users;
    while (current) {
        if (strcmp(current->email, email) == 0) {
            pthread_mutex_unlock(&connected_users_mutex);
            return 1;  // User is already connected
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&connected_users_mutex);
    return 0;  // User is not connected
}

// ----------------------------------------------------------------
//                          CHỨC NĂNG LOGIC SERVER
// ----------------------------------------------------------------

int get_username_by_email(const char* email, char* username_out) {
    pthread_mutex_lock(&users_mutex);
    
    User* current = users;
    while (current) {
        if (strcmp(current->email, email) == 0) {
            strcpy(username_out, current->username);
            pthread_mutex_unlock(&users_mutex);
            return 1;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&users_mutex);
    return 0;
}

int get_file_owner_info(const char* filehash, char* ip, int* port) {
    char owner_email[MAX_EMAIL] = {0};
    int found = 0;
    
    // First find the file to get owner's email
    pthread_mutex_lock(&files_mutex);
    SharedFile* current = files;
    while (current) {
        if (strcmp(current->filehash, filehash) == 0) {
            strncpy(owner_email, current->owner_email, MAX_EMAIL - 1);
            found = 1;
            break;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&files_mutex);
    
    if (!found) {
        return 0; // File not found
    }
    
    // Now find the owner in connected users
    pthread_mutex_lock(&connected_users_mutex);
    ConnectedUser* user = connected_users;
    while (user) {
        if (strcmp(user->email, owner_email) == 0) {
            strncpy(ip, user->ip, MAX_IP - 1);
            *port = user->port;
            pthread_mutex_unlock(&connected_users_mutex);
            return 1; 
        }
        user = user->next;
    }
    pthread_mutex_unlock(&connected_users_mutex);
    
    return 0; 
}

int add_user(const char* email, const char* username, const char* password) {
    pthread_mutex_lock(&users_mutex);
    
    User* current = users;
    while (current) {
        if (strcmp(current->email, email) == 0) {
            pthread_mutex_unlock(&users_mutex);
            return 0; 
        }
        current = current->next;
    }
    
    User* new_user = (User*)malloc(sizeof(User));
    strcpy(new_user->email, email);
    strcpy(new_user->username, username);
    strcpy(new_user->password, password);
    new_user->next = users;
    users = new_user;
    
    pthread_mutex_unlock(&users_mutex);
    save_users();
    return 1;
}

int authenticate(const char* email, const char* password) {
    pthread_mutex_lock(&users_mutex);
    
    User* current = users;
    while (current) {
        if (strcmp(current->email, email) == 0 &&
            strcmp(current->password, password) == 0) {
            pthread_mutex_unlock(&users_mutex);
            return 1;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&users_mutex);
    return 0;
}

void publish_file(const char* filename, const char* filehash, const char* owner_email,
                 long file_size, int chunk_size) {
    if (!filename || !filehash || !owner_email) {
        return;
    }
    pthread_mutex_lock(&files_mutex);
    
    // Check if file already exists with same hash and owner
    SharedFile* current = files;
    while (current) {
        if (strcmp(current->filehash, filehash) == 0 &&
            strcmp(current->owner_email, owner_email) == 0) {
            strncpy(current->filename, filename, MAX_FILENAME - 1);
            current->file_size = file_size;
            current->chunk_size = chunk_size;
            pthread_mutex_unlock(&files_mutex);
            save_shared_files();
            return;
        }
        current = current->next;
    }
    
    SharedFile* new_file = (SharedFile*)malloc(sizeof(SharedFile));
    if (!new_file) {
        pthread_mutex_unlock(&files_mutex);
        return;
    }
    
    strncpy(new_file->filename, filename, MAX_FILENAME - 1);
    strncpy(new_file->filehash, filehash, MAX_HASH - 1);
    strncpy(new_file->owner_email, owner_email, MAX_EMAIL - 1);
    new_file->file_size = file_size;
    new_file->chunk_size = chunk_size;
    
    // Add to the beginning of the list
    new_file->next = files;
    files = new_file;
    
    pthread_mutex_unlock(&files_mutex);
    save_shared_files();
}

int unpublish_file(const char* filehash, const char* owner_email) {
    pthread_mutex_lock(&files_mutex);
    
    SharedFile* current = files;
    SharedFile* prev = NULL;
    int file_removed = 0;
    
    while (current) {
        if (strcmp(current->filehash, filehash) == 0 &&
            strcmp(current->owner_email, owner_email) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                files = current->next;
            }
            SharedFile* to_delete = current;
            current = current->next;
            free(to_delete);
            file_removed = 1;
        } else {
            prev = current;
            current = current->next;
        }
    }
    
    pthread_mutex_unlock(&files_mutex);
    if (file_removed) {
        save_shared_files(); 
    }
    
    return file_removed;
}



char* create_session(const char* email) {
    pthread_mutex_lock(&sessions_mutex);
    
    static char token[64];
    snprintf(token, sizeof(token), "token_%ld_%s", time(NULL), email);
    
    Session* new_session = (Session*)malloc(sizeof(Session));
    strcpy(new_session->token, token);
    strcpy(new_session->email, email);
    new_session->login_time = time(NULL);
    new_session->next = sessions;
    sessions = new_session;
    
    pthread_mutex_unlock(&sessions_mutex);
    
    char* result = (char*)malloc(64);
    strcpy(result, token);
    return result;
}

int verify_token(const char* token, const char* email) {
    if (!token || strlen(token) == 0) {
        return 0;
    }
    
    pthread_mutex_lock(&sessions_mutex);
    
    Session* current = sessions;
    while (current) {
        if (strcmp(current->token, token) == 0 && 
            strcmp(current->email, email) == 0) {
            
            // Check if session expired
            time_t now = time(NULL);
            if (now - current->login_time > SESSION_TIMEOUT) {
                pthread_mutex_unlock(&sessions_mutex);
                return 0;
            }
            
            pthread_mutex_unlock(&sessions_mutex);
            return 1;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&sessions_mutex);
    return 0;
}

void destroy_session(const char* token) {
    pthread_mutex_lock(&sessions_mutex);
    
    Session* current = sessions;
    Session* prev = NULL;
    
    while (current) {
        if (strcmp(current->token, token) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                sessions = current->next;
            }
            free(current);
            break;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&sessions_mutex);
}

int is_file_owner(const char* filehash, const char* email) {
    pthread_mutex_lock(&files_mutex);
    
    SharedFile* current = files;
    while (current) {
        if (strcmp(current->filehash, filehash) == 0) {
            int is_owner = (strcmp(current->owner_email, email) == 0);
            pthread_mutex_unlock(&files_mutex);
            return is_owner;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&files_mutex);
    return 0;
}

int validate_email(const char* email) {
    if (!email || strlen(email) == 0 || strlen(email) >= MAX_EMAIL) {
        return 0;
    } 
    return strchr(email, '@') != NULL;
}

int validate_filename(const char* filename) {
    if (!filename || strlen(filename) == 0 || strlen(filename) >= MAX_FILENAME) {
        return 0;
    }
    
    if (strstr(filename, "..") != NULL || strchr(filename, '/') != NULL) {
        return 0;
    }
    
    return 1;
}

SearchResponse search_files(const char* keyword) {
    SearchResponse response;
    memset(&response, 0, sizeof(SearchResponse));
    
    // Initialize header fields (will be set by caller, but good practice)
    response.header.command = CMD_SEARCH;
    response.status = RESP_SUCCESS;
    response.count = 0;
    
    pthread_mutex_lock(&files_mutex);
    
    SharedFile* current = files;
    char seen_hashes[100][MAX_HASH];
    int seen_count = 0;
    
    while (current && response.count < 100) {
        if (strstr(current->filename, keyword) != NULL) {
            
            // Chỉ thêm hash nếu chưa thấy
            int already_added = 0;
            for (int i = 0; i < seen_count; i++) {
                if (strcmp(seen_hashes[i], current->filehash) == 0) {
                    already_added = 1;
                    break;
                }
            }
            
            if (!already_added) {
                strcpy(response.files[response.count].filename, current->filename);
                strcpy(response.files[response.count].filehash, current->filehash);
                response.files[response.count].file_size = current->file_size;
                response.files[response.count].chunk_size = current->chunk_size;
                
                strcpy(seen_hashes[seen_count], current->filehash);
                seen_count++;
                response.count++;
            }
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&files_mutex);
    
    if (response.count == 0) {
        response.status = RESP_NOT_FOUND;
    }
    
    return response;
}

SearchResponse browse_all_files(void) {
    SearchResponse response;
    memset(&response, 0, sizeof(SearchResponse));

    response.header.command = CMD_BROWSE_FILES;
    response.status = RESP_SUCCESS;
    response.count = 0;

    pthread_mutex_lock(&files_mutex);

    SharedFile* current = files;

    // Tránh trùng filehash
    char seen_hashes[100][MAX_HASH];
    int seen_count = 0;

    while (current && response.count < 100) {

        int already_added = 0;
        for (int i = 0; i < seen_count; i++) {
            if (strcmp(seen_hashes[i], current->filehash) == 0) {
                already_added = 1;
                break;
            }
        }

        if (!already_added) {
            strncpy(response.files[response.count].filename,
                    current->filename, MAX_FILENAME - 1);
            strncpy(response.files[response.count].filehash,
                    current->filehash, MAX_HASH - 1);
            response.files[response.count].file_size = current->file_size;
            response.files[response.count].chunk_size = current->chunk_size;

            strncpy(seen_hashes[seen_count],
                    current->filehash, MAX_HASH - 1);

            seen_count++;
            response.count++;
        }

        current = current->next;
    }

    pthread_mutex_unlock(&files_mutex);

    if (response.count == 0) {
        response.status = RESP_NOT_FOUND;
    }

    return response;
}

FindResponse find_peers(const char* filehash) {
    FindResponse resp;
    memset(&resp, 0, sizeof(FindResponse));
    
    // Initialize header fields
    resp.header.command = CMD_FIND;
    resp.status = RESP_SUCCESS;
    resp.count = 0;
    
    if (!filehash) {
        resp.status = RESP_NOT_FOUND;
        return resp;
    }

    char seen_peers[10][MAX_IP + 6] = {0}; 
    int seen_count = 0;
    
    char owner_emails[10][MAX_EMAIL] = {0};
    int owner_count = 0;
    
    pthread_mutex_lock(&files_mutex);
    SharedFile* current = files;
    
    while (current && owner_count < 10) {
        if (strcmp(current->filehash, filehash) == 0) {
            int already_added = 0;
            for (int i = 0; i < owner_count; i++) {
                if (strcmp(owner_emails[i], current->owner_email) == 0) {
                    already_added = 1;
                    break;
                }
            }
            
            if (!already_added) {
                strncpy(owner_emails[owner_count], current->owner_email, MAX_EMAIL - 1);
                owner_count++;
            }
        }
        current = current->next;
    }
    pthread_mutex_unlock(&files_mutex);  
    
    pthread_mutex_lock(&connected_users_mutex);
    for (int i = 0; i < owner_count && resp.count < 10; i++) {
        ConnectedUser* user = connected_users;
        while (user) {
            if (strcmp(user->email, owner_emails[i]) == 0) {
                char peer_key[MAX_IP + 6];
                snprintf(peer_key, sizeof(peer_key), "%s:%d", user->ip, user->port);
                
                int already_added = 0;
                for (int j = 0; j < seen_count; j++) {
                    if (strcmp(seen_peers[j], peer_key) == 0) {
                        already_added = 1;
                        break;
                    }
                }
                
                if (!already_added) {
                    strncpy(resp.peers[resp.count].ip, user->ip, MAX_IP - 1);
                    resp.peers[resp.count].port = user->port;
                    strncpy(seen_peers[seen_count], peer_key, sizeof(seen_peers[0]) - 1);
                    seen_count++;
                    resp.count++;
                }
                break;  
            }
            user = user->next;
        }
    }
    pthread_mutex_unlock(&connected_users_mutex);
    
    if (resp.count == 0) {
        resp.status = RESP_NOT_FOUND;
    }
    
    return resp;
}