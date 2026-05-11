#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_PORT        8080
#define DEFAULT_NUM_PROC    4
#define BACKLOG             10
#define BUF_SIZE            4096
#define RESPONSE_SIZE       8192

//  Tạo HTTP response hoàn chỉnh
void build_response(const char *body, char *response, size_t resp_size)
{
    int body_len = (int)strlen(body);
    snprintf(response, resp_size,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        body_len, body);
}

//  Xử lý một yêu cầu HTTP từ client
void handle_client(int client, int worker_id)
{
    char buf[BUF_SIZE];
    char response[RESPONSE_SIZE];
    char body[RESPONSE_SIZE];

    // Nhận request từ client
    int ret = recv(client, buf, sizeof(buf) - 1, 0);
    if (ret <= 0) {
        close(client);
        return;
    }
    buf[ret] = '\0';

    // In request ra màn hình (worker nào xử lý)
    printf("[Worker %d | PID %d] Request received:\n%s\n",
           worker_id, getpid(), buf);

    // Tạo nội dung trang HTML trả về
    snprintf(body, sizeof(body),
        "<html>"
        "<head><meta charset='UTF-8'>"
        "<title>HTTP Preforking Server</title></head>"
        "<body>"
        "<h1>Xin chao cac ban!</h1>"
        "<p>Day la HTTP Server su dung ky thuat <b>Preforking</b>.</p>"
        "<hr>"
        "<p><b>Worker PID:</b> %d &nbsp;|&nbsp; <b>Worker ID:</b> %d</p>"
        "<pre>%s</pre>"
        "</body></html>",
        getpid(), worker_id, buf);

    // Đóng gói và gửi HTTP response
    build_response(body, response, sizeof(response));
    send(client, response, strlen(response), 0);

    // Đóng kết nối
    close(client);
}

//  Vòng lặp của mỗi worker process
void worker_loop(int listener, int worker_id)
{
    printf("[Worker %d | PID %d] Started.\n", worker_id, getpid());

    while (1) {
        // Chờ kết nối mới
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client = accept(listener,
                            (struct sockaddr *)&client_addr,
                            &addr_len);
        if (client == -1) {
            if (errno == EINTR) continue;   // bị ngắt bởi signal, thử lại
            perror("accept() failed");
            continue;
        }

        printf("[Worker %d | PID %d] New connection from %s:%d\n",
               worker_id, getpid(),
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        // Xử lý yêu cầu
        handle_client(client, worker_id);
    }
}

// Signal handler: dọn dẹp khi tiến trình cha kết thúc
void sigchld_handler(int signo)
{
    (void)signo;
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

int main(int argc, char *argv[])
{
    int port         = DEFAULT_PORT;
    int num_proc     = DEFAULT_NUM_PROC;

    if (argc >= 2) port     = atoi(argv[1]);
    if (argc >= 3) num_proc = atoi(argv[2]);

    // Tạo socket lắng nghe
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("socket() failed");
        return 1;
    }

    // Cho phép tái sử dụng địa chỉ ngay sau khi restart
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Gắn địa chỉ
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind() failed");
        return 1;
    }

    if (listen(listener, BACKLOG) == -1) {
        perror("listen() failed");
        return 1;
    }

    printf("=== HTTP Preforking Server ===\n");
    printf("Port         : %d\n", port);
    printf("Num workers  : %d\n", num_proc);
    printf("URL          : http://127.0.0.1:%d/\n\n", port);

    // Xử lý tiến trình con zombie
    signal(SIGCHLD, sigchld_handler);

    // Preforking: tạo sẵn num_proc tiến trình con
    for (int i = 0; i < num_proc; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork() failed");
            return 1;
        }
        if (pid == 0) {
            // Tiến trình con: chạy vòng lặp xử lý
            worker_loop(listener, i + 1);
            exit(EXIT_SUCCESS);
        }
        // Tiến trình cha tiếp tục tạo worker tiếp theo
    }

    // Tiến trình cha: chờ tất cả worker kết thúc
    printf("[Parent | PID %d] All %d workers started. Waiting...\n",
           getpid(), num_proc);

    int status;
    while (wait(&status) > 0)
        ;

    close(listener);
    printf("[Parent] Server stopped.\n");
    return 0;
}