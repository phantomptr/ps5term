/* PS5Term - GPL-3.0-or-later. Experimental owned Linux worker. */
#ifndef PS5TERM_LINUX_SESSION_H
#define PS5TERM_LINUX_SESSION_H
#include "embedded_linux.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Ps5LinuxSession Ps5LinuxSession;
/* Explicit worker stack. The platform default is far smaller on PS5 than on
 * hosts, so relying on it hides overflows until the device runs them. */
#define PS5_LINUX_WORKER_STACK ((size_t)1024*1024)
typedef enum { PS5_SESSION_STOPPED,PS5_SESSION_STARTING,PS5_SESSION_RUNNING,
               PS5_SESSION_DRAINING,PS5_SESSION_STOPPING,PS5_SESSION_FAILED } Ps5LinuxSessionState;
typedef struct {
    Ps5LinuxSessionState state;
    Ps5LinuxError error;
    int thread_error;
    size_t heap_used;
    int output_blocked;
    int network_state; /* 0 absent/stopped, 1 backend active, -1 backend failed */
} Ps5LinuxSessionStatus;
Ps5LinuxSession *ps5_linux_session_create(size_t heap_limit);
void ps5_linux_session_destroy(Ps5LinuxSession *session);
/* Copies image METADATA, not bytes. The caller must keep all image buffers
 * immutable and alive until stop() returns after joining the worker.
 * Start reports thread creation, not successful boot; inspect status/output.
 * Public I/O, status, resize and stop are synchronized and may run concurrently.
 * Do not race destroy with any call, or a new start with a stop in progress.
 * No callbacks into application code. Guest runtime is confined to one worker.
 */
int ps5_linux_session_start(Ps5LinuxSession *session,const Ps5LinuxImages *images);
int ps5_linux_session_stop(Ps5LinuxSession *session);
size_t ps5_linux_session_write(Ps5LinuxSession *session,const void *bytes,size_t size);
size_t ps5_linux_session_read(Ps5LinuxSession *session,void *bytes,size_t size);
int ps5_linux_session_resize(Ps5LinuxSession *session,unsigned columns,unsigned rows);
Ps5LinuxSessionStatus ps5_linux_session_status(Ps5LinuxSession *session);
/* The agent's byte stream (Ps5LinuxImages.agent), independent of the console.
 * Lossless with partial acceptance; a stalled reader does not stop the guest. */
size_t ps5_linux_session_agent_write(Ps5LinuxSession *session,const void *bytes,size_t size);
size_t ps5_linux_session_agent_read(Ps5LinuxSession *session,void *bytes,size_t size);
#ifdef __cplusplus
}
#endif
#endif
