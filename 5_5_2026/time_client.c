#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    const char *ip   = (argc > 1) ? argv[1] : "127.0.0.1";
    int          port = (argc > 2) ? atoi(argv[2]) : 9000;

    int client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port        = htons(port);

    if (connect(client, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect"); return 1;
    }
    printf("Connected. Type: GET_TIME <format>  (Ctrl+C to quit)\n");

    char buf[256];
    while (1) {
        printf("> ");
        if (!fgets(buf, sizeof(buf), stdin)) break;
        send(client, buf, strlen(buf), 0);
        int len = recv(client, buf, sizeof(buf)-1, 0);
        if (len <= 0) break;
        buf[len] = '\0';
        printf("Server: %s", buf);
    }
    close(client);
    return 0;
}