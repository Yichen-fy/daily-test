/*-------------------- includes -------------------------*/ 
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

/*-------------------- defines --------------------------*/
#define CTRL_KEY(k) ((k) & 0x1f)                       //重构Ctrl组合键(Ctrl+字母 ASCII为1-26)
#define VERSION "0.0.1"
#define TAB_STOP 8
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
typedef struct erow {                                  //保存文本编辑器的一行
  int size;
  int rsize;
  char *chars;
  char *render;
} erow;


struct editorConfig {                                  //设置全局结构体
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios;
};

struct editorConfig E;

/*-------------------- terminal -------------------------*/
void enableRawMode();                                  //启用原始模式
void disableRawMode();                                 //关闭原始模式
int editorReadKey();                                   //按键读取函数
void die(const char *s);                               //报错处理
int getWindowSize(int *rows, int *cols);               //设置窗口大小（从<sys/ioctl.h>中获取）

/*--------------------row operation----------------------*/
void editorAppendRow(char *s, size_t len);
void editorUpdateRow(erow *row);
int editorRowCxToRx(erow *row, int cx);

/*--------------------- file i/o ------------------------*/

void editorOpen(char *filename);

/*-------------------- append buffer --------------------*/
struct abuf {                                          //缓冲区结构体
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}                             //abuf类型构造函数 

void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);                           //析构函数,释放abuf使用的动态内存

/*--------------------- output --------------------------*/
void editorRefreshScreen();                             //屏幕刷新
void editorDrawRows(struct abuf *ab);                   //画点什么
void editorScroll();                                    //滚动
void editorDrawStatusBar(struct abuf *ab);              //显示状态栏
void editorSetStatusMessage(const char *fmt, ...);      //可变参数函数，用于生成状态栏信息
void editorDrawMessageBar(struct abuf *ab);

/*--------------------- input ---------------------------*/
void editorProcessKeypress();                          //重构功能
void editorMoveCursor(int key);                        //重构光标移动键


/*---------------------- init ---------------------------*/
void initEditor() {
  E.cx = 0;                                          //初始化光标位置
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;                                     //初始读取行数
  E.row = NULL;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) 
  die("getWindowSize");                              //初始化屏幕大小
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();                           
    
    if (argc >= 2) {                                  //检查用户是否输入了文件名（argc>=2是因为程序名称本身也算一个参数）
        editorOpen(argv[1]);
    } 

    editorSetStatusMessage("HELP: Ctrl-Q = quit");

    while (1) {                             
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

void disableRawMode() {                                                       //程序退出时还原值规范模式
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

void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");                 //读取文件
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r')) 
      linelen--;                                  //忽略换行符的长度
    editorAppendRow(line,linelen);
  }
  free(line);                                     //释放内存
  fclose(fp);                                     //关闭文件
  
  // 添加以下两行代码
  E.coloff = 0;  // 重置列偏移
  E.rowoff = 0;  // 重置行偏移
}

void editorRefreshScreen() {
    editorScroll();
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);      
    abAppend(&ab, "\x1b[H", 3);         //转义序列esc[H表示定位光标至左上角
    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
      editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                              (E.rx - E.coloff) + 1);
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
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "wlecome editor -- version %s", VERSION);
        if (welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;                                //防止光标值变为负值，出现无法移动的情况
      } 
      else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;               //允许在行首时左移换至上一行
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      }
      else if (row && E.cx == row->size) {     //允许在行尾时右移换至下一行
       E.cy++;
       E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows) {
        E.cy++;
      }
      break;
  }
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) {
    E.cx = rowlen;
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
            if (E.cy < E.numrows)
            E.cx = E.row[E.cy].size;
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                  E.cy = E.rowoff;
                } 
                else if (c == PAGE_DOWN) {
                  E.cy = E.rowoff + E.screenrows - 1;
                  if (E.cy > E.numrows) E.cy = E.numrows;
                }

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

void editorAppendRow(char *s, size_t len) {
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  
  int at = E.numrows;                      //行数
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);       //分配空间
  memcpy(E.row[at].chars, s, len);         //拷贝内容到E.row.chars
  E.row[at].chars[len] = '\0';
  
  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  
  // 初始化后立即更新渲染内容
  editorUpdateRow(&E.row[at]);  // 添加这一行
  
  E.numrows++;
}

void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;
  
  free(row->render);
  row->render = malloc(row->size + tabs*(TAB_STOP - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
    row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (TAB_STOP - 1) - (rx % TAB_STOP);
    rx++;
  }
  return rx;
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines",
    E.filename ? E.filename : "[No Name]", E.numrows);
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
    E.cy + 1, E.numrows);
  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    }
    else {
    abAppend(ab, " ", 1);
    len++;
   }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}
void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}


void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}