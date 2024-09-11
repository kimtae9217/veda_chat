#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>

#define TCP_PORT 5100
#define MAX_CLIENTS 50
#define BUF_SIZE 100
#define NICKNAME_SIZE 20

static int g_noc = 0;
static int client_sockets[MAX_CLIENTS];
static int pipes_to_child[MAX_CLIENTS][2];
static int pipes_to_parent[MAX_CLIENTS][2];
static char nicknames[MAX_CLIENTS][NICKNAME_SIZE];

void sigchld_handler(int s) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        g_noc--;
    }
    errno = saved_errno;

    if (g_noc == 0) {
        printf("[시스템] 모든 클라이언트 연결이 종료되었습니다. 서버를 종료합니다.\n");
        exit(0);
    }
}

void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

void broadcast_message(char *message, int sender_index) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] != -1 && i != sender_index) {
            send(client_sockets[i], message, strlen(message), 0);
        }
    }
    printf("%s", message);
    fflush(stdout);
}

void handle_client(int client_index) {
    char mesg[BUF_SIZE];
    ssize_t str_len;

    while (1) {
        str_len = read(pipes_to_child[client_index][0], mesg, BUF_SIZE - 1);
        if (str_len > 0) {
            mesg[str_len] = 0;
            if (strncmp(mesg, "[NICKNAME]", 10) == 0) {
                strncpy(nicknames[client_index], mesg + 10, NICKNAME_SIZE - 1);
                nicknames[client_index][NICKNAME_SIZE - 1] = '\0';
                printf("[시스템] 클라이언트 %d의 닉네임: %s\n", client_index, nicknames[client_index]);
                
                // 닉네임 설정 완료 메시지를 부모 프로세스에게 전송
                char complete_msg[BUF_SIZE];
                snprintf(complete_msg, BUF_SIZE, "[NICKNAME_SET]%d", client_index);
                write(pipes_to_parent[client_index][1], complete_msg, strlen(complete_msg));
            } else {
                char broadcast_msg[BUF_SIZE + NICKNAME_SIZE + 20];
                snprintf(broadcast_msg, sizeof(broadcast_msg), "[%s] %s", nicknames[client_index], mesg);
                write(pipes_to_parent[client_index][1], broadcast_msg, strlen(broadcast_msg));
            }
            fflush(stdout);
        } else if (str_len == 0) {
            printf("[시스템] 클라이언트 %s(ID: %d) 연결 종료\n", nicknames[client_index], client_index);
            close(client_sockets[client_index]);
            close(pipes_to_child[client_index][0]);
            close(pipes_to_parent[client_index][1]);
            exit(0);
        }
    }
}

int main(int argc, char **argv) {
    int ssock, portno;
    socklen_t clen;
    struct sockaddr_in servaddr, cliaddr;
    char mesg[BUF_SIZE];

    portno = (argc == 2) ? atoi(argv[1]) : TCP_PORT;

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
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

    printf("[시스템] 서버가 시작되었습니다. 포트 %d에서 대기 중...\n", portno);
    fflush(stdout);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = -1;
        pipes_to_child[i][0] = -1;
        pipes_to_child[i][1] = -1;
        pipes_to_parent[i][0] = -1;
        pipes_to_parent[i][1] = -1;
        nicknames[i][0] = '\0';
    }

    clen = sizeof(cliaddr);
    set_nonblocking(ssock);

    while (1) {
        int csock = accept(ssock, (struct sockaddr *)&cliaddr, &clen);
        if (csock > 0) {
            inet_ntop(AF_INET, &cliaddr.sin_addr, mesg, BUF_SIZE);
            int client_index;
            for (client_index = 0; client_index < MAX_CLIENTS; client_index++) {
                if (client_sockets[client_index] == -1) {
                    break;
                }
            }
            printf("[시스템] 새 클라이언트 연결: ID %d, IP %s\n", client_index, mesg);
            fflush(stdout);

            if (g_noc >= MAX_CLIENTS) {
                printf("[시스템] 최대 클라이언트 수 초과\n");
                close(csock);
            } else {
                client_sockets[client_index] = csock;
                if (pipe(pipes_to_child[client_index]) == -1 || pipe(pipes_to_parent[client_index]) == -1) {
                    perror("pipe");
                    close(csock);
                } else {
                    g_noc++;
                    pid_t pid = fork();
                    if (pid == 0) {  // 자식 프로세스
                        close(ssock);
                        close(pipes_to_child[client_index][1]);
                        close(pipes_to_parent[client_index][0]);
                        handle_client(client_index);
                        exit(0);
                    } else if (pid > 0) {  // 부모 프로세스
                        close(pipes_to_child[client_index][0]);
                        close(pipes_to_parent[client_index][1]);
                        set_nonblocking(csock);
                        set_nonblocking(pipes_to_parent[client_index][0]);
                    } else {
                        perror("fork");
                        close(csock);
                        close(pipes_to_child[client_index][0]);
                        close(pipes_to_child[client_index][1]);
                        close(pipes_to_parent[client_index][0]);
                        close(pipes_to_parent[client_index][1]);
                    }
                }
            }
        } else if (csock < 0 && errno != EWOULDBLOCK) {
            perror("accept()");
        }

        // 모든 클라이언트로부터 메시지 읽기
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] != -1) {
                ssize_t str_len = recv(client_sockets[i], mesg, BUF_SIZE - 1, MSG_DONTWAIT);
                if (str_len > 0) {
                    mesg[str_len] = 0;
                    write(pipes_to_child[i][1], mesg, str_len);
                } else if (str_len == 0 || (str_len == -1 && errno != EWOULDBLOCK)) {
                    close(client_sockets[i]);
                    close(pipes_to_child[i][1]);
                    close(pipes_to_parent[i][0]);
                    client_sockets[i] = -1;
                    pipes_to_child[i][1] = -1;
                    pipes_to_parent[i][0] = -1;
                }
            }
        }

        // 자식 프로세스로부터 메시지 읽기 및 브로드캐스팅
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (pipes_to_parent[i][0] != -1) {
                ssize_t str_len = read(pipes_to_parent[i][0], mesg, BUF_SIZE - 1);
                if (str_len > 0) {
                    mesg[str_len] = 0;
                    if (strncmp(mesg, "[NICKNAME_SET]", 14) == 0) {
                        int client_id = atoi(mesg + 14);
                        printf("[시스템] 클라이언트 %d의 닉네임 설정 완료\n", client_id);
                    } else {
                        broadcast_message(mesg, i);
                    }
                }
            }
        }
    }

    close(ssock);
    return 0;
}