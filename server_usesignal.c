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
static pid_t child_pids[MAX_CLIENTS];

volatile sig_atomic_t sigusr1_received = 0;
volatile sig_atomic_t sigusr2_received = 0;
volatile sig_atomic_t all_children_terminated = 0;

void sigchld_handler(int s) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        g_noc--;
    }
    if (g_noc == 0) {
        all_children_terminated = 1;
    }
    errno = saved_errno;
}

void sigusr1_handler(int signo) {
    sigusr1_received = 1;
}

void sigusr2_handler(int signo) {
    sigusr2_received = 1;
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
                printf("클라이언트 %d의 닉네임: %s\n", client_index, nicknames[client_index]);
                
                ChatMessage complete_msg = {MSG_CHAT, "", ""};
                snprintf(complete_msg.content, BUF_SIZE, "[NICKNAME_SET]%d", client_index);
                write(pipes_to_parent[client_index][1], &complete_msg, sizeof(ChatMessage));
                
                // 새 클라이언트 연결을 알림
                kill(getppid(), SIGUSR1);
            } else if (message.type == MSG_LOGOUT) {
                printf("클라이언트 %s(ID: %d) 연결 종료\n", nicknames[client_index], client_index);
                ChatMessage logout_msg = {MSG_LOGOUT, "", ""};
                snprintf(logout_msg.content, BUF_SIZE, "[%s] 님께서 퇴장했습니다.\n", nicknames[client_index]);
                write(pipes_to_parent[client_index][1], &logout_msg, sizeof(ChatMessage));
                
                // 클라이언트 연결 종료를 알림
                kill(getppid(), SIGUSR2);
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

    portno = (argc == 2) ? atoi(argv[1]) : TCP_PORT;

    struct sigaction sa_chld, sa_usr1, sa_usr2;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction (SIGCHLD)");
        exit(1);
    }

    sa_usr1.sa_handler = sigusr1_handler;
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa_usr1, NULL) == -1) {
        perror("sigaction (SIGUSR1)");
        exit(1);
    }

    sa_usr2.sa_handler = sigusr2_handler;
    sigemptyset(&sa_usr2.sa_mask);
    sa_usr2.sa_flags = 0;
    if (sigaction(SIGUSR2, &sa_usr2, NULL) == -1) {
        perror("sigaction (SIGUSR2)");
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

    if (listen(ssock, 50) < 0) {
        perror("listen()");
        return -1;
    }

    printf("채팅 서버가 시작되었습니다. 포트 %d에서 대기 중...\n", portno);
    fflush(stdout);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = -1;
        pipes_to_child[i][0] = -1;
        pipes_to_child[i][1] = -1;
        pipes_to_parent[i][0] = -1;
        pipes_to_parent[i][1] = -1;
        nicknames[i][0] = '\0';
        child_pids[i] = -1;
    }

    clen = sizeof(cliaddr);
    set_nonblocking(ssock);

    while (!all_children_terminated) {
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
                        child_pids[client_index] = pid;
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
                ChatMessage mesg;
                ssize_t str_len = recv(client_sockets[i], &mesg, sizeof(ChatMessage), MSG_DONTWAIT);
                if (str_len > 0) {
                    write(pipes_to_child[i][1], &mesg, sizeof(ChatMessage));
                } else if (str_len == 0 || (str_len == -1 && errno != EWOULDBLOCK)) {
                    // 클라이언트 연결 종료 처리
                    close(client_sockets[i]);
                    close(pipes_to_child[i][1]);
                    close(pipes_to_parent[i][0]);
                    client_sockets[i] = -1;
                    g_noc--;
                }
            }
        }

        // 자식 프로세스로부터 메시지 읽기 및 메시지 보내기
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (pipes_to_parent[i][0] != -1) {
                ChatMessage mesg;
                ssize_t str_len = read(pipes_to_parent[i][0], &mesg, sizeof(ChatMessage));
                if (str_len > 0) {
                    if (strncmp(mesg.content, "[NICKNAME_SET]", 14) == 0) {
                        int client_id = atoi(mesg.content + 14);
                        printf("클라이언트 %d의 닉네임이 설정되었습니다: %s\n", client_id, nicknames[client_id]);
                    } else {
                        send_message(&mesg, i);
                    }
                }
            }
        }

        // SIGUSR1 처리 (새 클라이언트 연결)
        if (sigusr1_received) {
            printf("새 클라이언트가 연결되었습니다. 현재 연결 수: %d\n", g_noc);
            sigusr1_received = 0;
        }

        // SIGUSR2 처리 (클라이언트 연결 종료)
        if (sigusr2_received) {
            printf("클라이언트 연결이 종료되었습니다. 현재 연결 수: %d\n", g_noc);
            sigusr2_received = 0;
        }

        // 모든 자식 프로세스가 종료되었는지 확인
        if (all_children_terminated) {
            printf("모든 자식 프로세스가 종료되었습니다. 서버를 종료합니다.\n");
            break;
        }
    }

    // 서버 종료 전 정리 작업
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] != -1) {
            close(client_sockets[i]);
            close(pipes_to_child[i][1]);
            close(pipes_to_parent[i][0]);
        }
    }
    close(ssock);
    printf("서버가 정상적으로 종료되었습니다.\n");
    return 0;
}