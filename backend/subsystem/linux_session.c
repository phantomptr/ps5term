/* PS5Term - GPL-3.0-or-later. One worker owns the C guest; public queues are
 * synchronized independently so no engine call or heap teardown holds the UI
 * mutex. stop signals, wakes a paused worker, then joins exactly once. */
#include "linux_session.h"
#include "guest_trace.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

struct Ps5LinuxSession {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t thread;
    int joinable,joining,stop_requested;
    int flush_requested,flush_started,flush_done;
    Ps5LinuxImages images;
    size_t limit;
    Ps5LinuxSessionStatus status;
    unsigned columns,rows;
    int resize_pending;
    unsigned char input[PS5_LINUX_INPUT_CAPACITY],output[PS5_LINUX_OUTPUT_CAPACITY];
    size_t input_head,input_size,output_head,output_size;
    unsigned char agent_input[PS5_LINUX_INPUT_CAPACITY],agent_output[PS5_LINUX_OUTPUT_CAPACITY];
    size_t agent_input_head,agent_input_size,agent_output_head,agent_output_size;
};
/* On natural exit the engine's byte ring survives its arena teardown. Preserve
 * that tail before destroying the owner; explicit stop may discard queued I/O. */
static void drain_tail(Ps5LinuxSession *session,Ps5Linux *vm)
{
    unsigned char bytes[4096];size_t count;
    while((count=ps5_linux_read(vm,bytes,sizeof(bytes)))!=0) {
        size_t offset=0;
        pthread_mutex_lock(&session->mutex);
        while(offset<count && !session->stop_requested) {
            while(session->output_size==PS5_LINUX_OUTPUT_CAPACITY && !session->stop_requested) {
                int result=pthread_cond_wait(&session->changed,&session->mutex);
                if(result) { session->status.thread_error=result;session->stop_requested=1;session->status.state=PS5_SESSION_FAILED; }
            }
            while(offset<count && session->output_size<PS5_LINUX_OUTPUT_CAPACITY && !session->stop_requested)
                session->output[(session->output_head+session->output_size++)%PS5_LINUX_OUTPUT_CAPACITY]=bytes[offset++];
        }
        int cancelled=session->stop_requested;
        pthread_mutex_unlock(&session->mutex);
        if(cancelled) break;
    }
}
static void *worker(void *opaque)
{
    Ps5LinuxSession *session=opaque;
    pthread_mutex_lock(&session->mutex);
    int cancelled=session->stop_requested;
    pthread_mutex_unlock(&session->mutex);
    ps5_guest_trace("worker-entry",0);
    Ps5Linux *vm=cancelled ? NULL:ps5_linux_create(session->limit);
    ps5_guest_trace("engine-owner-created",vm!=NULL);
    int started=vm && ps5_linux_start(vm,&session->images);
    pthread_mutex_lock(&session->mutex);
    if(!session->stop_requested) {
        session->status.state=started ? PS5_SESSION_RUNNING:PS5_SESSION_FAILED;
        session->status.error=vm ? ps5_linux_error(vm):PS5_LINUX_MEMORY;
    }
    pthread_cond_broadcast(&session->changed);
    pthread_mutex_unlock(&session->mutex);
    ps5_guest_trace("engine-start-result",started);
    unsigned char bytes[4096];
    int traced_step=0;
    unsigned long flush_writes=0;struct timespec flush_mark={0,0};
    while(started) {
        pthread_mutex_lock(&session->mutex);
        if(session->stop_requested) { pthread_mutex_unlock(&session->mutex);break; }
        unsigned columns=session->columns,rows=session->rows;
        int resize=session->resize_pending;session->resize_pending=0;
        int flush_start=session->flush_requested && !session->flush_started;
        if(flush_start) { session->flush_started=1;session->input_head=session->input_size=0; }
        int flushing=session->flush_started;
        size_t input=session->input_size;
        if(input>sizeof(bytes)) input=sizeof(bytes);
        for(size_t i=0;i<input;i++) bytes[i]=session->input[(session->input_head+i)%PS5_LINUX_INPUT_CAPACITY];
        pthread_mutex_unlock(&session->mutex);
        if(flush_start) {
            /* The hvc driver acts on ^O before the tty layer, whatever the foreground
             * program is doing: emergency sync, then remount read-only (clean ext2). */
            ps5_linux_discard_input(vm);(void)ps5_linux_write(vm,"\x0fs\x0fu",4);
            flush_writes=ps5_linux_data_writes(vm);clock_gettime(CLOCK_MONOTONIC,&flush_mark);
        }
        if(resize) ps5_linux_resize(vm,columns,rows);
        size_t accepted=ps5_linux_write(vm,bytes,input);
        pthread_mutex_lock(&session->mutex);
        session->input_head=(session->input_head+accepted)%PS5_LINUX_INPUT_CAPACITY;session->input_size-=accepted;
        size_t room=PS5_LINUX_OUTPUT_CAPACITY-session->output_size;
        if(room>sizeof(bytes)) room=sizeof(bytes);
        pthread_mutex_unlock(&session->mutex);
        size_t output=ps5_linux_read(vm,bytes,room);
        pthread_mutex_lock(&session->mutex);
        for(size_t i=0;i<output;i++) session->output[(session->output_head+session->output_size++)%PS5_LINUX_OUTPUT_CAPACITY]=bytes[i];
        pthread_mutex_unlock(&session->mutex);
        pthread_mutex_lock(&session->mutex);
        size_t agent_in=session->agent_input_size;
        if(agent_in>sizeof(bytes)) agent_in=sizeof(bytes);
        for(size_t i=0;i<agent_in;i++) bytes[i]=session->agent_input[(session->agent_input_head+i)%PS5_LINUX_INPUT_CAPACITY];
        pthread_mutex_unlock(&session->mutex);
        const size_t agent_accepted=agent_in ? ps5_linux_agent_write(vm,bytes,agent_in):0;
        pthread_mutex_lock(&session->mutex);
        session->agent_input_head=(session->agent_input_head+agent_accepted)%PS5_LINUX_INPUT_CAPACITY;
        session->agent_input_size-=agent_accepted;
        size_t agent_room=PS5_LINUX_OUTPUT_CAPACITY-session->agent_output_size;
        if(agent_room>sizeof(bytes)) agent_room=sizeof(bytes);
        pthread_mutex_unlock(&session->mutex);
        const size_t agent_out=ps5_linux_agent_read(vm,bytes,agent_room);
        pthread_mutex_lock(&session->mutex);
        for(size_t i=0;i<agent_out;i++) session->agent_output[(session->agent_output_head+session->agent_output_size++)%PS5_LINUX_OUTPUT_CAPACITY]=bytes[i];
        pthread_mutex_unlock(&session->mutex);
        if(!traced_step) ps5_guest_trace("first-step",0);
        Ps5LinuxState state=ps5_linux_step(vm,50000);
        if(!traced_step) { ps5_guest_trace("first-step-result",state);traced_step=1; }
        pthread_mutex_lock(&session->mutex);
        session->status.heap_used=ps5_linux_heap_used(vm);
        session->status.output_blocked=ps5_linux_output_blocked(vm);
        session->status.network_state=ps5_linux_network_state(vm);
        if(state!=PS5_LINUX_RUNNING) {
            if(!session->stop_requested) session->status.state=PS5_SESSION_DRAINING;
            pthread_mutex_unlock(&session->mutex);
            drain_tail(session,vm);
            pthread_mutex_lock(&session->mutex);
            if(!session->stop_requested) {
                session->status.state=state==PS5_LINUX_FAILED ? PS5_SESSION_FAILED:PS5_SESSION_STOPPED;
                session->status.error=ps5_linux_error(vm);
            }
            pthread_mutex_unlock(&session->mutex);break;
        }
        while(!flushing && !session->flush_requested && !session->stop_requested && session->output_size==PS5_LINUX_OUTPUT_CAPACITY &&
              ps5_linux_output_blocked(vm)) {
            int result=pthread_cond_wait(&session->changed,&session->mutex);
            if(result) { session->status.thread_error=result;session->stop_requested=1;session->status.state=PS5_SESSION_FAILED; }
        }
        /* Publish this batch's output before any idle sleep. While flushing nobody reads. */
        room=PS5_LINUX_OUTPUT_CAPACITY-session->output_size;
        if(room>sizeof(bytes)) room=sizeof(bytes);
        pthread_mutex_unlock(&session->mutex);
        output=ps5_linux_read(vm,bytes,flushing ? sizeof(bytes):room);
        pthread_mutex_lock(&session->mutex);
        if(!flushing) for(size_t i=0;i<output;i++) session->output[(session->output_head+session->output_size++)%PS5_LINUX_OUTPUT_CAPACITY]=bytes[i];
        agent_room=PS5_LINUX_OUTPUT_CAPACITY-session->agent_output_size;
        if(agent_room>sizeof(bytes)) agent_room=sizeof(bytes);
        pthread_mutex_unlock(&session->mutex);
        size_t agent_published=ps5_linux_agent_read(vm,bytes,agent_room);
        pthread_mutex_lock(&session->mutex);
        for(size_t i=0;i<agent_published;i++) session->agent_output[(session->agent_output_head+session->agent_output_size++)%PS5_LINUX_OUTPUT_CAPACITY]=bytes[i];
        unsigned sleep=ps5_linux_sleep_hint(vm);
        if(flushing && !session->flush_done) {
            struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
            unsigned long w=ps5_linux_data_writes(vm);
            if(w!=flush_writes) { flush_writes=w;flush_mark=t; }
            else if((t.tv_sec-flush_mark.tv_sec)*1000+(t.tv_nsec-flush_mark.tv_nsec)/1000000>=250) {
                session->flush_done=1;pthread_cond_broadcast(&session->changed);
            }
        }
        /* Input the engine cannot take yet needs the guest to run, not a spinning worker. */
        if(!session->stop_requested && !session->resize_pending && !output && !agent_published &&
           (!session->input_size || !accepted) && (!session->agent_input_size || !agent_accepted) && sleep) {
            struct timespec until;
            if(clock_gettime(CLOCK_REALTIME,&until)) {
                session->status.thread_error=errno;session->stop_requested=1;session->status.state=PS5_SESSION_FAILED;
            } else {
                until.tv_nsec+=(long)sleep*1000000;
                if(until.tv_nsec>=1000000000) { until.tv_sec++;until.tv_nsec-=1000000000; }
                int result=pthread_cond_timedwait(&session->changed,&session->mutex,&until);
                if(result && result!=ETIMEDOUT) {
                    session->status.thread_error=result;session->stop_requested=1;session->status.state=PS5_SESSION_FAILED;
                }
            }
        }
        pthread_mutex_unlock(&session->mutex);
    }
    ps5_linux_destroy(vm);
    pthread_mutex_lock(&session->mutex);session->status.heap_used=0;session->status.network_state=0;
    pthread_cond_broadcast(&session->changed);pthread_mutex_unlock(&session->mutex);
    return NULL;
}
Ps5LinuxSession *ps5_linux_session_create(size_t limit)
{
    if(!limit || limit>PS5_LINUX_MAX_HEAP) return NULL;
    Ps5LinuxSession *session=calloc(1,sizeof(*session));if(!session) return NULL;
    if(pthread_mutex_init(&session->mutex,NULL)) { free(session);return NULL; }
    if(pthread_cond_init(&session->changed,NULL)) { pthread_mutex_destroy(&session->mutex);free(session);return NULL; }
    session->limit=limit;session->columns=80;session->rows=25;return session;
}
int ps5_linux_session_start(Ps5LinuxSession *session,const Ps5LinuxImages *images)
{
    if(!session || !images) return 0;
    pthread_mutex_lock(&session->mutex);
    if(session->joinable || session->joining) { pthread_mutex_unlock(&session->mutex);return 0; }
    session->images=*images;session->stop_requested=0;session->resize_pending=1;
    session->flush_requested=session->flush_started=session->flush_done=0;
    session->input_head=session->input_size=session->output_head=session->output_size=0;
    session->agent_input_head=session->agent_input_size=session->agent_output_head=session->agent_output_size=0;
    session->status=(Ps5LinuxSessionStatus){.state=PS5_SESSION_STARTING};
    pthread_attr_t attributes;
    int result=pthread_attr_init(&attributes);
    if(!result) {
        result=pthread_attr_setstacksize(&attributes,PS5_LINUX_WORKER_STACK);
        if(!result) result=pthread_create(&session->thread,&attributes,worker,session);
        pthread_attr_destroy(&attributes);
    }
    session->joinable=result==0;
    if(result) session->status=(Ps5LinuxSessionStatus){.state=PS5_SESSION_FAILED,.thread_error=result};
    pthread_mutex_unlock(&session->mutex);return !result;
}
int ps5_linux_session_stop(Ps5LinuxSession *session)
{
    if(!session) return 1;
    pthread_mutex_lock(&session->mutex);
    while(session->joining) pthread_cond_wait(&session->changed,&session->mutex);
    if(!session->joinable) { pthread_mutex_unlock(&session->mutex);return 1; }
    session->joining=1;
    if(session->status.state==PS5_SESSION_RUNNING) {
        /* Bounded clean stop: the worker asks the guest to flush and waits for quiet. */
        session->flush_requested=1;session->flush_started=session->flush_done=0;
        pthread_cond_broadcast(&session->changed);
        struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_sec+=3;
        while(!session->flush_done && session->status.state==PS5_SESSION_RUNNING &&
              pthread_cond_timedwait(&session->changed,&session->mutex,&until)!=ETIMEDOUT) {}
    }
    session->flush_requested=0;
    session->stop_requested=1;session->status.state=PS5_SESSION_STOPPING;
    pthread_cond_broadcast(&session->changed);pthread_mutex_unlock(&session->mutex);
    int result=pthread_join(session->thread,NULL);
    pthread_mutex_lock(&session->mutex);
    session->joining=0;
    if(!result) {
        session->joinable=0;session->status=(Ps5LinuxSessionStatus){.state=PS5_SESSION_STOPPED};
        session->input_head=session->input_size=session->output_head=session->output_size=0;
        session->agent_input_head=session->agent_input_size=session->agent_output_head=session->agent_output_size=0;
    } else { session->status.state=PS5_SESSION_FAILED;session->status.thread_error=result; }
    pthread_cond_broadcast(&session->changed);pthread_mutex_unlock(&session->mutex);return !result;
}
void ps5_linux_session_destroy(Ps5LinuxSession *session)
{
    if(!session || !ps5_linux_session_stop(session)) return;
    pthread_cond_destroy(&session->changed);pthread_mutex_destroy(&session->mutex);free(session);
}
size_t ps5_linux_session_write(Ps5LinuxSession *session,const void *bytes,size_t size)
{
    if(!session || !bytes) return 0;
    pthread_mutex_lock(&session->mutex);
    if(session->status.state!=PS5_SESSION_RUNNING && session->status.state!=PS5_SESSION_STARTING) size=0;
    /* hvc0 treats ^O as the SysRq prefix; ^O^O delivers one ^O. Keep each pair whole. */
    size_t taken=0;
    for(;taken<size;taken++) {
        unsigned char c=((const unsigned char *)bytes)[taken];
        if(PS5_LINUX_INPUT_CAPACITY-session->input_size<(c==0x0f ? 2u:1u)) break;
        session->input[(session->input_head+session->input_size++)%PS5_LINUX_INPUT_CAPACITY]=c;
        if(c==0x0f) session->input[(session->input_head+session->input_size++)%PS5_LINUX_INPUT_CAPACITY]=c;
    }
    pthread_cond_signal(&session->changed);pthread_mutex_unlock(&session->mutex);return taken;
}
size_t ps5_linux_session_agent_write(Ps5LinuxSession *session,const void *bytes,size_t size)
{
    if(!session || !bytes) return 0;
    pthread_mutex_lock(&session->mutex);
    if(session->status.state!=PS5_SESSION_RUNNING && session->status.state!=PS5_SESSION_STARTING) size=0;
    if(size>PS5_LINUX_INPUT_CAPACITY-session->agent_input_size) size=PS5_LINUX_INPUT_CAPACITY-session->agent_input_size;
    for(size_t i=0;i<size;i++) session->agent_input[(session->agent_input_head+session->agent_input_size++)%PS5_LINUX_INPUT_CAPACITY]=((const unsigned char *)bytes)[i];
    pthread_cond_signal(&session->changed);pthread_mutex_unlock(&session->mutex);return size;
}
size_t ps5_linux_session_agent_read(Ps5LinuxSession *session,void *bytes,size_t size)
{
    if(!session || !bytes) return 0;
    pthread_mutex_lock(&session->mutex);
    if(size>session->agent_output_size) size=session->agent_output_size;
    for(size_t i=0;i<size;i++) ((unsigned char *)bytes)[i]=session->agent_output[(session->agent_output_head+i)%PS5_LINUX_OUTPUT_CAPACITY];
    session->agent_output_head=(session->agent_output_head+size)%PS5_LINUX_OUTPUT_CAPACITY;session->agent_output_size-=size;
    pthread_mutex_unlock(&session->mutex);return size;
}
size_t ps5_linux_session_read(Ps5LinuxSession *session,void *bytes,size_t size)
{
    if(!session || !bytes) return 0;
    pthread_mutex_lock(&session->mutex);
    if(size>session->output_size) size=session->output_size;
    for(size_t i=0;i<size;i++) ((unsigned char *)bytes)[i]=session->output[(session->output_head+i)%PS5_LINUX_OUTPUT_CAPACITY];
    session->output_head=(session->output_head+size)%PS5_LINUX_OUTPUT_CAPACITY;session->output_size-=size;
    pthread_cond_signal(&session->changed);pthread_mutex_unlock(&session->mutex);return size;
}
int ps5_linux_session_resize(Ps5LinuxSession *session,unsigned columns,unsigned rows)
{
    if(!session || !columns || !rows || columns>1000 || rows>1000) return 0;
    pthread_mutex_lock(&session->mutex);session->columns=columns;session->rows=rows;session->resize_pending=1;
    pthread_cond_signal(&session->changed);pthread_mutex_unlock(&session->mutex);return 1;
}
Ps5LinuxSessionStatus ps5_linux_session_status(Ps5LinuxSession *session)
{
    if(!session) return (Ps5LinuxSessionStatus){.state=PS5_SESSION_STOPPED};
    pthread_mutex_lock(&session->mutex);Ps5LinuxSessionStatus result=session->status;pthread_mutex_unlock(&session->mutex);return result;
}
