#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/ioctl.h>

#define BUF_SIZE 100
#define NICKNAME_SIZE 20

// ANSI 색상 코드 추가
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_RESET   "\x1b[0m"

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

void error_handling(char *message);
void handle_sigint(int sig);
void receive_messages(int sock, const char *my_nickname);
void send_messages(int sock, const char *nickname);
void print_aligned_message(const char *nickname, const char *message, int is_my_message);

int sock;

int main(int argc, char *argv[]) {
    struct sockaddr_in serv_adr;
    pid_t pid;
    char nickname[NICKNAME_SIZE];

    if (argc != 3) {
        printf("Usage: %s <IP> <port>\n", argv[0]);
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

    printf("닉네임을 입력하세요 (최대 %d자): ", NICKNAME_SIZE - 1);
    fgets(nickname, NICKNAME_SIZE, stdin);
    nickname[strcspn(nickname, "\n")] = 0;  // 개행 문자 제거

    ChatMessage init_message = {MSG_NICKNAME, "", ""};
    strncpy(init_message.nickname, nickname, NICKNAME_SIZE - 1);
    init_message.nickname[NICKNAME_SIZE - 1] = '\0';
    write(sock, &init_message, sizeof(ChatMessage));

    printf("───────── VEDA 채팅방에 들어오신 것을 환영합니다─────────\n");
    signal(SIGINT, handle_sigint);

    pid = fork();
    if (pid == 0) {
        receive_messages(sock, nickname);
    } else if (pid > 0) {
        send_messages(sock, nickname);
    } else {
        error_handling("fork() error");
    }
    
    close(sock);
    return 0;
}

void receive_messages(int sock, const char *my_nickname) {
    ChatMessage message;
    
    while (1) {
        int str_len = read(sock, &message, sizeof(ChatMessage));
        if (str_len <= 0) {
            break;
        }

        int is_my_message = (strcmp(message.nickname, my_nickname) == 0);
        print_aligned_message(message.nickname, message.content, is_my_message);
    }
    kill(getppid(), SIGINT);
    exit(0);
}

void send_messages(int sock, const char *nickname) {
    ChatMessage message;
    strncpy(message.nickname, nickname, NICKNAME_SIZE - 1);
    message.nickname[NICKNAME_SIZE - 1] = '\0';

    while (1) {
        fgets(message.content, BUF_SIZE, stdin);
        if (!strcmp(message.content, "q\n") || !strcmp(message.content, "Q\n")) {
            message.type = MSG_LOGOUT;
            snprintf(message.content, BUF_SIZE, "%s님께서 퇴장했습니다.", nickname);
            write(sock, &message, sizeof(ChatMessage));
            break;
        }
        message.type = MSG_CHAT;
        write(sock, &message, sizeof(ChatMessage));

        print_aligned_message(nickname, message.content, 1);
    }
    kill(getpid(), SIGINT);
}

void print_aligned_message(const char *nickname, const char *message, int is_my_message) {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int terminal_width = w.ws_col;
    
    char formatted_message[BUF_SIZE + NICKNAME_SIZE + 3];
    snprintf(formatted_message, sizeof(formatted_message), "[%s] %s", nickname, message);
    
    if (is_my_message) {
        printf(ANSI_COLOR_GREEN "%*s" ANSI_COLOR_RESET "\n", terminal_width, formatted_message);
    } else {
        printf(ANSI_COLOR_YELLOW "%s" ANSI_COLOR_RESET "\n", formatted_message);
    }
    fflush(stdout);
}

void error_handling(char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

void handle_sigint(int sig) {
    close(sock);
    printf("\n채팅을 종료합니다.\n");
    exit(0);
}
