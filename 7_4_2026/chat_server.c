#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAX_CLIENTS 64
#define BUF_SIZE 1024
#define MAX_ID_LEN   64
#define MAX_NAME_LEN 128

// Trạng thái của mỗi client
#define STATE_REGISTERING 0 
#define STATE_CHATTING    1 

// Cấu trúc lưu thông tin từng client
typedef struct {
    int  fd;                    // File descriptor của client, -1 nếu slot trống
    int  state;                 // Trạng thái: REGISTERING hoặc CHATTING
    char id[MAX_ID_LEN];        // client_id
    char name[MAX_NAME_LEN];    // client_name
    char addr_str[INET_ADDRSTRLEN]; // Địa chỉ IP của client
} ClientInfo;

// Lấy timestamp hiện tại dưới dạng chuỗi "yyyy/mm/dd HH:MM:SS"
void get_timestamp(char *buf, size_t len) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(buf, len, "%Y/%m/%d %H:%M:%S", tm_info);
}

void trim_crlf(char *s) {
    int n = strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == '\n'))
        s[--n] = '\0';
}

// Kiểm tra xem client_id đã tồn tại trong danh sách hay chưa
int id_exists(ClientInfo clients[], int max, const char *id) {
    for (int i = 0; i < max; i++) {
        if (clients[i].fd != -1 &&
            clients[i].state == STATE_CHATTING &&
            strcmp(clients[i].id, id) == 0)
            return 1;
    }
    return 0;
}

// Gửi dữ liệu đến tất cả client đang ở trạng thái CHATTING, trừ client có fd = skip_fd
void broadcast(ClientInfo clients[], int max, int skip_fd, const char *msg) {
    int msg_len = strlen(msg);
    for (int i = 0; i < max; i++) {
        if (clients[i].fd != -1 &&
            clients[i].state == STATE_CHATTING &&
            clients[i].fd != skip_fd) {
            send(clients[i].fd, msg, msg_len, 0);
        }
    }
}

// Ngắt kết nối và dọn dẹp thông tin của một client
void remove_client(ClientInfo clients[], int max, int fd) {
    for (int i = 0; i < max; i++) {
        if (clients[i].fd == fd) {
            // Nếu client đã đăng ký, thông báo cho các client khác
            if (clients[i].state == STATE_CHATTING) {
                char ts[24];
                get_timestamp(ts, sizeof(ts));
                char notice[BUF_SIZE];
                snprintf(notice, sizeof(notice),
                         "[%s] *** %s (%s) đã ngắt kết nối ***\n",
                         ts, clients[i].name, clients[i].id);
                printf("%s", notice);
                broadcast(clients, max, fd, notice);
            }
            close(fd);
            clients[i].fd    = -1;
            clients[i].state = STATE_REGISTERING;
            clients[i].id[0] = '\0';
            clients[i].name[0] = '\0';
            break;
        }
    }
}

// Xử lý dữ liệu nhận từ client ở trạng thái REGISTERING
int handle_register(ClientInfo *client, ClientInfo clients[], int max, char *data) {
    trim_crlf(data);

    // Tìm dấu ": " phân cách id và name
    char *sep = strstr(data, ": ");
    if (sep == NULL) {
        // Sai cú pháp
        const char *err = "Sai cú pháp. Vui lòng nhập theo dạng: client_id: client_name\n";
        send(client->fd, err, strlen(err), 0);
        return 0;
    }

    // Tách id và name
    int id_len = sep - data;
    if (id_len == 0 || id_len >= MAX_ID_LEN) {
        const char *err = "client_id không hợp lệ (rỗng hoặc quá dài).\n";
        send(client->fd, err, strlen(err), 0);
        return 0;
    }

    char new_id[MAX_ID_LEN];
    char new_name[MAX_NAME_LEN];

    strncpy(new_id, data, id_len);
    new_id[id_len] = '\0';

    char *name_start = sep + 2; // Bỏ qua ": "
    if (strlen(name_start) == 0 || strlen(name_start) >= MAX_NAME_LEN) {
        const char *err = "client_name không hợp lệ (rỗng hoặc quá dài).\n";
        send(client->fd, err, strlen(err), 0);
        return 0;
    }
    strncpy(new_name, name_start, MAX_NAME_LEN - 1);
    new_name[MAX_NAME_LEN - 1] = '\0';

    // Kiểm tra client_id đã bị dùng chưa
    if (id_exists(clients, max, new_id)) {
        char err[BUF_SIZE];
        snprintf(err, sizeof(err),
                 "client_id '%s' đã được sử dụng. Hãy chọn ID khác.\n", new_id);
        send(client->fd, err, strlen(err), 0);
        return 0;
    }

    // Đăng ký thành công
    strncpy(client->id,   new_id,   MAX_ID_LEN - 1);
    strncpy(client->name, new_name, MAX_NAME_LEN - 1);
    client->state = STATE_CHATTING;

    // Gửi xác nhận cho client vừa đăng ký
    char welcome[BUF_SIZE];
    snprintf(welcome, sizeof(welcome),
             "Chào mừng %s (%s) đã tham gia phòng chat!\n",
             client->name, client->id);
    send(client->fd, welcome, strlen(welcome), 0);

    // Thông báo cho các client khác
    char ts[24];
    get_timestamp(ts, sizeof(ts));
    char notice[BUF_SIZE];
    snprintf(notice, sizeof(notice),
             "[%s] *** %s (%s) đã tham gia phòng chat ***\n",
             ts, client->name, client->id);
    printf("%s", notice);
    broadcast(clients, max, client->fd, notice);

    return 1;
}

// Xử lý tin nhắn từ client đang ở trạng thái CHATTING
// Định dạng broadcast: "yyyy/mm/dd HH:MM:SS client_id: message"
void handle_chat(ClientInfo *client, ClientInfo clients[], int max, char *data) {
    trim_crlf(data);
    if (strlen(data) == 0) return; // Bỏ qua tin nhắn rỗng

    char ts[24];
    get_timestamp(ts, sizeof(ts));

    // Tạo thông điệp broadcast
    char msg[BUF_SIZE * 2];
    snprintf(msg, sizeof(msg), "[%s] %s: %s\n", ts, client->id, data);

    // In ra màn hình server để theo dõi
    printf("%s", msg);

    // Gửi đến tất cả client khác (không gửi lại cho người gửi)
    broadcast(clients, max, client->fd, msg);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Cách dùng: %s <port>\n", argv[0]);
        fprintf(stderr, "Ví dụ:     %s 9090\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Số cổng không hợp lệ: %s\n", argv[1]);
        return 1;
    }

    // Tạo socket lắng nghe kết nối 
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("socket() thất bại");
        return 1;
    }

    // Cho phép tái sử dụng địa chỉ ngay sau khi server khởi động lại
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Gắn socket với địa chỉ và cổng
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port        = htons(port);

    if (bind(listener, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind() thất bại");
        close(listener);
        return 1;
    }

    // Chuyển socket sang trạng thái chờ kết nối, hàng đợi tối đa 10 client
    if (listen(listener, 10) == -1) {
        perror("listen() thất bại");
        close(listener);
        return 1;
    }

    printf("Chat server đang chạy trên cổng %d...\n", port);
    printf("Đang chờ kết nối từ client...\n");

    //  Khởi tạo mảng quản lý client 
    ClientInfo clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd    = -1;   // -1 nghĩa là slot trống
        clients[i].state = STATE_REGISTERING;
        clients[i].id[0] = '\0';
        clients[i].name[0] = '\0';
    }

    // ---- Vòng lặp chính sử dụng select() ----
    fd_set fdread;          
    char buf[BUF_SIZE];

    while (1) {
        // Khởi tạo lại tập fdread mỗi vòng lặp vì select() thay đổi giá trị của nó
        FD_ZERO(&fdread);
        FD_SET(listener, &fdread); 
        int maxfd = listener;

        // Thêm tất cả socket client đang hoạt động vào tập fdread
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd != -1) {
                FD_SET(clients[i].fd, &fdread);
                if (clients[i].fd > maxfd)
                    maxfd = clients[i].fd;
            }
        }

        // Chờ đến khi có sự kiện xảy ra (timeout = NULL: chờ vô hạn)
        int ready = select(maxfd + 1, &fdread, NULL, NULL, NULL);
        if (ready == -1) {
            perror("select() thất bại");
            break;
        }

        // Kiểm tra nếu có yêu cầu kết nối mới 
        if (FD_ISSET(listener, &fdread)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int new_fd = accept(listener, (struct sockaddr *)&client_addr, &addr_len);

            if (new_fd == -1) {
                perror("accept() thất bại");
            } else {
                // Tìm slot trống trong mảng clients
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == -1) { slot = i; break; }
                }

                if (slot == -1) {
                    // Không còn chỗ trống, từ chối kết nối
                    const char *full_msg = "Server đã đầy. Vui lòng thử lại sau.\n";
                    send(new_fd, full_msg, strlen(full_msg), 0);
                    close(new_fd);
                    printf("Từ chối kết nối: server đã đạt giới hạn %d client.\n", MAX_CLIENTS);
                } else {
                    // Lưu thông tin client mới
                    clients[slot].fd    = new_fd;
                    clients[slot].state = STATE_REGISTERING;
                    clients[slot].id[0] = '\0';
                    clients[slot].name[0] = '\0';
                    inet_ntop(AF_INET, &client_addr.sin_addr,
                              clients[slot].addr_str, INET_ADDRSTRLEN);

                    printf("Kết nối mới từ %s (fd=%d), slot=%d\n",
                           clients[slot].addr_str, new_fd, slot);

                    // Yêu cầu client đăng ký
                    const char *prompt =
                        "Chào mừng đến phòng chat!\n"
                        "Vui lòng đăng ký theo cú pháp: client_id: client_name\n"
                        "Ví dụ: abc: NguyenVanA\n";
                    send(new_fd, prompt, strlen(prompt), 0);
                }
            }
        }

        //  Kiểm tra sự kiện từ các client đang kết nối 
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd == -1) continue;           // Slot trống
            if (!FD_ISSET(clients[i].fd, &fdread)) continue; // Không có sự kiện

            // Nhận dữ liệu từ client
            memset(buf, 0, sizeof(buf));
            int ret = recv(clients[i].fd, buf, sizeof(buf) - 1, 0);

            if (ret <= 0) {
                // ret == 0: client đóng kết nối bình thường
                // ret < 0:  lỗi mạng
                if (ret == 0)
                    printf("Client fd=%d đã ngắt kết nối.\n", clients[i].fd);
                else
                    printf("recv() lỗi với client fd=%d: %s\n",
                           clients[i].fd, strerror(errno));

                remove_client(clients, MAX_CLIENTS, clients[i].fd);

            } else {
                buf[ret] = '\0'; // Đảm bảo chuỗi kết thúc đúng

                if (clients[i].state == STATE_REGISTERING) {
                    // Client chưa đăng ký, xử lý đăng ký
                    handle_register(&clients[i], clients, MAX_CLIENTS, buf);

                } else {
                    // Client đã đăng ký, xử lý tin nhắn chat
                    handle_chat(&clients[i], clients, MAX_CLIENTS, buf);
                }
            }
        }
    } // end while

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1)
            close(clients[i].fd);
    }
    close(listener);
    printf("Server đã đóng.\n");

    return 0;
}