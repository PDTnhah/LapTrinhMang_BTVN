#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <signal.h>

#define PORT 8080
#define BUFFER_SIZE 8192

// Hàm giải mã URL
void url_decode(char *src, char *dest) {
    int i = 0, j = 0;
    while (src[i]) {
        if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            char hex[3] = {src[i + 1], src[i + 2], 0};
            dest[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dest[j++] = ' ';
            i++;
        } else {
            dest[j++] = src[i++];
        }
    }
    dest[j] = '\0';
}

// Hàm xác định Content-Type dựa vào đuôi file
const char *get_mime_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream"; // Mặc định: tải file về

    if (strcmp(dot, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".txt") == 0)  return "text/plain; charset=utf-8";

    // Ảnh
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".png") == 0) return "image/png";
    if (strcmp(dot, ".gif") == 0) return "image/gif";

    // Audio
    if (strcmp(dot, ".mp3") == 0) return "audio/mpeg";
    if (strcmp(dot, ".wav") == 0) return "audio/wav";

    // Video
    if (strcmp(dot, ".mp4") == 0) return "video/mp4";
    if (strcmp(dot, ".webm")== 0) return "video/webm";

    return "application/octet-stream";
}

// Gửi file nhị phân (Video, Audio, Ảnh, Text...) cho client
void serve_file(int client_fd, const char *local_path) {
    FILE *file = fopen(local_path, "rb");
    if (!file) {
        char *err = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n<h1>404 File Not Found</h1>";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    // Lấy kích thước file
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    const char *mime = get_mime_type(local_path);

    // Gửi HTTP Header
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n", mime, file_size);
    send(client_fd, header, strlen(header), 0);

    // Đọc và gửi file theo từng đoạn nhỏ
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        int sent = send(client_fd, buffer, bytes_read, 0);
        if (sent <= 0) break; // Client ngắt kết nối giữa chừng
    }

    fclose(file);
}

// Tạo trang HTML hiển thị danh sách thư mục
void serve_directory(int client_fd, const char *local_path, const char *req_path) {
    DIR *dir = opendir(local_path);
    if (!dir) {
        char *err = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n<h1>403 Forbidden</h1>";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    // Gửi Header: Không cần Content-Length vì ta sẽ đóng kết nối khi gửi xong
    char *header = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/html; charset=utf-8\r\n"
                   "Connection: close\r\n\r\n";
    send(client_fd, header, strlen(header), 0);

    // Gửi phần đầu HTML
    char html_buffer[2048];
    snprintf(html_buffer, sizeof(html_buffer),
             "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Index of %s</title>"
             "<style>body{font-family: Arial;} a{text-decoration: none; color: #0066cc;} "
             "a:hover{text-decoration: underline;} li{margin: 8px 0;}</style>"
             "</head><body><h2>Thư mục: %s</h2><hr><ul>", req_path, req_path);
    send(client_fd, html_buffer, strlen(html_buffer), 0);

    // Nếu không phải thư mục gốc, thêm nút quay lại ".."
    if (strcmp(req_path, "/") != 0) {
        // Tìm dấu '/' cuối cùng để tạo đường dẫn thư mục cha
        char parent_path[512];
        strcpy(parent_path, req_path);
        char *last_slash = strrchr(parent_path, '/');
        if (last_slash && last_slash != parent_path) *last_slash = '\0';
        else strcpy(parent_path, "/");

        snprintf(html_buffer, sizeof(html_buffer), "<li><b><a href='%s'>[Quay lại thư mục cha]</a></b></li>", parent_path);
        send(client_fd, html_buffer, strlen(html_buffer), 0);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Bỏ qua thư mục hiện tại (.) và cha (..) mặc định của hệ thống
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        // Xây dựng đường dẫn vật lý để kiểm tra là file hay thư mục
        char full_local_path[1024];
        snprintf(full_local_path, sizeof(full_local_path), "%s/%s", local_path, entry->d_name);

        struct stat st;
        if (stat(full_local_path, &st) == -1) continue;

        // Xây dựng đường dẫn URL trên trình duyệt
        char url_path[1024];
        if (strcmp(req_path, "/") == 0) {
            snprintf(url_path, sizeof(url_path), "/%s", entry->d_name);
        } else {
            snprintf(url_path, sizeof(url_path), "%s/%s", req_path, entry->d_name);
        }

        // Tạo dòng HTML: Đậm cho thư mục, Nghiêng cho file
        if (S_ISDIR(st.st_mode)) {
            // Là thư mục: In Đậm (<b>)
            snprintf(html_buffer, sizeof(html_buffer), "<li><b><a href='%s'>📁 %s/</a></b></li>", url_path, entry->d_name);
        } else {
            // Là file: In Nghiêng (<i>)
            snprintf(html_buffer, sizeof(html_buffer), "<li><i><a href='%s'>📄 %s</a></i></li>", url_path, entry->d_name);
        }
        send(client_fd, html_buffer, strlen(html_buffer), 0);
    }

    closedir(dir);

    // Gửi phần đuôi HTML
    char *footer = "</ul><hr><p>C HTTP File Server</p></body></html>";
    send(client_fd, footer, strlen(footer), 0);
}

// Xử lý request HTTP từ trình duyệt
void handle_client(int client_fd) {
    char req_buf[BUFFER_SIZE];
    int received = recv(client_fd, req_buf, sizeof(req_buf) - 1, 0);
    if (received <= 0) return;
    req_buf[received] = '\0';

    char method[16], raw_path[512], protocol[16];
    sscanf(req_buf, "%s %s %s", method, raw_path, protocol);

    // Chỉ hỗ trợ lệnh GET để tải file/thư mục
    if (strcmp(method, "GET") != 0) {
        char *err = "HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    // Giải mã URL (xử lý tên file có dấu cách)
    char decoded_path[512];
    url_decode(raw_path, decoded_path);

    // Bảo mật: Ngăn chặn truy cập ra ngoài thư mục server bằng ".."
    if (strstr(decoded_path, "..")) {
        char *err = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n<h1>403 Forbidden: Invalid Path</h1>";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    // Lấy thư mục gốc (nơi chạy file exe làm thư mục gốc `.`)
    char local_path[1024];
    if (strcmp(decoded_path, "/") == 0) {
        strcpy(local_path, "."); // Trỏ vào thư mục hiện tại
    } else {
        snprintf(local_path, sizeof(local_path), ".%s", decoded_path);
    }

    // Kiểm tra thông tin đường dẫn
    struct stat path_stat;
    if (stat(local_path, &path_stat) == -1) {
        // File hoặc thư mục không tồn tại
        char *err = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n<h1>404 Not Found</h1>";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    if (S_ISDIR(path_stat.st_mode)) {
        // Nếu là Thư Mục -> Gửi trang liệt kê HTML
        serve_directory(client_fd, local_path, decoded_path);
    } else if (S_ISREG(path_stat.st_mode)) {
        // Nếu là File -> Đọc và gửi nội dung file
        serve_file(client_fd, local_path);
    }
}

int main() {
    // Bỏ qua lỗi SIGPIPE (tránh server bị crash khi user tắt video giữa chừng)
    signal(SIGPIPE, SIG_IGN);

    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("socket error");
        return 1;
    }

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind error");
        return 1;
    }

    if (listen(listener, 10) == -1) {
        perror("listen error");
        return 1;
    }

    printf("=== HTTP File Server đang chạy tại http://localhost:%d ===\n", PORT);
    printf("Thu muc goc la thu muc hien tai dang chay server.\n");
    printf("Nhan Ctrl+C de dung.\n\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listener, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) continue;

        printf("[+] Request tu %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        handle_client(client_fd);

        close(client_fd);
    }

    close(listener);
    return 0;
}