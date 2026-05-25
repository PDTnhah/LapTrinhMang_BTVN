#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 9000
#define BUFFER_SIZE 1024

// Hàm xác thực người dùng
int authenticate(char *credentials) {
    FILE *f = fopen("users.txt", "r");
    if (f == NULL) {
        perror("Khong the mo file users.txt");
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        
        // So sánh chuỗi client gửi lên với dữ liệu trong file
        if (strcmp(credentials, line) == 0) {
            fclose(f);
            return 1; 
        }
    }
    fclose(f);
    return 0; 
}

// Hàm thực thi cho mỗi luồng xử lý client
void *client_handler(void *arg) {
    // Ép kiểu và lấy giá trị socket client, sau đó giải phóng con trỏ
    int client_sock = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    int bytes_received;

    // Yêu cầu client gửi thông tin đăng nhập
    char *prompt = "Vui long nhap tai khoan va mat khau (Dinh dang: user pass):\n> ";
    send(client_sock, prompt, strlen(prompt), 0);

    // Nhận thông tin đăng nhập
    bytes_received = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        close(client_sock);
        pthread_exit(NULL);
    }

    buffer[bytes_received] = '\0';
    buffer[strcspn(buffer, "\r\n")] = '\0'; 

    // Kiểm tra đăng nhập
    if (!authenticate(buffer)) {
        char *err_msg = "Dang nhap that bai. Ngat ket noi!\n";
        send(client_sock, err_msg, strlen(err_msg), 0);
        close(client_sock);
        pthread_exit(NULL);
    }

    // Đăng nhập thành công
    char *success_msg = "Dang nhap thanh cong! Ban co the gui lenh shell.\n";
    send(client_sock, success_msg, strlen(success_msg), 0);

    // Vòng lặp nhận và thực thi lệnh từ client
    while (1) {
        char *cmd_prompt = "telnet> ";
        send(client_sock, cmd_prompt, strlen(cmd_prompt), 0);

        bytes_received = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            break; // Client ngắt kết nối
        }

        buffer[bytes_received] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0'; // Xóa ký tự xuống dòng

        // Bỏ qua nếu client chỉ nhấn Enter (chuỗi rỗng)
        if (strlen(buffer) == 0) continue;

        // Nếu client gõ "exit" hoặc "quit" thì ngắt kết nối
        if (strcmp(buffer, "exit") == 0 || strcmp(buffer, "quit") == 0) {
            break;
        }

        char out_filename[64];
        sprintf(out_filename, "out_%d.txt", client_sock);

        char shell_cmd[BUFFER_SIZE + 100];
        sprintf(shell_cmd, "%s > %s 2>&1", buffer, out_filename);

        // Gọi hàm system để thực thi lệnh
        system(shell_cmd);

        // Đọc kết quả từ file out_X.txt và gửi trả lại cho client
        FILE *out_file = fopen(out_filename, "r");
        if (out_file != NULL) {
            char file_buf[BUFFER_SIZE];
            int n;
            // Gửi từng phần file nếu kết quả quá dài
            while ((n = fread(file_buf, 1, BUFFER_SIZE, out_file)) > 0) {
                send(client_sock, file_buf, n, 0);
            }
            fclose(out_file);
        } else {
            char *err_read = "Loi: Khong the doc ket qua thuc thi lenh.\n";
            send(client_sock, err_read, strlen(err_read), 0);
        }

        // Xóa file tạm sau khi đã gửi xong
        remove(out_filename);
    }

    // Đóng socket và kết thúc luồng
    printf("Client %d da ngat ket noi.\n", client_sock);
    close(client_sock);
    pthread_exit(NULL);
}

int main() {
    // Khoi tao socket server
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("Khong the tao socket");
        return 1;
    }

    // Cho phép tái sử dụng port
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Khai bao cau truc dia chi server
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    // Gan socket vao cong port
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("Loi bind");
        return 1;
    }

    // Cho ket noi
    if (listen(listener, 10) == -1) {
        perror("Loi listen");
        return 1;
    }

    printf("Telnet Server dang chay tai cong %d...\n", PORT);

    // Vong lap chinh de nhan client
    while (1) {
        // Cấp phát bộ nhớ động cho socket client để tránh mất dữ liệu khi truyền vào thread
        int *client_sock = malloc(sizeof(int));
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        *client_sock = accept(listener, (struct sockaddr *)&client_addr, &client_len);
        if (*client_sock == -1) {
            perror("Loi accept");
            free(client_sock);
            continue;
        }

        printf("Client moi ket noi: %s:%d (Socket fd: %d)\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), *client_sock);

        // Tao luong (thread) moi cho client
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, (void *)client_sock) != 0) {
            perror("Loi tao thread");
            close(*client_sock);
            free(client_sock);
            continue;
        }

        // Đánh dấu luồng là detach để tự động giải phóng tài nguyên khi luồng kết thúc
        pthread_detach(tid);
    }

    close(listener);
    return 0;
}