#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>

#define PORT 9090
#define BUF_SIZE 2048

// Biến toàn cục để lưu trữ client đang chờ ghép cặp
int waiting_client = -1;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

// Cấu trúc lưu trữ thông tin một cặp client
struct ClientPair {
    int sock1;
    int sock2;
};

// Hàm chạy trong luồng xử lý cho mỗi cặp client
void *handle_chat_pair(void *arg) {
    struct ClientPair *pair = (struct ClientPair *)arg;
    int s1 = pair->sock1;
    int s2 = pair->sock2;
    free(pair); // Giải phóng bộ nhớ đã cấp phát cho struct

    // Thông báo cho cả 2 biết đã được ghép cặp
    char *ready_msg = "[Server] Đã ghép cặp thành công! Bắt đầu chat.\n";
    send(s1, ready_msg, strlen(ready_msg), 0);
    send(s2, ready_msg, strlen(ready_msg), 0);

    // Sử dụng poll để lắng nghe đồng thời cả 2 socket
    struct pollfd fds[2];
    fds[0].fd = s1;
    fds[0].events = POLLIN;
    fds[1].fd = s2;
    fds[1].events = POLLIN;

    char buf[BUF_SIZE];

    while (1) {
        int ret = poll(fds, 2, -1); // Chờ đến khi có dữ liệu từ ít nhất 1 socket
        if (ret < 0) {
            perror("poll()");
            break;
        }

        // Kiểm tra nếu có dữ liệu từ Client 1 
        if (fds[0].revents & POLLIN) {
            int n = recv(s1, buf, sizeof(buf), 0);
            if (n <= 0) {
                // Client 1 ngắt kết nối hoặc bị lỗi
                break; 
            }
            // Chuyển tiếp tin nhắn sang Client 2 
            send(s2, buf, n, 0);
        }

        // Kiểm tra nếu có dữ liệu từ Client 2 
        if (fds[1].revents & POLLIN) {
            int n = recv(s2, buf, sizeof(buf), 0);
            if (n <= 0) {
                // Client 2 ngắt kết nối hoặc bị lỗi
                break;
            }
            // Chuyển tiếp tin nhắn sang Client 1 
            send(s1, buf, n, 0);
        }
    }

    // Nếu 1 trong 2 client ngắt kết nối thì client còn lại cũng được ngắt kết nối
    printf("[Server] Một cặp client đã ngắt kết nối. Đang đóng socket...\n");
    close(s1);
    close(s2);
    
    // Kết thúc luồng
    pthread_exit(NULL);
}

int main() {
    int listener;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Tạo socket
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0) {
        perror("socket()");
        exit(EXIT_FAILURE);
    }

    // Cho phép tái sử dụng port nhanh chóng
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Cấu hình địa chỉ server
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    // Bind socket
    if (bind(listener, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind()");
        close(listener);
        exit(EXIT_FAILURE);
    }

    // Lắng nghe kết nối
    if (listen(listener, 10) < 0) {
        perror("listen()");
        close(listener);
        exit(EXIT_FAILURE);
    }

    printf("[Server] Đang lắng nghe tại port %d...\n", PORT);

    while (1) {
        // Chấp nhận client mới
        int client_sock = accept(listener, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) {
            perror("accept()");
            continue;
        }

        printf("[Server] Client mới kết nối: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        pthread_mutex_lock(&queue_mutex);
        
        if (waiting_client == -1) {
            // Nếu hàng đợi đang trống, lưu client này vào hàng đợi
            waiting_client = client_sock;
            char *wait_msg = "[Server] Đang chờ người ghép cặp...\n";
            send(client_sock, wait_msg, strlen(wait_msg), 0);
            pthread_mutex_unlock(&queue_mutex);
        } else {
            // Nếu đã có 1 client đang chờ, ghép cặp client hiện tại với client đó
            struct ClientPair *pair = malloc(sizeof(struct ClientPair));
            pair->sock1 = waiting_client;
            pair->sock2 = client_sock;
            
            // Reset hàng đợi
            waiting_client = -1;
            pthread_mutex_unlock(&queue_mutex);

            // Tạo luồng riêng để phục vụ cặp client này
            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, handle_chat_pair, (void *)pair) != 0) {
                perror("pthread_create()");
                close(pair->sock1);
                close(pair->sock2);
                free(pair);
            } else {
                // Detach luồng để hệ thống tự thu hồi tài nguyên khi luồng kết thúc
                pthread_detach(thread_id); 
            }
        }
    }

    close(listener);
    return 0;
}