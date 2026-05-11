#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define USERS_FILE  "users.txt"
#define OUTPUT_FILE "out.txt"
#define BUF_SIZE    4096
#define MAX_PENDING 10

// Đọc một dòng từ socket
static int recv_line(int fd, char *buf, int maxlen)
{
    int total = 0;
    char c;
    while (total < maxlen - 1) {
        int r = recv(fd, &c, 1, 0);
        if (r <= 0) return r;
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[total++] = c;
    }
    buf[total] = '\0';
    return total;
}

// Gửi chuỗi ký tự qua socket.
static void send_str(int fd, const char *msg)
{
    send(fd, msg, strlen(msg), 0);
}

// Kiểm tra username/password trong file users.txt
static int check_credentials(const char *username, const char *password)
{
    FILE *f = fopen(USERS_FILE, "r");
    if (!f) {
        perror("fopen(users.txt)");
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';

        char u[128] = {0}, p[128] = {0};
        if (sscanf(line, "%127s %127s", u, p) == 2) {
            if (strcmp(u, username) == 0 && strcmp(p, password) == 0) {
                fclose(f);
                return 1;
            }
        }
    }
    fclose(f);
    return 0;
}

// Thực thi lệnh, ghi kết quả vào out.txt, đọc và gửi về client.
static void execute_command(int client_fd, const char *cmd)
{
    // Tạo lệnh shell: <cmd> > out.txt 2>&1
    char shell_cmd[BUF_SIZE + 64];
    snprintf(shell_cmd, sizeof(shell_cmd), "%s > %s 2>&1", cmd, OUTPUT_FILE);
    system(shell_cmd);

    // Đọc kết quả từ file và gửi cho client
    FILE *f = fopen(OUTPUT_FILE, "r");
    if (!f) {
        send_str(client_fd, "(Không có kết quả)\r\n");
        return;
    }

    char buf[BUF_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        send(client_fd, buf, (int)n, 0);

    fclose(f);
    send_str(client_fd, "\r\n");
}

// Xử lý một client (chạy trong tiến trình con).
static void handle_client(int client_fd, struct sockaddr_in *cli_addr)
{
    char username[128] = {0};
    char password[128] = {0};
    char buf[BUF_SIZE];

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli_addr->sin_addr, client_ip, sizeof(client_ip));
    printf("[Child %d] Client kết nối: %s:%d\n",
           getpid(), client_ip, ntohs(cli_addr->sin_port));

    // Xác thực
    send_str(client_fd, "Username: ");
    if (recv_line(client_fd, username, sizeof(username)) <= 0) goto done;

    send_str(client_fd, "Password: ");
    if (recv_line(client_fd, password, sizeof(password)) <= 0) goto done;

    if (!check_credentials(username, password)) {
        send_str(client_fd, "Dang nhap that bai. Ket noi bi dong.\r\n");
        printf("[Child %d] Dang nhap that bai (user='%s')\n", getpid(), username);
        goto done;
    }

    printf("[Child %d] Dang nhap thanh cong (user='%s')\n", getpid(), username);
    send_str(client_fd, "Dang nhap thanh cong!\r\n");
    send_str(client_fd, "Nhap lenh (go 'exit' de thoat):\r\n");

    // Nhận và thực thi lệnh
    while (1) {
        send_str(client_fd, "$ ");

        int r = recv_line(client_fd, buf, sizeof(buf));
        if (r <= 0) break;                        /* client ngắt kết nối */
        if (strlen(buf) == 0) continue;           /* dòng trống */

        printf("[Child %d] Lenh nhan duoc: '%s'\n", getpid(), buf);

        if (strcmp(buf, "exit") == 0) {
            send_str(client_fd, "Tam biet!\r\n");
            break;
        }

        execute_command(client_fd, buf);
    }

done:
    close(client_fd);
    printf("[Child %d] Da dong ket noi voi %s:%d\n",
           getpid(), client_ip, ntohs(cli_addr->sin_port));
}

// Xử lý tín hiệu SIGCHLD
static void sigchld_handler(int signo)
{
    (void)signo;
    int stat;
    pid_t pid;
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
        printf("[Parent] Tien trinh con %d ket thuc.\n", pid);
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Su dung: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Cong khong hop le: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    // Đăng ký xử lý SIGCHLD để tránh zombie
    signal(SIGCHLD, sigchld_handler);

    // Tạo socket lắng nghe
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) { perror("socket"); exit(EXIT_FAILURE); }

    // Cho phép tái sử dụng cổng ngay sau khi server tắt
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)port);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind"); exit(EXIT_FAILURE);
    }
    if (listen(listener, MAX_PENDING) == -1) {
        perror("listen"); exit(EXIT_FAILURE);
    }

    printf("[Parent] Telnet server dang lang nghe tren cong %d ...\n", port);
    printf("[Parent] File nguoi dung: %s\n", USERS_FILE);

    // Vòng lặp chấp nhận kết nối
    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);

        int client = accept(listener, (struct sockaddr *)&cli_addr, &cli_len);
        if (client == -1) {
            if (errno == EINTR) continue;  /* bị ngắt bởi SIGCHLD, thử lại */
            perror("accept");
            continue;
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            close(client);
            continue;
        }

        if (pid == 0) {
            // Tiến trình CON
            close(listener);                  // con không cần socket lắng nghe
            handle_client(client, &cli_addr);
            exit(EXIT_SUCCESS);
        } else {
            // Tiến trình CHA
            close(client);                    // cha không cần socket client
            printf("[Parent] Da tao tien trinh con %d cho client moi.\n", pid);
        }
    }

    close(listener);
    return 0;
}