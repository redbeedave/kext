/*
 * Copyright (C) 2006-2008 Google. All Rights Reserved.
 */


#include "fuse.h"
#include "fuse_file.h"
#include "fuse_internal.h"
#include "fuse_ipc.h"
#include "fuse_node.h"
#include "fuse_sysctl.h"

/*
 * Because of the vagaries of how a filehandle can be used, we try not to
 * be too smart in here (we try to be smart elsewhere). It is required that
 * you come in here only if you really do not have the said filehandle--else
 * we panic.
 *
 * This function should be called with fufh_mtx mutex locked.
 */
int
fuse_filehandle_get(vnode_t       vp,
                    vfs_context_t context,
                    fufh_type_t   fufh_type,
                    int           mode)
{
    struct fuse_dispatcher  fdi;
    struct fuse_open_in    *foi;
    struct fuse_open_out   *foo;
    struct fuse_filehandle *fufh;
    struct fuse_vnode_data *fvdat = VTOFUD(vp);

    int err    = 0;
    int oflags = 0;
    int op     = FUSE_OPEN;

    fuse_trace_printf("fuse_filehandle_get(vp=%p, fufh_type=%d, mode=%x)\n",
                      vp, fufh_type, mode);

    fufh = &(fvdat->fufh[fufh_type]);

    if (FUFH_IS_VALID(fufh)) {
        panic("fuse4x: filehandle_get called despite valid fufh (type=%d)",
              fufh_type);
        /* NOTREACHED */
    }

    /*
     * Note that this means we are effectively FILTERING OUT open() flags.
     */
    oflags = fuse_filehandle_xlate_to_oflags(fufh_type);

    if (vnode_isdir(vp)) {
        op = FUSE_OPENDIR;
        if (fufh_type != FUFH_RDONLY) {
            log("fuse4x: non-rdonly fufh requested for directory\n");
            fufh_type = FUFH_RDONLY;
        }
    }

    fuse_dispatcher_init(&fdi, sizeof(*foi));
    fuse_dispatcher_make_vp(&fdi, op, vp, context);

    if (vnode_islnk(vp) && (mode & O_SYMLINK)) {
        oflags |= O_SYMLINK;
    }

    foi = fdi.indata;
    foi->flags = oflags;

    OSIncrementAtomic((SInt32 *)&fuse_fh_upcall_count);
    if ((err = fuse_dispatcher_wait_answer(&fdi))) {
        const char *vname = vnode_getname(vp);
        if (err == ENOENT) {
            /*
             * See comment in fuse_vnop_reclaim().
             */
            cache_purge(vp);
        }
        log("fuse4x: filehandle_get: failed for %s "
              "(type=%d, err=%d, caller=%p)\n",
              (vname) ? vname : "?", fufh_type, err,
               __builtin_return_address(0));
        if (vname) {
            vnode_putname(vname);
        }
        if (err == ENOENT) {
            fuse_vncache_purge(vp);
        }
        return err;
    }
    OSIncrementAtomic((SInt32 *)&fuse_fh_current);

    foo = fdi.answer;

    fufh->fh_id = foo->fh;
    fufh->open_count = 1;
    fufh->open_flags = oflags;
    fufh->fuse_open_flags = foo->open_flags;

    fuse_ticket_drop(fdi.ticket);

    return 0;
}

int
fuse_filehandle_put(vnode_t vp, vfs_context_t context, fufh_type_t fufh_type)
{
    struct fuse_dispatcher  fdi;
    struct fuse_release_in *fri;
    struct fuse_vnode_data *fvdat = VTOFUD(vp);
    struct fuse_filehandle *fufh  = NULL;

    fuse_trace_printf("fuse_filehandle_put(vp=%p, fufh_type=%d)\n",
                      vp, fufh_type);

    fufh = &(fvdat->fufh[fufh_type]);

    if (FUFH_IS_VALID(fufh)) {
        panic("fuse4x: filehandle_put called on a valid fufh (type=%d)",
              fufh_type);
        /* NOTREACHED */
    }

    if (fuse_isdeadfs(vp)) {
        goto out;
    }

    int op = vnode_isdir(vp) ? FUSE_RELEASEDIR : FUSE_RELEASE;
    fuse_dispatcher_init(&fdi, sizeof(*fri));
    fuse_dispatcher_make_vp(&fdi, op, vp, context);
    fri = fdi.indata;
    fri->fh = fufh->fh_id;
    fri->flags = fufh->open_flags;

    int err = fuse_dispatcher_wait_answer(&fdi);
    if (!err) {
        fuse_ticket_drop(fdi.ticket);
    }

out:
    OSDecrementAtomic((SInt32 *)&fuse_fh_current);
    fuse_invalidate_attr(vp);

    return err;
}
