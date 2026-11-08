/* PS5Term GPL-3.0-or-later. Included by embedded_linux.c after fs.h.
 * A fid is a bounded relative path plus, once opened, a host handle. Only the adapter object
 * is engine-allocated; bulk teardown owns that object. Host handles belong to the caller and
 * are closed with their fid or by the caller after worker join. */
#define S9_FIDS 512 /* Linux holds one per dentry in use: open files and their parents. */
struct FSFile { int used,opened,stale; unsigned handle; uint32_t flags; char path[PS5_SHARE_PATH]; };
typedef struct { FSDevice fs; Ps5LinuxShare api; FSFile fids[S9_FIDS]; } Share9p;
#define S9_ENAMETOOLONG 36
#define S9_EOPNOTSUPP 95
#define S9_API(fs) (((Share9p *)(fs))->api)
#define S9_CTX(fs) (((Share9p *)(fs))->api.context)
static Share9p *s9(FSDevice *fs){return (Share9p *)fs;}
static FSFile *s9_fid(FSDevice *fs,const char *path){
 for(unsigned i=0;i<S9_FIDS;i++)if(!s9(fs)->fids[i].used){FSFile *f=&s9(fs)->fids[i];memset(f,0,offsetof(FSFile,path));f->used=1;snprintf(f->path,sizeof f->path,"%s",path);return f;}
 return NULL;
}
static unsigned s9_key(FSDevice *fs,const FSFile *f){return (unsigned)(f-s9(fs)->fids);}
static int s9_name_ok(const char *name){
 size_t n=strlen(name);
 if(!n || !strcmp(name,".") || !strcmp(name,".."))return -P9_EINVAL;
 if(n>=PS5_SHARE_NAME)return -S9_ENAMETOOLONG;
 return strchr(name,'/') ? -P9_EINVAL:0;
}
static int s9_join(char *out,const char *dir,const char *name){
 int r=s9_name_ok(name);if(r)return r;
 int n=*dir ? snprintf(out,PS5_SHARE_PATH,"%s/%s",dir,name):snprintf(out,PS5_SHARE_PATH,"%s",name);
 return n<0 || n>=PS5_SHARE_PATH ? -S9_ENAMETOOLONG:0;
}
static void s9_qid(FSQID *q,const Ps5ShareStat *st){
 memset(q,0,sizeof(*q));
 const uint32_t type=st->mode&P9_S_IFMT;
 q->type=type==P9_S_IFDIR ? P9_QTDIR:type==P9_S_IFLNK ? P9_QTSYMLINK:P9_QTFILE;
 q->path=(st->device*0x9E3779B97F4A7C15ull)^st->inode; /* Unique across mounted filesystems. */
}
static void s9_end(FSDevice *fs){(void)fs;}
static void s9_close(FSDevice *fs,FSFile *f){
 if(f->handle)S9_API(fs).close(S9_CTX(fs),f->handle);
 S9_API(fs).list_close(S9_CTX(fs),s9_key(fs,f));
 f->handle=0;f->opened=0;
}
static void s9_delete(FSDevice *fs,FSFile *f){s9_close(fs,f);f->used=0;f->stale=0;f->path[0]=0;}
static void s9_statfs(FSDevice *fs,FSStatFS *st){
 Ps5ShareSpace s;memset(st,0,sizeof(*st));st->f_bsize=4096;
 if(S9_API(fs).space(S9_CTX(fs),&s))return;
 st->f_bsize=(uint32_t)s.block_size;st->f_blocks=s.blocks;st->f_bfree=s.free;st->f_bavail=s.available;st->f_files=s.files;st->f_ffree=s.files_free;
}
static int s9_attach(FSDevice *fs,FSFile **out,FSQID *q,uint32_t uid,const char *uname,const char *aname){
 (void)uid;(void)uname;if(*aname)return -P9_EPERM;
 Ps5ShareStat st;int r=S9_API(fs).stat(S9_CTX(fs),"",&st);if(r)return r;
 FSFile *f=s9_fid(fs,"");if(!f)return -P9_ENOSPC;*out=f;s9_qid(q,&st);return 0;
}
static int s9_walk(FSDevice *fs,FSFile **out,FSQID *q,FSFile *from,int n,char **names){
 if(n<0 || n>16)return -P9_EINVAL;
 if(from->stale)return -P9_ENOENT;
 char path[PS5_SHARE_PATH];snprintf(path,sizeof path,"%s",from->path);
 int i=0,error=-P9_ENOENT;
 for(;i<n;i++){
  char next[PS5_SHARE_PATH];Ps5ShareStat st;int r;
  if(!strcmp(names[i],"."))snprintf(next,sizeof next,"%s",path);
  else if(!strcmp(names[i],"..")){ /* The directory's parent, and never above the root. */
   snprintf(next,sizeof next,"%s",path);char *slash=strrchr(next,'/');
   if(slash)*slash=0;else next[0]=0;
  }
  else if((r=s9_join(next,path,names[i]))){error=r;break;}
  r=S9_API(fs).stat(S9_CTX(fs),next,&st);if(r){error=r;break;}
  if(i+1<n && (st.mode&P9_S_IFMT)!=P9_S_IFDIR){error=-P9_ENOTDIR;break;}
  s9_qid(q+i,&st);snprintf(path,sizeof path,"%s",next);
 }
 if(n && !i)return error;
 FSFile *f=s9_fid(fs,path);if(!f)return -P9_ENOSPC;*out=f;return i;
}
static int s9_host_flags(uint32_t flags){
 int host=(flags&3)==P9_O_RDONLY ? PS5_SHARE_READ:(flags&3)==P9_O_WRONLY ? PS5_SHARE_WRITE:PS5_SHARE_READ|PS5_SHARE_WRITE;
 if(flags&P9_O_TRUNC)host|=PS5_SHARE_TRUNCATE;
 return host;
}
static int s9_open(FSDevice *fs,FSQID *q,FSFile *f,uint32_t flags,FSOpenCompletionFunc *cb,void *opaque){
 (void)cb;(void)opaque;
 const uint32_t allowed=P9_O_CREAT|P9_O_NOCTTY|P9_O_WRONLY|P9_O_RDWR|P9_O_TRUNC|P9_O_APPEND|P9_O_NONBLOCK|P9_O_LARGEFILE|P9_O_DIRECTORY|P9_O_NOFOLLOW|P9_O_CLOEXEC|P9_O_SYNC|P9_O_DSYNC|P9_O_DIRECT|P9_O_NOATIME|P9_O_FASYNC;
 if((flags&~allowed) || (flags&3)==3)return -P9_EINVAL;
 if(f->stale)return -P9_ENOENT;
 if((flags&P9_O_TRUNC) && (flags&3)==P9_O_RDONLY)return -P9_EPERM;
 Ps5ShareStat st;int r=S9_API(fs).stat(S9_CTX(fs),f->path,&st);if(r)return r;
 const uint32_t type=st.mode&P9_S_IFMT;
 s9_close(fs,f);
 if(type==P9_S_IFDIR){if((flags&3)!=P9_O_RDONLY)return -P9_EPERM;}
 else {
  if(flags&P9_O_DIRECTORY)return -P9_ENOTDIR;
  if(type!=P9_S_IFREG)return -P9_EPERM; /* A host device or FIFO could stall the worker. */
  r=S9_API(fs).open(S9_CTX(fs),f->path,s9_host_flags(flags),0);if(r<0)return r;
  f->handle=(unsigned)r;
 }
 f->flags=flags;f->opened=1;s9_qid(q,&st);return 0;
}
static int s9_create(FSDevice *fs,FSQID *q,FSFile *f,const char *name,uint32_t flags,uint32_t mode,uint32_t gid){
 (void)gid;
 if(f->opened || f->stale)return -P9_EPERM;
 if((mode&P9_S_IFMT && (mode&P9_S_IFMT)!=P9_S_IFREG) || (mode&07000))return -P9_EPERM;
 if((flags&3)==3 || flags&~(P9_O_CREAT|P9_O_EXCL|P9_O_NOCTTY|P9_O_FASYNC|P9_O_DIRECT|P9_O_NOATIME|P9_O_WRONLY|P9_O_RDWR|P9_O_TRUNC|P9_O_APPEND|P9_O_NONBLOCK|P9_O_LARGEFILE|P9_O_NOFOLLOW|P9_O_CLOEXEC|P9_O_SYNC|P9_O_DSYNC))return -P9_EINVAL;
 if((flags&P9_O_TRUNC) && (flags&3)==P9_O_RDONLY)return -P9_EPERM;
 char path[PS5_SHARE_PATH];int r=s9_join(path,f->path,name);if(r)return r;
 r=S9_API(fs).open(S9_CTX(fs),path,s9_host_flags(flags)|PS5_SHARE_CREATE|(flags&P9_O_EXCL ? PS5_SHARE_EXCLUSIVE:0),mode&0777);if(r<0)return r;
 Ps5ShareStat st;int e=S9_API(fs).fstat(S9_CTX(fs),(unsigned)r,&st);
 if(e){S9_API(fs).close(S9_CTX(fs),(unsigned)r);return e;}
 f->handle=(unsigned)r;f->flags=flags;f->opened=1;snprintf(f->path,sizeof f->path,"%s",path);s9_qid(q,&st);return 0;
}
static int s9_stat(FSDevice *fs,FSFile *f,FSStat *out){
 /* After unlink Linux may ask through the dentry's fid for a file that is still open. */
 const FSFile *source=f;
 if(f->stale && !f->handle)for(unsigned i=0;i<S9_FIDS;i++){
  const FSFile *o=&s9(fs)->fids[i];
  if(o->used && o->stale && o->handle && !strcmp(o->path,f->path)){source=o;break;}
 }
 if(source->stale && !source->handle)return -P9_ENOENT;
 Ps5ShareStat st;int r=source->handle ? S9_API(fs).fstat(S9_CTX(fs),source->handle,&st):S9_API(fs).stat(S9_CTX(fs),source->path,&st);
 if(r)return r;
 memset(out,0,sizeof(*out));s9_qid(&out->qid,&st);
 out->st_mode=st.mode;out->st_nlink=st.nlink;out->st_size=st.size;out->st_blksize=4096;out->st_blocks=st.blocks;
 out->st_atime_sec=out->st_mtime_sec=out->st_ctime_sec=st.mtime_sec;
 out->st_atime_nsec=out->st_mtime_nsec=out->st_ctime_nsec=st.mtime_nsec;
 return 0;
}
static int s9_setattr(FSDevice *fs,FSFile *f,uint32_t mask,uint32_t mode,uint32_t uid,uint32_t gid,uint64_t size,uint64_t at,uint64_t an,uint64_t mt,uint64_t mn){
 /* Every file reports owner 0 and the native owner cannot change, so only that is accepted. */
 if(((mask&P9_SETATTR_UID) && uid) || ((mask&P9_SETATTR_GID) && gid))return -P9_EPERM;
 if(f->stale && !f->handle)return -P9_ENOENT;
 int r=0;
 if(mask&P9_SETATTR_SIZE){r=S9_API(fs).resize(S9_CTX(fs),f->handle,f->path,size);if(r)return r;}
 if(f->stale)return 0; /* The name is gone; what remains is timestamps nobody can see. */
 if(mask&P9_SETATTR_MODE){r=S9_API(fs).chmod(S9_CTX(fs),f->path,mode&07777);if(r)return r;}
 if(mask&(P9_SETATTR_ATIME|P9_SETATTR_MTIME)){
  const int now=!(mask&(P9_SETATTR_ATIME_SET|P9_SETATTR_MTIME_SET));
  r=S9_API(fs).times(S9_CTX(fs),f->path,at,(uint32_t)an,mt,(uint32_t)mn,now);
 }
 return r;
}
static void s9_le(uint8_t *b,uint64_t n,unsigned bytes){for(unsigned i=0;i<bytes;i++)b[i]=(uint8_t)(n>>(8*i));}
static uint64_t s9_hash(const char *a,const char *b){
 uint64_t h=0xcbf29ce484222325ull;
 for(;*a;a++)h=(h^(uint8_t)*a)*0x100000001b3ull;
 h=(h^'/')*0x100000001b3ull;
 for(;*b;b++)h=(h^(uint8_t)*b)*0x100000001b3ull;
 return h|1;
}
static int s9_readdir(FSDevice *fs,FSFile *f,uint64_t offset,uint8_t *buf,int count){
 if(f->handle || !f->opened || count<0)return -P9_EINVAL;
 int pos=0;
 for(uint64_t cookie=offset;;cookie++){
  char name[PS5_SHARE_NAME];
  int r=S9_API(fs).list(S9_CTX(fs),s9_key(fs,f),f->path,cookie,name,sizeof name);
  if(r<0)return pos ? pos:r;
  if(!r)break;
  size_t n=strnlen(name,sizeof name);if(!n || n>=sizeof name)return -P9_EIO;
  size_t bytes=24+n;if(bytes>(size_t)(count-pos))break;
  /* Type unknown: Linux stats when it cares, and a listing never touches each host file. */
  uint8_t *b=buf+pos;b[0]=P9_QTFILE;s9_le(b+1,0,4);s9_le(b+5,s9_hash(f->path,name),8);s9_le(b+13,cookie+1,8);b[21]=0;s9_le(b+22,n,2);memcpy(b+24,name,n);pos+=(int)bytes;
 }
 return pos;
}
static int s9_read(FSDevice *fs,FSFile *f,uint64_t off,uint8_t *buf,int n){
 if(!f->opened || !f->handle || n<0 || (unsigned)n>PS5_SHARE_IO || (f->flags&3)==P9_O_WRONLY)return -P9_EPERM;
 return S9_API(fs).read(S9_CTX(fs),f->handle,off,buf,(size_t)n);
}
static int s9_write(FSDevice *fs,FSFile *f,uint64_t off,const uint8_t *buf,int n){
 if(!f->opened || !f->handle || n<0 || (unsigned)n>PS5_SHARE_IO || (f->flags&3)==P9_O_RDONLY)return -P9_EPERM;
 if(f->flags&P9_O_APPEND){Ps5ShareStat st;int r=S9_API(fs).fstat(S9_CTX(fs),f->handle,&st);if(r)return r;off=st.size;}
 return S9_API(fs).write(S9_CTX(fs),f->handle,off,buf,(size_t)n);
}
static int s9_mkdir(FSDevice *fs,FSQID *q,FSFile *f,const char *n,uint32_t m,uint32_t g){
 (void)g;if(f->stale)return -P9_ENOENT;
 char path[PS5_SHARE_PATH];int r=s9_join(path,f->path,n);if(r)return r;
 r=S9_API(fs).mkdir(S9_CTX(fs),path,m&0777);if(r)return r;
 Ps5ShareStat st;r=S9_API(fs).stat(S9_CTX(fs),path,&st);if(r)return r;
 s9_qid(q,&st);return 0;
}
static int s9_link(FSDevice *fs,FSFile *d,FSFile *f,const char *n){(void)fs;(void)d;(void)f;(void)n;return -S9_EOPNOTSUPP;}
static int s9_symlink(FSDevice *fs,FSQID *q,FSFile *f,const char *n,const char *to,uint32_t gid){(void)fs;(void)q;(void)f;(void)n;(void)to;(void)gid;return -S9_EOPNOTSUPP;}
static int s9_mknod(FSDevice *fs,FSQID *q,FSFile *f,const char *n,uint32_t m,uint32_t a,uint32_t b,uint32_t g){(void)fs;(void)q;(void)f;(void)n;(void)m;(void)a;(void)b;(void)g;return -S9_EOPNOTSUPP;}
static int s9_readlink(FSDevice *fs,char *b,int n,FSFile *f){
 if(n<1 || f->stale)return -P9_EINVAL;
 return S9_API(fs).readlink(S9_CTX(fs),f->path,b,(size_t)n);
}
static int s9_under(const char *path,const char *top){
 const size_t n=strlen(top);
 return !strncmp(path,top,n) && (!path[n] || path[n]=='/');
}
/* Fids name paths, so a change to the tree has to reach the fids that named the old place.
 * Removed names go stale and never resolve to a newly created file of the same name. */
static void s9_retire(FSDevice *fs,const char *path){
 for(unsigned i=0;i<S9_FIDS;i++){FSFile *o=&s9(fs)->fids[i];if(o->used && !o->stale && s9_under(o->path,path))o->stale=1;}
}
static int s9_rename(FSDevice *fs,FSFile *a,const char *n,FSFile *b,const char *to){
 if(a->stale || b->stale)return -P9_ENOENT;
 char from[PS5_SHARE_PATH],target[PS5_SHARE_PATH];
 int r=s9_join(from,a->path,n);if(!r)r=s9_join(target,b->path,to);if(r)return r;
 r=S9_API(fs).rename(S9_CTX(fs),from,target);if(r)return r;
 if(!strcmp(from,target))return 0;
 s9_retire(fs,target);
 const size_t len=strlen(from);
 for(unsigned i=0;i<S9_FIDS;i++){
  FSFile *o=&s9(fs)->fids[i];
  if(!o->used || o->stale || !s9_under(o->path,from))continue;
  char moved[PS5_SHARE_PATH];int w=snprintf(moved,sizeof moved,"%s%s",target,o->path+len);
  if(w<0 || w>=(int)sizeof moved)o->stale=1;else memcpy(o->path,moved,(size_t)w+1);
 }
 return 0;
}
static int s9_unlink(FSDevice *fs,FSFile *f,const char *n){
 if(f->stale)return -P9_ENOENT;
 char path[PS5_SHARE_PATH];int r=s9_join(path,f->path,n);if(r)return r;
 r=S9_API(fs).remove(S9_CTX(fs),path);if(r)return r;
 s9_retire(fs,path);return 0;
}
static int s9_lock(FSDevice *fs,FSFile *f,const FSLock *lock){(void)fs;(void)f;(void)lock;return -S9_EOPNOTSUPP;}
static int s9_getlock(FSDevice *fs,FSFile *f,FSLock *lock){(void)fs;(void)f;(void)lock;return -S9_EOPNOTSUPP;}
static FSDevice *share9p_init(const Ps5LinuxShare *api){
 Share9p *s=ps5_linux_calloc(1,sizeof(*s));if(!s)return NULL;s->api=*api;
 s->fs=(FSDevice){s9_end,s9_delete,s9_statfs,s9_attach,s9_walk,s9_mkdir,s9_open,s9_create,s9_stat,s9_setattr,s9_close,s9_readdir,s9_read,s9_write,s9_link,s9_symlink,s9_mknod,s9_readlink,s9_rename,s9_unlink,s9_lock,s9_getlock};return &s->fs;
}
