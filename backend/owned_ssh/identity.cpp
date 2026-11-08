// PS5Term owned SSH identity. GPL-3.0-or-later.
#include "identity.hpp"
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/coding.h>
#include <wolfssl/wolfcrypt/random.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace term::ssh
{
namespace
{
void wipe(void *data,std::size_t size) noexcept
{
    auto *p=static_cast<volatile unsigned char *>(data);
    while(size--) *p++=0;
}
// Console storage may be root-owned even when the app has a distinct UID.
// Accept only root or our effective user; retain strict modes and file checks.
bool trusted_owner(uid_t owner) noexcept { return owner==0 || owner==geteuid(); }
bool supported_link_count(nlink_t count) noexcept
{
#if defined(PS5TERM_OWNED_SSH_NATIVE)
    // PS5 /data reports zero for linked regular files (verified on phat 13.60).
    // Zero is unavailable metadata, not proof of POSIX single-link semantics.
    return count<=1;
#else
    return count==1;
#endif
}
// 0 absent, 1 valid read, -1 unsafe or damaged. No following symlinks.
int read_private(const char *path,std::span<unsigned char> bytes,std::size_t &size,
                 char *detail,std::size_t capacity,bool allow_empty=false)
{
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)
    {
        if(errno==ENOENT) return 0;
        std::snprintf(detail,capacity,"SSH private file open failed (%d)",errno); return -1;
    }
    struct stat info{};
    const bool inspected=fstat(fd,&info)==0;
    bool ok=inspected && S_ISREG(info.st_mode) && trusted_owner(info.st_uid) &&
        (info.st_mode&0777)==0600 && supported_link_count(info.st_nlink) && (info.st_size>0 || (allow_empty && info.st_size==0)) &&
        static_cast<unsigned long long>(info.st_size)<=bytes.size();
    if(!ok)
        std::snprintf(detail,capacity,"SSH file check: stat=%u mode=%o uid=%u links=%u size=%lld",
            inspected ? 1U : 0U,static_cast<unsigned>(info.st_mode),static_cast<unsigned>(info.st_uid),
            static_cast<unsigned>(info.st_nlink),static_cast<long long>(info.st_size));
    if(ok)
    {
        size=static_cast<std::size_t>(info.st_size);
        std::size_t offset=0;
        while(offset<size)
        {
            const auto count=read(fd,bytes.data()+offset,size-offset);
            if(count<0 && errno==EINTR) continue;
            if(count<=0) { std::snprintf(detail,capacity,"SSH private file read failed (%d)",errno); ok=false; break; }
            offset+=count;
        }
        unsigned char extra=0;
        if(ok && read(fd,&extra,1)!=0) { std::snprintf(detail,capacity,"SSH private file length changed"); ok=false; }
    }
    close(fd);
    if(!ok) { wipe(bytes.data(),bytes.size()); size=0; }
    return ok ? 1 : -1;
}
// A crash can leave an update file behind, which would refuse every later change.
// Only a regular file is removed: a link still fails the exclusive create below.
void remove_stale_update(const char *path)
{
    const int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0) return;
    struct stat info{};
    const bool regular=fstat(fd,&info)==0 && S_ISREG(info.st_mode);
    close(fd);
    if(regular) unlink(path);
}
bool write_private(const char *path,const unsigned char *bytes,std::size_t size)
{
    const int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0) return false;
    std::size_t offset=0; bool ok=true;
    while(offset<size)
    {
        const auto count=write(fd,bytes+offset,size-offset);
        if(count<0 && errno==EINTR) continue;
        if(count<=0) { ok=false; break; }
        offset+=count;
    }
    if(ok && fsync(fd)<0) ok=false;
    if(close(fd)<0) ok=false;
    if(!ok) unlink(path);
    return ok;
}
bool valid_key(std::span<const unsigned char> bytes)
{
    ecc_key key{};
    if(wc_ecc_init(&key)!=0) return false;
    word32 offset=0;
    const bool ok=wc_EccPrivateKeyDecode(bytes.data(),&offset,&key,bytes.size())==0 &&
        offset==bytes.size() && wc_ecc_size(&key)==32 && wc_ecc_check_key(&key)==0;
    wc_ecc_free(&key); return ok;
}
}
void Identity::clear() noexcept
{
    directory_={}; authorized_keys_={}; authorized_count_=0;
    wipe(password_.data(),password_.size()); password_size_=0;
    wipe(key_.data(),key_.size()); key_size_=0;
}
bool Identity::load_authorized_keys(const char *directory) noexcept
{
    std::array<char,1024> path{};
    const int length=std::snprintf(path.data(),path.size(),"%s/authorized_keys",directory);
    if(length<0 || static_cast<std::size_t>(length)>=path.size()) return false;
    std::array<unsigned char,8192> buffer{}; std::size_t size=0;
    const int state=read_private(path.data(),buffer,size,detail_.data(),detail_.size(),true);
    if(state<0) { error_=detail_.data(); return false; }
    if(!state) return true; // Missing optional file means password-only login.
    return parse_authorized_keys({reinterpret_cast<const char *>(buffer.data()),size});
}
bool Identity::parse_authorized_keys(std::string_view text) noexcept
{
    authorized_keys_={}; authorized_count_=0;
    error_="Invalid authorized_keys (Ed25519/ECDSA, max 8 keys)";
    if(text.find('\0')!=text.npos) return false;
    auto trim=[](std::string_view value) {
        const auto start=value.find_first_not_of(" \t\r");
        if(start==value.npos) return std::string_view{};
        return std::string_view(value.data()+start,value.find_last_not_of(" \t\r")-start+1);
    };
    while(!text.empty())
    {
        const auto newline=text.find('\n');
        auto line=trim({text.data(),newline==text.npos ? text.size() : newline});
        text=newline==text.npos ? std::string_view{} : std::string_view(text.data()+newline+1,text.size()-newline-1);
        if(line.empty() || line.front()=='#') continue;
        const auto split=line.find_first_of(" \t");
        if(split==line.npos || authorized_count_==authorized_keys_.size()) return false;
        const std::string_view type(line.data(),split);
        const auto curve=type=="ecdsa-sha2-nistp256" ? "nistp256" :
                         type=="ecdsa-sha2-nistp384" ? "nistp384" : "";
        const bool ed25519=type=="ssh-ed25519";
        if(!*curve && !ed25519) return false; // No options/certs or unsupported algorithms.
        line=trim({line.data()+split,line.size()-split});
        const auto end=line.find_first_of(" \t");
        const std::string_view encoded(line.data(),end==line.npos ? line.size() : end);
        if(encoded.empty() || encoded.size()>344 || encoded.size()%4) return false;
        auto &key=authorized_keys_[authorized_count_]; word32 decoded=key.bytes.size();
        if(Base64_Decode(reinterpret_cast<const byte *>(encoded.data()),encoded.size(),key.bytes.data(),&decoded)!=0)
            return false;
        // Require canonical encoding: reject ignored characters and bad padding.
        if(encoded.size()!=4*((decoded+2)/3)) return false;
        constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for(std::size_t i=0,j=0;i<decoded;i+=3,j+=4)
        {
            const unsigned a=key.bytes[i], b=i+1<decoded ? key.bytes[i+1] : 0,
                           c=i+2<decoded ? key.bytes[i+2] : 0;
            if(encoded[j]!=alphabet[a>>2] || encoded[j+1]!=alphabet[((a&3)<<4)|(b>>4)] ||
               encoded[j+2]!=(i+1<decoded ? alphabet[((b&15)<<2)|(c>>6)] : '=') ||
               encoded[j+3]!=(i+2<decoded ? alphabet[c&63] : '=')) return false;
        }
        key.size=decoded;
        std::span<const unsigned char> wire(key.bytes.data(),key.size);
        auto field=[&](std::span<const unsigned char> &out) {
            if(wire.size()<4) return false;
            const std::size_t n=(std::size_t(wire[0])<<24)|(std::size_t(wire[1])<<16)|
                                (std::size_t(wire[2])<<8)|wire[3];
            wire=wire.subspan(4);
            if(n>wire.size()) return false;
            out=wire.first(n); wire=wire.subspan(n); return true;
        };
        std::span<const unsigned char> algorithm{},group{},point{};
        if(!field(algorithm) ||
           std::string_view(reinterpret_cast<const char *>(algorithm.data()),algorithm.size())!=type) return false;
        if(ed25519)
        {
            if(!field(point) || point.size()!=32 || !wire.empty()) return false;
            ++authorized_count_; continue; // wolfSSH validates the Ed25519 signature.
        }
        if(!field(group) || !field(point) || !wire.empty() ||
           std::string_view(reinterpret_cast<const char *>(group.data()),group.size())!=curve ||
           point.size()!=(type=="ecdsa-sha2-nistp256" ? 65U : 97U) || point[0]!=4) return false;
        ecc_key checked{};
        if(wc_ecc_init(&checked)!=0) return false;
        const bool valid=wc_ecc_import_x963_ex(point.data(),point.size(),&checked,
                type=="ecdsa-sha2-nistp256" ? ECC_SECP256R1 : ECC_SECP384R1)==0 && wc_ecc_check_key(&checked)==0;
        wc_ecc_free(&checked);
        if(!valid) return false;
        ++authorized_count_;
    }
    return true;
}
bool Identity::update_authorized_key(std::string_view text,bool remove) noexcept
{
    if(!directory_[0] || text.empty() || text.size()>512 || text.find('\n')!=text.npos || text.find('\r')!=text.npos)
        return false;
    Identity incoming,current;
    if(!incoming.parse_authorized_keys(text) || incoming.authorized_count_!=1 ||
       !current.load_authorized_keys(directory_.data()) || current.authorized_count_!=authorized_count_) return false;
    auto same=[](const AuthorizedKey &a,const AuthorizedKey &b) {
        return a.size==b.size && std::memcmp(a.bytes.data(),b.bytes.data(),a.size)==0;
    };
    // Refuse to overwrite independently edited configuration; restart reloads it.
    for(std::size_t i=0;i<authorized_count_;++i)
        if(!same(current.authorized_keys_[i],authorized_keys_[i])) return false;
    const auto &key=incoming.authorized_keys_[0];
    std::size_t found=authorized_count_;
    for(std::size_t i=0;i<authorized_count_;++i) if(same(key,authorized_keys_[i])) { found=i; break; }
    if(remove)
    {
        if(found==authorized_count_) return false;
        for(std::size_t i=found;i+1<current.authorized_count_;++i) current.authorized_keys_[i]=current.authorized_keys_[i+1];
        current.authorized_keys_[--current.authorized_count_]={};
    }
    else
    {
        if(found!=authorized_count_) return true; // Idempotent enrollment.
        if(current.authorized_count_==current.authorized_keys_.size()) return false;
        current.authorized_keys_[current.authorized_count_++]=key;
    }
    std::array<unsigned char,8192> output{}; std::size_t used=0;
    for(const auto &entry:current.authorized_keys())
    {
        const std::size_t type_size=(std::size_t(entry.bytes[0])<<24)|(std::size_t(entry.bytes[1])<<16)|
                                    (std::size_t(entry.bytes[2])<<8)|entry.bytes[3];
        // Every entry has already passed the strict SSH wire-format parser.
        std::memcpy(output.data()+used,entry.bytes.data()+4,type_size); used+=type_size;output[used++]=' ';
        word32 available=output.size()-used;
        if(Base64_Encode_NoNl(entry.bytes.data(),entry.size,output.data()+used,&available)!=0) return false;
        used+=available;output[used++]='\n';
    }
    std::array<char,1100> path{},temporary{};
    const int a=std::snprintf(path.data(),path.size(),"%s/authorized_keys",directory_.data());
    const int b=std::snprintf(temporary.data(),temporary.size(),"%s/authorized_keys.update",directory_.data());
    if(a<0 || b<0 || static_cast<std::size_t>(a)>=path.size() || static_cast<std::size_t>(b)>=temporary.size()) return false;
    remove_stale_update(temporary.data());
    if(!write_private(temporary.data(),output.data(),used)) return false;
    if(rename(temporary.data(),path.data())!=0) { unlink(temporary.data());return false; }
    authorized_keys_=current.authorized_keys_;authorized_count_=current.authorized_count_;
    return true;
}
bool Identity::update_password(std::string_view value) noexcept
{
    if(!directory_[0] || value.size()<8 || value.size()>64) return false;
    for(unsigned char c:value) if(c<32 || c>126) return false;
    std::array<char,1024> path{},temporary{};
    const int a=std::snprintf(path.data(),path.size(),"%s/password",directory_.data());
    const int b=std::snprintf(temporary.data(),temporary.size(),"%s/password.update",directory_.data());
    if(a<0 || b<0 || static_cast<std::size_t>(a)>=path.size() || static_cast<std::size_t>(b)>=temporary.size()) return false;
    std::array<unsigned char,128> previous{}; std::size_t size=0;
    const bool unchanged=read_private(path.data(),previous,size,detail_.data(),detail_.size())==1 &&
        size==password_size_ && std::memcmp(previous.data(),password_.data(),size)==0;
    wipe(previous.data(),previous.size());
    remove_stale_update(temporary.data());
    if(!unchanged || !write_private(temporary.data(),reinterpret_cast<const unsigned char *>(value.data()),value.size())) return false;
    if(rename(temporary.data(),path.data())!=0) { unlink(temporary.data()); return false; }
    std::memmove(password_.data(),value.data(),value.size()); password_size_=value.size();
    wipe(password_.data()+password_size_,password_.size()-password_size_);
    return true;
}
Identity::~Identity() { clear(); }
bool Identity::load_or_create(const char *directory) noexcept
{
    clear(); error_="Unsafe or damaged SSH identity";
    if(!directory || !*directory) return false;
    // A first run may have no parent yet, such as /data/ps5term on a fresh console.
    std::array<char,1024> parent{};
    if(const auto length=std::strlen(directory);length<parent.size())
    {
        std::memcpy(parent.data(),directory,length);
        if(auto *slash=std::strrchr(parent.data(),'/');slash && slash!=parent.data()) { *slash=0; (void)mkdir(parent.data(),0700); }
    }
    if(mkdir(directory,0700)<0 && errno!=EEXIST) { error_="Cannot create SSH identity directory"; return false; }
    struct stat info{};
    const int directory_fd=open(directory,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(directory_fd<0) { error_="Cannot open SSH identity directory"; return false; }
    const bool inspected=fstat(directory_fd,&info)==0;
    const bool closed=close(directory_fd)==0;
    if(!inspected || !closed) { error_="Cannot inspect SSH identity directory"; return false; }
    if(!S_ISDIR(info.st_mode)) { error_="SSH identity path is not a directory"; return false; }
    if(!trusted_owner(info.st_uid))
    {
        std::snprintf(detail_.data(),detail_.size(),"SSH directory owner %u differs from app uid %u",
            static_cast<unsigned>(info.st_uid),static_cast<unsigned>(geteuid()));
        error_=detail_.data(); return false;
    }
    if((info.st_mode&0777)!=0700) { error_="SSH identity directory permissions unsafe"; return false; }
    const auto directory_size=std::strlen(directory);
    if(directory_size>=directory_.size()) return false;
    std::memcpy(directory_.data(),directory,directory_size+1);
    std::array<char,1024> key_path{},password_path{};
    const int a=std::snprintf(key_path.data(),key_path.size(),"%s/host-key.der",directory);
    const int b=std::snprintf(password_path.data(),password_path.size(),"%s/password",directory);
    if(a<0 || b<0 || static_cast<std::size_t>(a)>=key_path.size() || static_cast<std::size_t>(b)>=password_path.size()) return false;
    std::array<unsigned char,128> password{}; std::size_t password_size=0;
    error_=detail_.data();
    const int key_state=read_private(key_path.data(),key_,key_size_,detail_.data(),detail_.size());
    if(key_state<0) { clear(); return false; }
    const int password_state=read_private(password_path.data(),password,password_size,detail_.data(),detail_.size());
    if(password_state<0) { clear(); wipe(password.data(),password.size()); return false; }
    error_="Unsafe or damaged SSH identity";
    if(!load_authorized_keys(directory)) { clear(); wipe(password.data(),password.size()); return false; }
    // The key is written first, so a first run interrupted after it leaves this state.
    // Keep that key rather than refusing every later start; only the password is new.
    const bool keep_key=key_state==1 && password_state==0 && valid_key(key());
    if(key_state==1 && password_state==1)
    {
        bool valid=valid_key(key()) && password_size>=8 && password_size<=64;
        for(auto c:std::span(password.data(),password_size)) if(c<32 || c>126) valid=false;
        if(valid) { std::memcpy(password_.data(),password.data(),password_size); password_size_=password_size; }
        wipe(password.data(),password.size());
        if(!valid) { clear(); return false; }
        error_=""; return true;
    }
    wipe(password.data(),password.size());
    if(!keep_key && (key_state!=0 || password_state!=0)) { error_="SSH identity files unsafe, missing or unreadable"; clear(); return false; }
    error_="SSH identity generation failed";
    WC_RNG rng{};
    const int rng_result=wc_InitRng(&rng);
    if(rng_result!=0)
    {
        std::snprintf(detail_.data(),detail_.size(),"SSH entropy initialization failed (%d)",rng_result);
        error_=detail_.data(); return false;
    }
    ecc_key key{};
    const bool initialized=wc_ecc_init(&key)==0;
    std::array<unsigned char,24> random{};
    bool ok=initialized && (keep_key || wc_ecc_make_key_ex(&rng,32,&key,ECC_SECP256R1)==0) &&
        wc_RNG_GenerateBlock(&rng,random.data(),random.size())==0;
    if(ok && !keep_key)
    {
        const int size=wc_EccKeyToDer(&key,key_.data(),key_.size());
        ok=size>0;
        if(ok) key_size_=size;
    }
    if(initialized) wc_ecc_free(&key);
    wc_FreeRng(&rng);
    if(ok)
    {
        constexpr char hex[]="0123456789abcdef";
        for(auto c:random) { password_[password_size_++]=hex[c>>4]; password_[password_size_++]=hex[c&15]; }
    }
    wipe(random.data(),random.size());
    if(!ok) { clear(); return false; }
    // Written whole, then renamed, so a crash never leaves a truncated final file.
    // Exclusive temporary creation refuses a link planted at that name.
    const auto persist=[](const char *target,const unsigned char *bytes,std::size_t size) noexcept
    {
        std::array<char,1100> temporary{};
        const int n=std::snprintf(temporary.data(),temporary.size(),"%s.update",target);
        if(n<0 || static_cast<std::size_t>(n)>=temporary.size()) return false;
        remove_stale_update(temporary.data());
        if(!write_private(temporary.data(),bytes,size)) return false;
        if(rename(temporary.data(),target)!=0) { unlink(temporary.data()); return false; }
        return true;
    };
    if(!keep_key && !persist(key_path.data(),key_.data(),key_size_))
    { error_="Cannot persist SSH host key"; clear(); return false; }
    if(!persist(password_path.data(),reinterpret_cast<const unsigned char *>(password_.data()),password_size_))
    { error_="Cannot persist SSH password"; clear(); return false; }
    error_=""; return true;
}
}
