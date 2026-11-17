// reader: simple tui text reader
//
// Every codepoint is one cell. Wide characters (CJK, emoji) take two, so
// their lines overflow the right edge and are cut off: not supported.

#include "../vendor/termbox2.h"
#include "prelude.c"
#include <fcntl.h>

typedef struct {
    int32_t   padding;
    ptrdiff_t page;
    int32_t   width;
    int32_t   rows;
} State;

// Byte offset of the first character of each page.
typedef struct {
    ptrdiff_t *data;
    ptrdiff_t  len;
    ptrdiff_t  cap;
} Pages;

// Splits s into a row of at most n codepoints, breaking at the last space
// so words stay whole. A word longer than n is cut mid-word.
static Cut wrap(Str s, ptrdiff_t n)
{
    Cut r = {0};
    if (!s.len) return r;
    ptrdiff_t i = 0, space = 0;
    for (ptrdiff_t cps = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if ((c & 0xc0) != 0x80 && cps++ == n) break;
        if (c == ' ' && i) space = i;  // a break at 0 would leave the row empty
    }
    ptrdiff_t end = i;
    if (i < s.len && s.data[i] != ' ' && space) end = space;
    r.head = span(s.data, s.data+end);
    r.tail = trimleft(span(s.data+end, s.data+s.len));
    r.ok   = r.tail.len > 0;
    return r;
}

// Lays out one page of text at (x, y), drawing it when draw is set.
// Returns the text after the page.
static Str layout(Str text, int32_t x, int32_t y, int32_t width, int32_t rows,
                  _Bool draw)
{
    int32_t bottom = y + rows;
    Cut line = {0};
    line.tail = text;
    while (width > 0 && line.tail.len && y < bottom) {
        line = cut(line.tail, '\n');
        Cut part = {0};
        part.tail = line.head;
        do {  // an empty line still takes a row
            part = wrap(part.tail, width);
            if (draw) {
                tb_printf(x, y, TB_DEFAULT, TB_DEFAULT, "%.*s",
                          (int)part.head.len, part.head.data);
            }
            y++;
        } while (part.tail.len && y < bottom);
        if (part.tail.len) {  // page ends mid-line: the next one continues it
            return span(part.tail.data, text.data+text.len);
        }
    }
    return line.tail;
}

static Pages paginate(Str text, int32_t width, int32_t rows, Arena *perm)
{
    Pages pages = {0};
    Str   rest  = text;
    do {
        *push(perm, &pages) = rest.data - text.data;
        Str next = layout(rest, 0, 0, width, rows, 0);
        if (next.len == rest.len) break;  // no room for even one row
        rest = next;
    } while (rest.len);
    return pages;
}

static void render(Str text, Pages pages, State state)
{
    assert(state.page >= 0 && state.page < pages.len);

    tb_clear();
    Str page = substring(text, pages.data[state.page]);
    layout(page, state.padding, state.padding, state.width, state.rows, 1);
    tb_printf(state.padding, tb_height() - 1, TB_DEFAULT, TB_DEFAULT,
              "page %td/%td (n/p/q)", state.page + 1, pages.len);
    tb_present();
}

static int32_t fail(Out *err, Str path, char *why)
{
    append(err, S("reader: "));
    if (path.len) {
        append(err, path);
        append(err, S(": "));
    }
    append(err, fromcstr(why));
    append(err, S("\n"));
    flush(err);
    return 1;
}

int main(int argc, char **argv)
{
    static char mem[1<<10];
    Arena scratch = {0};
    scratch.beg = mem;
    scratch.end = mem + countof(mem);
    Out err = newout(&scratch, 512, STDERR_FILENO);

    if (argc) {
        argc--;
        argv++;
    }
    if (argc != 1) {
        append(&err, S("usage: reader FILE\n"));
        flush(&err);
        return 2;
    }
    Str path = fromcstr(argv[0]);

    State state = {0};
    state.padding = 1;

    // Also the largest file reader can open.
    Arena book = newarena(1024 * 1024 * 1024);

    int32_t fd = open(argv[0], O_RDONLY);
    if (fd < 0) return fail(&err, path, strerror(errno));

    Loaded file = loadfd(&book, fd);
    char *why = file.full ? "file too large" : file.ok ? 0 : strerror(errno);
    close(fd);
    if (!file.ok) return fail(&err, path, why);

    int32_t r = tb_init();
    if (r != TB_OK) return fail(&err, (Str){0}, (char *)tb_strerror(r));

    state.width = tb_width() - 2*state.padding;
    state.rows  = tb_height() - 2*state.padding;
    Pages pages = paginate(file.data, state.width, state.rows, &book);

    for (;;) {
        render(file.data, pages, state);
        struct tb_event ev = {0};
        r = tb_poll_event(&ev);
        if (r == TB_ERR_POLL && tb_last_errno() == EINTR) continue;
        if (r != TB_OK) break;
        if (ev.type != TB_EVENT_KEY) continue;
        if (ev.ch == 'q') break;
        if (ev.ch == 'n' && state.page < pages.len - 1) state.page++;
        if (ev.ch == 'p' && state.page > 0) state.page--;
    }

    tb_shutdown();
    return 0;
}
