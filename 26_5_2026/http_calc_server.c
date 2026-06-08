#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT        8080
#define BACKLOG     10
#define BUF_SIZE    8192

// Giải mã URL encoding
void url_decode(const char *src, char *dst, int dst_len) {
    int i = 0, j = 0;
    while (src[i] && j < dst_len - 1) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            // Chuyển 2 ký tự hex thành byte
            char hex[3] = { src[i+1], src[i+2], '\0' };
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            // Dấu + trong query string là khoảng trắng
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}

// Lấy giá trị của tham số từ query string
int get_param(const char *query, const char *key, char *value, int val_len) {
    char buf[BUF_SIZE];
    char search[64];
    snprintf(search, sizeof(search), "%s=", key);

    const char *pos = strstr(query, search);
    if (!pos) return 0;

    pos += strlen(search); // trỏ tới đầu giá trị

    int i = 0;
    while (pos[i] && pos[i] != '&' && i < val_len - 1) {
        buf[i] = pos[i];
        i++;
    }
    buf[i] = '\0';

    url_decode(buf, value, val_len);
    return 1;
}

// Thực hiện phép tính
int calculate(const char *op, double a, double b, double *result, char *err_msg) {
    if (strcmp(op, "add") == 0 || strcmp(op, "+") == 0) {
        *result = a + b;
    } else if (strcmp(op, "sub") == 0 || strcmp(op, "-") == 0) {
        *result = a - b;
    } else if (strcmp(op, "mul") == 0 || strcmp(op, "*") == 0 || strcmp(op, "x") == 0) {
        *result = a * b;
    } else if (strcmp(op, "div") == 0 || strcmp(op, "/") == 0) {
        if (b == 0.0) {
            snprintf(err_msg, 128, "Lỗi: Không thể chia cho 0");
            return 0;
        }
        *result = a / b;
    } else {
        snprintf(err_msg, 128, "Lỗi: Toán tử không hợp lệ: %s", op);
        return 0;
    }
    return 1;
}

// Trả về ký hiệu hiển thị
const char *op_symbol(const char *op) {
    if (strcmp(op, "add") == 0 || strcmp(op, "+") == 0) return "+";
    if (strcmp(op, "sub") == 0 || strcmp(op, "-") == 0) return "-";
    if (strcmp(op, "mul") == 0 || strcmp(op, "*") == 0 || strcmp(op, "x") == 0) return "×";
    if (strcmp(op, "div") == 0 || strcmp(op, "/") == 0) return "÷";
    return op;
}

// Tạo trang HTML
void build_html(char *html_buf, int buf_len,
                const char *op_val, const char *a_val, const char *b_val,
                const char *result_str, const char *method_used) {

    char sel_add[12] = "", sel_sub[12] = "", sel_mul[12] = "", sel_div[12] = "";
    if (strcmp(op_val, "add") == 0) snprintf(sel_add, sizeof(sel_add), "selected");
    else if (strcmp(op_val, "sub") == 0) snprintf(sel_sub, sizeof(sel_sub), "selected");
    else if (strcmp(op_val, "mul") == 0) snprintf(sel_mul, sizeof(sel_mul), "selected");
    else if (strcmp(op_val, "div") == 0) snprintf(sel_div, sizeof(sel_div), "selected");

    snprintf(html_buf, buf_len,
        "<!DOCTYPE html>\n"
        "<html lang='vi'>\n"
        "<head>\n"
        "  <meta charset='UTF-8'>\n"
        "  <meta name='viewport' content='width=device-width, initial-scale=1.0'>\n"
        "  <title>Máy Tính HTTP Server</title>\n"
        "  <style>\n"
        "    body { font-family: Arial, sans-serif; background: #f0f2f5;\n"
        "           display: flex; justify-content: center; padding: 40px; }\n"
        "    .card { background: white; border-radius: 12px; padding: 32px;\n"
        "            box-shadow: 0 4px 16px rgba(0,0,0,0.12); width: 380px; }\n"
        "    h1 { margin: 0 0 8px; color: #1a1a2e; font-size: 22px; }\n"
        "    .sub { color: #888; font-size: 13px; margin-bottom: 24px; }\n"
        "    label { display: block; font-size: 13px; color: #555;\n"
        "            margin-bottom: 4px; font-weight: 600; }\n"
        "    input[type=number], select {\n"
        "      width: 100%%; padding: 10px 12px; border: 1px solid #ddd;\n"
        "      border-radius: 8px; font-size: 15px; box-sizing: border-box;\n"
        "      margin-bottom: 16px; outline: none; }\n"
        "    input[type=number]:focus, select:focus { border-color: #4a90e2; }\n"
        "    .btn-row { display: flex; gap: 10px; margin-top: 4px; }\n"
        "    button { flex: 1; padding: 11px; border: none; border-radius: 8px;\n"
        "             font-size: 14px; font-weight: 600; cursor: pointer; }\n"
        "    .btn-get  { background: #4a90e2; color: white; }\n"
        "    .btn-post { background: #27ae60; color: white; }\n"
        "    .btn-get:hover  { background: #357abd; }\n"
        "    .btn-post:hover { background: #1e8449; }\n"
        "    .result { margin-top: 24px; padding: 16px 20px;\n"
        "              background: #eaf4ff; border-radius: 10px;\n"
        "              border-left: 4px solid #4a90e2; }\n"
        "    .result.error { background: #fff0f0; border-color: #e74c3c; }\n"
        "    .result .label { font-size: 12px; color: #888; margin-bottom: 6px; }\n"
        "    .result .expr  { font-size: 18px; color: #1a1a2e; font-weight: bold; }\n"
        "    .method-badge { display: inline-block; padding: 2px 8px;\n"
        "      border-radius: 4px; font-size: 11px; font-weight: bold;\n"
        "      margin-left: 8px; vertical-align: middle; }\n"
        "    .get-badge  { background: #dceefb; color: #2471a3; }\n"
        "    .post-badge { background: #d5f5e3; color: #1e8449; }\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n"
        "<div class='card'>\n"
        "  <h1>Máy Tính</h1>\n"
        "  <p class='sub'>HTTP Server C — hỗ trợ GET &amp; POST</p>\n"
        "\n"
        "  <form method='GET' action='/'>\n"
        "    <label>Toán hạng A</label>\n"
        "    <input type='number' name='a' step='any' value='%s' required>\n"
        "    <label>Toán tử</label>\n"
        "    <select name='op'>\n"
        "      <option value='add' %s>+ Cộng</option>\n"
        "      <option value='sub' %s>- Trừ</option>\n"
        "      <option value='mul' %s>× Nhân</option>\n"
        "      <option value='div' %s>÷ Chia</option>\n"
        "    </select>\n"
        "    <label>Toán hạng B</label>\n"
        "    <input type='number' name='b' step='any' value='%s' required>\n"
        "    <div class='btn-row'>\n"
        "      <button type='submit' class='btn-get'>Tính bằng GET</button>\n"
        "      <button type='submit' formmethod='POST' class='btn-post'>Tính bằng POST</button>\n"
        "    </div>\n"
        "  </form>\n"
        "\n"
        "  %s\n"
        "</div>\n"
        "</body></html>\n",
        a_val, sel_add, sel_sub, sel_mul, sel_div, b_val, result_str
    );
}

// Gửi HTTP response
void send_response(int client_fd, int status_code, const char *body) {
    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code,
        (status_code == 200) ? "OK" : "Bad Request",
        strlen(body)
    );
    send(client_fd, header, strlen(header), 0);
    send(client_fd, body, strlen(body), 0);
}

// Xử lý một kết nối HTTP
void handle_client(int client_fd) {
    char req_buf[BUF_SIZE];
    int received = recv(client_fd, req_buf, sizeof(req_buf) - 1, 0);
    if (received <= 0) return;
    req_buf[received] = '\0';

    // ĐẢM BẢO ĐỌC HẾT BODY CỦA POST
    char *body_start = strstr(req_buf, "\r\n\r\n");
    if (body_start) {
        char *cl_ptr = strstr(req_buf, "Content-Length:");
        if (cl_ptr) {
            int content_length = atoi(cl_ptr + 15);
            int header_length = (body_start + 4) - req_buf;
            int body_received = received - header_length;

            // Nếu body nhận được ít hơn Content-Length, phải đọc thêm cho đủ
            while (body_received < content_length && received < BUF_SIZE - 1) {
                int r = recv(client_fd, req_buf + received, BUF_SIZE - 1 - received, 0);
                if (r > 0) {
                    received += r;
                    body_received += r;
                    req_buf[received] = '\0';
                } else {
                    break;
                }
            }
            // Cập nhật lại body_start phòng trường hợp buffer thay đổi
            body_start = strstr(req_buf, "\r\n\r\n");
        }
    }

    // Xác định phương thức (GET / POST)
    char method[8] = "";
    sscanf(req_buf, "%7s", method);

    char query[BUF_SIZE] = "";
    char method_used[8] = "";

    if (strcmp(method, "GET") == 0) {
        strncpy(method_used, "GET", sizeof(method_used));
        char path[512] = "";
        sscanf(req_buf, "GET %511s", path);
        char *q = strchr(path, '?');
        if (q) strncpy(query, q + 1, sizeof(query) - 1);

    } else if (strcmp(method, "POST") == 0) {
        strncpy(method_used, "POST", sizeof(method_used));
        if (body_start) {
            strncpy(query, body_start + 4, sizeof(query) - 1);
        }
    } else {
        send_response(client_fd, 400, "<h1>400 Bad Request</h1><p>Chỉ hỗ trợ GET và POST.</p>");
        return;
    }

    // Không có tham số => hiển thị form trống
    if (strlen(query) == 0) {
        char html[8192];
        build_html(html, sizeof(html), "add", "", "", "", "");
        send_response(client_fd, 200, html);
        return;
    }

    // Parse tham số: op, a, b
    char op_raw[32] = "", a_raw[32] = "", b_raw[32] = "";
    get_param(query, "op", op_raw, sizeof(op_raw));
    get_param(query, "a",  a_raw,  sizeof(a_raw));
    get_param(query, "b",  b_raw,  sizeof(b_raw));

    if (strlen(op_raw) == 0 || strlen(a_raw) == 0 || strlen(b_raw) == 0) {
        char html[8192];
        char err_block[256];
        snprintf(err_block, sizeof(err_block),
            "<div class='result error'>"
            "<div class='label'>Lỗi</div>"
            "<div class='expr'>Thiếu tham số. Cần có: op, a, b</div>"
            "</div>");
        build_html(html, sizeof(html), op_raw, a_raw, b_raw, err_block, method_used);
        send_response(client_fd, 200, html);
        return;
    }

    char *end_ptr;
    double a = strtod(a_raw, &end_ptr);
    if (*end_ptr != '\0') {
        char html[8192];
        char err_block[256];
        snprintf(err_block, sizeof(err_block),
            "<div class='result error'>"
            "<div class='label'>Lỗi</div>"
            "<div class='expr'>Toán hạng A không hợp lệ: %s</div>"
            "</div>", a_raw);
        build_html(html, sizeof(html), op_raw, a_raw, b_raw, err_block, method_used);
        send_response(client_fd, 200, html);
        return;
    }

    double b = strtod(b_raw, &end_ptr);
    if (*end_ptr != '\0') {
        char html[8192];
        char err_block[256];
        snprintf(err_block, sizeof(err_block),
            "<div class='result error'>"
            "<div class='label'>Lỗi</div>"
            "<div class='expr'>Toán hạng B không hợp lệ: %s</div>"
            "</div>", b_raw);
        build_html(html, sizeof(html), op_raw, a_raw, b_raw, err_block, method_used);
        send_response(client_fd, 200, html);
        return;
    }

    // Thực hiện tính toán
    double result = 0.0;
    char err_msg[128] = "";
    char result_block[512] = "";

    const char *badge_class = (strcmp(method_used, "GET") == 0) ? "get-badge" : "post-badge";

    if (calculate(op_raw, a, b, &result, err_msg)) {
        char a_disp[32], b_disp[32], r_disp[32];
        if (a == (long long)a)      snprintf(a_disp, sizeof(a_disp), "%.0f", a);
        else                        snprintf(a_disp, sizeof(a_disp), "%g",   a);
        if (b == (long long)b)      snprintf(b_disp, sizeof(b_disp), "%.0f", b);
        else                        snprintf(b_disp, sizeof(b_disp), "%g",   b);
        if (result == (long long)result) snprintf(r_disp, sizeof(r_disp), "%.0f", result);
        else                             snprintf(r_disp, sizeof(r_disp), "%g",   result);

        snprintf(result_block, sizeof(result_block),
            "<div class='result'>"
            "  <div class='label'>"
            "    Kết quả"
            "    <span class='method-badge %s'>%s</span>"
            "  </div>"
            "  <div class='expr'>%s %s %s = <span style='color:#2471a3'>%s</span></div>"
            "</div>",
            badge_class, method_used,
            a_disp, op_symbol(op_raw), b_disp, r_disp
        );
    } else {
        snprintf(result_block, sizeof(result_block),
            "<div class='result error'>"
            "  <div class='label'>Lỗi <span class='method-badge %s'>%s</span></div>"
            "  <div class='expr'>%s</div>"
            "</div>",
            badge_class, method_used, err_msg
        );
    }

    char html[8192];
    build_html(html, sizeof(html), op_raw, a_raw, b_raw, result_block, method_used);
    send_response(client_fd, 200, html);
}

int main() {
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("socket() failed");
        return 1;
    }

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(PORT);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind() failed");
        return 1;
    }

    if (listen(listener, BACKLOG) == -1) {
        perror("listen() failed");
        return 1;
    }

    printf("=== HTTP Calc Server đang chạy tại http://localhost:%d ===\n", PORT);
    printf("Nhấn Ctrl+C để dừng.\n\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listener, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) continue;

        printf("[+] Kết nối từ %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        handle_client(client_fd);

        close(client_fd);
    }

    close(listener);
    return 0;
}