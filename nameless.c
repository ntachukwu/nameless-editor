/*** includes ***/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)
#define NAMELESS_VERSION "0.0.1"

/*** data ***/
struct editorConfig {
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag &= ~(CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  return c;
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/** append buffer */
// Too many calls to the write function.
// It's best to use a buffer. Like a python list'
// Since C does not suppoert dynamic strings
// abuf will server as our buffer.
// It consists of a pointer to our buffer in memory, and a length.
// ABUF_INIT serves as the constructor for abuf structs.
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT                                                              \
  { NULL, 0 }

void abAppend(struct abuf *ab, const char *s, int len) {
  // create a new memory location with realloc.
  // ab->b means, access the property b from the structure ab
  // ab-> len + len means, access the property len from the structure ab and add
  // len to it. The len that was passed in to abappend
  //
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return; // realloc returns NULL if an error occured and a new memory could
            // not be allocated.
  // Copy the content of s into &new[ab->len] (the last position of the newly
  // allocated memory)
  memcpy(&new[ab->len], s, len);
  // asign the new memory to ab and increase the len of the buffer
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  // free up the ab buffer
  free(ab->b);
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    if (y == E.screenrows / 3) {
      // write a welcome message to the screen
      char welcome[80];
      int welcomelen =
          snprintf(welcome, sizeof(welcome), "Nameless editor --version %s",
                   NAMELESS_VERSION);
      if (welcomelen > E.screencols)
        welcomelen = E.screencols;

      int padding = (E.screencols - welcomelen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        // padding--;
      }
      while (padding--)
        abAppend(ab, " ", 1);
      abAppend(ab, welcome, welcomelen);
    } else {
      abAppend(ab, "~", 1);
    }

    // K Erase in-line.
    // 0 erases the part of the line to the right.
    // 0 is the default argument.
    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

/*** input ***/

void editorProcessKeypress() {
  char c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  }
}

void editorRefreshScreen() {
  // \x1b = 27 (in decimal) = The escape charactor. it is followed by a [
  // charactor. These are known as escape sequences. With 2J we ae passing the
  // argument of 2 to the command J. J is the command to clear screen. H is the
  // command to reaturn cursor position. All these are from the VT100. Check the
  // VT100 User Guide for the complete documentation of each escape sequence.

  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l",
           6); // Hide cursor first. To prevent flickering on some systems.
  // abAppend(&ab, "\x1b[2J", 4); // Clear Screen
  // Instead of clearing the entire screen like the commented line of code above
  // suggests. We should instead put a <esc>[k sequence at the end of each line
  // we draw inside the editorDrawRows function.
  abAppend(&ab, "\x1b[H",
           3); // Return cursor to the top-most-left corner of the screen.

  editorDrawRows(&ab); // Draw the ~ char on every row

  abAppend(&ab, "\x1b[H", 3); // Return cursor to top-most-left corner
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** init */
void initEditor() {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
}

int main() {
  enableRawMode();
  initEditor();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}
