#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <ncurses.h>
#include <locale.h>

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

void error_handling(char *message);
void handle_sigint(int sig);
void receive_messages(int sock, const char *my_nickname);
void send_messages(int sock, const char *nickname);
void print_chat_message(WINDOW *chat_win, const char *nickname, const char *message, const char *my_nickname);
void redraw_input_window();

int sock;
WINDOW *chat_win, *input_win;

int main(int argc, char *argv[]) {

    setlocale(LC_ALL, "ko_KR.UTF-8");

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

    printf("닉네임을 입력하세요 : ");
    fgets(nickname, NICKNAME_SIZE, stdin);
    nickname[strcspn(nickname, "\n")] = 0;  // 개행 문자 제거

    ChatMessage init_message = {MSG_NICKNAME, "", ""};
    strncpy(init_message.nickname, nickname, NICKNAME_SIZE - 1);
    init_message.nickname[NICKNAME_SIZE - 1] = '\0';
    write(sock, &init_message, sizeof(ChatMessage));

    // ncurses 초기화
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    // 색상 설정
    start_color();
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_YELLOW, COLOR_BLACK);

    // 화면 크기 가져오기
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // 채팅창 생성 (크기 조정)
    chat_win = newwin(max_y - 4, max_x, 0, 0);
    scrollok(chat_win, TRUE);

    // 입력창 생성 (위치 조정)
    input_win = newwin(3, max_x, max_y - 3, 0);
    
    // 구분선 추가
    mvhline(max_y - 4, 0, ACS_HLINE, max_x);

    // 환영 메시지 출력
    wprintw(chat_win, "──────────────────────VEDA 채팅방에 들어오신 것을 환영합니다 ─────────────────────\n");

    // 화면 새로고침
    refresh();
    wrefresh(chat_win);
    redraw_input_window();  // 입력 창 초기화

    signal(SIGINT, handle_sigint);

    pid = fork();
    if (pid == 0) {
        receive_messages(sock, nickname);
    } else if (pid > 0) {
        send_messages(sock, nickname);
    } else {
        error_handling("fork() error");
    }
    
    // ncurses 종료
    endwin();
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

        print_chat_message(chat_win, message.nickname, message.content, my_nickname);
        
        // 입력창 다시 그리기
        redraw_input_window();
    }
    kill(getppid(), SIGINT);
    exit(0);
}

void send_messages(int sock, const char *nickname) {
    ChatMessage message;
    strncpy(message.nickname, nickname, NICKNAME_SIZE - 1);
    message.nickname[NICKNAME_SIZE - 1] = '\0';

    while (1) {
        redraw_input_window();

        // 사용자 입력 받기
        char input[BUF_SIZE];
        echo();
        mvwgetnstr(input_win, 1, 10, input, BUF_SIZE - 1);
        noecho();

        if (!strcmp(input, "q") || !strcmp(input, "Q")) {
            message.type = MSG_LOGOUT;
            snprintf(message.content, BUF_SIZE, "%s 님께서 퇴장했습니다.", nickname);
            write(sock, &message, sizeof(ChatMessage));
            break;
        }
        message.type = MSG_CHAT;
        strncpy(message.content, input, BUF_SIZE - 1);
        message.content[BUF_SIZE - 1] = '\0';
        write(sock, &message, sizeof(ChatMessage));

        print_chat_message(chat_win, nickname, message.content, nickname);

        // 메시지 전송 후 입력창 비우기
        werase(input_win);
        box(input_win, 0, 0);
        wrefresh(input_win);
    }
}

void print_chat_message(WINDOW *chat_win, const char *nickname, const char *message, const char *my_nickname) {
    int max_y, max_x;
    getmaxyx(chat_win, max_y, max_x);

    // 현재 커서 위치 확인
    int cur_y, cur_x;
    getyx(chat_win, cur_y, cur_x);

    // 메시지 길이 계산
    int msg_len = strlen(nickname) + strlen(message) + 3; // []:  형식 고려

    // 퇴장 메시지인 경우 출력
    if (strstr(message, "님께서 퇴장했습니다.")) {
        wprintw(chat_win, "%s", message);
    }else{
         // 내 메시지인 경우 오른쪽 정렬
    if (strcmp(nickname, my_nickname) == 0) {
        wmove(chat_win, cur_y, max_x - msg_len - 1);
        wattron(chat_win, COLOR_PAIR(1));
        wprintw(chat_win, "[%s]: %s", nickname, message);
        wattroff(chat_win, COLOR_PAIR(1));
    } else {
        // 다른 사람의 메시지는 왼쪽 정렬
        wmove(chat_win, cur_y, 0);
        wattron(chat_win, COLOR_PAIR(2));
        wprintw(chat_win, "[%s]: ", nickname);
        wattroff(chat_win, COLOR_PAIR(2));
        wprintw(chat_win, "%s", message);
        }
    }

    // 커서를 다음 줄로 이동
    wmove(chat_win, cur_y + 1, 0);

    // 화면 갱신
    wrefresh(chat_win);
    
    // 입력창 다시 그리기 (커서 위치 유지)
    getyx(input_win, cur_y, cur_x);
    redraw_input_window();
    wmove(input_win, cur_y, cur_x);
    wrefresh(input_win);
}

void error_handling(char *message) {
    endwin();  // ncurses 종료
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

void handle_sigint(int sig) {
    printf("\n채팅을 종료합니다.\n");
    ChatMessage logout_message = {MSG_LOGOUT, "", ""};
    strncpy(logout_message.nickname, "", NICKNAME_SIZE - 1);
    logout_message.nickname[NICKNAME_SIZE - 1] = '\0';
    snprintf(logout_message.content, BUF_SIZE, "%s 님께서 퇴장했습니다.", "");
    send(sock, &logout_message, sizeof(logout_message), 0);
    close(sock);
    exit(0);
}

void redraw_input_window() {
    werase(input_win);
    box(input_win, 0, 0);
    mvwprintw(input_win, 1, 1, "메시지: ");
    wmove(input_win, 1, 10);  // 커서를 입력 위치로 이동
    wrefresh(input_win);
}