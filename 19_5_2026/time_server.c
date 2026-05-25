#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define PORT 9000
#define BUFFER_SIZE 256

// Hàm xử lý tương tác với từng client (Chạy trong luồng riêng)
void *client_handler(void *arg) {
    // Ép kiểu và lấy giá trị socket client, sau đó giải phóng con trỏ
    int client_sock = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    int bytes_received;

    // Gửi lời chào và hướng dẫn sử dụng khi client vừa kết nối
    char *welcome_msg = "=== TIME SERVER ===\n"
                        "Cu phap: GET_TIME [format]\n"
                        "Format ho tro: dd/mm/yyyy, dd/mm/yy, mm/dd/yyyy, mm/dd/yy\n"
                        "Go 'exit' hoac 'quit' de thoat.\n"
                        "===================\n";
    send(client_sock, welcome_msg, strlen(welcome_msg), 0);

    // Vòng lặp duy trì kết nối cho phép nhập nhiều lệnh
    while (1) {
        char *prompt = "> ";
        send(client_sock, prompt, strlen(prompt), 0);

        // Chờ nhận dữ liệu từ client
        bytes_received = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            break; 
        }

        buffer[bytes_received] = '\0';
        
        buffer[strcspn(buffer, "\r\n")] = '\0';

        // Bỏ qua nếu người dùng chỉ nhấn Enter (chuỗi rỗng)
        if (strlen(buffer) == 0) {
            continue;
        }

        if (strcmp(buffer, "exit") == 0 || strcmp(buffer, "quit") == 0) {
            char *bye = "Tam biet!\n";
            send(client_sock, bye, strlen(bye), 0);
            break; // Thoát vòng lặp
        }

        char response[BUFFER_SIZE];
        
        if (strncmp(buffer, "GET_TIME ", 9) == 0) {
            // Tách lấy phần format phía sau chuỗi "GET_TIME "
            char *format = buffer + 9;
            
            // Lấy thời gian hiện tại
            time_t t = time(NULL);
            struct tm *tm_info = localtime(&t);
            
            // Xử lý các định dạng
            if (strcmp(format, "dd/mm/yyyy") == 0) {
                strftime(response, sizeof(response), "%d/%m/%Y\n", tm_info);
            } 
            else if (strcmp(format, "dd/mm/yy") == 0) {
                strftime(response, sizeof(response), "%d/%m/%y\n", tm_info);
            } 
            else if (strcmp(format, "mm/dd/yyyy") == 0) {
                strftime(response, sizeof(response), "%m/%d/%Y\n", tm_info);
            } 
            else if (strcmp(format, "mm/dd/yy") == 0) {
                strftime(response, sizeof(response), "%m/%d/%y\n", tm_info);
            } 
            else {
                strcpy(response, "Loi: Dinh dang thoi gian khong duoc ho tro.\n");
            }
        } 
        else {
            strcpy(response, "Loi: Sai cu phap. Hay su dung 'GET_TIME [format]'.\n");
        }

        // Trả kết quả về cho client
        send(client_sock, response, strlen(response), 0);
    }

    // Đóng socket và kết thúc luồng
    printf("Client (Socket fd: %d) da ngat ket noi.\n", client_sock);
    close(client_sock);
    pthread_exit(NULL);
}

int main() {
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("Khong the tao socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("Loi bind");
        return 1;
    }

    if (listen(listener, 10) == -1) {
        perror("Loi listen");
        return 1;
    }

    printf("Time Server (Interactive) dang chay tai cong %d...\n", PORT);

    while (1) {
        int *client_sock = malloc(sizeof(int));
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        *client_sock = accept(listener, (struct sockaddr *)&client_addr, &client_len);
        if (*client_sock == -1) {
            perror("Loi accept");
            free(client_sock);
            continue;
        }

        printf("Client moi ket noi tu %s:%d (Socket fd: %d)\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), *client_sock);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, (void *)client_sock) != 0) {
            perror("Loi tao thread");
            close(*client_sock);
            free(client_sock);
            continue;
        }

        pthread_detach(tid);
    }

    close(listener);
    return 0;
}