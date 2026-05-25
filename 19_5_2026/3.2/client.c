#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUF_SIZE 2048

int sock;

// Luồng phụ chịu trách nhiệm nhận dữ liệu từ server
void *receive_thread(void *arg) {
    char buf[BUF_SIZE];
    while (1) {
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            printf("\n\033[1;31m[Client] Kết nối đã bị đóng (Server ngắt hoặc đối phương thoát).\033[0m\n");
            close(sock);
            exit(EXIT_SUCCESS);
        }
        buf[n] = '\0'; 
        
        // Kiểm tra xem ai là người gửi tin nhắn này
        if (strncmp(buf, "[Server]", 8) == 0) {
            printf("\r\033[1;33m%s\033[0m", buf);
        } else {
            printf("\r\033[1;32m[Đối phương]: \033[0m%s", buf);
        }
        
        printf("\033[1;36m[Tôi]: \033[0m"); 
        fflush(stdout);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Cách dùng: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    // Tạo socket
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        perror("socket()");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(port);

    // Kết nối đến server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect()");
        close(sock);
        return 1;
    }

    printf("\033[1;33m[Client] Đã kết nối đến server %s:%d\033[0m\n", server_ip, port);

    // Tạo luồng để lắng nghe tin nhắn đến
    pthread_t recv_tid;
    if (pthread_create(&recv_tid, NULL, receive_thread, NULL) != 0) {
        perror("pthread_create()");
        close(sock);
        return 1;
    }

    // Luồng chính chịu trách nhiệm đọc từ bàn phím và gửi đi
    char send_buf[BUF_SIZE];
    while (1) {
        // In ra dấu nhắc hiển thị cho bản thân trước khi đợi nhập liệu
        printf("\033[1;36m[Tôi]: \033[0m");
        fflush(stdout);

        if (fgets(send_buf, sizeof(send_buf), stdin) == NULL) {
            break;
        }
        
        // Bỏ qua nếu chỉ nhấn Enter (không gõ gì) để tránh rác log
        if (strcmp(send_buf, "\n") == 0) {
            continue;
        }
        
        // Gửi dữ liệu xuống socket
        if (send(sock, send_buf, strlen(send_buf), 0) < 0) {
            perror("send()");
            break;
        }
    }

    close(sock);
    return 0;
}