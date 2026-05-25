#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define NUM_THREADS 8
#define BUFFER_SIZE 2048

// Hàm thực thi của các luồng
void *thread_proc(void *arg) {
    int listener = *(int *)arg;
    char buf[BUFFER_SIZE];

    // Vòng lặp vô hạn, luồng sẽ liên tục phục vụ các client
    while (1) {
        // Tất cả các luồng cùng chờ ở hàm accept()
        // Khi có client kết nối, hệ điều hành sẽ chọn 1 luồng để accept
        int client = accept(listener, NULL, NULL);
        if (client == -1) {
            continue; // Bỏ qua nếu lỗi
        }

        // In ra ID của luồng đang xử lý kết nối này để kiểm chứng Prethreading
        printf("[Thread %ld] Dang xu ly ket noi moi (Socket %d)...\n", pthread_self(), client);

        // Nhận dữ liệu (HTTP Request) từ client
        int ret = recv(client, buf, sizeof(buf) - 1, 0);
        if (ret <= 0) {
            close(client);
            continue;
        }
        
        buf[ret] = '\0';
        
        // Trả lại kết quả (HTTP Response) cho client theo đúng định dạng
        char *msg = "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "<html>"
                    "<head><title>Prethreading HTTP Server</title></head>"
                    "<body>"
                    "<h1>Xin chao cac ban!</h1>"
                    "<p>Day la ket qua tu HTTP Server su dung ky thuat Prethreading.</p>"
                    "</body>"
                    "</html>";
                    
        send(client, msg, strlen(msg), 0);

        // Đóng kết nối với client hiện tại và quay lại vòng lặp chờ client tiếp theo
        close(client);
    }
    
    return NULL;
}

int main() {
    // Khởi tạo socket
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("Khong the tao socket");
        return 1;
    }

    // Thiết lập tùy chọn tái sử dụng port
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Cấu hình địa chỉ server
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    // Gắn socket với địa chỉ
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("Loi bind");
        return 1;
    }

    // Lắng nghe kết nối
    if (listen(listener, 10) == -1) {
        perror("Loi listen");
        return 1;
    }

    printf("HTTP Server (Prethreading) dang chay tai http://localhost:%d\n", PORT);
    printf("Da tao %d luong (threads) cho san de phuc vu...\n\n", NUM_THREADS);

    // Khởi tạo Pool gồm NUM_THREADS luồng
    pthread_t thread_id[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        // Truyền con trỏ của biến listener vào cho các luồng
        if (pthread_create(&thread_id[i], NULL, thread_proc, &listener) != 0) {
            perror("Loi tao luong");
        }
    }

    // Đợi các luồng (Thực tế các luồng chạy vô hạn nên main sẽ bị block ở đây)
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(thread_id[i], NULL);
    }

    // Đóng socket listener (Mặc dù code sẽ không bao giờ chạm đến dòng này)
    close(listener);
    return 0;
}