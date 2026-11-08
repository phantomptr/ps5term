/* PS5Term GPL-3.0-or-later. Direct 9p adapter contract, no guest required. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs.h"
#include "shared_folder.h"
#define ps5_linux_calloc calloc
#include "shared_9p.inc"
/* A tiny host: "", "dir" and "dir/sub" are directories, "file" and "dir/f" are files, "link" a
 * symlink, and "dev" a device. Handles are 1 for any open file. */
static char contents[32] = "native";
static uint64_t length = 6;
static int opens, closes, list_closes, removes, renames, resizes, chmods, times_set, mkdirs;
static int last_flags, times_now;
static char last_path[PS5_SHARE_PATH], last_to[PS5_SHARE_PATH];
static int known(const char *p)
{
    const char *all[] = {"",    "dir",   "dir/sub",   "file",    "dir/f", "link",
                         "dev", "moved", "moved/sub", "moved/f", "dir2"};
    for (size_t i = 0; i < sizeof(all) / sizeof(*all); i++)
        if (!strcmp(p, all[i]))
            return 1;
    return 0;
}
static uint32_t mode_of(const char *p)
{
    if (!*p || !strcmp(p, "dir") || !strcmp(p, "dir/sub") || !strcmp(p, "moved") ||
        !strcmp(p, "moved/sub"))
        return P9_S_IFDIR | 0755;
    if (!strcmp(p, "link"))
        return P9_S_IFLNK | 0777;
    if (!strcmp(p, "dev"))
        return P9_S_IFCHR | 0600;
    return P9_S_IFREG | 0644;
}
static int st_path(void *c, const char *p, Ps5ShareStat *st)
{
    (void)c;
    if (!known(p))
        return -2;
    memset(st, 0, sizeof(*st));
    st->device = 7;
    st->inode = strlen(p) + 1;
    st->size = length;
    st->blocks = 8;
    st->mtime_sec = 1234;
    st->mtime_nsec = 5;
    st->nlink = 1;
    st->mode = mode_of(p);
    return 0;
}
static int st_handle(void *c, unsigned h, Ps5ShareStat *st)
{
    (void)c;
    assert(h == 1);
    return st_path(c, "file", st);
}
static int do_open(void *c, const char *p, int flags, unsigned mode)
{
    (void)c;
    (void)mode;
    opens++;
    last_flags = flags;
    snprintf(last_path, sizeof last_path, "%s", p);
    return 1;
}
static void do_close(void *c, unsigned h)
{
    (void)c;
    assert(h == 1);
    closes++;
}
static int rd(void *c, unsigned h, uint64_t off, void *b, size_t n)
{
    (void)c;
    assert(h == 1);
    if (off > length)
        return 0;
    if (n > length - off)
        n = length - off;
    memcpy(b, contents + off, n);
    return (int)n;
}
static int wr(void *c, unsigned h, uint64_t off, const void *b, size_t n)
{
    (void)c;
    assert(h == 1);
    if (off + n > sizeof(contents))
        return -28;
    memcpy(contents + off, b, n);
    if (off + n > length)
        length = off + n;
    return (int)n;
}
static int rs(void *c, unsigned h, const char *p, uint64_t n)
{
    (void)c;
    (void)h;
    (void)p;
    resizes++;
    length = n;
    return 0;
}
static int cm(void *c, const char *p, unsigned m)
{
    (void)c;
    (void)p;
    (void)m;
    chmods++;
    return 0;
}
static int tm(void *c, const char *p, uint64_t a, uint32_t an, uint64_t m, uint32_t mn, int now)
{
    (void)c;
    (void)p;
    (void)a;
    (void)an;
    (void)m;
    (void)mn;
    times_set++;
    times_now = now;
    return 0;
}
static int md(void *c, const char *p, unsigned m)
{
    (void)c;
    (void)m;
    mkdirs++;
    snprintf(last_path, sizeof last_path, "%s", p);
    return 0;
}
static int rm(void *c, const char *p)
{
    (void)c;
    removes++;
    snprintf(last_path, sizeof last_path, "%s", p);
    return known(p) ? 0 : -2;
}
static int mv(void *c, const char *a, const char *b)
{
    (void)c;
    renames++;
    snprintf(last_path, sizeof last_path, "%s", a);
    snprintf(last_to, sizeof last_to, "%s", b);
    return 0;
}
static int rl(void *c, const char *p, char *b, size_t n)
{
    (void)c;
    (void)p;
    snprintf(b, n, "/target");
    return 0;
}
static int ls(void *c, unsigned key, const char *p, uint64_t cookie, char *b, size_t n)
{
    (void)c;
    (void)key;
    (void)p;
    if (cookie)
        return 0;
    snprintf(b, n, "file");
    return 1;
}
static void lc(void *c, unsigned key)
{
    (void)c;
    (void)key;
    list_closes++;
}
static int sp(void *c, Ps5ShareSpace *s)
{
    (void)c;
    memset(s, 0, sizeof(*s));
    s->block_size = 512;
    s->blocks = 100;
    s->free = 50;
    s->available = 40;
    s->files = 9;
    s->files_free = 8;
    return 0;
}
int main(void)
{
    static int context;
    Ps5LinuxShare api = {&context, st_path, st_handle, do_open, do_close, rd, wr, rs, cm,
                         tm,       md,      rm,        mv,      rl,       ls, lc, sp};
    FSDevice *fs = share9p_init(&api);
    assert(fs);
    FSFile *root, *f, *g, *bad = NULL;
    FSQID q, qs[16];
    FSStat st;
    assert(fs->fs_attach(fs, &root, &q, 0, "root", "other") < 0);
    assert(!fs->fs_attach(fs, &root, &q, 0, "root", ""));
    assert(q.type == P9_QTDIR);

    /* Walking: subdirectories, dots, a partial result, and no way above the root. */
    char *file[] = {"file"}, *deep[] = {"dir", "sub"}, *up[] = {"dir", "..", "..", "file"};
    char *missing[] = {"absent"}, *partial[] = {"dir", "absent"}, *through[] = {"file", "x"};
    char *slash[] = {"a/b"};
    assert(fs->fs_walk(fs, &f, qs, root, 2, deep) == 2 && qs[0].type == P9_QTDIR &&
           qs[1].type == P9_QTDIR);
    assert(!strcmp(f->path, "dir/sub"));
    fs->fs_delete(fs, f);
    assert(fs->fs_walk(fs, &f, qs, root, 4, up) == 4 && !strcmp(f->path, "file") &&
           qs[3].type == P9_QTFILE);
    fs->fs_delete(fs, f);
    assert(fs->fs_walk(fs, &bad, qs, root, 1, missing) == -2 && !bad);
    assert(fs->fs_walk(fs, &bad, qs, root, 1, slash) == -22 && !bad);
    assert(fs->fs_walk(fs, &f, qs, root, 2, partial) == 1);
    fs->fs_delete(fs, f);
    assert(fs->fs_walk(fs, &f, qs, root, 2, through) == -20);
    assert(fs->fs_walk(fs, &f, qs, root, 0, NULL) == 0 && !f->path[0]);
    fs->fs_delete(fs, f);

    /* A regular file opens; a device and a symlink never reach the host's open. */
    char *dev[] = {"dev"}, *link_name[] = {"link"};
    int before = opens;
    assert(fs->fs_walk(fs, &f, qs, root, 1, dev) == 1);
    assert(fs->fs_open(fs, &q, f, P9_O_RDONLY, NULL, NULL) == -P9_EPERM && opens == before);
    fs->fs_delete(fs, f);
    assert(fs->fs_walk(fs, &f, qs, root, 1, link_name) == 1 && qs[0].type == P9_QTSYMLINK);
    assert(fs->fs_open(fs, &q, f, P9_O_RDONLY, NULL, NULL) == -P9_EPERM && opens == before);
    char target[32];
    assert(!fs->fs_readlink(fs, target, sizeof target, f) && !strcmp(target, "/target"));
    fs->fs_delete(fs, f);
    assert(fs->fs_walk(fs, &f, qs, root, 1, file) == 1);
    assert(fs->fs_open(fs, &q, f, P9_O_RDONLY | P9_O_TRUNC, NULL, NULL) == -P9_EPERM);
    assert(fs->fs_open(fs, &q, f, P9_O_RDONLY | P9_O_DIRECTORY, NULL, NULL) == -P9_ENOTDIR);
    assert(fs->fs_open(fs, &q, f, 3, NULL, NULL) == -P9_EINVAL);
    assert(!fs->fs_open(fs, &q, f, P9_O_RDWR, NULL, NULL));
    assert(last_flags == (PS5_SHARE_READ | PS5_SHARE_WRITE));
    char b[128];
    assert(fs->fs_read(fs, f, 0, (uint8_t *)b, 6) == 6 && !memcmp(b, "native", 6));
    assert(fs->fs_read(fs, f, 0, (uint8_t *)b, PS5_SHARE_IO + 1) == -P9_EPERM);
    assert(fs->fs_write(fs, f, 0, (const uint8_t *)"guest", 5) == 5);
    /* Reopening does not leak a handle; append writes at the end whatever offset was asked. */
    int closed = closes;
    assert(!fs->fs_open(fs, &q, f, P9_O_WRONLY | P9_O_APPEND | P9_O_CREAT, NULL, NULL));
    assert(closes == closed + 1);
    assert(fs->fs_write(fs, f, 0, (const uint8_t *)"!", 1) == 1 && length == 7 &&
           contents[6] == '!');
    assert(fs->fs_read(fs, f, 0, (uint8_t *)b, 1) == -P9_EPERM);

    /* Attributes. */
    assert(!fs->fs_stat(fs, f, &st) && st.st_size == 7 && st.st_uid == 0 &&
           st.st_mtime_sec == 1234);
    assert(!fs->fs_setattr(fs, f, P9_SETATTR_SIZE, 0, 0, 0, 2, 0, 0, 0, 0) && length == 2 &&
           resizes == 1);
    assert(!fs->fs_setattr(fs, f, P9_SETATTR_SIZE | P9_SETATTR_MTIME | P9_SETATTR_CTIME, 0, 0, 0, 2,
                           0, 0, 0, 0));
    assert(times_set == 1 && times_now == 1);
    assert(
        !fs->fs_setattr(fs, f, P9_SETATTR_MTIME | P9_SETATTR_MTIME_SET, 0, 0, 0, 0, 0, 0, 77, 0) &&
        !times_now);
    assert(!fs->fs_setattr(fs, f, P9_SETATTR_MODE, 0640, 0, 0, 0, 0, 0, 0, 0) && chmods == 1);
    assert(!fs->fs_setattr(fs, f, P9_SETATTR_UID | P9_SETATTR_GID, 0, 0, 0, 0, 0, 0, 0,
                           0)); /* to root */
    assert(fs->fs_setattr(fs, f, P9_SETATTR_UID, 0, 1, 0, 0, 0, 0, 0, 0) == -P9_EPERM);
    assert(fs->fs_setattr(fs, f, P9_SETATTR_GID, 0, 0, 1, 0, 0, 0, 0, 0) == -P9_EPERM);
    closed = closes;
    fs->fs_delete(fs, f);
    assert(closes == closed + 1);

    /* Directories: read only, listing with a cookie, and a closed reader on release. */
    assert(fs->fs_walk(fs, &f, qs, root, 0, NULL) == 0);
    assert(fs->fs_readdir(fs, f, 0, (uint8_t *)b, 128) == -P9_EINVAL); /* not opened */
    assert(fs->fs_open(fs, &q, f, P9_O_WRONLY, NULL, NULL) == -P9_EPERM);
    assert(!fs->fs_open(fs, &q, f, P9_O_RDONLY, NULL, NULL) && q.type == P9_QTDIR);
    assert(fs->fs_read(fs, f, 0, (uint8_t *)b, 1) == -P9_EPERM);
    assert(fs->fs_readdir(fs, f, 0, (uint8_t *)b, 27) == 0); /* does not fit */
    assert(fs->fs_readdir(fs, f, 0, (uint8_t *)b, 128) == 28);
    assert(b[21] == 0 && b[22] == 4 && !memcmp(b + 24, "file", 4) && b[13] == 1);
    assert(fs->fs_readdir(fs, f, 1, (uint8_t *)b, 128) == 0);
    int readers = list_closes;
    fs->fs_delete(fs, f);
    assert(list_closes > readers);

    /* Create: under a subfolder, exclusive, moving the fid to the new file. */
    assert(fs->fs_walk(fs, &f, qs, root, 1, deep) == 1);
    assert(fs->fs_create(fs, &q, f, "bad/name", P9_O_WRONLY, 0644, 0) == -P9_EINVAL);
    assert(fs->fs_create(fs, &q, f, "..", P9_O_WRONLY, 0644, 0) == -P9_EINVAL);
    assert(fs->fs_create(fs, &q, f, "x", P9_O_WRONLY, P9_S_IFCHR | 0644, 0) == -P9_EPERM);
    assert(fs->fs_create(fs, &q, f, "x", P9_O_RDONLY | P9_O_TRUNC, 0644, 0) == -P9_EPERM);
    assert(!fs->fs_create(fs, &q, f, "new", P9_O_WRONLY | P9_O_EXCL | P9_O_CREAT, 0640, 0));
    assert(!strcmp(f->path, "dir/new") && !strcmp(last_path, "dir/new") && f->opened);
    assert(last_flags == (PS5_SHARE_WRITE | PS5_SHARE_CREATE | PS5_SHARE_EXCLUSIVE));
    assert(fs->fs_create(fs, &q, f, "again", P9_O_WRONLY, 0644, 0) == -P9_EPERM);
    fs->fs_delete(fs, f);

    /* mkdir, and what stays refused. */
    assert(fs->fs_walk(fs, &f, qs, root, 1, deep) == 1);
    assert(!fs->fs_mkdir(fs, &q, f, "sub", 0755, 0) && !strcmp(last_path, "dir/sub") &&
           q.type == P9_QTDIR);
    assert(fs->fs_mkdir(fs, &q, f, "..", 0755, 0) == -P9_EINVAL);
    assert(fs->fs_symlink(fs, &q, f, "s", "/etc/passwd", 0) == -S9_EOPNOTSUPP);
    assert(fs->fs_mknod(fs, &q, f, "n", P9_S_IFCHR | 0600, 1, 1, 0) == -S9_EOPNOTSUPP);
    assert(fs->fs_link(fs, f, root, "h") == -S9_EOPNOTSUPP);
    fs->fs_delete(fs, f);

    /* Removal: a removed name's fid goes stale and never resolves to a new file of that name,
     * but an open fid keeps working, and answers for the dentry's fid. */
    assert(fs->fs_walk(fs, &f, qs, root, 1, file) == 1);
    assert(fs->fs_walk(fs, &g, qs, root, 1, file) == 1);
    assert(!fs->fs_open(fs, &q, g, P9_O_RDONLY, NULL, NULL));
    assert(!fs->fs_unlinkat(fs, root, "file") && !strcmp(last_path, "file"));
    assert(fs->fs_unlinkat(fs, root, "..") == -P9_EINVAL);
    assert(fs->fs_unlinkat(fs, root, "absent") == -2);
    assert(f->stale && g->stale);
    assert(fs->fs_open(fs, &q, f, P9_O_RDONLY, NULL, NULL) == -P9_ENOENT);
    assert(!fs->fs_stat(fs, f, &st)); /* through the open fid */
    assert(fs->fs_read(fs, g, 0, (uint8_t *)b, 1) == 1);
    assert(!fs->fs_stat(fs, g, &st));
    fs->fs_delete(fs, g);
    assert(fs->fs_stat(fs, f, &st) == -P9_ENOENT); /* nothing open any more */
    assert(fs->fs_walk(fs, &bad, qs, f, 0, NULL) == -P9_ENOENT);
    assert(fs->fs_setattr(fs, f, P9_SETATTR_SIZE, 0, 0, 0, 0, 0, 0, 0, 0) == -P9_ENOENT);
    fs->fs_delete(fs, f);

    /* Rename moves the fids of the renamed tree, and retires what it replaced. */
    FSFile *dir, *sub, *other, *neighbour;
    char *other_name[] = {"file"}, *neighbour_name[] = {"dir2"};
    assert(fs->fs_walk(fs, &neighbour, qs, root, 1, neighbour_name) == 1);
    assert(fs->fs_walk(fs, &dir, qs, root, 1, deep) == 1);
    assert(fs->fs_walk(fs, &sub, qs, root, 2, deep) == 2);
    assert(fs->fs_walk(fs, &other, qs, root, 1, other_name) == 1);
    assert(!fs->fs_renameat(fs, root, "dir", root, "moved"));
    assert(!strcmp(last_path, "dir") && !strcmp(last_to, "moved"));
    assert(!strcmp(dir->path, "moved") && !strcmp(sub->path, "moved/sub") &&
           !strcmp(other->path, "file"));
    /* "dir" must not match a neighbour that merely starts with the same characters. */
    assert(!strcmp(neighbour->path, "dir2") && !neighbour->stale);
    fs->fs_delete(fs, neighbour);
    /* Into a folder, by that folder's fid. A name with a slash is not a name. */
    assert(fs->fs_renameat(fs, root, "file", root, "moved/f") == -P9_EINVAL);
    assert(!fs->fs_renameat(fs, root, "file", dir, "f") && !strcmp(other->path, "moved/f"));
    assert(fs->fs_renameat(fs, root, "..", root, "x") == -P9_EINVAL);
    assert(fs->fs_renameat(fs, root, "moved", root, "a/b") == -P9_EINVAL);
    assert(!fs->fs_renameat(fs, root, "moved", root, "moved")); /* no fids change */
    assert(!strcmp(dir->path, "moved"));
    FSFile *victim;
    char *moved_sub[] = {"moved", "sub"};
    assert(fs->fs_walk(fs, &victim, qs, root, 2, moved_sub) == 2);
    assert(!fs->fs_renameat(fs, dir, "f", dir, "sub")); /* replaces moved/sub */
    assert(victim->stale && sub->stale && !strcmp(other->path, "moved/sub") && !other->stale);
    fs->fs_delete(fs, victim);
    fs->fs_delete(fs, sub);
    fs->fs_delete(fs, other);
    fs->fs_delete(fs, dir);

    /* Too long a path is an error, not a truncated host path. */
    char name[255];
    memset(name, 'x', sizeof name - 1);
    name[sizeof name - 1] = 0;
    char *names[] = {name, name, name, name, name};
    assert(fs->fs_walk(fs, &bad, qs, root, 5, names) == -2); /* each step checks the host */
    char longest[300];
    memset(longest, 'y', sizeof longest - 1);
    longest[sizeof longest - 1] = 0;
    assert(fs->fs_unlinkat(fs, root, longest) == -S9_ENAMETOOLONG);

    FSStatFS space;
    fs->fs_statfs(fs, &space);
    assert(space.f_bsize == 512 && space.f_blocks == 100 && space.f_bavail == 40 &&
           space.f_ffree == 8);
    fs->fs_delete(fs, root);

    /* The fid table is bounded and recovers. */
    FSFile *fids[S9_FIDS];
    for (unsigned i = 0; i < S9_FIDS; i++)
        assert(!fs->fs_attach(fs, &fids[i], &q, 0, "root", ""));
    assert(fs->fs_attach(fs, &bad, &q, 0, "root", "") == -28);
    for (unsigned i = 0; i < S9_FIDS; i++)
        fs->fs_delete(fs, fids[i]);
    assert(!fs->fs_attach(fs, &root, &q, 0, "root", ""));
    free(fs);
    puts("PASS: bounded 9p fids, walk, open policy, attributes, listing, create, remove and rename "
         "fid lifetime");
}
