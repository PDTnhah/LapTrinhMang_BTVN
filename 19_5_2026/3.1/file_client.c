#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUF_SIZE 4096

// Đọc một dòng từ socket (kết thúc bằng \r\n), từng byte một
int recv_line(int sock, char *buf, int maxlen) {
    int n = 0;
    char c;
    while (n < maxlen - 1) {
        int ret = recv(sock, &c, 1, 0);
        if (ret <= 0) return -1; // Kết nối đóng hoặc lỗi
        if (c == '\n') break;    // Kết thúc dòng
        if (c == '\r') continue; // Bỏ qua \r
        buf[n++] = c;
    }
    buf[n] = '\0';
    return n; // 0 = dòng rỗng
}

// Nhận đủ filesize byte từ socket và ghi vào file
int recv_file(int sock, const char *filename, long filesize) {
    FILE *f = fopen(filename, "wb");
    if (!f) { perror("fopen"); return -1; }

    char buf[BUF_SIZE];
    long remaining = filesize;
    while (remaining > 0) {
        int to_recv = (remaining < BUF_SIZE) ? (int)remaining : BUF_SIZE;
        int n = recv(sock, buf, to_recv, 0);
        if (n <= 0) { fclose(f); return -1; }
        fwrite(buf, 1, n, f);
        remaining -= n;
    }
    fclose(f);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port       = atoi(argv[2]);

    // Tạo và kết nối socket 
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1) { perror("socket()"); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family       = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port        = htons(port);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect()");
        close(sock);
        return 1;
    }
    printf("[Client] Connected to %s:%d\n", ip, port);

    // Nhận dòng đầu "OK N" hoặc "ERROR ..."
    char line[512];
    int ret = recv_line(sock, line, sizeof(line));
    if (ret < 0) {
        printf("[Client] Server closed connection.\n");
        close(sock); return 1;
    }

    if (strncmp(line, "ERROR", 5) == 0) {
        printf("[Client] Server says: %s\n", line);
        close(sock); return 1;
    }

    if (strncmp(line, "OK", 2) != 0) {
        printf("[Client] Unexpected: %s\n", line);
        close(sock); return 1;
    }

    int file_count = 0;
    sscanf(line + 3, "%d", &file_count);
    printf("[Client] Server has %d file(s):\n", file_count);

    // Nhận danh sách file kết thúc khi gặp dòng rỗng
    char filenames[256][256];
    int idx = 0;
    while (1) {
        ret = recv_line(sock, line, sizeof(line));
        if (ret < 0) {
            printf("[Client] Connection lost while receiving list.\n");
            close(sock); return 1;
        }
        if (ret == 0) break; // Dòng rỗng = kết thúc danh sách

        printf("  [%d] %s\n", idx + 1, line);
        if (idx < 256) {
            strncpy(filenames[idx], line, 255);
            filenames[idx][255] = '\0';
            idx++;
        }
    }

    // Người dùng chọn file, gửi yêu cầu, nhận file 
    char req[256];
    while (1) {
        printf("\n[Client] Enter filename to download (or 'quit'): ");
        fflush(stdout);

        if (fgets(req, sizeof(req), stdin) == NULL) {
            printf("\n[Client] Input closed. Exiting.\n");
            break;
        }

        // Xóa ký tự xuống dòng
        char *p = req + strlen(req) - 1;
        while (p >= req && (*p == '\r' || *p == '\n')) *p-- = '\0';

        if (strlen(req) == 0) continue;
        if (strcmp(req, "quit") == 0) break;

        // Gửi tên file cho server kết thúc bằng \r\n
        char sendbuf[260];
        snprintf(sendbuf, sizeof(sendbuf), "%s\r\n", req);
        send(sock, sendbuf, strlen(sendbuf), 0);

        // Nhận phản hồi từ server
        ret = recv_line(sock, line, sizeof(line));
        if (ret < 0) {
            printf("[Client] Server closed connection.\n");
            break;
        }

        if (strncmp(line, "ERROR", 5) == 0) {
            // File không tồn tại, thử lại
            printf("[Client] Server: %s\n", line);
            printf("[Client] Please enter a valid filename.\n");
            continue;
        }

        if (strncmp(line, "OK", 2) == 0) {
            long filesize = 0;
            sscanf(line + 3, "%ld", &filesize);
            printf("[Client] Downloading '%s' (%ld bytes)...\n", req, filesize);

            if (recv_file(sock, req, filesize) == 0) {
                printf("[Client] Saved as '%s'.\n", req);
            } else {
                printf("[Client] Error receiving file.\n");
            }
            break; // Server đóng kết nối sau khi gửi file
        }

        printf("[Client] Unknown response: %s\n", line);
        break;
    }

    close(sock);
    printf("[Client] Connection closed.\n");
    return 0;
}