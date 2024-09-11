#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>

#define BUF_SIZE 100

void error_handling(char *message);
void set_nonblocking(int sock);

int main(int argc, char *argv[]) {
    int sock;
    char message[BUF_SIZE];
    int str_len;
    struct sockaddr_in serv_adr;

    if (argc != 3) {
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_adr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
        error_handling("connect() error");
    else
        puts("Connected...........");

    set_nonblocking(sock);
    set_nonblocking(STDIN_FILENO);

    while (1) {
        // 서버로부터 메시지 수신 시도
        str_len = recv(sock, message, BUF_SIZE - 1, 0);
        if (str_len > 0) {
            message[str_len] = 0;
            printf("서버로부터 받은 메시지: %s", message);
            fflush(stdout);
        } else if (str_len == -1 && errno != EWOULDBLOCK) {
            error_handling("recv() error");
        } else if (str_len == 0) {
            printf("서버 연결 종료\n");
            break;
        }

        // 사용자 입력 확인
        str_len = read(STDIN_FILENO, message, BUF_SIZE - 1);
        if (str_len > 0) {
            message[str_len] = 0;
            if (!strcmp(message, "q\n") || !strcmp(message, "Q\n")) {
                break;
            }
            send(sock, message, strlen(message), 0);
        } else if (str_len == -1 && errno != EWOULDBLOCK) {
            error_handling("read() error");
        }
    }

    close(sock);
    return 0;
}

void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

void error_handling(char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}