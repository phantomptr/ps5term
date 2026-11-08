/* PS5Term - GPL-3.0-or-later. Pinned TinyEMU in-process research adapter.
 * The recovery boundary and all engine callbacks are C-only. Never longjmp
 * through application C++ callbacks/destructors. All upstream allocations are
 * redirected at compile time; teardown discards even partially built graphs.
 */
#include "embedded_linux.h"
#include "tinyemu_hooks.h"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup
#undef exit
#undef abort
#undef printf
#undef fprintf
#include "guest_memory.h"
#include <stdatomic.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#if defined(__x86_64__)
#define JUMP_BUFFER void *jump[5]
#define SAVE(vm) __builtin_setjmp((vm)->jump)
#define ESCAPE(vm) __builtin_longjmp((vm)->jump,1)
#else
#include <setjmp.h>
#define JUMP_BUFFER jmp_buf jump
#define SAVE(vm) setjmp((vm)->jump)
#define ESCAPE(vm) longjmp((vm)->jump,1)
#endif
#include "cutils.h"
#include "virtio.h"
#include "machine.h"
#include "shared_9p.inc"

typedef union Allocation Allocation;
union Allocation {
    struct { Allocation *next,*previous; size_t size; } link;
    max_align_t alignment;
};
struct Ps5Linux {
    JUMP_BUFFER;
    Allocation *allocations;
    size_t used,limit,allocations_left;
    Ps5LinuxState state;
    Ps5LinuxError error;
    VirtMachine *machine;
    CharacterDevice console,agent_console;
    uint8_t agent_input[PS5_LINUX_INPUT_CAPACITY],agent_output[PS5_LINUX_OUTPUT_CAPACITY];
    size_t agent_input_head,agent_input_size,agent_output_head,agent_output_size;
    BlockDevice block,data_block;
    Ps5LinuxDisk data_disk;
    EthernetDevice ethernet;
    Ps5LinuxNetwork network;
    int network_state;
    size_t network_pending;
    unsigned long data_writes;
    uint8_t network_packet[PS5_LINUX_ETHERNET_FRAME];
    uint8_t *disk;
    size_t disk_size;
    uint8_t input[PS5_LINUX_INPUT_CAPACITY],output[PS5_LINUX_OUTPUT_CAPACITY];
    size_t input_head,input_size,output_head,output_size;
    const Ps5LinuxImages *pending;
    unsigned cycles,columns,rows;
    int size_dirty;
    int output_blocked;
    unsigned sleep_hint;
};
/* Entry is serialized; no TLS dependency in the future native SDK build. */
static atomic_flag engine_entered=ATOMIC_FLAG_INIT;
static Ps5Linux *active;
static _Noreturn void fail(Ps5LinuxError error)
{
    /* Only pinned engine code calls hooks, while invoke's boundary is active. */
    if(!active) abort();
    active->error=error;ESCAPE(active);
}
void *ps5_linux_malloc(size_t size)
{
    Ps5Linux *vm=active;
    if(!vm) abort();
    if(!size) size=1;
    if(size>SIZE_MAX-sizeof(Allocation) || size+sizeof(Allocation)>vm->limit-vm->used ||
       !vm->allocations_left) fail(PS5_LINUX_MEMORY);
    Allocation *block=ps5_guest_buffer_alloc(sizeof(*block)+size);
    if(!block) fail(PS5_LINUX_MEMORY);
    if(vm->allocations_left!=SIZE_MAX) vm->allocations_left--;
    block->link.size=size;block->link.previous=NULL;block->link.next=vm->allocations;
    if(vm->allocations) vm->allocations->link.previous=block;
    vm->allocations=block;vm->used+=sizeof(*block)+size;
    return block+1;
}
void *ps5_linux_calloc(size_t count,size_t size)
{
    if(size && count>SIZE_MAX/size) fail(PS5_LINUX_MEMORY);
    size_t total=count*size;void *pointer=ps5_linux_malloc(total);memset(pointer,0,total);return pointer;
}
void ps5_linux_free(void *pointer)
{
    if(!pointer) return;
    Allocation *block=(Allocation *)pointer-1;
    if(block->link.previous) block->link.previous->link.next=block->link.next;
    else active->allocations=block->link.next;
    if(block->link.next) block->link.next->link.previous=block->link.previous;
    active->used-=sizeof(*block)+block->link.size;
    ps5_guest_buffer_free(block,sizeof(*block)+block->link.size);
}
void *ps5_linux_realloc(void *pointer,size_t size)
{
    if(!pointer) return ps5_linux_malloc(size);
    if(!size) { ps5_linux_free(pointer);return NULL; }
    Allocation *old=(Allocation *)pointer-1;
    void *replacement=ps5_linux_malloc(size);
    memcpy(replacement,pointer,size<old->link.size ? size:old->link.size);
    ps5_linux_free(pointer);return replacement;
}
char *ps5_linux_strdup(const char *text)
{
    size_t size=strlen(text)+1;char *copy=ps5_linux_malloc(size);memcpy(copy,text,size);return copy;
}
_Noreturn void ps5_linux_exit(int status)
{
    if(!status && active) { active->state=PS5_LINUX_STOPPED;fail(PS5_LINUX_OK); }
    fail(PS5_LINUX_ENGINE);
}
_Noreturn void ps5_linux_abort(void) { fail(PS5_LINUX_ENGINE); }
_Noreturn void ps5_linux_assert(void) { fail(PS5_LINUX_ENGINE); }
/* Debug prints never touch the app's global stdout/stderr. Guest console bytes
 * use CharacterDevice below. A production diagnostic sink is a separate layer. */
int ps5_linux_printf(const char *format,...) { (void)format;return 0; }
void vm_error(const char *format,...) { (void)format;fail(PS5_LINUX_ENGINE); }
static void discard(Ps5Linux *vm)
{
    vm->network_state=0;vm->network_pending=0;
    memset(&vm->network,0,sizeof(vm->network));
    while(vm->allocations) {
        Allocation *block=vm->allocations;vm->allocations=block->link.next;ps5_guest_buffer_free(block,sizeof(*block)+block->link.size);
    }
    vm->used=0;vm->machine=NULL;vm->disk=NULL;vm->disk_size=0;
}
typedef void Operation(Ps5Linux *vm);
static int invoke(Ps5Linux *vm,Operation *operation)
{
    if(atomic_flag_test_and_set_explicit(&engine_entered,memory_order_acquire)) {
        vm->error=PS5_LINUX_BUSY;return 0;
    }
    active=vm;
    int success=0;
    if(SAVE(vm)) {
        if(vm->error!=PS5_LINUX_OK) vm->state=PS5_LINUX_FAILED;
        discard(vm);
    } else { operation(vm);success=1; }
    active=NULL;atomic_flag_clear_explicit(&engine_entered,memory_order_release);
    return success;
}
static void console_output(void *opaque,const uint8_t *bytes,int count)
{
    Ps5Linux *vm=opaque;
    if(count<0 || (size_t)count>PS5_LINUX_OUTPUT_CAPACITY-vm->output_size) fail(PS5_LINUX_OUTPUT_FULL);
    for(int i=0;i<count;i++) vm->output[(vm->output_head+vm->output_size++)%PS5_LINUX_OUTPUT_CAPACITY]=bytes[i];
}
static void agent_output(void *opaque,const uint8_t *bytes,int count)
{
    Ps5Linux *vm=opaque;
    if(count<0 || (size_t)count>PS5_LINUX_OUTPUT_CAPACITY-vm->agent_output_size) fail(PS5_LINUX_OUTPUT_FULL);
    for(int i=0;i<count;i++) vm->agent_output[(vm->agent_output_head+vm->agent_output_size++)%PS5_LINUX_OUTPUT_CAPACITY]=bytes[i];
}
static int agent_input(void *opaque,uint8_t *bytes,int size) { (void)opaque;(void)bytes;(void)size;return 0; }
int ps5_linux_console_accept(void *device,int size)
{
    if(active && device==(void *)&active->agent_console)
        return size>=0 && (size_t)size<=PS5_LINUX_OUTPUT_CAPACITY-active->agent_output_size;

    if(size<0 || size>PS5_LINUX_OUTPUT_CAPACITY) fail(PS5_LINUX_OUTPUT_FULL);
    if((size_t)size>PS5_LINUX_OUTPUT_CAPACITY-active->output_size) {
        active->output_blocked=1;return 0;
    }
    return 1;
}
static int console_input(void *opaque,uint8_t *bytes,int size)
{ (void)opaque;(void)bytes;(void)size;return 0; }
static int64_t disk_sectors(BlockDevice *block)
{ Ps5Linux *vm=block->opaque;return (int64_t)(vm->disk_size/512); }
static int disk_range(Ps5Linux *vm,uint64_t sector,int count)
{ return count>=0 && sector<=vm->disk_size/512 && (uint64_t)count<=vm->disk_size/512-sector; }
static int disk_read(BlockDevice *block,uint64_t sector,uint8_t *bytes,int count,BlockDeviceCompletionFunc *cb,void *opaque)
{
    (void)cb;(void)opaque;Ps5Linux *vm=block->opaque;
    if(!disk_range(vm,sector,count)) return -1;
    memcpy(bytes,vm->disk+(size_t)sector*512,(size_t)count*512);return 0;
}
static int disk_write(BlockDevice *block,uint64_t sector,const uint8_t *bytes,int count,BlockDeviceCompletionFunc *cb,void *opaque)
{
    (void)cb;(void)opaque;Ps5Linux *vm=block->opaque;
    if(!disk_range(vm,sector,count)) return -1;
    memcpy(vm->disk+(size_t)sector*512,bytes,(size_t)count*512);return 0;
}
static int64_t data_sectors(BlockDevice *block)
{ Ps5Linux *vm=block->opaque;return (int64_t)vm->data_disk.sectors; }
static int data_range(Ps5Linux *vm,uint64_t sector,int count)
{ return count>=0 && sector<=vm->data_disk.sectors && (uint64_t)count<=vm->data_disk.sectors-sector; }
static int data_read(BlockDevice *block,uint64_t sector,uint8_t *bytes,int count,BlockDeviceCompletionFunc *cb,void *opaque)
{
    (void)cb;(void)opaque;Ps5Linux *vm=block->opaque;
    if(!data_range(vm,sector,count)) return -1;
    if(!count) return 0;
    return vm->data_disk.read(vm->data_disk.context,sector,bytes,(size_t)count)==0 ? 0:-1;
}
static int data_write(BlockDevice *block,uint64_t sector,const uint8_t *bytes,int count,BlockDeviceCompletionFunc *cb,void *opaque)
{
    (void)cb;(void)opaque;Ps5Linux *vm=block->opaque;
    if(!data_range(vm,sector,count)) return -1;
    if(!count) return 0;
    vm->data_writes++;
    return vm->data_disk.write(vm->data_disk.context,sector,bytes,(size_t)count)==0 ? 0:-1;
}
static void network_failed(Ps5Linux *vm)
{
    vm->network_state=-1;vm->network_pending=0;
    if(vm->ethernet.device_set_carrier)vm->ethernet.device_set_carrier(&vm->ethernet,FALSE);
}
static void network_write(EthernetDevice *device,const uint8_t *bytes,int size)
{
    Ps5Linux *vm=device->opaque;
    if(vm->network_state!=1)return;
    if(size<14 || size>PS5_LINUX_ETHERNET_FRAME || vm->network.write(vm->network.context,bytes,(size_t)size))network_failed(vm);
}
static void network_step(Ps5Linux *vm)
{
    if(vm->network_state!=1)return;
    if(vm->network.poll(vm->network.context)){network_failed(vm);return;}
    /* One held frame plus bounded backend queues. Never consume an entire
     * busy network in one interpreter batch or lose a frame to guest pressure. */
    for(unsigned budget=0;budget<16;budget++) {
        if(!vm->network_pending) {
            int n=vm->network.read(vm->network.context,vm->network_packet,sizeof(vm->network_packet));
            if(!n)return;
            if(n<14 || n>PS5_LINUX_ETHERNET_FRAME){network_failed(vm);return;}
            vm->network_pending=(size_t)n;
        }
        if(!vm->ethernet.device_can_write_packet(&vm->ethernet))return;
        vm->ethernet.device_write_packet(&vm->ethernet,vm->network_packet,(int)vm->network_pending);
        vm->network_pending=0;
    }
}
static void construct(Ps5Linux *vm)
{
    const Ps5LinuxImages *images=vm->pending;
    vm->disk=ps5_linux_malloc(images->disk_size);vm->disk_size=images->disk_size;
    memcpy(vm->disk,images->disk,images->disk_size);
    vm->console=(CharacterDevice){vm,console_output,console_input};
    vm->agent_console=(CharacterDevice){vm,agent_output,agent_input};
    vm->block=(BlockDevice){disk_sectors,disk_read,disk_write,vm};
    VirtMachineParams params={0};
    params.vmc=&riscv_machine_class;params.machine_name="riscv64";
    params.ram_size=128*1024*1024;params.console=&vm->console;params.rtc_real_time=TRUE;
    if(images->agent) params.agent_console=&vm->agent_console;
    params.drive_count=1;params.tab_drive[0].block_dev=&vm->block;
    vm->data_disk=images->data_disk;
    if(vm->data_disk.sectors) {
        vm->data_block=(BlockDevice){data_sectors,data_read,data_write,vm};
        params.drive_count=2;params.tab_drive[1].block_dev=&vm->data_block;
    }
    if(images->shared_folder.context) {
        params.fs_count=1;params.tab_fs[0].tag="ps5term";
        params.tab_fs[0].fs_dev=share9p_init(&images->shared_folder);
    }
    vm->network=images->network;
    if(vm->network.context) {
        memset(&vm->ethernet,0,sizeof(vm->ethernet));
        const uint8_t mac[6]={0x02,0x50,0x53,0x35,0x00,0x01};
        memcpy(vm->ethernet.mac_addr,mac,sizeof(mac));
        vm->ethernet.opaque=vm;vm->ethernet.write_packet=network_write;
        params.eth_count=1;params.tab_eth[0].net=&vm->ethernet;
        vm->network_state=1;
    }
    char command_line[96];
    int command_length=images->wall_clock>0
        ? snprintf(command_line,sizeof(command_line),"console=hvc0 root=/dev/vda rw quiet ps5term.epoch=%lld",(long long)images->wall_clock)
        : snprintf(command_line,sizeof(command_line),"console=hvc0 root=/dev/vda rw quiet");
    if(command_length<0 || (size_t)command_length>=sizeof(command_line)) fail(PS5_LINUX_ARGUMENT);
    params.cmdline=command_line;
    params.rng_seed=images->rng_seed;params.rng_seed_len=(int)images->rng_seed_size;
    params.files[VM_FILE_BIOS].buf=(uint8_t *)images->bios;params.files[VM_FILE_BIOS].len=(int)images->bios_size;
    params.files[VM_FILE_KERNEL].buf=(uint8_t *)images->kernel;params.files[VM_FILE_KERNEL].len=(int)images->kernel_size;
    vm->machine=riscv_machine_class.virt_machine_init(&params);
    if(!vm->machine) fail(PS5_LINUX_ENGINE);
    /* Raising a resize IRQ before the Linux virtio driver has initialized can
     * wedge this reference kernel. Deliver it only once its input queue is ready. */
    vm->size_dirty=1;
    vm->state=PS5_LINUX_RUNNING;
}
Ps5Linux *ps5_linux_create(size_t limit)
{
    if(!limit || limit>PS5_LINUX_MAX_HEAP) return NULL;
    Ps5Linux *vm=calloc(1,sizeof(*vm));if(!vm) return NULL;
    vm->limit=limit;vm->allocations_left=SIZE_MAX;vm->columns=80;vm->rows=25;return vm;
}
int ps5_linux_start(Ps5Linux *vm,const Ps5LinuxImages *images)
{
    if(!vm || vm->state==PS5_LINUX_RUNNING) return 0;
    if(!images || !images->bios || !images->kernel || !images->disk ||
       !images->bios_size || images->bios_size>1024*1024 || !images->kernel_size || images->kernel_size>32*1024*1024 ||
       images->disk_size<512 || images->disk_size>32*1024*1024 || images->disk_size%512 ||
       (images->data_disk.sectors && (!images->data_disk.read || !images->data_disk.write ||
                                    images->data_disk.sectors>1024u*1024u*1024u/512u))) {
        vm->error=PS5_LINUX_ARGUMENT;return 0;
    }
    if(images->shared_folder.context && (!images->shared_folder.stat || !images->shared_folder.fstat ||
       !images->shared_folder.open || !images->shared_folder.close || !images->shared_folder.read ||
       !images->shared_folder.write || !images->shared_folder.resize || !images->shared_folder.chmod ||
       !images->shared_folder.times || !images->shared_folder.mkdir || !images->shared_folder.remove ||
       !images->shared_folder.rename || !images->shared_folder.readlink || !images->shared_folder.list ||
       !images->shared_folder.list_close || !images->shared_folder.space)) { vm->error=PS5_LINUX_ARGUMENT;return 0; }
    if(images->rng_seed_size>64 || (images->rng_seed_size && !images->rng_seed)) {
        vm->error=PS5_LINUX_ARGUMENT;
        return 0;
    }
    if(images->network.context && (!images->network.write || !images->network.poll || !images->network.read)) {
        vm->error=PS5_LINUX_ARGUMENT;return 0;
    }
    ps5_linux_stop(vm);vm->error=PS5_LINUX_OK;vm->pending=images;
    int result=invoke(vm,construct);vm->pending=NULL;return result;
}
void ps5_linux_stop(Ps5Linux *vm)
{
    if(!vm) return;
    discard(vm);vm->state=PS5_LINUX_STOPPED;
    vm->input_head=vm->input_size=vm->output_head=vm->output_size=0;vm->output_blocked=0;
    vm->agent_input_head=vm->agent_input_size=vm->agent_output_head=vm->agent_output_size=0;
}
void ps5_linux_destroy(Ps5Linux *vm) { if(vm) { ps5_linux_stop(vm);free(vm); } }
static void execute(Ps5Linux *vm)
{
    network_step(vm);
    /* This also raises timer IRQs to wake a CPU waiting in WFI. Merely calling
     * the interpreter leaves an idle guest asleep indefinitely. */
    (void)virt_machine_get_sleep_duration(vm->machine,0);
    VIRTIODevice *console=vm->machine->console_dev;
    VIRTIODevice *agent=vm->machine->agent_console_dev;
    if(agent) {
        virtio_console_flush_output(agent);
        if(vm->agent_input_size && virtio_console_can_write_data(agent)) {
            int available=virtio_console_get_write_len(agent);
            size_t count=available>0 ? (size_t)available:0;
            if(count>vm->agent_input_size) count=vm->agent_input_size;
            if(count>PS5_LINUX_INPUT_CAPACITY-vm->agent_input_head) count=PS5_LINUX_INPUT_CAPACITY-vm->agent_input_head;
            if(count) {
                int accepted=virtio_console_write_data(agent,vm->agent_input+vm->agent_input_head,(int)count);
                if(accepted>0) {
                    vm->agent_input_head=(vm->agent_input_head+(size_t)accepted)%PS5_LINUX_INPUT_CAPACITY;
                    vm->agent_input_size-=(size_t)accepted;
                }
            }
        }
    }
    vm->output_blocked=0;
    virtio_console_flush_output(console);
    if(vm->output_blocked) { vm->sleep_hint=0;return; }
    if(vm->size_dirty && virtio_console_can_write_data(console)) {
        virtio_console_resize_event(console,(int)vm->columns,(int)vm->rows);
        vm->size_dirty=0;
    }
    if(vm->input_size && virtio_console_can_write_data(console)) {
        int available=virtio_console_get_write_len(console);
        size_t count=available>0 ? (size_t)available:0;
        if(count>vm->input_size) count=vm->input_size;
        if(count>PS5_LINUX_INPUT_CAPACITY-vm->input_head) count=PS5_LINUX_INPUT_CAPACITY-vm->input_head;
        if(count) {
            int accepted=virtio_console_write_data(console,vm->input+vm->input_head,(int)count);
            if(accepted<0 || (size_t)accepted>count) fail(PS5_LINUX_ENGINE);
            vm->input_head=(vm->input_head+(size_t)accepted)%PS5_LINUX_INPUT_CAPACITY;vm->input_size-=(size_t)accepted;
        }
    }
    vm->machine->vmc->virt_machine_interp(vm->machine,(int)vm->cycles);
    int delay=virt_machine_get_sleep_duration(vm->machine,10);
    vm->sleep_hint=delay>0 ? (unsigned)delay:0;
    /* Queued input the guest can take now is work, not idleness. */
    if(vm->sleep_hint && vm->input_size && virtio_console_can_write_data(console)) vm->sleep_hint=0;
}
Ps5LinuxState ps5_linux_step(Ps5Linux *vm,unsigned cycles)
{
    if(!vm) return PS5_LINUX_STOPPED;
    if(vm->state!=PS5_LINUX_RUNNING || !cycles || cycles>50000) return vm->state;
    vm->cycles=cycles;invoke(vm,execute);return vm->state;
}
size_t ps5_linux_write(Ps5Linux *vm,const void *bytes,size_t size)
{
    if(!vm || vm->state!=PS5_LINUX_RUNNING || !bytes) return 0;
    if(size>PS5_LINUX_INPUT_CAPACITY-vm->input_size) size=PS5_LINUX_INPUT_CAPACITY-vm->input_size;
    for(size_t i=0;i<size;i++) vm->input[(vm->input_head+vm->input_size++)%PS5_LINUX_INPUT_CAPACITY]=((const uint8_t *)bytes)[i];
    return size;
}
void ps5_linux_discard_input(Ps5Linux *vm) { if(vm) vm->input_head=vm->input_size=0; }
unsigned long ps5_linux_data_writes(const Ps5Linux *vm) { return vm ? vm->data_writes:0; }
size_t ps5_linux_read(Ps5Linux *vm,void *bytes,size_t size)
{
    if(!vm || !bytes) return 0;
    if(size>vm->output_size) size=vm->output_size;
    for(size_t i=0;i<size;i++) ((uint8_t *)bytes)[i]=vm->output[(vm->output_head+i)%PS5_LINUX_OUTPUT_CAPACITY];
    vm->output_head=(vm->output_head+size)%PS5_LINUX_OUTPUT_CAPACITY;vm->output_size-=size;return size;
}
size_t ps5_linux_agent_write(Ps5Linux *vm,const void *bytes,size_t size)
{
    if(!vm || !bytes || vm->state!=PS5_LINUX_RUNNING) return 0;
    if(size>PS5_LINUX_INPUT_CAPACITY-vm->agent_input_size) size=PS5_LINUX_INPUT_CAPACITY-vm->agent_input_size;
    for(size_t i=0;i<size;i++) vm->agent_input[(vm->agent_input_head+vm->agent_input_size++)%PS5_LINUX_INPUT_CAPACITY]=((const uint8_t *)bytes)[i];
    return size;
}
size_t ps5_linux_agent_read(Ps5Linux *vm,void *bytes,size_t size)
{
    if(!vm || !bytes) return 0;
    if(size>vm->agent_output_size) size=vm->agent_output_size;
    for(size_t i=0;i<size;i++) ((uint8_t *)bytes)[i]=vm->agent_output[(vm->agent_output_head+i)%PS5_LINUX_OUTPUT_CAPACITY];
    vm->agent_output_head=(vm->agent_output_head+size)%PS5_LINUX_OUTPUT_CAPACITY;vm->agent_output_size-=size;
    return size;
}
static void resize(Ps5Linux *vm)
{
    if(virtio_console_can_write_data(vm->machine->console_dev)) {
        virtio_console_resize_event(vm->machine->console_dev,(int)vm->columns,(int)vm->rows);
        vm->size_dirty=0;
    }
}
int ps5_linux_resize(Ps5Linux *vm,unsigned columns,unsigned rows)
{
    if(!vm || !columns || !rows || columns>1000 || rows>1000) return 0;
    vm->columns=columns;vm->rows=rows;vm->size_dirty=1;
    return vm->state==PS5_LINUX_RUNNING ? invoke(vm,resize):1;
}
Ps5LinuxState ps5_linux_state(const Ps5Linux *vm) { return vm ? vm->state:PS5_LINUX_STOPPED; }
Ps5LinuxError ps5_linux_error(const Ps5Linux *vm) { return vm ? vm->error:PS5_LINUX_ARGUMENT; }
size_t ps5_linux_heap_used(const Ps5Linux *vm) { return vm ? vm->used:0; }
int ps5_linux_output_blocked(const Ps5Linux *vm) { return vm && vm->output_blocked; }
unsigned ps5_linux_sleep_hint(const Ps5Linux *vm) { return vm ? vm->sleep_hint:0; }
int ps5_linux_network_state(const Ps5Linux *vm) { return vm ? vm->network_state:0; }
#ifdef PS5TERM_SUBSYSTEM_TESTING
static void allocator_roundtrip(Ps5Linux *vm)
{
    (void)vm;
    unsigned char *p=ps5_linux_malloc(2*1024*1024);
    p[0]=19;p[2*1024*1024-1]=71;
    p=ps5_linux_realloc(p,3*1024*1024);
    if(p[0]!=19 || p[2*1024*1024-1]!=71) fail(PS5_LINUX_ENGINE);
    ps5_linux_free(p);
}
int ps5_linux_test_allocator(Ps5Linux *vm)
{
    if(!vm || vm->used) return 0;
    return invoke(vm,allocator_roundtrip) && !vm->used;
}
void ps5_linux_test_fail_after(Ps5Linux *vm,size_t count) { vm->allocations_left=count; }
#endif
