#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>

void enableRawMode();
void disableRawMode();
void die(const char *s);

struct termios orig_termios;

int main() {
    enableRawMode();

    char c = '\0';
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (iscntrl(c)) {
            printf("%d\n", c);
        } 
        else {
            printf("%d ('%c')\n", c, c);
        }
            printf("%d\r", c);
        if (c == 'q') break;
    }

    disableRawMode();
    return 0;
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        die("tcgetattr");
    }

    struct termios raw = orig_termios;

    // 输入模式：禁用BRKINT、INPCK、ISTRIP、IXON等
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // 本地模式：禁用ECHO、ICANON、ISIG、IEXTEN
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    // 设置非规范模式下的参数：VMIN和VTIME
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        die("tcgetattr");
    }
}

void die(const char *s){
  perror(s);
  exit(1);
}