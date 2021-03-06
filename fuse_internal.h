/*
 * Copyright (C) 2006-2008 Google. All Rights Reserved.
 * Copyright (C) 2010 Tuxera. All Rights Reserved.
 * Copyright (C) 2011 Anatol Pomozov. All Rights Reserved.
 */


#ifndef _FUSE_INTERNAL_H_
#define _FUSE_INTERNAL_H_

#include "fuse.h"
#include "fuse_ipc.h"
#include "fuse_node.h"
#include "fuse_kernel.h"

#include <AvailabilityMacros.h>
#include <kern/clock.h>
#include <sys/types.h>
#include <sys/kauth.h>
#include <sys/kernel_types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/ubc.h>
#include <sys/uio.h>
#include <sys/ucred.h>
#include <sys/vnode.h>
#include <sys/xattr.h>
#include <stdbool.h>


struct fuse_attr;
struct fuse_data;
struct fuse_dispatcher;
struct fuse_filehandle;
struct fuse_iov;
struct fuse_ticket;

/* time */

#define fuse_timespec_add(vvp, uvp)            \
    do {                                       \
           (vvp)->tv_sec += (uvp)->tv_sec;     \
           (vvp)->tv_nsec += (uvp)->tv_nsec;   \
           if ((vvp)->tv_nsec >= 1000000000) { \
               (vvp)->tv_sec++;                \
               (vvp)->tv_nsec -= 1000000000;   \
           }                                   \
    } while (0)

#define fuse_timespec_cmp(tvp, uvp, cmp)       \
        (((tvp)->tv_sec == (uvp)->tv_sec) ?    \
         ((tvp)->tv_nsec cmp (uvp)->tv_nsec) : \
         ((tvp)->tv_sec cmp (uvp)->tv_sec))


#ifndef MAC_OS_X_VERSION_10_6
extern const char *vnode_getname(vnode_t vp);
extern void  vnode_putname(const char *name);
#endif


static __inline__
int
fuse_match_cred(kauth_cred_t daemoncred, kauth_cred_t requestcred)
{
#ifdef MAC_OS_X_VERSION_10_7
    // SDK 10.7 uses the new 'posix' credentials
    posix_cred_t daemonpcred = &(daemoncred->cr_posix);
    posix_cred_t requestpcred = &(requestcred->cr_posix);
#else
    kauth_cred_t daemonpcred = daemoncred;
    kauth_cred_t requestpcred = requestcred;
#endif

    if ((daemonpcred->cr_uid == requestpcred->cr_uid)             &&
        (daemonpcred->cr_groups[0] == requestpcred->cr_groups[0])) {

        // macfuse/fuse4x used to check following conditions as well.
        // but on Lion appeared a new service used for drag'n'drop (Locum?)
        // that works on behalf of user but has "real user" equals to root
        // (ruid==cr_rgid==0), if we reject Locum then drag'n'drop does not work in Finder.
        // (daemonpcred->cr_uid == requestpcred->cr_ruid)       &&
        // (daemonpcred->cr_groups[0] == requestpcred->cr_rgid) &&
        // (daemonpcred->cr_groups[0] == requestpcred->cr_svgid)

        return 0;
    }

    return EPERM;
}

static __inline__
int
fuse_vfs_context_issuser(vfs_context_t context)
{
    return (kauth_cred_getuid(vfs_context_ucred(context)) == 0);
}

static __inline__
int
fuse_isautocache_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_AUTO_CACHE);
}

static __inline__
bool
fuse_isdeadfs_mp(mount_t mp)
{
    struct fuse_data *data = fuse_get_mpdata(mp);
    return !data || data->dead;
}

static __inline__
bool
fuse_isdeadfs(vnode_t vp)
{
    return fuse_isdeadfs_mp(vnode_mount(vp));
}

static __inline__
int
fuse_isdirectio(vnode_t vp)
{
    /* Try global first. */
    if (fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_DIRECT_IO) {
        return 1;
    }

    return (VTOFUD(vp)->flag & FN_DIRECT_IO);
}

static __inline__
int
fuse_isdirectio_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_DIRECT_IO);
}

static __inline__
int
fuse_isnoattrcache(vnode_t vp)
{
    /* Try global first. */
    if (fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_NO_ATTRCACHE) {
        return 1;
    }

    return 0;
}

static __inline__
int
fuse_isnoattrcache_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_NO_ATTRCACHE);
}

static __inline__
int
fuse_isnoreadahead(vnode_t vp)
{
    /* Try global first. */
    if (fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_NO_READAHEAD) {
        return 1;
    }

    /* In our model, direct_io implies no readahead. */
    return fuse_isdirectio(vp);
}

static __inline__
int
fuse_isnosynconclose(vnode_t vp)
{
    if (fuse_isdirectio(vp)) {
        return 0;
    }

    return (fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_NO_SYNCONCLOSE);
}

static __inline__
int
fuse_isnosyncwrites_mp(mount_t mp)
{
    /* direct_io implies we won't have nosyncwrites. */
    if (fuse_isdirectio_mp(mp)) {
        return 0;
    }

    return (fuse_get_mpdata(mp)->dataflags & FSESS_NO_SYNCWRITES);
}

static __inline__
void
fuse_setnosyncwrites_mp(mount_t mp)
{
    vfs_clearflags(mp, MNT_SYNCHRONOUS);
    vfs_setflags(mp, MNT_ASYNC);
    fuse_get_mpdata(mp)->dataflags |= FSESS_NO_SYNCWRITES;
}

static __inline__
void
fuse_clearnosyncwrites_mp(mount_t mp)
{
    if (!vfs_issynchronous(mp)) {
        vfs_clearflags(mp, MNT_ASYNC);
        vfs_setflags(mp, MNT_SYNCHRONOUS);
        fuse_get_mpdata(mp)->dataflags &= ~FSESS_NO_SYNCWRITES;
    }
}

static __inline__
int
fuse_isnoubc(vnode_t vp)
{
    /* Try global first. */
    if (fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_NO_UBC) {
        return 1;
    }

    /* In our model, direct_io implies no UBC. */
    return fuse_isdirectio(vp);
}

static __inline__
int
fuse_isnoubc_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_NO_UBC);
}

static __inline__
int
fuse_isnegativevncache_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_NEGATIVE_VNCACHE);
}

static __inline__
int
fuse_isnovncache(vnode_t vp)
{
    /* Try global first. */
    if (fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_NO_VNCACHE) {
        return 1;
    }

    /* In our model, direct_io implies no vncache for this vnode. */
    return fuse_isdirectio(vp);
}

static __inline__
int
fuse_isnovncache_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_NO_VNCACHE);
}

static __inline__
int
fuse_isextendedsecurity(vnode_t vp)
{
    return (fuse_get_mpdata(vnode_mount(vp))->dataflags & \
            FSESS_EXTENDED_SECURITY);
}

static __inline__
int
fuse_isextendedsecurity_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_EXTENDED_SECURITY);
}

static __inline__
int
fuse_isdefaultpermissions(vnode_t vp)
{
    return (fuse_get_mpdata(vnode_mount(vp))->dataflags & \
            FSESS_DEFAULT_PERMISSIONS);
}

static __inline__
int
fuse_isdefaultpermissions_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_DEFAULT_PERMISSIONS);
}

static __inline__
int
fuse_isdeferpermissions(vnode_t vp)
{
    return (fuse_get_mpdata(vnode_mount(vp))->dataflags & \
            FSESS_DEFER_PERMISSIONS);
}

static __inline__
int
fuse_isdeferpermissions_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_DEFER_PERMISSIONS);
}

static __inline__
int
fuse_isxtimes(vnode_t vp)
{
    return (fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_XTIMES);
}

static __inline__
int
fuse_isxtimes_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_XTIMES);
}

static __inline__
int
fuse_issparse_mp(mount_t mp)
{
    return (fuse_get_mpdata(mp)->dataflags & FSESS_SPARSE);
}

static __inline__
uint32_t
fuse_round_powerof2(uint32_t size)
{
    uint32_t result = 512;
    size = size & 0x7FFFFFFFU; /* clip at 2G */

    while (result < size) {
        result <<= 1;
    }

    return result;
}

static __inline__
uint32_t
fuse_round_size(uint32_t size, uint32_t b_min, uint32_t b_max)
{
    uint32_t candidate = fuse_round_powerof2(size);

    /* We assume that b_min and b_max will already be powers of 2. */

    if (candidate < b_min) {
        candidate = b_min;
    }

    if (candidate > b_max) {
        candidate = b_max;
    }

    return candidate;
}

static __inline__
int
fuse_skip_apple_double_mp(mount_t mp, char *nameptr, long namelen)
{
#define DS_STORE ".DS_Store"
    int ismpoption = fuse_get_mpdata(mp)->dataflags & FSESS_NO_APPLEDOUBLE;

    if (ismpoption && nameptr) {
        /* This _will_ allow just "._", that is, a namelen of 2. */
        if (namelen > 2) {
            if ((namelen == ((sizeof(DS_STORE)/sizeof(char)) - 1)) &&
                (bcmp(nameptr, DS_STORE, sizeof(DS_STORE)) == 0)) {
                return 1;
            } else if (nameptr[0] == '.' && nameptr[1] == '_') {
                return 1;
            }
        }
    }
#undef DS_STORE

    return 0;
}

static __inline__
int
fuse_blanket_deny(vnode_t vp, vfs_context_t context)
{
    mount_t mp = vnode_mount(vp);
    struct fuse_data *data = fuse_get_mpdata(mp);
    int issuser = fuse_vfs_context_issuser(context);
    int isvroot = vnode_isvroot(vp);

    /* if allow_other is set */
    if (data->dataflags & FSESS_ALLOW_OTHER) {
        return 0;
    }

    /* if allow_root is set */
    if (issuser && (data->dataflags & FSESS_ALLOW_ROOT)) {
        return 0;
    }

    /* if this is the user who mounted the fs */
    if (fuse_match_cred(data->daemoncred, vfs_context_ucred(context)) == 0) {
        return 0;
    }

    if (!data->inited && isvroot && issuser) {
        return 0;
    }

    if (fuse_isdeadfs(vp) && isvroot) {
        return 0;
    }

    /* If kernel itself, allow. */
    if (vfs_context_pid(context) == 0) {
        return 0;
    }

    return 1;
}

#define CHECK_BLANKET_DENIAL(vp, context, err) \
    { \
        if (fuse_blanket_deny(vp, context)) { \
            return err; \
        } \
    }

/* access */

int
fuse_internal_access(vnode_t                   vp,
                     int                       action,
                     vfs_context_t             context);

/* attributes */

int
fuse_internal_loadxtimes(vnode_t vp, struct vnode_attr *out_vap,
                         vfs_context_t context);

int
fuse_internal_attr_vat2fsai(mount_t                 mp,
                            vnode_t                 vp,
                            struct vnode_attr      *vap,
                            struct fuse_setattr_in *fsai,
                            uint64_t               *newsize);

static __inline__
void
fuse_internal_attr_fat2vat(vnode_t            vp,
                           struct fuse_attr  *fat,
                           struct vnode_attr *vap)
{
    struct timespec t;
    mount_t mp = vnode_mount(vp);
    struct fuse_data *data = fuse_get_mpdata(mp);
    struct fuse_vnode_data *fvdat = VTOFUD(vp);

    VATTR_INIT(vap);

    VATTR_RETURN(vap, va_fsid, vfs_statfs(mp)->f_fsid.val[0]);
    VATTR_RETURN(vap, va_fileid, fat->ino);
    VATTR_RETURN(vap, va_linkid, fat->ino);

    /*
     * If we have asynchronous writes enabled, our local in-kernel size
     * takes precedence over what the daemon thinks.
     */
    /* ATTR_FUDGE_CASE */
    if (!vfs_issynchronous(mp)) {
        fat->size = fvdat->filesize;
    }
    VATTR_RETURN(vap, va_data_size, fat->size);

    /*
     * The kernel will compute the following for us if we leave them
     * untouched (and have sane values in statvfs):
     *
     * va_total_size
     * va_data_alloc
     * va_total_alloc
     */
    if (fuse_issparse_mp(mp)) {
        VATTR_RETURN(vap, va_data_alloc, fat->blocks * S_BLKSIZE);
    }

    t.tv_sec = (typeof(t.tv_sec))fat->atime; /* XXX: truncation */
    t.tv_nsec = fat->atimensec;
    VATTR_RETURN(vap, va_access_time, t);

    t.tv_sec = (typeof(t.tv_sec))fat->ctime; /* XXX: truncation */
    t.tv_nsec = fat->ctimensec;
    VATTR_RETURN(vap, va_change_time, t);

    t.tv_sec = (typeof(t.tv_sec))fat->mtime; /* XXX: truncation */
    t.tv_nsec = fat->mtimensec;
    VATTR_RETURN(vap, va_modify_time, t);

    t.tv_sec = (typeof(t.tv_sec))fat->crtime; /* XXX: truncation */
    t.tv_nsec = fat->crtimensec;
    VATTR_RETURN(vap, va_create_time, t);

    VATTR_RETURN(vap, va_mode, fat->mode & ~S_IFMT);
    VATTR_RETURN(vap, va_nlink, fat->nlink);
    VATTR_RETURN(vap, va_uid, fat->uid);
    VATTR_RETURN(vap, va_gid, fat->gid);
    VATTR_RETURN(vap, va_rdev, fat->rdev);

    VATTR_RETURN(vap, va_type, IFTOVT(fat->mode));

    VATTR_RETURN(vap, va_iosize, data->iosize);

    VATTR_RETURN(vap, va_flags, fat->flags);
}

static __inline__
void
fuse_internal_attr_loadvap(vnode_t vp, struct vnode_attr *out_vap,
                           vfs_context_t context)
{
    mount_t mp = vnode_mount(vp);
    struct vnode_attr *in_vap = VTOVA(vp);
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    int purged = 0;
    long hint = 0;

    if (in_vap == out_vap) {
        return;
    }

    VATTR_RETURN(out_vap, va_fsid, in_vap->va_fsid);

    VATTR_RETURN(out_vap, va_fileid, in_vap->va_fileid);
    VATTR_RETURN(out_vap, va_linkid, in_vap->va_linkid);
    VATTR_RETURN(out_vap, va_gen,
        (typeof(out_vap->va_gen))fvdat->generation); /* XXX: truncation */
    if (!vnode_isvroot(vp)) {
        /*
         * If we do return va_parentid for our root vnode, things get
         * a bit too interesting for the Finder.
         */
        VATTR_RETURN(out_vap, va_parentid, fvdat->parent_nodeid);
    }

    /*
     * If we have asynchronous writes enabled, our local in-kernel size
     * takes precedence over what the daemon thinks.
     */
    /* ATTR_FUDGE_CASE */
    if (!vfs_issynchronous(mp)) {
        /* Bring in_vap up to date if need be. */
        VATTR_RETURN(in_vap,  va_data_size, fvdat->filesize);
    } else {
        /* The size might have changed remotely. */
        if (fvdat->filesize != (off_t)in_vap->va_data_size) {
            hint |= NOTE_WRITE;
            /* Remote size overrides what we have. */
            (void)ubc_msync(vp, (off_t)0, fvdat->filesize, NULL,
                            UBC_PUSHALL | UBC_INVALIDATE | UBC_SYNC);
            purged = 1;
            if (fvdat->filesize > (off_t)in_vap->va_data_size) {
                hint |= NOTE_EXTEND;
            }
            fvdat->filesize = in_vap->va_data_size;
            ubc_setsize(vp, fvdat->filesize);
        }
    }
    VATTR_RETURN(out_vap, va_data_size, in_vap->va_data_size);

    if (fuse_issparse_mp(mp)) {
        VATTR_RETURN(out_vap, va_data_alloc, in_vap->va_data_alloc);
    }

    VATTR_RETURN(out_vap, va_mode, in_vap->va_mode);
    VATTR_RETURN(out_vap, va_nlink, in_vap->va_nlink);
    VATTR_RETURN(out_vap, va_uid, in_vap->va_uid);
    VATTR_RETURN(out_vap, va_gid, in_vap->va_gid);
    VATTR_RETURN(out_vap, va_rdev, in_vap->va_rdev);

    VATTR_RETURN(out_vap, va_type, in_vap->va_type);

    VATTR_RETURN(out_vap, va_iosize, in_vap->va_iosize);

    VATTR_RETURN(out_vap, va_flags, in_vap->va_flags);

    VATTR_RETURN(out_vap, va_access_time, in_vap->va_access_time);
    VATTR_RETURN(out_vap, va_change_time, in_vap->va_change_time);
    VATTR_RETURN(out_vap, va_modify_time, in_vap->va_modify_time);

    /*
     * When _DARWIN_FEATURE_64_BIT_INODE is not enabled, the User library
     * will set va_create_time to -1. In that case, we will have
     * to ask for it separately, if necessary.
     */
    if (in_vap->va_create_time.tv_sec != (int64_t)-1) {
        VATTR_RETURN(out_vap, va_create_time, in_vap->va_create_time);
    }

    if ((fvdat->modify_time.tv_sec != in_vap->va_modify_time.tv_sec) ||
        (fvdat->modify_time.tv_nsec != in_vap->va_modify_time.tv_nsec)) {
        fvdat->modify_time.tv_sec = in_vap->va_modify_time.tv_sec;
        fvdat->modify_time.tv_nsec = in_vap->va_modify_time.tv_nsec;
        hint |= NOTE_ATTRIB;
        if (fuse_isautocache_mp(mp) && !purged) {
            (void)ubc_msync(vp, (off_t)0, fvdat->filesize, NULL,
                            UBC_PUSHALL | UBC_INVALIDATE | UBC_SYNC);
        }
    }

    if (VATTR_IS_ACTIVE(out_vap, va_backup_time) ||
        (VATTR_IS_ACTIVE(out_vap, va_create_time) &&
         !VATTR_IS_SUPPORTED(out_vap, va_create_time))) {
        (void)fuse_internal_loadxtimes(vp, out_vap, context);
    }
}

#define cache_attrs(vp, fuse_out) do {                               \
    struct timespec uptsp_ ## __func__;                              \
                                                                     \
    /* XXX: truncation; user space sends us a 64-bit tv_sec */       \
    VTOFUD(vp)->attr_valid.tv_sec = (time_t)(fuse_out)->attr_valid;  \
    VTOFUD(vp)->attr_valid.tv_nsec = (fuse_out)->attr_valid_nsec;    \
    nanouptime(&uptsp_ ## __func__);                                 \
                                                                     \
    fuse_timespec_add(&VTOFUD(vp)->attr_valid, &uptsp_ ## __func__); \
                                                                     \
    fuse_internal_attr_fat2vat(vp, &(fuse_out)->attr, VTOVA(vp));    \
} while (0)

#ifdef FUSE4X_ENABLE_EXCHANGE

/* exchange */

int
fuse_internal_exchange(vnode_t       fvp,
                       const char   *fname,
                       size_t        flen,
                       vnode_t       tvp,
                       const char   *tname,
                       size_t        tlen,
                       int           options,
                       vfs_context_t context);

#endif /* FUSE4X_ENABLE_EXCHANGE */

/* fsync */

int
fuse_internal_fsync(vnode_t                 vp,
                    vfs_context_t           context,
                    struct fuse_filehandle *fufh,
                    void                   *param);

int
fuse_internal_fsync_callback(struct fuse_ticket *ticket, uio_t uio);


/* readdir */

int
fuse_internal_readdir(vnode_t                 vp,
                      uio_t                   uio,
                      vfs_context_t           context,
                      struct fuse_filehandle *fufh,
                      struct fuse_iov        *cookediov,
                      int                    *numdirent);

int
fuse_internal_readdir_processdata(vnode_t          vp,
                                  uio_t            uio,
                                  size_t           reqsize,
                                  void            *buf,
                                  size_t           bufsize,
                                  struct fuse_iov *cookediov,
                                  int             *numdirent);

/* remove */

int
fuse_internal_remove(vnode_t               dvp,
                     vnode_t               vp,
                     struct componentname *cnp,
                     enum fuse_opcode      op,
                     vfs_context_t         context);

/* rename */

int
fuse_internal_rename(vnode_t               fdvp,
                     vnode_t               fvp,
                     struct componentname *fcnp,
                     vnode_t               tdvp,
                     vnode_t               tvp,
                     struct componentname *tcnp,
                     vfs_context_t         context);


/* strategy */

int
fuse_internal_strategy(vnode_t vp, buf_t bp);

errno_t
fuse_internal_strategy_buf(struct vnop_strategy_args *ap);


/* entity creation */

static __inline__
int
fuse_internal_checkentry(struct fuse_entry_out *feo, enum vtype vtype)
{
    if (vtype != IFTOVT(feo->attr.mode)) {
        return EINVAL;
    }

    if (feo->nodeid == FUSE_NULL_ID) {
        return EINVAL;
    }

    if (feo->nodeid == FUSE_ROOT_ID) {
        return EINVAL;
    }

    return 0;
}

int
fuse_internal_newentry(vnode_t               dvp,
                       vnode_t              *vpp,
                       struct componentname *cnp,
                       enum fuse_opcode      op,
                       void                 *buf,
                       size_t                bufsize,
                       enum vtype            vtype,
                       vfs_context_t         context);

void
fuse_internal_newentry_makerequest(mount_t                 mp,
                                   uint64_t                dnid,
                                   struct componentname   *cnp,
                                   enum fuse_opcode        op,
                                   void                   *buf,
                                   size_t                  bufsize,
                                   struct fuse_dispatcher *dispatcher,
                                   vfs_context_t           context);

int
fuse_internal_newentry_core(vnode_t                 dvp,
                            vnode_t                *vpp,
                            struct componentname   *cnp,
                            enum vtype              vtyp,
                            struct fuse_dispatcher *dispatcher,
                            vfs_context_t           context);

/* entity destruction */

int
fuse_internal_forget_callback(struct fuse_ticket *ticket, uio_t uio);

void
fuse_internal_forget_send(mount_t                 mp,
                          vfs_context_t           context,
                          uint64_t                nodeid,
                          uint64_t                nlookup,
                          struct fuse_dispatcher *dispatcher);

void
fuse_internal_interrupt_send(struct fuse_ticket *ticket);

/* fuse start/stop */

int fuse_internal_init_callback(struct fuse_ticket *ticket, uio_t uio);
int fuse_send_init(struct fuse_data *data, vfs_context_t context);

/* other */

static __inline__
int
fuse_implemented(struct fuse_data *data, uint64_t which)
{
    return (int)!(data->noimplflags & which);
}

static __inline__
void
fuse_clear_implemented(struct fuse_data *data, uint64_t which)
{
    data->noimplflags |= which;
}

void
fuse_internal_print_vnodes(mount_t mp);

void
fuse_preflight_log(vnode_t vp, fufh_type_t fufh_type, int err, char *message);

#endif /* _FUSE_INTERNAL_H_ */
