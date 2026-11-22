#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stat.h>
#include "internal.h"

static FILE f_stdin  = { .fd = 0, .flags = F_READ,                 .unget = -1 };
static FILE f_stdout = { .fd = 1, .flags = F_WRITE,                .unget = -1 };
static FILE f_stderr = { .fd = 2, .flags = F_WRITE | F_NOBUF,      .unget = -1 };

FILE *stdin  = &f_stdin;
FILE *stdout = &f_stdout;
FILE *stderr = &f_stderr;

static FILE *open_files;                    // every FILE created by fopen/fdopen

// ---------- buffers ---------- //

// Gives the stream a buffer. Returns -1 if it is unbuffered.
static int setup_buf(FILE *f) {
    if (f->buf) return (f->flags & F_NOBUF) ? -1 : 0;
    if (f->flags & F_NOBUF) return -1;

    f->buf = malloc(BUFSIZ);
    if (f->buf) {
        f->bufsize = BUFSIZ;
        f->flags |= F_OWNBUF;
    } else {
        f->buf = f->sbuf;
        f->bufsize = 1;
    }
    if ((f->flags & F_WRITE) && isatty(f->fd)) f->flags |= F_LINEBUF;
    return 0;
}

static int flush_w(FILE *f) {
    size_t off = 0;

    while (off < f->wpos) {
        ssize_t n = write(f->fd, f->buf + off, f->wpos - off);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            f->flags |= F_ERR;
            f->wpos = 0;
            return EOF;
        }
        off += (size_t)n;
    }
    f->wpos = 0;
    return 0;
}

static int to_write(FILE *f) {
    if (!(f->flags & F_WRITE)) { f->flags |= F_ERR; errno = EBADF; return -1; }
    if (f->rend > f->rpos)                                  // give back what was read ahead
        lseek(f->fd, -(off_t)(f->rend - f->rpos), SEEK_CUR);
    f->rpos = f->rend = 0;
    f->unget = -1;
    return 0;
}

static int to_read(FILE *f) {
    if (!(f->flags & F_READ)) { f->flags |= F_ERR; errno = EBADF; return -1; }
    if (f->wpos && flush_w(f)) return -1;
    return 0;
}

static int fill(FILE *f) {
    if (!f->buf) {
        f->buf = malloc(BUFSIZ);
        if (f->buf) { f->bufsize = BUFSIZ; f->flags |= F_OWNBUF; }
        else        { f->buf = f->sbuf; f->bufsize = 1; }
    }
    if (f == stdin && stdout->wpos) fflush(stdout);         // show the prompt before waiting

    ssize_t n = read(f->fd, f->buf, f->bufsize);
    if (n < 0) { f->flags |= F_ERR; return -1; }
    if (n == 0) { f->flags |= F_EOF; return 0; }
    f->rpos = 0;
    f->rend = (size_t)n;
    return (int)n;
}

// ---------- output ---------- //

int fputc(int c, FILE *f) {
    unsigned char ch = (unsigned char)c;

    if (to_write(f)) return EOF;
    if (setup_buf(f) != 0) {
        if (write(f->fd, &ch, 1) != 1) { f->flags |= F_ERR; return EOF; }
        return ch;
    }

    f->buf[f->wpos++] = ch;
    if (f->wpos == f->bufsize || ((f->flags & F_LINEBUF) && ch == '\n'))
        if (flush_w(f)) return EOF;
    return ch;
}

int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c)       { return fputc(c, stdout); }

size_t fwrite(const void *p, size_t size, size_t n, FILE *f) {
    size_t total = size * n;
    const unsigned char *s = p;

    if (total == 0) return 0;
    if (to_write(f)) return 0;

    if (setup_buf(f) != 0) {
        size_t off = 0;
        while (off < total) {
            ssize_t w = write(f->fd, s + off, total - off);
            if (w <= 0) { f->flags |= F_ERR; return off / size; }
            off += (size_t)w;
        }
        return n;
    }

    for (size_t i = 0; i < total; i++) {
        f->buf[f->wpos++] = s[i];
        if (f->wpos == f->bufsize || ((f->flags & F_LINEBUF) && s[i] == '\n'))
            if (flush_w(f)) return i / size;
    }
    return n;
}

int fputs(const char *s, FILE *f) {
    size_t n = strlen(s);
    return fwrite(s, 1, n, f) == n ? 0 : EOF;
}

int puts(const char *s) {
    if (fputs(s, stdout) == EOF) return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}

int fflush(FILE *f) {
    if (!f) {
        int r = 0;
        for (FILE *p = open_files; p; p = p->next)
            if (p->wpos && flush_w(p)) r = EOF;
        if (f_stdout.wpos && flush_w(&f_stdout)) r = EOF;
        if (f_stderr.wpos && flush_w(&f_stderr)) r = EOF;
        return r;
    }
    return f->wpos ? flush_w(f) : 0;
}

void __stdio_flush_all(void) {
    fflush(NULL);
}

// ---------- input ---------- //

int fgetc(FILE *f) {
    if (f->unget >= 0) {
        int c = f->unget;
        f->unget = -1;
        return c;
    }
    if (to_read(f)) return EOF;
    if (f->rpos == f->rend && fill(f) <= 0) return EOF;
    return f->buf[f->rpos++];
}

int getc(FILE *f)  { return fgetc(f); }
int getchar(void)  { return fgetc(stdin); }

int ungetc(int c, FILE *f) {
    if (c == EOF || f->unget >= 0) return EOF;
    f->unget = (unsigned char)c;
    f->flags &= ~F_EOF;
    return c;
}

size_t fread(void *p, size_t size, size_t n, FILE *f) {
    size_t total = size * n, got = 0;
    unsigned char *d = p;

    if (total == 0) return 0;
    while (got < total) {
        int c = fgetc(f);
        if (c == EOF) break;
        d[got++] = (unsigned char)c;
    }
    return got / size;
}

char *fgets(char *s, int n, FILE *f) {
    int i = 0;

    if (n <= 0) return NULL;
    while (i < n - 1) {
        int c = fgetc(f);
        if (c == EOF) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return NULL;
    s[i] = '\0';
    return s;
}

ssize_t getdelim(char **line, size_t *cap, int delim, FILE *f) {
    size_t len = 0;

    if (!*line || *cap == 0) {
        *cap = 128;
        *line = malloc(*cap);
        if (!*line) return -1;
    }
    for (;;) {
        int c = fgetc(f);
        if (c == EOF) break;
        if (len + 2 > *cap) {
            char *p = realloc(*line, *cap * 2);
            if (!p) return -1;
            *line = p;
            *cap *= 2;
        }
        (*line)[len++] = (char)c;
        if (c == delim) break;
    }
    if (len == 0) return -1;
    (*line)[len] = '\0';
    return (ssize_t)len;
}

ssize_t getline(char **line, size_t *cap, FILE *f) {
    return getdelim(line, cap, '\n', f);
}

// ---------- positioning ---------- //

long ftell(FILE *f) {
    long p = (long)lseek(f->fd, 0, SEEK_CUR);
    if (p < 0) return -1;
    p -= (long)(f->rend - f->rpos);
    p += (long)f->wpos;
    if (f->unget >= 0) p--;
    return p;
}

int fseek(FILE *f, long off, int whence) {
    if (f->wpos && flush_w(f)) return -1;
    if (whence == SEEK_CUR) {
        off -= (long)(f->rend - f->rpos);
        if (f->unget >= 0) off--;
    }
    f->rpos = f->rend = 0;
    f->unget = -1;
    if (lseek(f->fd, off, whence) < 0) return -1;
    f->flags &= ~F_EOF;
    return 0;
}

void rewind(FILE *f) {
    fseek(f, 0, SEEK_SET);
    f->flags &= ~F_ERR;
}

int fgetpos(FILE *f, fpos_t *pos) {
    long p = ftell(f);
    if (p < 0) return -1;
    *pos = p;
    return 0;
}

int fsetpos(FILE *f, const fpos_t *pos) {
    return fseek(f, *pos, SEEK_SET);
}

// ---------- status ---------- //

int feof(FILE *f)      { return (f->flags & F_EOF) != 0; }
int ferror(FILE *f)    { return (f->flags & F_ERR) != 0; }
void clearerr(FILE *f) { f->flags &= ~(F_EOF | F_ERR); }
int fileno(FILE *f)    { return f->fd; }

int setvbuf(FILE *f, char *buf, int mode, size_t size) {
    if (f->wpos) flush_w(f);
    f->flags &= ~(F_NOBUF | F_LINEBUF);

    if (mode == _IONBF) f->flags |= F_NOBUF;
    else if (mode == _IOLBF) f->flags |= F_LINEBUF;

    if (buf && size > 0 && mode != _IONBF) {
        if ((f->flags & F_OWNBUF) && f->buf) free(f->buf);
        f->buf = (unsigned char *)buf;
        f->bufsize = size;
        f->flags &= ~F_OWNBUF;
    }
    return 0;
}

void setbuf(FILE *f, char *buf) {
    setvbuf(f, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

// ---------- opening and closing ---------- //

static int parse_mode(const char *mode, int *oflags, int *fflags) {
    int plus = strchr(mode, '+') != NULL;

    switch (mode[0]) {
    case 'r': *oflags = plus ? O_RDWR : O_RDONLY;                          *fflags = F_READ | (plus ? F_WRITE : 0); break;
    case 'w': *oflags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;    *fflags = F_WRITE | (plus ? F_READ : 0); break;
    case 'a': *oflags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;   *fflags = F_WRITE | (plus ? F_READ : 0); break;
    default:  errno = EINVAL; return -1;
    }
    return 0;
}

static FILE *new_file(int fd, int fflags) {
    FILE *f = calloc(1, sizeof(*f));
    if (!f) return NULL;

    f->fd = fd;
    f->flags = fflags | F_MALLOCED;
    f->unget = -1;
    f->next = open_files;
    open_files = f;
    return f;
}

FILE *fopen(const char *path, const char *mode) {
    int oflags, fflags;

    if (parse_mode(mode, &oflags, &fflags)) return NULL;
    int fd = open(path, oflags, 0666);
    if (fd < 0) return NULL;

    FILE *f = new_file(fd, fflags);
    if (!f) { close(fd); return NULL; }
    return f;
}

FILE *fdopen(int fd, const char *mode) {
    int oflags, fflags;

    if (parse_mode(mode, &oflags, &fflags)) return NULL;
    return new_file(fd, fflags);
}

int fclose(FILE *f) {
    int r = 0;

    if (f->wpos && flush_w(f)) r = EOF;
    if (f->rend > f->rpos) lseek(f->fd, -(off_t)(f->rend - f->rpos), SEEK_CUR);
    if (close(f->fd) < 0) r = EOF;

    for (FILE **pp = &open_files; *pp; pp = &(*pp)->next) {
        if (*pp == f) { *pp = f->next; break; }
    }
    if ((f->flags & F_OWNBUF) && f->buf) free(f->buf);
    if (f->flags & F_MALLOCED) free(f);
    return r;
}

FILE *freopen(const char *path, const char *mode, FILE *f) {
    int oflags, fflags;

    if (f->wpos) flush_w(f);
    if (parse_mode(mode, &oflags, &fflags)) return NULL;

    int fd = open(path, oflags, 0666);
    if (fd < 0) return NULL;

    dup2(fd, f->fd);                                // keeps stdin/stdout/stderr working
    close(fd);
    f->flags = (f->flags & (F_MALLOCED | F_OWNBUF)) | fflags;
    f->rpos = f->rend = f->wpos = 0;
    f->unget = -1;
    return f;
}

void perror(const char *s) {
    if (s && *s) {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}

int remove(const char *path) {
    struct stat st;

    if (stat(path, &st) < 0) return -1;
    return S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path);
}