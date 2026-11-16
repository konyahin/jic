#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define assert(c)     while (!(c)) __builtin_unreachable()
#define countof(a)    ((ptrdiff_t)(sizeof(a) / sizeof(*(a))))
#define new(a, n, t)  (t *)alloc_(a, n, sizeof(t), _Alignof(t))
#define S(s)          (Str){s, sizeof(s)-1}

typedef struct {
    char *beg;
    char *end;
} Arena;

static Arena newarena(ptrdiff_t cap)
{
    Arena a = {0};
    a.beg = malloc((size_t)cap);
    a.end = a.beg ? a.beg+cap : 0;
    return a;
}

__attribute((malloc, alloc_align(4)))
static void *alloc_(Arena *a, ptrdiff_t count, ptrdiff_t size, ptrdiff_t align)
{
    assert(count >= 0);
    ptrdiff_t pad = -(uintptr_t)a->beg & (align - 1);
    if (count >= (a->end - a->beg - pad)/size) {
        abort();
    }
    void *r = a->beg + pad;
    a->beg += pad + count*size;
    return memset(r, 0, (size_t)(count*size));
}

typedef struct {
    char     *data;
    ptrdiff_t len;
} Str;

static Str span(char *beg, char *end)
{
    Str r = {0};
    r.data = beg;
    r.len  = beg ? end-beg : 0;
    return r;
}

// Null-safe: a null pointer gives the zero Str.
static Str fromcstr(char *s)
{
    Str r = {0};
    r.data = s;
    for (; s && s[r.len]; r.len++) {}
    return r;
}

static _Bool equals(Str a, Str b)
{
    return a.len==b.len && (!a.len || !memcmp(a.data, b.data, (size_t)a.len));
}

static Str trimleft(Str s)
{
    for (; s.len && (unsigned char)*s.data<=' '; s.data++, s.len--) {}
    return s;
}

static Str trimright(Str s)
{
    for (; s.len && (unsigned char)s.data[s.len-1]<=' '; s.len--) {}
    return s;
}

// Drops the first i bytes. if (i): the zero Str must not do null + 0.
static Str substring(Str s, ptrdiff_t i)
{
    assert(i >= 0 && i <= s.len);
    if (i) {
        s.data += i;
        s.len  -= i;
    }
    return s;
}

typedef struct {
    Str   head;
    Str   tail;
    _Bool ok;
} Cut;

static Cut cut(Str s, char c)
{
    Cut r = {0};
    if (!s.len) return r;
    char *beg = s.data;
    char *end = s.data + s.len;
    char *at  = beg;
    for (; at<end && *at!=c; at++) {}
    r.ok   = at < end;
    r.head = span(beg, at);
    r.tail = span(at+r.ok, end);
    return r;
}

static Str copy(Arena *a, Str s)
{
    Str r = {0};
    r.data = new(a, s.len, char);
    r.len  = s.len;
    if (r.len) memcpy(r.data, s.data, (size_t)r.len);
    return r;
}

// Extends head in place when it ends at the arena's bump pointer.
static Str concat(Arena *a, Str head, Str tail)
{
    if (!head.data || head.data+head.len != a->beg) {
        head = copy(a, head);
    }
    head.len += copy(a, tail).len;
    return head;
}

// Any struct {T *data; ptrdiff_t len; ptrdiff_t cap;}.
#define push(a, s) \
  ((s)->len == (s)->cap \
    ? (s)->data = push_((a), (s)->data, &(s)->cap, \
          sizeof(*(s)->data), _Alignof(__typeof__(*(s)->data))), \
      (s)->data + (s)->len++ \
    : (s)->data + (s)->len++)

static void *push_(Arena *a, void *data, ptrdiff_t *pcap, ptrdiff_t size, ptrdiff_t align)
{
    ptrdiff_t cap = *pcap;
    if (!data || a->beg != (char *)data + cap*size) {
        void *copy = alloc_(a, cap, size, align);
        if (data) memcpy(copy, data, (size_t)(cap*size));
        data = copy;
    }
    ptrdiff_t extend = cap ? cap : 4;
    alloc_(a, extend, size, 1);
    *pcap = cap + extend;
    return data;
}

// lookup(&m, k, 0) looks up; lookup(&m, k, a) upserts.
typedef struct Env Env;
struct Env {
    // https://xkcd.com/221/
    Env *child[4];
    Str  key;
    Str  value;
};

static uint64_t hash64(Str s, uint64_t seed)
{
    uint64_t h = seed ^ 0x100;
    for (ptrdiff_t i = 0; i < s.len; i++) {
        h ^= (unsigned char)s.data[i];
        h *= 1111111111111111111u;
    }
    return h;
}

static Str *lookup(Env **env, Str key, Arena *a)
{
    uint64_t seed = *env ? (uintptr_t)*env : 0;
    for (uint64_t h = hash64(key, seed); *env; h <<= 2) {
        if (equals(key, (*env)->key)) {
            return &(*env)->value;
        }
        env = &(*env)->child[h>>62];
    }
    if (!a) return 0;
    *env = new(a, 1, Env);
    (*env)->key = key;
    return &(*env)->value;
}

typedef struct {
    int32_t value;
    _Bool   ok;
} ParsedI32;

// Accumulates on the negative side so INT32_MIN parses without overflow.
static ParsedI32 parsei32(Str s)
{
    ParsedI32 r = {0};
    _Bool neg = s.len && s.data[0]=='-';
    if (neg == s.len) return r;
    int32_t v = 0;
    for (ptrdiff_t i = neg; i < s.len; i++) {
        int32_t digit = s.data[i] - '0';
        if (digit < 0 || digit > 9) return r;
        if (v < (INT32_MIN + digit)/10) return r;
        v = v*10 - digit;
    }
    if (!neg) {
        if (v == INT32_MIN) return r;
        v = -v;
    }
    r.value = v;
    r.ok    = 1;
    return r;
}

typedef struct {
    Str   data;
    _Bool ok;
    _Bool full;  // the input did not fit in the arena
} Loaded;

// Reads fd to EOF into the arena. On a read error or a full arena it
// commits nothing; full tells them apart. Input that fills the arena
// exactly counts as full: telling it from more to come takes another read.
static Loaded loadfd(Arena *a, int32_t fd)
{
    Loaded r = {0};
    Str    s = {0};
    s.data = a->beg;
    for (;;) {
        ptrdiff_t avail = a->end - a->beg - s.len;
        if (!avail) {
            r.full = 1;
            return r;
        }
        ptrdiff_t n = read(fd, s.data+s.len, (size_t)avail);
        if (n < 0) return r;
        if (!n) break;
        assert(n <= avail);
        s.len += n;
    }
    a->beg += s.len;
    r.data  = s;
    r.ok    = 1;
    return r;
}

enum { OUT_ERR = 1<<0 };

// fd = -1 is memory-only: overflowing it is an error.
typedef struct {
    char     *buf;
    ptrdiff_t cap;
    ptrdiff_t len;
    int32_t   fd;
    int32_t   flags;
} Out;

static Out newout(Arena *a, ptrdiff_t cap, int32_t fd)
{
    Out out = {0};
    out.buf = new(a, cap, char);
    out.cap = cap;
    out.fd  = fd;
    return out;
}

static void flush(Out *out)
{
    for (ptrdiff_t off = 0; !(out->flags & OUT_ERR) && off < out->len;) {
        ssize_t r = write(out->fd, out->buf+off, (size_t)(out->len-off));
        if (r < 1) {
            out->flags |= OUT_ERR;
        } else {
            off += r;
        }
    }
    out->len = 0;
}

static void append(Out *out, Str s)
{
    if (!out->cap && s.len) {
        out->flags |= OUT_ERR;
    }
    for (ptrdiff_t off = 0; !(out->flags & OUT_ERR) && off < s.len;) {
        ptrdiff_t left   = s.len - off;
        ptrdiff_t avail  = out->cap - out->len;
        ptrdiff_t amount = avail<left ? avail : left;
        memcpy(out->buf+out->len, s.data+off, (size_t)amount);
        out->len += amount;
        off    += amount;
        if (amount < left) {
            flush(out);
        }
    }
}

static void appendi64(Out *out, int64_t x)
{
    char  tmp[32];
    char *end = tmp + countof(tmp);
    char *beg = end;
    int64_t t = x<0 ? x : -x;
    do {
        *--beg = (char)('0' - t%10);
    } while (t /= 10);
    if (x < 0) {
        *--beg = '-';
    }
    append(out, span(beg, end));
}

#ifdef PRELUDE_TEST
#include <stdio.h>

typedef struct {
    Str      *data;
    ptrdiff_t len;
    ptrdiff_t cap;
} Strs;

static Strs split(Str s, char sep, Arena *perm)
{
    Strs r = {0};
    Cut  c = {0};
    c.tail = s;
    c.ok   = 1;
    while (c.ok) {
        c = cut(c.tail, sep);
        *push(perm, &r) = c.head;
    }
    return r;
}

static void unittest(Arena *perm, Arena temp)
{
    Strs parts = split(S("a,bb,,ccc"), ',', perm);
    assert(parts.len == 4);
    assert(equals(parts.data[2], S("")));
    assert(equals(parts.data[3], S("ccc")));

    assert(equals(trimright(trimleft(S(" \t hi \n"))), S("hi")));

    assert(equals(substring(S("abc"), 1), S("bc")));
    assert(substring(S("abc"), 3).len == 0);
    Str none = {0};
    assert(!substring(none, 0).data);

    assert(equals(fromcstr("abc"), S("abc")));
    assert(fromcstr("").len == 0);
    assert(!fromcstr(0).data && !fromcstr(0).len);

    Env *env = 0;
    for (int32_t i = 0; i < 1000; i++) {
        Arena scratch = temp;
        Str k = concat(&scratch, S("key"), parts.data[i%4]);
        *lookup(&env, copy(perm, k), perm) = parts.data[i%4];
    }
    assert(equals(*lookup(&env, S("keybb"), 0), S("bb")));
    assert(!lookup(&env, S("nope"), 0));

    ParsedI32 p = parsei32(S("2147483647"));
    assert(p.ok && p.value == INT32_MAX);
    p = parsei32(S("-2147483648"));
    assert(p.ok && p.value == INT32_MIN);
    assert(parsei32(S("-0")).ok);
    assert(!parsei32(S("2147483648")).ok);
    assert(!parsei32(S("-2147483649")).ok);
    assert(!parsei32(S("-")).ok);
    assert(!parsei32(S("")).ok);
    assert(!parsei32(S("+1")).ok);

    Out zero = {0};
    append(&zero, S(""));
    assert(!zero.flags);
    append(&zero, S("x"));
    assert(zero.flags & OUT_ERR);
}

static void loadtest(Arena scratch)
{
    int fds[2];
    assert(!pipe(fds));
    assert(write(fds[1], "hello\nworld", 11) == 11);
    close(fds[1]);
    Loaded r = loadfd(&scratch, fds[0]);
    close(fds[0]);
    assert(r.ok && equals(r.data, S("hello\nworld")));
    char *after = new(&scratch, 16, char);
    assert(after >= r.data.data+r.data.len);
    assert(equals(r.data, S("hello\nworld")));

    char *before = scratch.beg;
    assert(!loadfd(&scratch, -1).ok && scratch.beg == before);

    assert(!pipe(fds));
    Out out = newout(&scratch, 4, fds[1]);
    append(&out, S("hello, "));
    append(&out, S("world"));
    flush(&out);
    close(fds[1]);
    assert(!out.flags);
    r = loadfd(&scratch, fds[0]);
    close(fds[0]);
    assert(r.ok && equals(r.data, S("hello, world")));

    // Larger than the arena, exactly its size, one byte less.
    char junk[100] = {0};
    ptrdiff_t sizes[] = {100, 64, 63};
    for (ptrdiff_t i = 0; i < countof(sizes); i++) {
        Arena small = {0};
        small.beg = new(&scratch, 64, char);
        small.end = small.beg + 64;
        assert(!pipe(fds));
        assert(write(fds[1], junk, (size_t)sizes[i]) == sizes[i]);
        close(fds[1]);
        char *mark = small.beg;
        r = loadfd(&small, fds[0]);
        close(fds[0]);
        if (sizes[i] < 64) {
            assert(r.ok && !r.full && r.data.len == sizes[i]);
        } else {
            assert(!r.ok && r.full && small.beg == mark);
        }
    }
}

// splitmix64
static uint64_t rand64(uint64_t *rng)
{
    uint64_t z = *rng += 0x9e3779b97f4a7c15u;
    z = (z ^ z>>30) * 0xbf58476d1ce4e5b9u;
    z = (z ^ z>>27) * 0x94d049bb133111ebu;
    return z ^ z>>31;
}

static ptrdiff_t randint(uint64_t *rng, ptrdiff_t n)
{
    return (ptrdiff_t)(((rand64(rng)>>32) * (uint64_t)n) >> 32);
}

static ParsedI32 parsei32_ref(Str s)
{
    ParsedI32 r = {0};
    ptrdiff_t i = 0;
    int64_t   sign = 1;
    if (s.len && s.data[0]=='-') {
        sign = -1;
        i    = 1;
    }
    if (i == s.len) return r;
    int64_t v = 0;
    for (; i < s.len; i++) {
        if (s.data[i]<'0' || s.data[i]>'9') return r;
        v = v*10 + (s.data[i] - '0');
        if (v > (int64_t)1<<31) return r;
    }
    v *= sign;
    if (v < INT32_MIN || v > INT32_MAX) return r;
    r.value = (int32_t)v;
    r.ok    = 1;
    return r;
}

static ptrdiff_t gen(uint64_t *rng, char *buf)
{
    static char *seeds[] = {
        "2147483647", "2147483648", "-2147483648", "-2147483649", "-", "-0",
    };
    static char alpha[] = "0123456789-,, \t\n";
    ptrdiff_t len = 0;
    if (!randint(rng, 4)) {
        char *seed = seeds[randint(rng, countof(seeds))];
        for (; seed[len]; len++) buf[len] = seed[len];
    }
    for (ptrdiff_t n = randint(rng, 17); n; n--) {
        buf[len++] = randint(rng, 8)
            ? alpha[randint(rng, countof(alpha)-1)]
            : (char)randint(rng, 256);
    }
    return len;
}

static void fuzz(ptrdiff_t iters, Arena scratch)
{
    uint64_t rng = 1;
    char    *src = 0;
    for (ptrdiff_t it = 0; it < iters; it++) {
        Arena t = scratch;

        // Exact-size heap copy, so ASan sees any overread.
        char      tmp[48];
        ptrdiff_t len = gen(&rng, tmp);
        src = realloc(src, (size_t)(len ? len : 1));
        assert(src);
        if (len) memcpy(src, tmp, (size_t)len);
        Str s = {0};
        if (len) s = span(src, src+len);

        Str       z    = concat(&t, s, S("\0"));
        ptrdiff_t zlen = 0;
        for (; zlen < s.len && s.data[zlen]; zlen++) {}
        assert(equals(fromcstr(z.data), span(z.data, z.data+zlen)));

        ParsedI32 got  = parsei32(s);
        ParsedI32 want = parsei32_ref(s);
        assert(got.ok == want.ok && got.value == want.value);

        Strs      parts  = split(s, ',', &t);
        ptrdiff_t commas = 0;
        for (ptrdiff_t i = 0; i < s.len; i++) commas += s.data[i]==',';
        assert(parts.len == commas+1);
        Str joined = {0};
        for (ptrdiff_t i = 0; i < parts.len; i++) {
            assert(!cut(parts.data[i], ',').ok);
            if (i) joined = concat(&t, joined, S(","));
            joined = concat(&t, joined, parts.data[i]);
        }
        assert(equals(joined, s));

        Str       tr  = trimright(trimleft(s));
        ptrdiff_t beg = tr.len ? tr.data-s.data : s.len;
        assert(beg >= 0 && beg+tr.len <= s.len);
        if (tr.len) {
            assert((unsigned char)tr.data[0] > ' ');
            assert((unsigned char)tr.data[tr.len-1] > ' ');
        }
        for (ptrdiff_t i = 0; i < s.len; i++) {
            if (i < beg || i >= beg+tr.len) assert((unsigned char)s.data[i] <= ' ');
        }

        Env *env = 0;
        for (ptrdiff_t i = 0; i < parts.len; i++) {
            *lookup(&env, parts.data[i], &t) = parts.data[i];
        }
        for (ptrdiff_t i = 0; i < parts.len; i++) {
            Str *v = lookup(&env, parts.data[i], 0);
            assert(v && equals(*v, parts.data[i]));
        }

        Out out = newout(&t, randint(&rng, 9), -1);
        for (ptrdiff_t off = 0; off < s.len;) {
            ptrdiff_t n = 1 + randint(&rng, s.len-off);
            append(&out, span(s.data+off, s.data+off+n));
            off += n;
        }
        if (s.len <= out.cap) {
            assert(!out.flags && equals(span(out.buf, out.buf+out.len), s));
        } else {
            assert(out.flags & OUT_ERR);
        }

        int64_t x = (int64_t)rand64(&rng);
        switch (randint(&rng, 16)) {
        case 0: x = INT64_MIN; break;
        case 1: x = INT64_MAX; break;
        case 2: x = (int32_t)x; break;
        }
        Out  nb = newout(&t, 32, -1);
        char ref[32];
        appendi64(&nb, x);
        int  n  = snprintf(ref, sizeof(ref), "%lld", (long long)x);
        Str  text = span(nb.buf, nb.buf+nb.len);
        assert(!nb.flags && equals(text, span(ref, ref+n)));
        if (x >= INT32_MIN && x <= INT32_MAX) {
            ParsedI32 back = parsei32(text);
            assert(back.ok && back.value == x);
        }
    }
}

int main(int argc, char **argv)
{
    // Separate arenas: a scratch copied from perm would alias its free space.
    Arena perm    = newarena((ptrdiff_t)1<<26);
    Arena scratch = newarena((ptrdiff_t)1<<24);

    ptrdiff_t iters = 200000;
    if (argc > 1) {
        ParsedI32 n = parsei32(fromcstr(argv[1]));
        if (!n.ok || n.value < 0) return 2;
        iters = n.value;
    }

    unittest(&perm, scratch);
    loadtest(scratch);
    fuzz(iters, scratch);

    Out out = newout(&perm, 64, 1);
    append(&out, S("prelude ok: "));
    appendi64(&out, iters);
    append(&out, S(" fuzz iterations\n"));
    flush(&out);
    return out.flags & OUT_ERR;
}
#endif
