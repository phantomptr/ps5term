/* PS5Term SPDX-License-Identifier: GPL-3.0-or-later
 * Private compatibility subset for pinned libslirp only, not a GLib ABI.
 * Every allocation belongs to a guarded network operation. */
#ifndef PS5TERM_SLIRP_GLIB_H
#define PS5TERM_SLIRP_GLIB_H
#include "resource.h"
#include "socket_policy.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
static const struct in6_addr ps5term_slirp_in6addr_any=IN6ADDR_ANY_INIT;
static const struct in6_addr ps5term_slirp_in6addr_loopback=IN6ADDR_LOOPBACK_INIT;
#define in6addr_any ps5term_slirp_in6addr_any
#define in6addr_loopback ps5term_slirp_in6addr_loopback
/* Only numeric IPv4 names used by diagnostics. Never invoke a host resolver. */
static inline int ps5term_slirp_numeric_name(const struct sockaddr *address,socklen_t length,
 char *host,socklen_t host_size,char *service,socklen_t service_size,int flags){
 if(!address || length<sizeof(struct sockaddr_in) || address->sa_family!=AF_INET || flags!=(NI_NUMERICHOST|NI_NUMERICSERV))return EAI_FAMILY;
 const struct sockaddr_in *a=(const struct sockaddr_in *)address;
 if(host && !inet_ntop(AF_INET,&a->sin_addr,host,host_size))return EAI_OVERFLOW;
 if(service){int n=snprintf(service,service_size,"%u",ntohs(a->sin_port));if(n<0 || (unsigned)n>=service_size)return EAI_OVERFLOW;}
 return 0;
}
#define getnameinfo ps5term_slirp_numeric_name
#define GUINT16_FROM_BE ntohs
#define GUINT16_TO_BE htons
#define GUINT32_FROM_BE ntohl
#define GUINT32_TO_BE htonl
#define GINT16_FROM_BE(x) ((int16_t)ntohs((uint16_t)(x)))
#define GINT16_TO_BE(x) ((int16_t)htons((uint16_t)(x)))
#define GINT32_FROM_BE(x) ((int32_t)ntohl((uint32_t)(x)))
#define GINT32_TO_BE(x) ((int32_t)htonl((uint32_t)(x)))
#define G_GNUC_PRINTF(a,b) __attribute__((format(printf,a,b)))
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#define G_N_ELEMENTS(a) (sizeof(a)/sizeof((a)[0]))
#define G_UNLIKELY(x) (x)
#define G_STATIC_ASSERT(x) _Static_assert(x,#x)
#define G_LITTLE_ENDIAN 1234
#define G_BIG_ENDIAN 4321
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define G_BYTE_ORDER G_LITTLE_ENDIAN
#else
#define G_BYTE_ORDER G_BIG_ENDIAN
#endif
#define TRUE 1
#define FALSE 0
#define g_malloc ps5_net_alloc
#define g_malloc0(n) ps5_net_calloc(1,(n))
#define g_realloc ps5_net_realloc
#define g_free ps5_net_free
#define g_new(t,n) ((t *)ps5_net_calloc((n),sizeof(t)))
#define g_new0(t,n) g_new(t,n)
#define g_assert(x) do {if(!(x))ps5_net_fail();}while(0)
#define g_assert_not_reached() ps5_net_fail()
#define g_error(...) ps5_net_fail()
/* No console logging in the packet path. Nonfatal diagnostics are discarded. */
#define g_debug(...) ((void)0)
#define g_warning(...) ((void)0)
#define g_critical(...) ((void)0)
#define g_warn_if_fail(x) ((void)(x))
#define g_warn_if_reached() ((void)0)
#define g_return_if_fail(x) do {if(!(x))return;}while(0)
#define g_return_val_if_fail(x,v) do {if(!(x))return (v);}while(0)
#define g_snprintf snprintf
#define g_vsnprintf vsnprintf
#define g_strerror strerror
#define g_ascii_strcasecmp strcasecmp
/* Process environment and GLib debug parsing are deliberately unavailable. */
typedef struct {const char *key;unsigned value;} GDebugKey;
static inline const char *g_getenv(const char *s){(void)s;return NULL;}
static inline unsigned g_parse_debug_string(const char *s,const GDebugKey *k,unsigned n){(void)s;(void)k;(void)n;return 0;}
typedef char **GStrv;
static inline unsigned g_strv_length(GStrv v){unsigned n=0;if(v)while(v[n])n++;return n;}
static inline char *g_strdup(const char *s){if(!s)return NULL;size_t n=strlen(s)+1;char *p=g_malloc(n);memcpy(p,s,n);return p;}
static inline int g_str_has_prefix(const char *s,const char *prefix){return strncmp(s,prefix,strlen(prefix))==0;}
static inline size_t g_strlcpy(char *dst,const char *src,size_t cap){size_t n=strlen(src);if(cap){size_t k=n<cap-1?n:cap-1;memcpy(dst,src,k);dst[k]=0;}return n;}
static inline char *g_strstr_len(const char *s,ptrdiff_t len,const char *needle){
 if(len<0)return strstr(s,needle);size_t n=strlen(needle);if(!n)return (char *)s;
 for(size_t i=0;i+n<=(size_t)len;i++)if(!memcmp(s+i,needle,n))return (char *)(s+i);return NULL;
}
/* libslirp uses GRand solely for ephemeral UDP/RA selection. Use the platform
 * random generator rather than introducing a deterministic per-instance PRNG. */
typedef struct {unsigned unused;} GRand;
static inline GRand *g_rand_new(void){return g_new0(GRand,1);}
#define g_rand_free g_free
static inline int32_t g_rand_int_range(GRand *r,int32_t low,int32_t high){(void)r;if(high<=low)ps5_net_fail();return (int32_t)((int64_t)low+ps5_net_random_uniform((uint32_t)((int64_t)high-low)));}
typedef struct {char *str;size_t len,allocated_len;} GString;
static inline GString *g_string_new(const char *value){GString *s=g_new0(GString,1);s->str=g_strdup(value?value:"");s->len=strlen(s->str);s->allocated_len=s->len+1;return s;}
static inline void g_string_append_printf(GString *s,const char *format,...) G_GNUC_PRINTF(2,3);
static inline void g_string_append_printf(GString *s,const char *format,...){
 va_list ap,copy;va_start(ap,format);va_copy(copy,ap);int n=vsnprintf(NULL,0,format,copy);va_end(copy);
 if(n<0 || (size_t)n>SIZE_MAX-s->len-1){va_end(ap);ps5_net_fail();}
 size_t needed=s->len+(size_t)n+1;s->str=g_realloc(s->str,needed);s->allocated_len=needed;
 vsnprintf(s->str+s->len,(size_t)n+1,format,ap);va_end(ap);s->len+=(size_t)n;
}
static inline char *g_string_free(GString *s,int free_segment){char *p=s->str;if(free_segment){g_free(p);p=NULL;}g_free(s);return p;}
#endif
