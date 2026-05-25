#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAX_PATH  512
#define BUF_SIZE  4096

// dọn dẹp tiến trình con đã kết thúc
void signal_handler(int signo) {
    int stat;
    while (waitpid(-1, &stat, WNOHANG) > 0);
}

// Gửi đủ số byte xuống socket (lặp cho đến khi gửi hết)
int send_all(int sock, const char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int ret = send(sock, buf + sent, len - sent, 0);
        if (ret <= 0) return -1;
        sent += ret;
    }
    return sent;
}

// Nhận một dòng kết thúc bằng \r\n từ socket 
int recv_line(int sock, char *buf, int maxlen) {
    int n = 0;
    char c;
    while (n < maxlen - 1) {
        int ret = recv(sock, &c, 1, 0);
        if (ret <= 0) return -1; // Lỗi hoặc đóng kết nối
        if (c == '\n') break;    // Kết thúc dòng
        if (c == '\r') continue; // Bỏ qua \r
        buf[n++] = c;
    }
    buf[n] = '\0';
    return n; // 0 = dòng rỗng (chỉ có \r\n), > 0 = có nội dung
}

// Gửi nội dung file xuống socket theo từng chunk
int send_file_content(int sock, const char *filepath, long filesize) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;
    char buf[BUF_SIZE];
    long remaining = filesize;
    while (remaining > 0) {
        int to_read = (remaining < BUF_SIZE) ? (int)remaining : BUF_SIZE;
        int n = (int)fread(buf, 1, to_read, f);
        if (n <= 0) { fclose(f); return -1; }
        if (send_all(sock, buf, n) < 0) { fclose(f); return -1; }
        remaining -= n;
    }
    fclose(f);
    return 0;
}

// Xử lý toàn bộ giao tiếp với một client 
void handle_client(int client, const char *dir) {
    char msg[BUF_SIZE];

    // Quét thư mục, lấy danh sách file thường 
    DIR *dp = opendir(dir);
    if (!dp) {
        snprintf(msg, sizeof(msg), "ERROR Cannot open directory\r\n");
        send_all(client, msg, strlen(msg));
        close(client);
        exit(EXIT_FAILURE);
    }

    char filenames[256][256];
    int file_count = 0;
    struct dirent *entry;

    while ((entry = readdir(dp)) != NULL && file_count < 256) {
        // Dùng stat() thay vì d_type cho tương thích rộng hơn
        char fullpath[MAX_PATH];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, entry->d_name);
        struct stat st;
        if (stat(fullpath, &st) == -1) continue;
        if (!S_ISREG(st.st_mode)) continue; // Chỉ lấy file thường
        strncpy(filenames[file_count], entry->d_name, 255);
        filenames[file_count][255] = '\0';
        file_count++;
    }
    closedir(dp);

    // Gửi danh sách file cho client 
    if (file_count == 0) {
        // Không có file: gửi lỗi và đóng kết nối
        snprintf(msg, sizeof(msg), "ERROR No files to download\r\n");
        send_all(client, msg, strlen(msg));
        printf("[Server-PID%d] No files found. Closing.\n", getpid());
        fflush(stdout);
        close(client);
        exit(EXIT_SUCCESS);
    }

    // Dòng đầu: "OK N\r\n"
    snprintf(msg, sizeof(msg), "OK %d\r\n", file_count);
    send_all(client, msg, strlen(msg));

    // Mỗi tên file kết thúc bằng "\r\n"
    for (int i = 0; i < file_count; i++) {
        snprintf(msg, sizeof(msg), "%s\r\n", filenames[i]);
        send_all(client, msg, strlen(msg));
    }

    // Kết thúc danh sách: thêm một "\r\n" nữa => toàn bộ list kết thúc bằng "\r\n\r\n"
    send_all(client, "\r\n", 2);
    printf("[Server-PID%d] Sent list (%d files) to client.\n", getpid(), file_count);
    fflush(stdout);

    // Nhận tên file từ client, gửi file 
    // Dùng buffer nhỏ (255 byte) để đọc tên file
    char fname[256];
    while (1) {
        int ret = recv_line(client, fname, sizeof(fname));
        if (ret < 0) {
            printf("[Server-PID%d] Client disconnected.\n", getpid());
            fflush(stdout);
            break;
        }
        if (ret == 0) continue; // Dòng rỗng, bỏ qua

        printf("[Server-PID%d] Client requests: '%s'\n", getpid(), fname);
        fflush(stdout);

        // Kiểm tra file có tồn tại và là file thường
        char filepath[MAX_PATH];
        snprintf(filepath, sizeof(filepath), "%s/%s", dir, fname);
        struct stat st;
        if (stat(filepath, &st) == -1 || !S_ISREG(st.st_mode)) {
            // File không tồn tại: báo lỗi, yêu cầu gửi lại
            snprintf(msg, sizeof(msg), "ERROR File not found: %s\r\n", fname);
            send_all(client, msg, strlen(msg));
            printf("[Server-PID%d] File not found. Waiting for retry...\n", getpid());
            fflush(stdout);
            continue;
        }

        // File tồn tại: gửi "OK <filesize>\r\n" rồi nội dung file
        long filesize = (long)st.st_size;
        snprintf(msg, sizeof(msg), "OK %ld\r\n", filesize);
        send_all(client, msg, strlen(msg));

        printf("[Server-PID%d] Sending '%s' (%ld bytes)...\n",
                getpid(), fname, filesize);
        fflush(stdout);

        if (send_file_content(client, filepath, filesize) < 0) {
            printf("[Server-PID%d] Error while sending file.\n", getpid());
        } else {
            printf("[Server-PID%d] File sent successfully.\n", getpid());
        }
        fflush(stdout);
        break; // Đóng kết nối sau khi gửi file
    }

    close(client);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <file_directory>\n", argv[0]);
        return 1;
    }

    int port          = atoi(argv[1]);
    const char *dir = argv[2];

    // Đăng ký xử lý SIGCHLD tránh zombie
    signal(SIGCHLD, signal_handler);

    // --- Tạo socket lắng nghe ---
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) { perror("socket()"); return 1; }

    // Cho phép tái sử dụng cổng (tránh "Address already in use")
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family       = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind()"); close(listener); return 1;
    }
    if (listen(listener, 10) == -1) {
        perror("listen()"); close(listener); return 1;
    }

    printf("[Server] Listening on port %d | Directory: %s\n", port, dir);
    fflush(stdout);

    // Vòng lặp chính: chấp nhận kết nối và fork 
    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);

        int client = accept(listener, (struct sockaddr *)&caddr, &clen);
        if (client == -1) {
            if (errno == EINTR) continue; // Bị ngắt bởi SIGCHLD, thử lại
            perror("accept()");
            continue;
        }

        printf("[Server] New client: %s:%d\n",
                inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
        fflush(stdout);

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork()");
            close(client);
            continue;
        }

        if (pid == 0) {
            // Tiến trình con: đóng socket lắng nghe, xử lý client
            close(listener);
            handle_client(client, dir);
            // handle_client gọi exit() nên không cần thêm gì
        } else {
            // Tiến trình cha: đóng socket client (đã được con giữ)
            close(client);
        }
    }

    close(listener);
    return 0;
}