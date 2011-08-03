/*
 * Copyright (C) 2006-2008 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE4X_H_
#define _FUSE4X_H_

#include <sys/param.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/proc.h>
#include <kern/thread_call.h>
#include <libkern/OSMalloc.h>
#include <libkern/locks.h>
#include <IOKit/IOLib.h>

#include <fuse_param.h>
#include <fuse_version.h>

// #define FUSE_COUNT_MEMORY  1
// #define FUSE_DEBUG         1
// #define FUSE_KDEBUG        1
// #define FUSE_KTRACE_OP     1
// #define FUSE_TRACE         1
// #define FUSE_TRACE_LK      1
// #define FUSE_TRACE_MSLEEP  1
// #define FUSE_TRACE_OP      1
// #define FUSE_TRACE_VNCACHE 1

// #define M_FUSE4X_ENABLE_LOCK_LOGGING 1

#define M_FUSE4X_ENABLE_FIFOFS            0
#define M_FUSE4X_ENABLE_INTERRUPT         1
#define M_FUSE4X_ENABLE_SPECFS            0
#define M_FUSE4X_ENABLE_TSLOCKING         1
#define M_FUSE4X_ENABLE_UNSUPPORTED       1
#define M_FUSE4X_ENABLE_XATTR             1

#if M_FUSE4X_ENABLE_UNSUPPORTED
#define M_FUSE4X_ENABLE_DSELECT         0
#define M_FUSE4X_ENABLE_EXCHANGE        1
#define M_FUSE4X_ENABLE_KQUEUE          1
#if __LP64__
#define M_FUSE4X_ENABLE_INTERIM_FSNODE_LOCK 1
#endif /* __LP64__ */
#endif /* M_FUSE4X_ENABLE_UNSUPPORTED */

#if M_FUSE4X_ENABLE_INTERIM_FSNODE_LOCK
#define M_FUSE4X_ENABLE_HUGE_LOCK 0
#define FUSE_VNOP_EXPORT __private_extern__
#else
#define FUSE_VNOP_EXPORT static
#endif /* M_FUSE4X_ENABLE_INTERIM_FSNODE_LOCK */


#if M_FUSE4X_ENABLE_LOCK_LOGGING
extern lck_mtx_t *fuse_log_lock;

// In case if tracing (lock,sleep,operations,..) enabled it produces a lot of log output.
// Because these logs are written from multiple threads they interference with each other.
// To make log more readable we need to searialize the output. It is done in log() function
// in case if M_FUSE4X_ENABLE_LOCK_LOGGING defined.
#define log(fmt, args...) \
    do { \
        lck_mtx_lock(fuse_log_lock); \
        IOLog(fmt, ##args); \
        lck_mtx_unlock(fuse_log_lock); \
    } while(0)

#else
#define log(fmt, args...) IOLog(fmt, ##args)
#endif /* M_FUSE4X_ENABLE_LOCK_LOGGING */


#define FUSE4X_TIMESTAMP __DATE__ ", " __TIME__


#define FUSEFS_SIGNATURE 0x55464553 // 'FUSE'

#ifdef FUSE_TRACE
#define fuse_trace_printf(fmt, ...) log(fmt, ## __VA_ARGS__)
#define fuse_trace_printf_func()    log("%s\n", __FUNCTION__)
#else
#define fuse_trace_printf(fmt, ...) {}
#define fuse_trace_printf_func()    {}
#endif

#ifdef FUSE_TRACE_OP
#define fuse_trace_printf_vfsop()     log("%s\n", __FUNCTION__)
#define fuse_trace_printf_vnop_novp() log("%s\n", __FUNCTION__)
#define fuse_trace_printf_vnop()      log("%s vp=%p\n", __FUNCTION__, vp)
#else
#define fuse_trace_printf_vfsop()     {}
#define fuse_trace_printf_vnop()      {}
#define fuse_trace_printf_vnop_novp() {}
#endif

#ifdef FUSE_TRACE_MSLEEP

static __inline__ int
fuse_msleep(void *chan, lck_mtx_t *mtx, int pri, const char *wmesg,
            struct timespec *ts)
{
    int ret;

    log("0: msleep(%p, %s)\n", (chan), (wmesg));
    ret = msleep(chan, mtx, pri, wmesg, ts);
    log("1: msleep(%p, %s)\n", (chan), (wmesg));

    return ret;
}
#define fuse_wakeup(chan)                        \
{                                                \
    log("1: wakeup(%p)\n", (chan));              \
    wakeup((chan));                              \
    log("0: wakeup(%p)\n", (chan));              \
}
#define fuse_wakeup_one(chan)                    \
{                                                \
    log("1: wakeup_one(%p)\n", (chan));          \
    wakeup_one((chan));                          \
    log("0: wakeup_one(%p)\n", (chan));          \
}
#else
#define fuse_msleep(chan, mtx, pri, wmesg, ts) \
    msleep((chan), (mtx), (pri), (wmesg), (ts))
#define fuse_wakeup(chan)     wakeup((chan))
#define fuse_wakeup_one(chan) wakeup_one((chan))
#endif

#ifdef FUSE_KTRACE_OP
#undef  fuse_trace_printf_vfsop
#undef  fuse_trace_printf_vnop
#define fuse_trace_printf_vfsop() kprintf("%s\n", __FUNCTION__)
#define fuse_trace_printf_vnop()  kprintf("%s\n", __FUNCTION__)
#endif

#ifdef FUSE_DEBUG
#define debug_printf(fmt, ...) \
  log("%s[%s:%d]: " fmt, __FUNCTION__, __FILE__, __LINE__, ## __VA_ARGS__)
#else
#define debug_printf(fmt, ...) {}
#endif

#ifdef FUSE_KDEBUG
#undef debug_printf
#define debug_printf(fmt, ...) \
  log("%s[%s:%d]: " fmt, __FUNCTION__, __FILE__, __LINE__, ## __VA_ARGS__);\
  kprintf("%s[%s:%d]: " fmt, __FUNCTION__, __FILE__, __LINE__, ## __VA_ARGS__)
#define kdebug_printf(fmt, ...) debug_printf(fmt, ## __VA_ARGS__)
#else
#define kdebug_printf(fmt, ...) {}
#endif

#define FUSE_ASSERT(a)                                                    \
    {                                                                     \
        if (!(a)) {                                                       \
            log("File "__FILE__", line %d: assertion ' %s ' failed.\n",   \
                  __LINE__, #a);                                          \
        }                                                                 \
    }

#define E_NONE 0

#define fuse_round_page_32(x) \
    (((uint32_t)(x) + 0x1000 - 1) & ~(0x1000 - 1))

#define FUSE_ZERO_SIZE 0x0000000000000000ULL
#define FUSE_ROOT_SIZE 0xFFFFFFFFFFFFFFFFULL

extern OSMallocTag fuse_malloc_tag;

#ifdef FUSE_COUNT_MEMORY

extern int32_t fuse_memory_allocated;

static __inline__
void *
FUSE_OSMalloc(size_t size, OSMallocTag tag)
{
    void *addr = OSMalloc((uint32_t)size, tag);

    if (!addr) {
        panic("fuse4x: memory allocation failed (size=%lu)", size);
    }

    OSAddAtomic((UInt32)size, (SInt32 *)&fuse_memory_allocated);

    return addr;
}

static __inline__
void
FUSE_OSFree(void *addr, size_t size, OSMallocTag tag)
{
    OSFree(addr, (uint32_t)size, tag);

    OSAddAtomic(-(UInt32)(size), (SInt32 *)&fuse_memory_allocated);
}

#else

#define FUSE_OSMalloc(size, tag)           OSMalloc((uint32_t)(size), (tag))
#define FUSE_OSFree(addr, size, tag)       OSFree((addr), (uint32_t)(size), (tag))

#endif /* FUSE_COUNT_MEMORY */

typedef enum fuse_op_waitfor {
    FUSE_OP_BACKGROUNDED = 0,
    FUSE_OP_FOREGROUNDED = 1,
} fuse_op_waitfor_t;

#endif /* _FUSE4X_H_ */
