#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#define PORT     9000
#define BUF_SIZE 256

// Thu hồi tiến trình con kết thúc
void signal_handler(int signo) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

// Kiểm tra format hợp lệ, nếu đúng ghi chuỗi strftime vào out_fmt
int parse_format(const char *fmt, char *out_fmt) {
    if (strcmp(fmt, "dd/mm/yyyy") == 0) { strcpy(out_fmt, "%d/%m/%Y"); return 1; }
    if (strcmp(fmt, "dd/mm/yy")   == 0) { strcpy(out_fmt, "%d/%m/%y"); return 1; }
    if (strcmp(fmt, "mm/dd/yyyy") == 0) { strcpy(out_fmt, "%m/%d/%Y"); return 1; }
    if (strcmp(fmt, "mm/dd/yy")   == 0) { strcpy(out_fmt, "%m/%d/%y"); return 1; }
    return 0;
}

// Hàm xử lý client, chạy trong tiến trình con
void handle_client(int client, struct sockaddr_in *ca) {
    char buf[BUF_SIZE], response[BUF_SIZE];
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ca->sin_addr, ip, sizeof(ip));
    printf("[PID %d] Client %s:%d connected\n", getpid(), ip, ntohs(ca->sin_port));

    while (1) {
        memset(buf, 0, sizeof(buf));
        int ret = recv(client, buf, sizeof(buf) - 1, 0);
        if (ret <= 0) {
            printf("[PID %d] Client %s disconnected\n", getpid(), ip);
            break;
        }
        buf[ret] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';
        printf("[PID %d] Recv: [%s]\n", getpid(), buf);

        // Phân tích lệnh: GET_TIME <format>
        char cmd[64] = {0}, fmt[64] = {0};
        int n = sscanf(buf, "%63s %63s", cmd, fmt);

        if (n < 1 || strcmp(cmd, "GET_TIME") != 0) {
            snprintf(response, sizeof(response),
                "ERROR Invalid command. Usage: GET_TIME <format>\n");
            send(client, response, strlen(response), 0);
            continue;
        }
        if (n < 2) {
            snprintf(response, sizeof(response),
                "ERROR Missing format. "
                "Supported: dd/mm/yyyy | dd/mm/yy | mm/dd/yyyy | mm/dd/yy\n");
            send(client, response, strlen(response), 0);
            continue;
        }

        char strfmt[32] = {0};
        if (!parse_format(fmt, strfmt)) {
            snprintf(response, sizeof(response),
                "ERROR Unknown format '%s'. "
                "Supported: dd/mm/yyyy | dd/mm/yy | mm/dd/yyyy | mm/dd/yy\n", fmt);
            send(client, response, strlen(response), 0);
            continue;
        }

        // Lấy thời gian và định dạng theo yêu cầu
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char tbuf[64];
        strftime(tbuf, sizeof(tbuf), strfmt, t);

        snprintf(response, sizeof(response), "OK %s\n", tbuf);
        send(client, response, strlen(response), 0);
        printf("[PID %d] Sent: %s", getpid(), response);
    }

    close(client);
}

int main() {
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(PORT);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr))) { perror("bind"); return 1; }
    if (listen(listener, 10)) { perror("listen"); return 1; }

    printf("=== time_server (multiprocessing) on port %d ===\n", PORT);
    printf("Command: GET_TIME <format>\n");
    printf("Formats: dd/mm/yyyy | dd/mm/yy | mm/dd/yyyy | mm/dd/yy\n\n");

    signal(SIGCHLD, signal_handler);

    while (1) {
        struct sockaddr_in ca;
        socklen_t ca_len = sizeof(ca);
        int client = accept(listener, (struct sockaddr *)&ca, &ca_len);
        if (client == -1) {
            if (errno == EINTR) continue;
            perror("accept"); continue;
        }
        printf("[Main] Accepted %s:%d\n",
               inet_ntoa(ca.sin_addr), ntohs(ca.sin_port));

        pid_t pid = fork();
        if (pid == 0) {
            close(listener);
            handle_client(client, &ca);
            exit(EXIT_SUCCESS);
        } else if (pid > 0) {
            close(client);   // cha đóng socket, con đang dùng
        } else {
            perror("fork"); close(client);
        }
    }
    close(listener);
    return 0;
}