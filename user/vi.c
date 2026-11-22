#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

#define MAX_LINES  1000
#define LINE_LEN   200          // longest line: 199 characters
#define YANK_MAX   100
#define MAX_ROWS   64           // largest screen we handle
#define MAX_COLS   160
#define TABW       4

enum { K_UP = 1000, K_DOWN, K_RIGHT, K_LEFT, K_HOME, K_END, K_PGUP, K_PGDN, K_DEL };
enum { M_NORMAL, M_INSERT };

// screen size, read from the console at start-up: the last two rows are the status bar and the message line
static int ROWS = 25, COLS = 80, TEXT_ROWS = 23;

static char  pool[MAX_LINES][LINE_LEN];
static char *lines[MAX_LINES];
static int   nlines;
static int   freelist[MAX_LINES];
static int   nfree;

static char  snap[MAX_LINES][LINE_LEN];         // one level of undo
static int   snap_n, snap_ok, snap_cy, snap_cx;

static char  yank_lines[YANK_MAX][LINE_LEN];
static int   yank_n;
static char  yank_chars[LINE_LEN];
static int   yank_kind;                         // 0 = nothing, 1 = whole lines, 2 = characters

static int   cy, cx, rowoff, coloff;
static int   mode = M_NORMAL;
static int   count;
static int   modified;
static char  filename[128];
static char  msg[80];
static char  searchpat[64];

static int   in_prompt;
static char  promptch;
static char  promptbuf[80];
static int   promptlen;

// ---------- small string helpers ---------- //

static int scopy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
    return i;
}

static void sappend(char *dst, const char *src, int max) {
    int n = (int)ustrlen(dst);
    if (n >= max - 1) return;
    scopy(dst + n, src, max - n);
}

static void set_msg(const char *s) {
    scopy(msg, s, sizeof(msg));
}

static void set_msg_err(const char *what, int e) {
    scopy(msg, what, sizeof(msg));
    sappend(msg, ": ", sizeof(msg));
    sappend(msg, errstr(e), sizeof(msg));
}

static void num_to_str(char *dst, int v) {
    char t[12];
    int i = 11;
    unsigned int u = v < 0 ? -(unsigned int)v : (unsigned int)v;

    t[i] = '\0';
    do { t[--i] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) t[--i] = '-';
    scopy(dst, t + i, 12);
}

static void set_msg_saved(const char *path, int nl, int bytes) {
    char t[12];

    scopy(msg, "\"", sizeof(msg));
    sappend(msg, path, sizeof(msg) - 30);
    sappend(msg, "\" ", sizeof(msg));
    num_to_str(t, nl);
    sappend(msg, t, sizeof(msg));
    sappend(msg, "L, ", sizeof(msg));
    num_to_str(t, bytes);
    sappend(msg, t, sizeof(msg));
    sappend(msg, "B", sizeof(msg));
}

// ---------- line storage ---------- //

static void pool_reset(void) {
    nfree = 0;
    for (int i = MAX_LINES - 1; i >= 0; i--) freelist[nfree++] = i;
    nlines = 0;
}

static char *line_new(void) {
    if (nfree == 0) return 0;
    char *p = pool[freelist[--nfree]];
    p[0] = '\0';
    return p;
}

static void line_free(char *p) {
    freelist[nfree++] = (int)((p - pool[0]) / LINE_LEN);
}

static int llen(int i) {
    return (int)ustrlen(lines[i]);
}

static int insert_line(int at, const char *text) {
    if (nlines >= MAX_LINES) { set_msg("Too many lines"); return -1; }

    char *p = line_new();
    if (!p) return -1;

    scopy(p, text, LINE_LEN);
    for (int i = nlines; i > at; i--) lines[i] = lines[i - 1];
    lines[at] = p;
    nlines++;
    return 0;
}

static void remove_line(int at) {
    line_free(lines[at]);
    for (int i = at; i < nlines - 1; i++) lines[i] = lines[i + 1];
    nlines--;
    if (nlines == 0) insert_line(0, "");
}

static void save_undo(void) {
    for (int i = 0; i < nlines; i++) scopy(snap[i], lines[i], LINE_LEN);
    snap_n  = nlines;
    snap_cy = cy;
    snap_cx = cx;
    snap_ok = 1;
}

static void do_undo(void) {
    if (!snap_ok) { set_msg("Nothing to undo"); return; }

    pool_reset();
    for (int i = 0; i < snap_n; i++) insert_line(i, snap[i]);
    cy = snap_cy;
    cx = snap_cx;
    snap_ok = 0;
    modified = 1;
    set_msg("Undone");
}

// ---------- editing operations ---------- //

static void clamp_cx(int allow_end) {
    int len = llen(cy);
    int max = allow_end ? len : (len > 0 ? len - 1 : 0);
    if (cx > max) cx = max;
    if (cx < 0) cx = 0;
}

static void ins_char(char c) {
    char *l = lines[cy];
    int len = llen(cy);

    if (len >= LINE_LEN - 1) { set_msg("Line too long"); return; }
    for (int i = len + 1; i > cx; i--) l[i] = l[i - 1];
    l[cx++] = c;
    modified = 1;
}

static void del_chars(int at, int n, int to_yank) {
    char *l = lines[cy];
    int len = llen(cy);

    if (at >= len || n <= 0) return;
    if (at + n > len) n = len - at;

    if (to_yank) {
        for (int i = 0; i < n; i++) yank_chars[i] = l[at + i];
        yank_chars[n] = '\0';
        yank_kind = 2;
    }
    for (int i = at; i + n <= len; i++) l[i] = l[i + n];    // also moves the NUL
    modified = 1;
}

static void split_line(void) {
    char tail[LINE_LEN];

    if (nlines >= MAX_LINES) { set_msg("Too many lines"); return; }

    scopy(tail, lines[cy] + cx, LINE_LEN);
    lines[cy][cx] = '\0';
    insert_line(cy + 1, tail);
    cy++;
    cx = 0;
    modified = 1;
}

static void backspace(void) {
    if (cx > 0) {
        del_chars(cx - 1, 1, 0);
        cx--;
    } else if (cy > 0) {
        int plen = llen(cy - 1);
        if (plen + llen(cy) >= LINE_LEN) { set_msg("Line too long"); return; }

        sappend(lines[cy - 1], lines[cy], LINE_LEN);
        remove_line(cy);
        cy--;
        cx = plen;
        modified = 1;
    }
}

static void join_lines(void) {
    if (cy >= nlines - 1) return;

    int len = llen(cy);
    if (len + 1 + llen(cy + 1) >= LINE_LEN) { set_msg("Line too long"); return; }

    save_undo();
    sappend(lines[cy], " ", LINE_LEN);
    sappend(lines[cy], lines[cy + 1], LINE_LEN);
    remove_line(cy + 1);
    cx = len;
    modified = 1;
}

static void delete_lines(int n) {
    if (cy + n > nlines) n = nlines - cy;
    if (n <= 0) return;

    save_undo();
    yank_n = 0;
    for (int i = 0; i < n && yank_n < YANK_MAX; i++)
        scopy(yank_lines[yank_n++], lines[cy + i], LINE_LEN);
    yank_kind = 1;

    for (int i = 0; i < n; i++) remove_line(cy);
    if (cy >= nlines) cy = nlines - 1;
    cx = 0;
    modified = 1;
}

static void yank_n_lines(int n) {
    if (cy + n > nlines) n = nlines - cy;

    yank_n = 0;
    for (int i = 0; i < n && yank_n < YANK_MAX; i++)
        scopy(yank_lines[yank_n++], lines[cy + i], LINE_LEN);
    yank_kind = 1;
    set_msg("Yanked");
}

static void put_register(int after) {
    if (yank_kind == 1) {
        save_undo();
        int at = after ? cy + 1 : cy;
        for (int i = 0; i < yank_n; i++)
            if (insert_line(at + i, yank_lines[i]) < 0) break;
        cy = at;
        cx = 0;
        modified = 1;
    } else if (yank_kind == 2) {
        save_undo();
        int len = (int)ustrlen(yank_chars);
        if (after && llen(cy) > 0) cx++;
        for (int i = 0; i < len; i++) ins_char(yank_chars[i]);
        if (cx > 0) cx--;
    } else {
        set_msg("Nothing in the register");
    }
}

static int is_word(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static int is_space(char c) {
    return c == ' ' || c == '\t';
}

static void word_forward(void) {
    const char *l = lines[cy];
    int len = llen(cy);

    if (cx < len) {
        if (is_word(l[cx]))
            while (cx < len && is_word(l[cx])) cx++;
        else if (!is_space(l[cx]))
            while (cx < len && !is_word(l[cx]) && !is_space(l[cx])) cx++;
        while (cx < len && is_space(l[cx])) cx++;
    }

    if (cx >= len) {
        if (cy < nlines - 1) {
            cy++;
            cx = 0;
            l = lines[cy];
            while (l[cx] && is_space(l[cx])) cx++;
        } else {
            cx = len > 0 ? len - 1 : 0;
        }
    }
}

static void word_backward(void) {
    if (cx == 0) {
        if (cy == 0) return;
        cy--;
        cx = llen(cy);
    }

    const char *l = lines[cy];
    while (cx > 0 && is_space(l[cx - 1])) cx--;
    if (cx > 0) {
        if (is_word(l[cx - 1]))
            while (cx > 0 && is_word(l[cx - 1])) cx--;
        else
            while (cx > 0 && !is_word(l[cx - 1]) && !is_space(l[cx - 1])) cx--;
    }
}

// ---------- searching ---------- //

static int find_in(const char *line, const char *pat, int from) {
    int len = (int)ustrlen(line);
    if (!pat[0] || from > len) return -1;

    for (int i = from; line[i]; i++) {
        int k = 0;
        while (pat[k] && line[i + k] == pat[k]) k++;
        if (!pat[k]) return i;
    }
    return -1;
}

static int find_last_before(const char *line, const char *pat, int before) {
    int found = -1;

    for (int i = 0; i < before && line[i]; i++) {
        int k = 0;
        while (pat[k] && line[i + k] == pat[k]) k++;
        if (!pat[k]) found = i;
    }
    return found;
}

static void search(int forward) {
    if (!searchpat[0]) { set_msg("No previous search pattern"); return; }

    for (int k = 0; k <= nlines; k++) {
        int li, at;

        if (forward) {
            li = (cy + k) % nlines;
            at = find_in(lines[li], searchpat, k == 0 ? cx + 1 : 0);
        } else {
            li = ((cy - k) % nlines + nlines) % nlines;
            at = find_last_before(lines[li], searchpat, k == 0 ? cx : LINE_LEN);
        }
        if (at >= 0) {
            cy = li;
            cx = at;
            return;
        }
    }
    set_msg("Pattern not found");
}

// ---------- files ---------- //

static void load_file(const char *path) {
    char buf[512], cur[LINE_LEN];
    int clen = 0, have = 0, truncated = 0, full = 0, n;

    pool_reset();

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        insert_line(0, "");
        if (fd == -2) set_msg("[New File]");
        else          set_msg_err(path, fd);
        return;
    }

    while (!full && (n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                cur[clen] = '\0';
                if (insert_line(nlines, cur) < 0) { full = 1; break; }
                clen = 0;
                have = 0;
            } else {
                have = 1;
                if (clen < LINE_LEN - 1) cur[clen++] = buf[i];
                else                     truncated = 1;
            }
        }
    }
    if (have && !full) {
        cur[clen] = '\0';
        insert_line(nlines, cur);
    }
    close(fd);

    if (nlines == 0) insert_line(0, "");

    set_msg_saved(path, nlines, 0);
    if (truncated || full) set_msg("File too big: long lines cut / extra lines dropped!");
}

static int flush_w(int fd, char *wbuf, int *wl) {
    if (*wl == 0) return 0;

    int r = write(fd, wbuf, (unsigned int)*wl);
    int ok = (r == *wl);
    *wl = 0;
    return ok ? 0 : (r < 0 ? r : -28);
}

// returns the number of bytes written, or -errno
static int save_file(const char *path) {
    char wbuf[512];
    int wl = 0, total = 0;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return fd;

    for (int i = 0; i < nlines; i++) {
        for (const char *s = lines[i]; ; s++) {
            char c = *s ? *s : '\n';

            if (wl == (int)sizeof(wbuf)) {
                int r = flush_w(fd, wbuf, &wl);
                if (r) { close(fd); return r; }
            }
            wbuf[wl++] = c;
            total++;
            if (!*s) break;
        }
    }

    int r = flush_w(fd, wbuf, &wl);
    close(fd);
    return r ? r : total;
}

static int write_out(const char *path) {
    int r = save_file(path);
    if (r < 0) {
        set_msg_err(path, r);
        return -1;
    }
    set_msg_saved(path, nlines, r);
    return 0;
}

// ---------- screen ---------- //

static char out[16384];
static int  olen;

static void oc(char c) { if (olen < (int)sizeof(out)) out[olen++] = c; }
static void os(const char *s) { while (*s) oc(*s++); }

static void on(int v) {
    char t[12];
    num_to_str(t, v);
    os(t);
}

static void cup(int r, int c) {
    os("\033[");
    on(r);
    oc(';');
    on(c);
    oc('H');
}

static void flush_out(void) {
    if (olen) write(1, out, (unsigned int)olen);
    olen = 0;
}

static int vcol_of(int line, int upto) {
    const char *l = lines[line];
    int v = 0;

    for (int i = 0; i < upto && l[i]; i++)
        v += (l[i] == '\t') ? TABW - (v % TABW) : 1;
    return v;
}

static void scroll_fix(void) {
    if (cy < rowoff) rowoff = cy;
    if (cy >= rowoff + TEXT_ROWS) rowoff = cy - TEXT_ROWS + 1;

    int v = vcol_of(cy, cx);
    if (v < coloff) coloff = v;
    if (v >= coloff + COLS) coloff = v - COLS + 1;
}

static void draw_line(const char *s) {
    int v = 0;

    for (; *s; s++) {
        int w = (*s == '\t') ? TABW - (v % TABW) : 1;
        for (int k = 0; k < w; k++) {
            if (v >= coloff && v < coloff + COLS) {
                char c = *s;
                if (c == '\t') c = ' ';
                else if (c < 32 || c > 126) c = '?';       // never send control codes to the terminal
                oc(c);
            }
            v++;
        }
    }
}

static int bput(char *b, int pos, int max, const char *s) {
    while (*s && pos < max) b[pos++] = *s++;
    return pos;
}

static void draw(void) {
    char bar[MAX_COLS + 1];
    char t[12];

    scroll_fix();
    olen = 0;

    for (int r = 0; r < TEXT_ROWS; r++) {
        int li = rowoff + r;
        cup(r + 1, 1);
        if (li < nlines) draw_line(lines[li]);
        else             oc('~');
        os("\033[K");
    }

    // status bar (inverse video)
    for (int i = 0; i < COLS; i++) bar[i] = ' ';
    bar[COLS] = '\0';

    int p = bput(bar, 0, COLS, mode == M_INSERT ? " INSERT  " : " NORMAL  ");
    p = bput(bar, p, COLS, filename[0] ? filename : "[No Name]");
    if (modified) p = bput(bar, p, COLS, " [+]");

    char rt[24];
    rt[0] = '\0';
    num_to_str(t, cy + 1);
    scopy(rt, t, sizeof(rt));
    sappend(rt, ",", sizeof(rt));
    num_to_str(t, vcol_of(cy, cx) + 1);
    sappend(rt, t, sizeof(rt));
    sappend(rt, "  ", sizeof(rt));
    int rl = (int)ustrlen(rt);
    bput(bar, COLS - rl, COLS, rt);

    cup(TEXT_ROWS + 1, 1);
    os("\033[7m");
    for (int i = 0; i < COLS; i++) oc(bar[i]);
    os("\033[0m");

    // message / command line: at most COLS-1 characters, so it never wraps off the screen
    cup(ROWS, 1);
    if (in_prompt) {
        oc(promptch);
        for (int i = 0; i < promptlen && i < COLS - 2; i++) oc(promptbuf[i]);
    } else {
        for (int i = 0; msg[i] && i < COLS - 1; i++) oc(msg[i]);
    }
    os("\033[K");

    // put the cursor where it belongs
    if (in_prompt) {
        int px = promptlen + 2;
        cup(ROWS, px > COLS - 1 ? COLS - 1 : px);
    } else {
        cup(cy - rowoff + 1, vcol_of(cy, cx) - coloff + 1);
    }
    flush_out();
}

// ---------- keyboard ---------- //

static unsigned char ibuf[32];
static int ilen, ipos;

static int read_key(void) {
    if (ipos >= ilen) {
        int n = read(0, ibuf, sizeof(ibuf));
        if (n <= 0) return -1;
        ilen = n;
        ipos = 0;
    }

    int c = ibuf[ipos++];

    // arrow keys etc. arrive as one burst: ESC [ A, ESC [ 3 ~ ...
    if (c == 27 && ilen - ipos >= 2 && ibuf[ipos] == '[') {
        int a = ibuf[ipos + 1];
        int k = 0, used = 2;

        switch (a) {
        case 'A': k = K_UP;    break;
        case 'B': k = K_DOWN;  break;
        case 'C': k = K_RIGHT; break;
        case 'D': k = K_LEFT;  break;
        case 'H': k = K_HOME;  break;
        case 'F': k = K_END;   break;
        default:
            if (a >= '1' && a <= '8' && ilen - ipos >= 3 && ibuf[ipos + 2] == '~') {
                used = 3;
                switch (a) {
                case '1': case '7': k = K_HOME; break;
                case '3':           k = K_DEL;  break;
                case '4': case '8': k = K_END;  break;
                case '5':           k = K_PGUP; break;
                case '6':           k = K_PGDN; break;
                default:            k = 0;      break;
                }
            }
            break;
        }

        if (k) {
            ipos += used;
            return k;
        }
    }
    return c;
}

// reads a line of text on the bottom row; returns 1 on Enter, 0 if cancelled
static int prompt_line(char ch) {
    in_prompt = 1;
    promptch = ch;
    promptlen = 0;
    promptbuf[0] = '\0';

    for (;;) {
        draw();
        int c = read_key();

        if (c < 0 || c == 27) { in_prompt = 0; return 0; }
        if (c == '\n')        { in_prompt = 0; return 1; }

        if (c == 8 || c == 127) {
            if (promptlen > 0) promptbuf[--promptlen] = '\0';
            else { in_prompt = 0; return 0; }
        } else if (c >= 32 && c < 127 && promptlen < (int)sizeof(promptbuf) - 2) {
            promptbuf[promptlen++] = (char)c;
            promptbuf[promptlen] = '\0';
        }
    }
}

// ---------- commands ---------- //

// returns 1 if the editor should quit
static int exec_command(const char *cmd) {
    char word[8];
    int bang = 0, wl = 0;

    while (*cmd == ' ') cmd++;
    if (!*cmd) return 0;

    if (*cmd >= '0' && *cmd <= '9') {                       // :N goes to line N
        int n = 0;
        while (*cmd >= '0' && *cmd <= '9') n = n * 10 + (*cmd++ - '0');
        cy = n < 1 ? 0 : (n > nlines ? nlines - 1 : n - 1);
        cx = 0;
        return 0;
    }
    if (*cmd == '$' && !cmd[1]) {
        cy = nlines - 1;
        cx = 0;
        return 0;
    }

    while (((*cmd >= 'a' && *cmd <= 'z') || (*cmd >= 'A' && *cmd <= 'Z')) && wl < 7)
        word[wl++] = *cmd++;
    word[wl] = '\0';
    if (*cmd == '!') { bang = 1; cmd++; }
    while (*cmd == ' ') cmd++;

    if (streq(word, "w") || streq(word, "write")) {
        const char *target = *cmd ? cmd : filename;
        if (!target[0]) { set_msg("No file name"); return 0; }
        if (write_out(target) == 0) {
            if (!filename[0]) scopy(filename, target, sizeof(filename));
            if (target == filename || streq(target, filename)) modified = 0;
        }
        return 0;
    }
    if (streq(word, "q") || streq(word, "quit")) {
        if (modified && !bang) {
            set_msg("No write since last change (add ! to override)");
            return 0;
        }
        return 1;
    }
    if (streq(word, "wq") || streq(word, "x")) {
        const char *target = *cmd ? cmd : filename;
        if (!target[0]) { set_msg("No file name"); return 0; }
        if (write_out(target) != 0) return 0;
        return 1;
    }

    scopy(msg, "Not an editor command: ", sizeof(msg));
    sappend(msg, word, sizeof(msg));
    return 0;
}

static int run_prompt_command(void) {
    if (!prompt_line(':')) return 0;
    return exec_command(promptbuf);
}

static void search_prompt(void) {
    if (!prompt_line('/')) return;
    if (promptbuf[0]) scopy(searchpat, promptbuf, sizeof(searchpat));
    search(1);
}

static void page_move(int dir, int amount) {
    cy += dir * amount;
    if (cy < 0) cy = 0;
    if (cy >= nlines) cy = nlines - 1;
    clamp_cx(0);
}

// returns 1 to quit
static int normal(int c) {
    int had = count;

    if ((c >= '1' && c <= '9') || (c == '0' && count > 0)) {
        count = count * 10 + (c - '0');
        return 0;
    }

    int n = count ? count : 1;
    count = 0;
    msg[0] = '\0';

    switch (c) {
    case 'h': case K_LEFT:
        cx -= n;
        if (cx < 0) cx = 0;
        break;
    case 'l': case K_RIGHT:
        cx += n;
        clamp_cx(0);
        break;
    case 'j': case K_DOWN:
        cy += n;
        if (cy >= nlines) cy = nlines - 1;
        clamp_cx(0);
        break;
    case 'k': case K_UP:
        cy -= n;
        if (cy < 0) cy = 0;
        clamp_cx(0);
        break;
    case '0': case K_HOME:
        cx = 0;
        break;
    case '$': case K_END:
        cx = LINE_LEN;
        clamp_cx(0);
        break;
    case '^':
        cx = 0;
        while (lines[cy][cx] && is_space(lines[cy][cx])) cx++;
        break;
    case 'G':
        cy = had ? (had > nlines ? nlines - 1 : had - 1) : nlines - 1;
        cx = 0;
        break;
    case 'g':
        if (read_key() == 'g') {
            cy = had ? (had > nlines ? nlines - 1 : had - 1) : 0;
            cx = 0;
        }
        break;
    case 'w':
        for (int i = 0; i < n; i++) word_forward();
        clamp_cx(0);
        break;
    case 'b':
        for (int i = 0; i < n; i++) word_backward();
        break;
    case K_PGDN: case 6:
        page_move(1, TEXT_ROWS - 1);
        break;
    case K_PGUP: case 2:
        page_move(-1, TEXT_ROWS - 1);
        break;
    case 4:
        page_move(1, TEXT_ROWS / 2);
        break;
    case 21:
        page_move(-1, TEXT_ROWS / 2);
        break;

    case 'x': case K_DEL:
        if (llen(cy) > 0) {
            save_undo();
            del_chars(cx, n, 1);
            clamp_cx(0);
        }
        break;
    case 'X':
        if (cx > 0) {
            save_undo();
            int k = n > cx ? cx : n;
            del_chars(cx - k, k, 1);
            cx -= k;
        }
        break;
    case 'd': {
        int k = read_key();
        if (k == 'd') {
            delete_lines(n);
        } else if (k == 'w') {
            save_undo();
            int start = cx, e = cx, len = llen(cy);
            const char *l = lines[cy];
            for (int i = 0; i < n; i++) {
                if (e < len && is_word(l[e]))        while (e < len && is_word(l[e])) e++;
                else if (e < len && !is_space(l[e])) while (e < len && !is_word(l[e]) && !is_space(l[e])) e++;
                while (e < len && is_space(l[e])) e++;
            }
            del_chars(start, e - start, 1);
            clamp_cx(0);
        } else if (k == '$') {
            save_undo();
            del_chars(cx, llen(cy) - cx, 1);
            clamp_cx(0);
        }
        break;
    }
    case 'D':
        save_undo();
        del_chars(cx, llen(cy) - cx, 1);
        clamp_cx(0);
        break;
    case 'C':
        save_undo();
        del_chars(cx, llen(cy) - cx, 1);
        mode = M_INSERT;
        break;
    case 'y':
        if (read_key() == 'y') yank_n_lines(n);
        break;
    case 'p': put_register(1); break;
    case 'P': put_register(0); break;
    case 'J': join_lines(); break;
    case 'u': do_undo(); break;
    case '~':
        if (cx < llen(cy)) {
            char ch = lines[cy][cx];
            save_undo();
            if (ch >= 'a' && ch <= 'z')      lines[cy][cx] = (char)(ch - 32);
            else if (ch >= 'A' && ch <= 'Z') lines[cy][cx] = (char)(ch + 32);
            modified = 1;
            cx++;
            clamp_cx(0);
        }
        break;
    case 'r': {
        int k = read_key();
        if (k >= 32 && k < 127 && cx < llen(cy)) {
            save_undo();
            lines[cy][cx] = (char)k;
            modified = 1;
        }
        break;
    }

    case 'i':
        save_undo();
        mode = M_INSERT;
        break;
    case 'a':
        save_undo();
        if (llen(cy) > 0) cx++;
        mode = M_INSERT;
        break;
    case 'I':
        save_undo();
        cx = 0;
        while (lines[cy][cx] && is_space(lines[cy][cx])) cx++;
        mode = M_INSERT;
        break;
    case 'A':
        save_undo();
        cx = llen(cy);
        mode = M_INSERT;
        break;
    case 'o':
        save_undo();
        if (insert_line(cy + 1, "") == 0) {
            cy++;
            cx = 0;
            mode = M_INSERT;
            modified = 1;
        }
        break;
    case 'O':
        save_undo();
        if (insert_line(cy, "") == 0) {
            cx = 0;
            mode = M_INSERT;
            modified = 1;
        }
        break;

    case '/': search_prompt(); break;
    case 'n': search(1); break;
    case 'N': search(0); break;
    case ':': return run_prompt_command();
    case 'Z':
        if (read_key() == 'Z') {
            if (!filename[0]) { set_msg("No file name"); break; }
            if (write_out(filename) == 0) return 1;
        }
        break;
    default:
        break;
    }
    return 0;
}

static void insert_key(int c) {
    switch (c) {
    case 27:
        mode = M_NORMAL;
        if (cx > 0) cx--;
        break;
    case '\n': case '\r':
        split_line();
        break;
    case 8: case 127:
        backspace();
        break;
    case K_DEL:
        if (cx < llen(cy)) del_chars(cx, 1, 0);
        break;
    case K_LEFT:
        if (cx > 0) cx--;
        break;
    case K_RIGHT:
        if (cx < llen(cy)) cx++;
        break;
    case K_UP:
        if (cy > 0) { cy--; clamp_cx(1); }
        break;
    case K_DOWN:
        if (cy < nlines - 1) { cy++; clamp_cx(1); }
        break;
    case K_HOME:
        cx = 0;
        break;
    case K_END:
        cx = llen(cy);
        break;
    default:
        if (c == '\t' || (c >= 32 && c < 127)) ins_char((char)c);
        break;
    }
}

int main(int argc, char **argv) {
    struct winsize ws;

    // use the whole screen, whatever its size
    if (ioctl(0, TIOCGWINSZ, (unsigned int)&ws) == 0 && ws.ws_row >= 5 && ws.ws_col >= 20) {
        ROWS = ws.ws_row > MAX_ROWS ? MAX_ROWS : ws.ws_row;
        COLS = ws.ws_col > MAX_COLS ? MAX_COLS : ws.ws_col;
    }
    TEXT_ROWS = ROWS - 2;

    pool_reset();

    if (argc > 1) {
        scopy(filename, argv[1], sizeof(filename));
        load_file(filename);
    } else {
        insert_line(0, "");
    }

    ioctl(0, TIOCSRAW, 1);

    for (;;) {
        clamp_cx(mode == M_INSERT);
        draw();

        int c = read_key();
        if (c < 0) break;

        if (mode == M_INSERT) insert_key(c);
        else if (normal(c))   break;
    }

    ioctl(0, TIOCSRAW, 0);
    puts("\033[0m\033[2J\033[H");
    return 0;
}