#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define MAX_CLIENTS 64
#define BUF_SIZE 2048

// Cấu trúc lưu trữ thông tin client
typedef struct {
    int socket;
    char id[50];
    char name[50];
    int is_registered;
} ClientInfo;

ClientInfo clients[MAX_CLIENTS];
int num_clients = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// Hàm lấy thời gian hiện tại theo định dạng: YYYY/MM/DD HH:MM:SSPM
void get_current_time(char *time_buf, int size) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(time_buf, size, "%Y/%m/%d %I:%M:%S%p", tm_info);
}

// Hàm gửi tin nhắn cho tất cả các client đã đăng nhập 
void broadcast_message(int sender_socket, const char *sender_id, const char *message) {
    char time_buf[64];
    get_current_time(time_buf, sizeof(time_buf));

    char send_buf[BUF_SIZE + 100];
    // Định dạng thông điệp"
    snprintf(send_buf, sizeof(send_buf), "%s %s: %s", time_buf, sender_id, message);

    pthread_mutex_lock(&clients_mutex); // Khóa Mutex trước khi đọc mảng
    for (int i = 0; i < num_clients; i++) {
        if (clients[i].socket != sender_socket && clients[i].is_registered) {
            send(clients[i].socket, send_buf, strlen(send_buf), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex); // Mở khóa Mutex
}

// Hàm xóa client khỏi mảng khi ngắt kết nối
void remove_client(int socket) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < num_clients; i++) {
        if (clients[i].socket == socket) {
            // Đẩy client cuối lên để thế chỗ client bị xóa
            clients[i] = clients[num_clients - 1];
            num_clients--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Hàm thực thi luồng cho mỗi client
void *client_handler(void *arg) {
    int client_sock = *(int *)arg;
    free(arg); // Giải phóng con trỏ tham số truyền vào luồng

    char buf[BUF_SIZE];
    char client_id[50];
    char client_name[50];
    int is_registered = 0;

    // Yêu cầu nhập đúng cú pháp
    char *prompt = "Vui lòng nhập theo cú pháp 'client_id: client_name': \n";
    send(client_sock, prompt, strlen(prompt), 0);

    // Vòng lặp đăng ký
    while (!is_registered) {
        int ret = recv(client_sock, buf, sizeof(buf) - 1, 0);
        if (ret <= 0) {
            close(client_sock);
            return NULL;
        }
        buf[ret] = 0;

        // Cắt bỏ ký tự xuống dòng (\n, \r)
        if (buf[ret - 1] == '\n') buf[ret - 1] = 0;
        if (ret > 1 && buf[ret - 2] == '\r') buf[ret - 2] = 0;

        // Kiểm tra cú pháp chuỗi đầu vào
        if (sscanf(buf, "%[^:]: %s", client_id, client_name) == 2) {
            is_registered = 1;

            pthread_mutex_lock(&clients_mutex); // Khóa Mutex trước khi ghi vào mảng
            clients[num_clients].socket = client_sock;
            strcpy(clients[num_clients].id, client_id);
            strcpy(clients[num_clients].name, client_name);
            clients[num_clients].is_registered = 1;
            num_clients++;
            pthread_mutex_unlock(&clients_mutex);

            char *success_msg = "Đăng ký thành công! Bạn có thể bắt đầu chat.\n";
            send(client_sock, success_msg, strlen(success_msg), 0);
            printf("Client tham gia: %s (ID: %s)\n", client_name, client_id);
        } else {
            char *error_msg = "Sai cú pháp. Vui lòng thử lại 'client_id: client_name': \n";
            send(client_sock, error_msg, strlen(error_msg), 0);
        }
    }

    // Vòng lặp chat
    while (1) {
        int ret = recv(client_sock, buf, sizeof(buf) - 1, 0);
        if (ret <= 0) {
            printf("Client %s (ID: %s) đã ngắt kết nối.\n", client_name, client_id);
            break;
        }
        buf[ret] = 0;

        // Phân phát thông điệp
        broadcast_message(client_sock, client_id, buf);
    }

    // Dọn dẹp tài nguyên khi client thoát
    remove_client(client_sock);
    close(client_sock);
    return NULL;
}

int main(int argc, char *argv[]) {
    // Nhận cổng từ dòng lệnh
    if (argc != 2) {
        printf("Sử dụng: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0) {
        perror("Lỗi tạo socket");
        return 1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Lỗi bind");
        return 1;
    }

    if (listen(listener, 10) < 0) {
        perror("Lỗi listen");
        return 1;
    }

    printf("Chat server (Multithread) đang chờ kết nối ở cổng %d...\n", port);

    // Vòng lặp chờ kết nối
    while (1) {
        // Cấp phát động bộ nhớ cho socket client để truyền vào thread an toàn
        int *client_sock = malloc(sizeof(int));
        *client_sock = accept(listener, NULL, NULL);

        if (*client_sock < 0) {
            perror("Lỗi accept");
            free(client_sock);
            continue;
        }

        pthread_t thread_id;
        // Khởi tạo luồng để xử lý kết nối từ client
        if (pthread_create(&thread_id, NULL, client_handler, client_sock) != 0) {
            perror("Lỗi tạo thread");
            free(client_sock);
            continue;
        }

        // Tách rời luồng để hệ điều hành tự thu hồi tài nguyên khi kết thúc
        pthread_detach(thread_id);
    }

    close(listener);
    return 0;
}