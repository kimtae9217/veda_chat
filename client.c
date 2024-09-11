#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#define BUF_SIZE 100
#define NICKNAME_SIZE 20

void error_handling(char *message);
void set_nonblocking(int sock);
void handle_sigint(int sig);

int sock;
char nickname[NICKNAME_SIZE];
int pipes[2][2];

int main(int argc, char *argv[]) {
    struct sockaddr_in serv_adr;
    pid_t pid;
    char message[BUF_SIZE];

    if (argc != 3) {
        printf("사용법: %s <IP> <port>\n", argv[0]);
        exit(1);
    }

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() 오류");

    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_adr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
        error_handling("connect() 오류");
    else
        puts("연결되었습니다...........");

    // 닉네임 입력 받기
    printf("닉네임을 입력하세요 (최대 %d자): ", NICKNAME_SIZE - 1);
    fgets(nickname, NICKNAME_SIZE, stdin);
    nickname[strcspn(nickname, "\n")] = 0;  // 개행 문자 제거

    // 서버에 닉네임 전송
    sprintf(message, "[NICKNAME]%s", nickname);
    write(sock, message, strlen(message));

    if (pipe(pipes[0]) == -1 || pipe(pipes[1]) == -1)
        error_handling("pipe() 오류");

    signal(SIGINT, handle_sigint);

    pid = fork();
    if (pid == 0) { // 자식 프로세스: 서버로부터 메시지 수신
        close(pipes[0][1]);
        close(pipes[1][0]);

        while (1) {
            int str_len = read(sock, message, BUF_SIZE - 1);
            if (str_len <= 0) {
                break;
            }
            message[str_len] = 0;
            printf("%s", message);
            fflush(stdout);
            write(pipes[1][1], message, str_len);
        }
        kill(getppid(), SIGINT);
        exit(0);
    } else { // 부모 프로세스: 사용자 입력 처리 및 서버로 전송
        close(pipes[0][0]);
        close(pipes[1][1]);

        while (1) {
            fgets(message, BUF_SIZE, stdin);
            if (!strcmp(message, "q\n") || !strcmp(message, "Q\n")) {
                kill(pid, SIGINT);
                break;
            }
            write(sock, message, strlen(message));
        }
    }

    close(sock);
    return 0;
}

void error_handling(char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

void handle_sigint(int sig) {
    close(sock);
    printf("\n채팅을 종료합니다.\n");
    exit(0);
}