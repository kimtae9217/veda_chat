#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/socket.h>

#define TCP_PORT 5100
#define MAX_CLIENTS 50
#define BUF_SIZE 100
#define NICKNAME_SIZE 20

typedef enum {
    MSG_NICKNAME,
    MSG_CHAT,
    MSG_LOGOUT
} MessageType;

typedef struct {
    MessageType type;
    char nickname[NICKNAME_SIZE];
    char content[BUF_SIZE];
} ChatMessage;

static int g_noc = 0;
static int client_sockets[MAX_CLIENTS];
static int pipes_to_child[MAX_CLIENTS][2];
static int pipes_to_parent[MAX_CLIENTS][2];
static char nicknames[MAX_CLIENTS][NICKNAME_SIZE];

void sigchld_handler(int s) {
    int saved_errno = errno;
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        g_noc--;
    }
    errno = saved_errno;

    if (g_noc == 0) {
        printf("모든 클라이언트 연결이 종료되었습니다. 서버를 종료합니다.\n");
        exit(0);  // 서버 즉시 종료
    }
}

void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

void send_message(ChatMessage *message, int sender_index) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] != -1 && i != sender_index) {
            send(client_sockets[i], message, sizeof(ChatMessage), 0);
        }
    }
    printf("[%s] %s", message->nickname, message->content);
    fflush(stdout);
}

void handle_client(int client_index) {
    ChatMessage message;
    ssize_t str_len;

    while (1) {
        str_len = read(pipes_to_child[client_index][0], &message, sizeof(ChatMessage));
        if (str_len > 0) {
            if (message.type == MSG_NICKNAME) {
                strncpy(nicknames[client_index], message.nickname, NICKNAME_SIZE - 1);
                nicknames[client_index][NICKNAME_SIZE - 1] = '\0';
                printf(" 클라이언트 %d의 닉네임: %s\n", client_index, nicknames[client_index]);
                
                ChatMessage complete_msg = {MSG_CHAT, "Server", ""};
                snprintf(complete_msg.content, BUF_SIZE, "[NICKNAME_SET]%d", client_index);
                write(pipes_to_parent[client_index][1], &complete_msg, sizeof(ChatMessage));
            } else if (message.type == MSG_LOGOUT) {
                printf("클라이언트 %s(ID: %d) 연결 종료\n", nicknames[client_index], client_index);
                ChatMessage logout_msg = {MSG_LOGOUT, "Server", ""};
                snprintf(logout_msg.content, BUF_SIZE, "%s 님께서 퇴장했습니다.\n", nicknames[client_index]);
                write(pipes_to_parent[client_index][1], &logout_msg, sizeof(ChatMessage));
                break;
            } else {
                write(pipes_to_parent[client_index][1], &message, sizeof(ChatMessage));
            }
            fflush(stdout);
        } else if (str_len <= 0) {
            break;
        }
    }

    close(client_sockets[client_index]);
    close(pipes_to_child[client_index][0]);
    close(pipes_to_parent[client_index][1]);
    exit(0);
}

int main(int argc, char **argv) {
    int ssock, portno;
    socklen_t clen;
    struct sockaddr_in servaddr, cliaddr;
    ChatMessage mesg;

    portno = (argc == 2) ? atoi(argv[1]) : TCP_PORT;

    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction (SIGCHLD)");
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

    printf("VEAD 서버가 시작되었습니다. 포트 %d에서 대기 중...\n", portno);
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
            char ip[BUF_SIZE];
            inet_ntop(AF_INET, &cliaddr.sin_addr, ip, BUF_SIZE);
            int client_index;
            for (client_index = 0; client_index < MAX_CLIENTS; client_index++) {
                if (client_sockets[client_index] == -1) {
                    break;
                }
            }
            printf("새 클라이언트 연결: ID %d, IP %s\n", client_index, ip);
            fflush(stdout);

            if (g_noc >= MAX_CLIENTS) {
                printf("최대 클라이언트 수 초과\n");
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
                        g_noc--;
                    }
                }
            }
        } else if (csock < 0 && errno != EWOULDBLOCK) {
            perror("accept()");
        }

        // 모든 클라이언트로부터 메시지 읽기
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] != -1) {
                ssize_t str_len = recv(client_sockets[i], &mesg, sizeof(ChatMessage), MSG_DONTWAIT);
                if (str_len > 0) {
                    write(pipes_to_child[i][1], &mesg, sizeof(ChatMessage));
                } else if (str_len == 0 || (str_len == -1 && errno != EWOULDBLOCK)) {
                    // 클라이언트 연결 종료 처리
                    close(client_sockets[i]);
                    close(pipes_to_child[i][1]);
                    close(pipes_to_parent[i][0]);
                    client_sockets[i] = -1;
                    pipes_to_child[i][1] = -1;
                    pipes_to_parent[i][0] = -1;
                }
            }
        }

        // 자식 프로세스로부터 메시지 읽기 및 메세지 보내기
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (pipes_to_parent[i][0] != -1) {
                ssize_t str_len = read(pipes_to_parent[i][0], &mesg, sizeof(ChatMessage));
                if (str_len > 0) {
                    if (strncmp(mesg.content, "[NICKNAME_SET]", 14) == 0) {
                        int client_id = atoi(mesg.content + 14);
                        //printf("클라이언트 %d의 닉네임 설정 완료\n", client_id);
                    } else {
                        send_message(&mesg, i);
                    }
                }
            }
        }

        // 잠시 대기하여 CPU 사용률 감소
        usleep(10000);  // 10ms 대기
    }

    // 서버 종료 전 정리 작업
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] != -1) {
            close(client_sockets[i]);
        }
        if (pipes_to_child[i][1] != -1) {
            close(pipes_to_child[i][1]);
        }
        if (pipes_to_parent[i][0] != -1) {
            close(pipes_to_parent[i][0]);
        }
    }
    close(ssock);
    printf("서버가 정상적으로 종료되었습니다.\n");
    return 0;
}
