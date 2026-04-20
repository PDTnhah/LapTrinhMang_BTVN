#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_CLIENTS     64          // Số client tối đa đồng thời
#define BUFFER_SIZE     4096        // Kích thước buffer nhận dữ liệu
#define CMD_OUTPUT_FILE "out.txt"   // File tạm lưu kết quả lệnh shell
#define USER_DB_FILE    "users.txt" // File cơ sở dữ liệu tài khoản
#define MAX_USERNAME    64          // Độ dài tối đa tên đăng nhập
#define MAX_PASSWORD    64          // Độ dài tối đa mật khẩu

// Trạng thái xác thực của từng client
typedef enum {
    STATE_WAIT_USER,    // Đang chờ client gửi username
    STATE_WAIT_PASS,    // Đang chờ client gửi password
    STATE_LOGGED_IN,    // Đã đăng nhập thành công, nhận lệnh
    STATE_EMPTY         // Ô trống 
} ClientState;

// Thông tin của từng client
typedef struct {
    int     fd;                         // Socket descriptor
    ClientState state;                  // Trạng thái xác thực
    char    username[MAX_USERNAME];     // Tên đăng nhập đã nhập
} ClientInfo;

// Loại bỏ ký tự xuống dòng '\n' và '\r' ở cuối chuỗi
void strip_newline(char *s) {
    int len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

// Kiểm tra username/password trong file cơ sở dữ liệu users.txt
int check_credentials(const char *username, const char *password) {
    FILE *fp = fopen(USER_DB_FILE, "r");
    if (!fp) {
        // Không mở được file -> coi như không có tài khoản nào hợp lệ
        perror("fopen users.txt");
        return 0;
    }

    char line[256];
    char db_user[MAX_USERNAME];
    char db_pass[MAX_PASSWORD];

    // Đọc từng dòng trong file, mỗi dòng có dạng: <user> <pass>
    while (fgets(line, sizeof(line), fp)) {
        strip_newline(line);
        if (strlen(line) == 0) continue; // Bỏ qua dòng trống

        // Phân tách username và password trong dòng
        if (sscanf(line, "%63s %63s", db_user, db_pass) == 2) {
            // So sánh với thông tin client vừa gửi
            if (strcmp(username, db_user) == 0 &&
                strcmp(password, db_pass) == 0) {
                fclose(fp);
                return 1;
            }
        }
    }

    fclose(fp);
    return 0; 
}

// Gửi chuỗi thông báo đến client
void send_msg(int fd, const char *msg) {
    send(fd, msg, strlen(msg), 0);
}

// Thực thi lệnh shell và gửi kết quả về cho client
// Dùng hàm system() để chạy lệnh, chuyển hướng output sang file tạm, sau đó đọc file tạm và gửi nội dung về client.
void execute_and_send(int fd, const char *command) {
    char shell_cmd[BUFFER_SIZE + 64];

    // Tạo lệnh shell: <command> > out.txt
    snprintf(shell_cmd, sizeof(shell_cmd),
             "%s > %s", command, CMD_OUTPUT_FILE);

    // Thực thi lệnh, kết quả ghi vào CMD_OUTPUT_FILE
    int ret = system(shell_cmd);
    if (ret == -1) {
        send_msg(fd, "ERROR: Could not execute command.\r\n");
        return;
    }

    // Mở file kết quả và đọc nội dung
    FILE *fp = fopen(CMD_OUTPUT_FILE, "r");
    if (!fp) {
        send_msg(fd, "ERROR: Could not read command output.\r\n");
        return;
    }

    // Đọc từng đoạn và gửi về client
    char buf[BUFFER_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        send(fd, buf, n, 0);
    }
    fclose(fp);

    // Gửi dấu nhắc để client biết lệnh đã xong
    send_msg(fd, "\r\n$ ");
}

// Xử lý dữ liệu nhận được từ client tùy theo trạng thái xác thực
void handle_client_data(ClientInfo *client, char *data) {
    strip_newline(data); // Loại bỏ '\n', '\r' cuối chuỗi

    switch (client->state) {

        // Đang chờ nhận username 
        case STATE_WAIT_USER:
            // Lưu username client vừa gửi
            strncpy(client->username, data, MAX_USERNAME - 1);
            client->username[MAX_USERNAME - 1] = '\0';

            // Chuyển sang trạng thái chờ password
            client->state = STATE_WAIT_PASS;
            send_msg(client->fd, "Password: ");
            break;

        // Đang chờ nhận password 
        case STATE_WAIT_PASS: {
            char password[MAX_PASSWORD];
            strncpy(password, data, MAX_PASSWORD - 1);
            password[MAX_PASSWORD - 1] = '\0';

            // Kiểm tra tài khoản trong file users.txt
            if (check_credentials(client->username, password)) {
                // Đăng nhập thành công
                client->state = STATE_LOGGED_IN;
                printf("[INFO] Client fd=%d logged in as '%s'\n",
                       client->fd, client->username);

                send_msg(client->fd, "\r\nLogin successful! Welcome ");
                send_msg(client->fd, client->username);
                send_msg(client->fd, "\r\nType a command (e.g. ls, pwd) or 'exit' to quit.\r\n$ ");
            } else {
                // Sai thông tin -> thông báo lỗi và cho nhập lại từ đầu
                printf("[INFO] Client fd=%d failed login (user='%s')\n",
                       client->fd, client->username);

                send_msg(client->fd, "\r\nLogin failed: invalid username or password.\r\nUsername: ");
                client->state = STATE_WAIT_USER;
                memset(client->username, 0, sizeof(client->username));
            }
            break;
        }

        // Đã đăng nhập, nhận và thực thi lệnh 
        case STATE_LOGGED_IN:
            if (strlen(data) == 0) {
                // Lệnh rỗng -> chỉ gửi lại dấu nhắc
                send_msg(client->fd, "$ ");
                break;
            }

            // Nếu client gửi 'exit' -> đóng kết nối
            if (strcmp(data, "exit") == 0) {
                send_msg(client->fd, "Goodbye!\r\n");
                // Đặt fd = -1 để vòng lặp chính biết cần xóa client này
                close(client->fd);
                client->fd = -1;
                client->state = STATE_EMPTY;
                break;
            }

            printf("[CMD] Client '%s' (fd=%d) executed: %s\n",
                   client->username, client->fd, data);

            // Thực thi lệnh và gửi kết quả về client
            execute_and_send(client->fd, data);
            break;

        default:
            break;
    }
}

int main(int argc, char *argv[]) {
    // Kiểm tra tham số dòng lệnh
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return 1;
    }

    // Tạo socket lắng nghe
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("socket");
        return 1;
    }

    // Cho phép tái sử dụng địa chỉ ngay khi restart server
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Cấu hình địa chỉ server
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Lắng nghe mọi interface
    server_addr.sin_port        = htons(port);

    // Gắn socket với địa chỉ
    if (bind(listener, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(listener);
        return 1;
    }

    // Chuyển sang chế độ lắng nghe, hàng đợi tối đa 10 kết nối chờ
    if (listen(listener, 10) == -1) {
        perror("listen");
        close(listener);
        return 1;
    }

    printf("[SERVER] Telnet server started on port %d\n", port);
    printf("[SERVER] User database: %s\n", USER_DB_FILE);
    printf("[SERVER] Waiting for connections...\n");

    // Khởi tạo mảng quản lý các client
    ClientInfo clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd    = -1;          // -1 = ô trống
        clients[i].state = STATE_EMPTY;
        memset(clients[i].username, 0, MAX_USERNAME);
    }

    // Khởi tạo mảng pollfd để dùng với hàm poll()
    struct pollfd fds[MAX_CLIENTS + 1];

    fds[0].fd     = listener;
    fds[0].events = POLLIN;

    // Khởi tạo các ô client còn lại là trống (fd = -1)
    // poll() sẽ bỏ qua các phần tử có fd = -1
    for (int i = 1; i <= MAX_CLIENTS; i++) {
        fds[i].fd     = -1;
        fds[i].events = POLLIN;
    }

    // nfds: số phần tử thực sự đang dùng trong mảng fds
    int nfds = 1;

    char buf[BUFFER_SIZE];

    // Vòng lặp chính - dùng poll() để thăm dò sự kiện
    while (1) {
        // Chờ sự kiện xảy ra trên bất kỳ fd nào trong mảng fds
        int activity = poll(fds, nfds, -1);
        if (activity < 0) {
            if (errno == EINTR) continue;   // Bị ngắt bởi signal -> tiếp tục
            perror("poll");
            break;
        }

        // Kiểm tra sự kiện kết nối mới trên socket listener 
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);

            int new_fd = accept(listener,
                                (struct sockaddr *)&client_addr,
                                &addr_len);
            if (new_fd == -1) {
                perror("accept");
            } else {
                printf("[CONNECT] New client fd=%d from %s:%d\n",
                       new_fd,
                       inet_ntoa(client_addr.sin_addr),
                       ntohs(client_addr.sin_port));

                // Tìm ô trống trong mảng clients[] và fds[] để lưu client mới
                int placed = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == -1) {
                        // Lưu thông tin client
                        clients[i].fd    = new_fd;
                        clients[i].state = STATE_WAIT_USER;
                        memset(clients[i].username, 0, MAX_USERNAME);

                        // Gắn fd vào mảng fds 
                        fds[i + 1].fd     = new_fd;
                        fds[i + 1].events = POLLIN; 

                        // Cập nhật nfds nếu cần mở rộng phạm vi kiểm tra
                        if (i + 2 > nfds)
                            nfds = i + 2;

                        placed = 1;

                        // Yêu cầu client nhập username
                        send_msg(new_fd, "Welcome to Telnet Server\r\nUsername: ");
                        break;
                    }
                }

                // Nếu mảng đã đầy, từ chối kết nối
                if (!placed) {
                    send_msg(new_fd, "Server full. Try again later.\r\n");
                    close(new_fd);
                    printf("[WARN] Max clients reached, rejected fd=%d\n", new_fd);
                }
            }
        }

        // Kiểm tra sự kiện dữ liệu từ các client đang kết nối
        // fds[i+1] tương ứng với clients[i]
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd == -1) continue; // Ô trống, bỏ qua

            // Kiểm tra sự kiện đọc hoặc lỗi trên socket client
            if (fds[i + 1].revents & (POLLIN | POLLERR)) {
                int n = recv(clients[i].fd, buf, sizeof(buf) - 1, 0);

                if (n <= 0) {
                    if (n == 0)
                        printf("[DISCONNECT] Client fd=%d disconnected\n",
                               clients[i].fd);
                    else
                        perror("recv");

                    // Đóng socket và dọn dẹp
                    close(clients[i].fd);
                    clients[i].fd    = -1;
                    clients[i].state = STATE_EMPTY;
                    memset(clients[i].username, 0, MAX_USERNAME);

                    // Đặt fd = -1 trong fds[] để poll() bỏ qua ô này
                    fds[i + 1].fd = -1;

                } else {
                    // Có dữ liệu -> xử lý theo trạng thái xác thực
                    buf[n] = '\0';
                    handle_client_data(&clients[i], buf);

                    // Nếu handle đặt fd = -1 thì dọn dẹp ô fds[] tương ứng
                    if (clients[i].fd == -1) {
                        printf("[DISCONNECT] Client logged out\n");
                        clients[i].state = STATE_EMPTY;
                        memset(clients[i].username, 0, MAX_USERNAME);
                        fds[i + 1].fd = -1; // poll() sẽ bỏ qua ô này
                    }
                }
            }
        }
    } 

    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd > 0)
            close(clients[i].fd);
    close(listener);
    return 0;
}