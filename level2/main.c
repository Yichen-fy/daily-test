/*-------------------- includes -------------------------*/ 
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <string.h>

/*-------------------- defines --------------------------*/
#define CTRL_KEY(k) ((k) & 0x1f)                       //重构Ctrl组合键(Ctrl+字母 ASCII为1-26)
#define VERSION "0.0.1"
enum editorKey {
  ARROW_LEFT = 1000,                                   //为了防止冲突，设一个很大的键值
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};
/*--------------------- data ----------------------------*/
struct editorConfig {                                  //设置全局结构体
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*-------------------- terminal -------------------------*/
void enableRawMode();                                  //启用原始模式
void disableRawMode();                                 //关闭原始模式
int editorReadKey();                                  //按键读取函数
void die(const char *s);                               //报错处理
int getWindowSize(int *rows, int *cols);               //设置窗口大小（从<sys/ioctl.h>中获取）


/*-------------------- append buffer --------------------*/
struct abuf {                                          //缓冲区结构体
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}                             //abuf类型构造函数 

void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);                           //析构函数,释放abuf使用的动态内存

/*--------------------- output --------------------------*/
void editorRefreshScreen();                           //屏幕刷新
void editorDrawRows(struct abuf *ab);                 //画点什么

/*--------------------- input ---------------------------*/
void editorProcessKeypress();                         //重构功能
void editorMoveCursor(int key);                       //重构光标移动键


/*---------------------- init ---------------------------*/
void initEditor() {
  E.cx = 0;                                          //初始化光标位置
  E.cy = 0;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) 
  die("getWindowSize"); //初始化屏幕大小
}

int main() {
    enableRawMode();
    initEditor();                           //初始化结构体
    while (1) {                             //删去显示，读取功能转移至读取函数中
        editorRefreshScreen();
        editorProcessKeypress();            
    }
    disableRawMode();
    return 0;
}


/*-------------------------------------------------------*/


void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("tcgetattr");
    }
    atexit(disableRawMode);
    struct termios raw = E.orig_termios;

    //输出模式：禁用输出后的处理
    raw.c_oflag &= ~(OPOST);
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
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcgetattr");
    }
}

int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                switch (seq[1]) {
                    case '1': return HOME_KEY;
                    case '3': return DEL_KEY;
                    case '4': return END_KEY;
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                    }
                }
            } 
            else {
                switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            }
        }
    return '\x1b';
    } 
    else {
        return c;
    }
}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4);    //检测到错误后清屏
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);      
    abAppend(&ab, "\x1b[H", 3);         //转义序列esc[H表示定位光标至左上角
    editorDrawRows(&ab);
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));
    abAppend(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.b,ab.len);  //写入缓冲区内容
    abFree(&ab);
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    } 
    else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    abAppend(ab, "*", 1);
    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenrows - 1) {
        abAppend(ab, "\r\n", 2);
    }
  }
}

void editorMoveCursor(int key) {
  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;                                //防止光标值变为负值，出现无法移动的情况
      }
      break;
    case ARROW_RIGHT:
      if (E.cx != E.screencols - 1) {
        E.cx++;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy != E.screenrows - 1) {
        E.cy++;
      }
      break;
  }
}

void editorProcessKeypress() {
    int c = editorReadKey();
    switch (c) {
        case CTRL_KEY('q'):                 //将ctrl+q重构为退出键
            write(STDOUT_FILENO, "\x1b[2J", 4); //退出时清屏
            write(STDOUT_FILENO, "\x1b[H", 3);  
            exit(0);
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screencols - 1;
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
  }
}