#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>

#define MAX_CLIENTS  64     // Số client tối đa 
#define BUF_SIZE     1024   // Kích thước buffer
#define ID_SIZE      64     // Kích thước tối đa client_id
#define NAME_SIZE    128    // Kích thước tối đa client_name

//  Cấu trúc lưu thông tin mỗi client 
typedef struct {
    int  fd;                    
    int  registered;             // 1 = đã đăng ký tên, 0 = chưa
    char client_id[ID_SIZE];     // ID client 
    char client_name[NAME_SIZE]; // Tên client 
} ClientInfo;

static ClientInfo  clients[MAX_CLIENTS]; 
static struct pollfd fds[MAX_CLIENTS];   
static int nfds = 0;                

// Trả về chuỗi thời gian vào buf
static void get_timestamp(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, len, "%Y/%m/%d %H:%M:%S", t);
}

static void trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r'))
        s[--len] = '\0';
}

// Gửi chuỗi msg đến socket fd
static int send_msg(int fd, const char *msg) {
    return send(fd, msg, strlen(msg), 0);
}

// Xóa client ở vị trí idx, dịch chuyển mảng lấp chỗ trống
static void remove_client(int idx) {
    printf("[Server] Client fd=%d (id='%s') ngắt kết nối.\n",
           fds[idx].fd, clients[idx].client_id);
    close(fds[idx].fd);

    // Dịch chuyển các phần tử phía sau lên 1 vị trí
    for (int i = idx; i < nfds - 1; i++) {
        fds[i]     = fds[i + 1];
        clients[i] = clients[i + 1];
    }

    // Xóa phần tử cuối khỏi mảng
    memset(&fds[nfds-1],     0, sizeof(struct pollfd));
    memset(&clients[nfds-1], 0, sizeof(ClientInfo));
    fds[nfds-1].fd = -1;
    nfds--;
}

// Gửi msg đến tất cả client đã đăng ký, trừ sender_fd
static void broadcast(int sender_fd, const char *msg) {
    for (int i = 1; i < nfds; i++) {         
        if (fds[i].fd == sender_fd) continue;  
        if (!clients[i].registered)  continue; 
        send_msg(fds[i].fd, msg);
    }
}

// Phân tích chuỗi "client_id: client_name".
static int parse_registration(const char *line,
                               char *out_id,   size_t id_sz,
                               char *out_name, size_t name_sz) {
    // Tìm dấu : phân cách id và tên
    const char *colon = strchr(line, ':');
    if (colon == NULL) return 0;  // Không có dấu :, sai cú pháp

    // Tách client_id 
    size_t id_len = colon - line;
    if (id_len == 0 || id_len >= id_sz) return 0;
    strncpy(out_id, line, id_len);
    out_id[id_len] = '\0';
    trim_newline(out_id);

    // Tách client_name, bỏ khoảng trắng đầu
    const char *name_start = colon + 1;
    while (*name_start == ' ') name_start++;
    if (*name_start == '\0') return 0;  // Tên rỗng

    strncpy(out_name, name_start, name_sz - 1);
    out_name[name_sz - 1] = '\0';
    trim_newline(out_name);

    return (strlen(out_name) > 0) ? 1 : 0;
}

// Xử lý client chưa đăng ký 

static int handle_unregistered(int idx) {
    char buf[BUF_SIZE];
    int ret = recv(fds[idx].fd, buf, sizeof(buf) - 1, 0);
    if (ret <= 0) return -1;  

    buf[ret] = '\0';
    trim_newline(buf);

    char new_id[ID_SIZE], new_name[NAME_SIZE];

    // Kiểm tra cú pháp đăng ký
    if (!parse_registration(buf, new_id, sizeof(new_id),
                                  new_name, sizeof(new_name))) {
        // Sai cú pháp thì yêu cầu thử lại
        send_msg(fds[idx].fd,
                 "Cú pháp không đúng. Vui lòng gửi: client_id: client_name\n");
        return 0;
    }

    // Lưu thông tin client vào mảng
    strncpy(clients[idx].client_id,   new_id,   sizeof(clients[idx].client_id)   - 1);
    strncpy(clients[idx].client_name, new_name, sizeof(clients[idx].client_name) - 1);
    clients[idx].registered = 1;

    printf("[Server] Đã đăng ký: fd=%d, id='%s', name='%s'\n",
           fds[idx].fd, new_id, new_name);

    // Xác nhận thành công cho client vừa đăng ký 
    char ack[BUF_SIZE]; 
    snprintf(ack, sizeof(ack),
             "Chào mừng %s (%s) đã vào phòng chat!\n", new_name, new_id);
    send_msg(fds[idx].fd, ack);

    // Thông báo cho toàn phòng chat
    char notice[BUF_SIZE];
    snprintf(notice, sizeof(notice),
             "[Server] %s (%s) vừa tham gia phòng chat.\n", new_name, new_id);
    broadcast(fds[idx].fd, notice);

    return 0;
}

//  Xử lý tin nhắn từ client đã đăng ký 

static int handle_registered(int idx) {
    char buf[BUF_SIZE];
    int ret = recv(fds[idx].fd, buf, sizeof(buf) - 1, 0);
    if (ret <= 0) {
        // Thông báo cho phòng chat biết người này đã rời đi
        char notice[BUF_SIZE];
        snprintf(notice, sizeof(notice),
                 "[Server] %s (%s) đã rời khỏi phòng chat.\n",
                 clients[idx].client_name, clients[idx].client_id);
        broadcast(fds[idx].fd, notice);
        return -1;
    }

    buf[ret] = '\0';
    trim_newline(buf);
    if (strlen(buf) == 0) return 0;  // Bỏ qua tin nhắn rỗng

    // Lấy thời gian hiện tại
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    // Tạo thông điệp broadcast: "[timestamp] client_id: nội dung"
    char msg[BUF_SIZE + ID_SIZE + 64];
    snprintf(msg, sizeof(msg),
             "[%s] %s: %s\n", timestamp, clients[idx].client_id, buf);

    printf("[Chat] %s", msg);   // In ra màn hình server để theo dõi

    broadcast(fds[idx].fd, msg);
    return 0;
}

// Khởi tạo socket listener 

static int create_listener(int port) {
    // Tạo socket TCP
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) { perror("socket()"); return -1; }

    // Cho phép tái sử dụng cổng ngay khi server khởi động lại
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Gắn địa chỉ
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // Lắng nghe trên mọi giao diện mạng
    addr.sin_port        = htons((uint16_t)port);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind()"); close(listener); return -1;
    }

    // Bắt đầu lắng nghe, hàng đợi tối đa 10 kết nối chờ
    if (listen(listener, 10) == -1) {
        perror("listen()"); close(listener); return -1;
    }

    printf("[Server] Đang lắng nghe trên cổng %d ...\n", port);
    return listener;
}

int main(int argc, char *argv[]) {
    // Kiểm tra tham số dòng lệnh
    if (argc != 2) {
        fprintf(stderr, "Cách dùng: %s <port>\n  VD: %s 9090\n", argv[0], argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Cổng không hợp lệ: %s\n", argv[1]);
        return 1;
    }

    // Khởi tạo listener
    int listener = create_listener(port);
    if (listener == -1) return 1;

    // Khởi tạo mảng pollfd và clients
    memset(fds,     0, sizeof(fds));
    memset(clients, 0, sizeof(clients));

    // Vị trí 0 luôn là listener socket
    fds[0].fd     = listener;
    fds[0].events = POLLIN;    // Quan tâm sự kiện có kết nối mới
    nfds = 1;

    // Vòng lặp chính 
    while (1) {
        // poll() chờ sự kiện trên bất kỳ socket nào
        int ret = poll(fds, nfds, -1);
        if (ret == -1) { perror("poll()"); break; }

        // Duyệt qua tất cả socket để tìm socket có sự kiện
        for (int i = 0; i < nfds; i++) {

            // Kiểm tra có sự kiện đọc hoặc lỗi không
            if (!(fds[i].revents & (POLLIN | POLLERR))) continue;

            if (fds[i].fd == listener) {
                struct sockaddr_in caddr;
                socklen_t clen = sizeof(caddr);
                int cfd = accept(listener, (struct sockaddr *)&caddr, &clen);
                if (cfd == -1) { perror("accept()"); continue; }

                if (nfds >= MAX_CLIENTS) {
                    send_msg(cfd, "Server đầy, vui lòng thử lại sau.\n");
                    close(cfd);
                    printf("[Server] Từ chối kết nối: đã đủ %d client.\n", MAX_CLIENTS);
                    continue;
                }

                // Thêm client mới vào mảng
                fds[nfds].fd        = cfd;
                fds[nfds].events    = POLLIN;
                clients[nfds].fd    = cfd;
                clients[nfds].registered = 0;  
                memset(clients[nfds].client_id,   0, ID_SIZE);
                memset(clients[nfds].client_name, 0, NAME_SIZE);
                nfds++;

                printf("[Server] Kết nối mới: fd=%d, IP=%s\n",
                       cfd, inet_ntoa(caddr.sin_addr));

                // Hướng dẫn đăng ký
                send_msg(cfd,
                    "=== Chào mừng đến phòng chat! ===\n"
                    "Vui lòng đăng ký theo cú pháp:\n"
                    "  client_id: client_name\n");
                continue;
            }

            int rc;
            if (!clients[i].registered) {
                // Chưa đăng ký → xử lý bước đăng ký
                rc = handle_unregistered(i);
            } else {
                // Đã đăng ký → xử lý tin nhắn chat
                rc = handle_registered(i);
            }

            // Nếu client ngắt kết nối → xóa khỏi danh sách
            if (rc == -1) {
                remove_client(i);
                i--;  // Sau khi xóa, phần tử tại i đã thay bởi phần tử kế tiếp
            }

        } 
    } 

    // Dọn dẹp trước khi thoát
    for (int i = 0; i < nfds; i++)
        if (fds[i].fd != -1) close(fds[i].fd);

    printf("[Server] Đã dừng.\n");
    return 0;
}