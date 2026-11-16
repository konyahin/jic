// try: place for 

#include "../vendor/termbox2.h"
#include "prelude.c"

// Width counts codepoints: right for Latin and Cyrillic, not for wide or
// combining characters.
static void draw(int32_t y, Str s, uintattr_t fg)
{
    int32_t w = 0;
    for (ptrdiff_t i = 0; i < s.len; i++) {
        w += ((unsigned char)s.data[i] & 0xc0) != 0x80;
    }
    tb_printf((tb_width() - w)/2, y, fg, TB_DEFAULT, "%.*s", (int)s.len, s.data);
}

static void render(Arena scratch)
{
    Out out = newout(&scratch, 64, -1);
    appendi64(&out, tb_width());
    append(&out, S("x"));
    appendi64(&out, tb_height());
    append(&out, S(", any key quits"));

    int32_t mid = tb_height() / 2;
    tb_clear();
    draw(mid-1, S("привет, новый дивный классный старый ужасный мир"), TB_GREEN|TB_BOLD);
    draw(mid+1, span(out.buf, out.buf+out.len), TB_DEFAULT);
    tb_present();
}

int main(void)
{
    static char mem[1<<16];
    Arena scratch = {0};
    scratch.beg = mem;
    scratch.end = mem + countof(mem);

    int r = tb_init();
    if (r != TB_OK) {
        Out  err = newout(&scratch, 256, STDERR_FILENO);
        char *msg = (char *)tb_strerror(r);
        append(&err, S("try: "));
        append(&err, fromcstr(msg));
        append(&err, S("\n"));
        flush(&err);
        return 1;
    }

    for (;;) {
        render(scratch);
        struct tb_event ev = {0};
        r = tb_poll_event(&ev);
        if (r == TB_ERR_POLL && tb_last_errno() == EINTR) continue;
        if (r != TB_OK || ev.type == TB_EVENT_KEY) break;
    }

    tb_shutdown();
    return 0;
}
