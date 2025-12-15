# Makefile cho ứng dụng P2P File Sharing

# Tên file thực thi
SERVER_EXEC = server
CLIENT_EXEC = client

# Cờ biên dịch chung
CFLAGS = -Wall -Wextra -std=c99

# Cờ liên kết (thư viện)
# -pthread: Đa luồng cho cả Server và Client
# -lssl -lcrypto: Thư viện OpenSSL để tính SHA256 (chỉ cần cho Client)
LDFLAGS_SERVER = -pthread
LDFLAGS_CLIENT = -pthread -lssl -lcrypto 

# ----------------------------------------------------------------
# Cấu hình Server
# ----------------------------------------------------------------

SERVER_SRCS = server_code/server.c server_code/data_manager.c
SERVER_OBJS = $(SERVER_SRCS:.c=.o)

# ----------------------------------------------------------------
# Cấu hình Client
# ----------------------------------------------------------------

CLIENT_SRCS = client_code/client.c client_code/client_cs_protocol.c client_code/client_p2p_protocol.c client_code/client_utils.c
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

# ----------------------------------------------------------------
# Mục tiêu chính (Targets)
# ----------------------------------------------------------------

.PHONY: all clean run server client

# Mục tiêu mặc định: Biên dịch cả Server và Client
all: $(SERVER_EXEC) $(CLIENT_EXEC)
	@echo "--- Biên dịch hoàn tất ---"

# Mục tiêu biên dịch Server
$(SERVER_EXEC): $(SERVER_OBJS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS_SERVER) -o $@
	@echo "Server đã được biên dịch thành công."

# Mục tiêu biên dịch Client
$(CLIENT_EXEC): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS_CLIENT) -o $@
	@echo "Client đã được biên dịch thành công."

# ----------------------------------------------------------------
# Phụ thuộc (Dependencies)
# ----------------------------------------------------------------

# Tệp .o phụ thuộc vào tệp .c và tệp .h liên quan
server.o: server.c data_manager.h protocol.h
data_manager.o: data_manager.c data_manager.h protocol.h

client.o: client.c client_utils.h client_cs_protocol.h client_p2p_protocol.h protocol.h
client_utils.o: client_utils.c client_utils.h protocol.h
client_cs_protocol.o: client_cs_protocol.c client_cs_protocol.h client_utils.h protocol.h
client_p2p_protocol.o: client_p2p_protocol.c client_p2p_protocol.h client_utils.h client_cs_protocol.h protocol.h

# ----------------------------------------------------------------
# Mục tiêu tiện ích
# ----------------------------------------------------------------

# Xóa các tệp biên dịch
clean:
	clear
	rm -f $(SERVER_EXEC) $(CLIENT_EXEC) *.o
	rm -f server_code/*.o client_code/*.o
	rm -f shared_files.txt connected_users.txt
	@echo "Đã xóa các file biên dịch và file dữ liệu (.o, server, client)."

# Chạy Server
run_server: $(SERVER_EXEC)
	./$(SERVER_EXEC)

# Chạy Client
run_client: $(CLIENT_EXEC)
	./$(CLIENT_EXEC)