#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

#define TCP_PORT 5100  // 포트 번호
#define MAX_CLIENTS 10 // 최대 클라이언트 수
#define BUF_SIZE 100   // 메시지 버퍼 크기

static int g_noc = 0;
static int client_sockets[MAX_CLIENTS];

void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void set_nonblocking(int sock)
{
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

void broadcast_message(char *message, int sender_index)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (i != sender_index && client_sockets[i] != -1) {
            send(client_sockets[i], message, strlen(message), MSG_DONTWAIT);
        }
    }
    printf("브로드캐스트 메시지: %s", message);
    fflush(stdout);
}

void handle_client(int client_index)
{
    char mesg[BUF_SIZE];
    ssize_t str_len;

    str_len = recv(client_sockets[client_index], mesg, sizeof(mesg) - 1, 0);
    if (str_len > 0) {
        mesg[str_len] = 0;
        printf("클라이언트 %d로부터 받은 메시지: %s", client_index, mesg);
        fflush(stdout);
        char broadcast_msg[BUF_SIZE + 20];
        snprintf(broadcast_msg, sizeof(broadcast_msg), "클라이언트 %d: %s", client_index, mesg);
        broadcast_message(broadcast_msg, client_index);
    } else if (str_len == 0 || (str_len == -1 && errno != EWOULDBLOCK)) {
        printf("클라이언트 %d 연결 종료\n", client_index);
        close(client_sockets[client_index]);
        client_sockets[client_index] = -1;
        g_noc--;
    }
}

int main(int argc, char **argv)
{
    int ssock, portno;
    socklen_t clen;
    struct sockaddr_in servaddr, cliaddr;
    char mesg[BUF_SIZE];

    portno = (argc == 2) ? atoi(argv[1]) : TCP_PORT;

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(portno);

    if (bind(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind()");
        return -1;
    }

    if (listen(ssock, 8) < 0) {
        perror("listen()");
        return -1;
    }

    printf("서버가 시작되었습니다. 포트 %d에서 대기 중...\n", portno);
    fflush(stdout);

    set_nonblocking(ssock);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = -1;
    }

    clen = sizeof(cliaddr);
    while (1) {
        int csock = accept(ssock, (struct sockaddr *)&cliaddr, &clen);
        if (csock < 0) {
            if (errno != EWOULDBLOCK) {
                perror("accept()");
            }
        } else {
            inet_ntop(AF_INET, &cliaddr.sin_addr, mesg, BUF_SIZE);
            printf("클라이언트 연결됨: %s\n", mesg);
            fflush(stdout);

            if (g_noc >= MAX_CLIENTS) {
                printf("최대 클라이언트 수 초과\n");
                close(csock);
            } else {
                set_nonblocking(csock);
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (client_sockets[i] == -1) {
                        client_sockets[i] = csock;
                        g_noc++;
                        break;
                    }
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] != -1) {
                handle_client(i);
            }
        }
    }

    close(ssock);
    return 0;
}
