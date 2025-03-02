/**
 * ntfsdir.c - devoptab directory routines for NTFS-based devices.
 *
 * Copyright (c) 2023 Extrems
 * Copyright (c) 2010 Dimok
 * Copyright (c) 2009 Rhys "Shareese" Koedijk
 * Copyright (c) 2006 Michael "Chishm" Chisholm
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "ntfsinternal.h"
#include "ntfsdir.h"
#include "device.h"
#include <sys/dir.h>

#define STATE(x)    ((ntfs_dir_state*)(x)->dirStruct)

void ntfsCloseDir (ntfs_dir_state *dir)
{
    // Sanity check
    if (!dir || !dir->vd)
        return;

    // Free the directory entries (if any)
    while (dir->first) {
        ntfs_dir_entry *next = dir->first->next;
        ntfs_free(dir->first->name);
        ntfs_free(dir->first);
        dir->first = next;
    }

    // Close the directory (if open)
    if (dir->ni)
        ntfsCloseEntry(dir->vd, dir->ni);

    // Reset the directory state
    dir->ni = NULL;
    dir->first = NULL;
    dir->current = NULL;

    return;
}

int ntfs_stat_r (struct _reent *r, const char *path, struct stat *st)
{
    ntfs_log_trace("path %s, st %p\n", path, st);

    ntfs_vd *vd = NULL;
    ntfs_inode *ni = NULL;

    // Get the volume descriptor for this path
    vd = ntfsGetVolume(path, true);
    if (!vd) {
        r->_errno = ENODEV;
        return -1;
    }

    // Short circuit cases were we don't actually have to do anything
    if (!st)
        return 0;

    // Lock
    ntfsLock(vd);

    // Find the entry
    ni = ntfsParseEntry(vd, path, 1);
    if (!ni) {
        r->_errno = errno;
        ntfsUnlock(vd);
        return -1;
    }

    // Get the entry stats
    int ret = ntfsStat(vd, ni, st);
    if (ret)
        r->_errno = errno;

    // Close the entry
    ntfsCloseEntry(vd, ni);

    // Unlock
    ntfsUnlock(vd);

    return 0;
}

int ntfs_lstat_r (struct _reent *r, const char *path, struct stat *st)
{
    ntfs_log_trace("path %s, st %p\n", path, st);

    ntfs_vd *vd = NULL;
    ntfs_inode *ni = NULL;

    // Get the volume descriptor for this path
    vd = ntfsGetVolume(path, true);
    if (!vd) {
        r->_errno = ENODEV;
        return -1;
    }

    // Short circuit cases were we don't actually have to do anything
    if (!st)
        return 0;

    // Lock
    ntfsLock(vd);

    // Find the entry
    ni = ntfsParseEntry(vd, path, 0);
    if (!ni) {
        r->_errno = errno;
        ntfsUnlock(vd);
        return -1;
    }

    // Get the entry stats
    int ret = ntfsStat(vd, ni, st);
    if (ret)
        r->_errno = errno;

    // Close the entry
    ntfsCloseEntry(vd, ni);

    // Unlock
    ntfsUnlock(vd);

    return 0;
}

int ntfs_symlink_r (struct _reent *r, const char *target, const char *linkpath)
{
    ntfs_log_trace("target %s, linkpath %s\n", target, linkpath);

    ntfs_vd *vd = NULL;
    ntfs_inode *ni = NULL;

    // Get the volume descriptor for this path
    vd = ntfsGetVolume(linkpath, true);
    if (!vd) {
        r->_errno = ENODEV;
        return -1;
    }

    // Lock
    ntfsLock(vd);

    // Create a symbolic link between the two paths
    ni = ntfsCreate(vd, linkpath, S_IFLNK, target);
    if (!ni) {
        ntfsUnlock(vd);
        r->_errno = errno;
        return -1;
    }

    // Close the symbolic link
    ntfsCloseEntry(vd, ni);

    // Unlock
    ntfsUnlock(vd);

    return 0;
}

int ntfs_unlink_r (struct _reent *r, const char *name)
{
    ntfs_log_trace("name %s\n", name);

    ntfs_vd *vd = NULL;

    // Get the volume descriptor for this path
    vd = ntfsGetVolume(name, true);
    if (!vd) {
        r->_errno = ENODEV;
        return -1;
    }

    // Unlink the entry
    int ret = ntfsUnlink(vd, name, S_IFLNK);
    if (ret)
        r->_errno = errno;

    return ret;
}

int ntfs_chdir_r (struct _reent *r, const char *name)
{
    ntfs_log_trace("name %s\n", name);

    ntfs_vd *vd = NULL;
    ntfs_inode *ni = NULL;

    // Get the volume descriptor for this path
    vd = ntfsGetVolume(name, true);
    if (!vd) {
        r->_errno = ENODEV;
        return -1;
    }

    // Lock
    ntfsLock(vd);

    // Find the directory
    ni = ntfsOpenEntry(vd, name);
    if (!ni) {
        ntfsUnlock(vd);
        r->_errno = ENOENT;
        return -1;
    }

    // Ensure that this directory is indeed a directory
    if (!(ni->mrec->flags && MFT_RECORD_IS_DIRECTORY)) {
        ntfsCloseEntry(vd, ni);
        ntfsUnlock(vd);
        r->_errno = ENOTDIR;
        return -1;
    }

    // Close the old current directory (if any)
    if (vd->cwd_ni)
        ntfsCloseEntry(vd, vd->cwd_ni);

    // Set the new current directory
    vd->cwd_ni = ni;

    // Unlock
    ntfsUnlock(vd);

    return 0;
}

int ntfs_rename_r (struct _reent *r, const char *oldName, const char *newName)
{
    ntfs_log_trace("oldName %s, newName %s\n", oldName, newName);

    ntfs_vd *vd = NULL;
    ntfs_inode *ni = NULL;

    // Get the volume descriptor for this path
    vd = ntfsGetVolume(oldName, true);
    if (!vd) {
        r->_errno = ENODEV;
        return -1;
    }

    // Lock
    ntfsLock(vd);

    // You cannot rename between devices
    if(vd != ntfsGetVolume(newName, true)) {
        ntfsUnlock(vd);
        r->_errno = EXDEV;
        return -1;
    }

    // Check that there is no existing entry with the new name
    ni = ntfsOpenEntry(vd, newName);
    if (ni) {
        ntfsCloseEntry(vd, ni);
        ntfsUnlock(vd);
        r->_errno = EEXIST;
        return -1;
    }

    // Link the old entry with the new one
    if (ntfsLink(vd, oldName, newName)) {
        ntfsUnlock(vd);
        return -1;
    }

    // Unlink the old entry
    if (ntfsUnlink(vd, oldName, 0)) {
        if (ntfsUnlink(vd, newName, 0)) {
            ntfsUnlock(vd);
            return -1;
        }
        ntfsUnlock(vd);
        return -1;
    }

    // Unlock
    ntfsUnlock(vd);

    return 0;
}

int ntfs_mkdir_r (struct _reent *r, const char *path, int mode)
{
    ntfs_log_trace("path %s, mode %i\n", path, mode);

    ntfs_vd *vd = NULL;
    ntfs_inode *ni = NULL;

    // Get the volume descriptor for this path
    vd = ntfsGetVolume(path, true);
    if (!vd) {
        r->_errno = ENODEV;
        return -1;
    }

    // Lock
    ntfsLock(vd);

    // Create the directory
    ni = ntfsCreate(vd, path, S_IFDIR, NULL);
    if (!ni) {
        ntfsUnlock(vd);
        r->_errno = errno;
        return -1;
    }

    // Close the directory
    ntfsCloseEntry(vd, ni);

    // Unlock
    ntfsUnlock(vd);

    return 0;
}

int ntfs_rmdir_r (struct _reent *r, const char *path)
{
    ntfs_log_trace("path %s\n", path);

    ntfs_vd *vd = NULL;

    // Get the volume descriptor for this path
    vd = ntfsGetVolume(path, true);
    if (!vd) {
        r->_errno = ENODEV;
        return -1;
    }

    // Unlink the entry
    int ret = ntfsUnlink(vd, path, S_IFDIR);
    if (ret)
        r->_errno = errno;

    return ret;
}

int ntfs_statvfs_r (struct _reent *r, const char *path, struct statvfs *buf)
{
    ntfs_log_trace("path %s, buf %p\n", path, buf);

    ntfs_vd *vd = NULL;
    s64 size;
    int delta_bits;

    // Get the volume descriptor for this path
    vd = ntfsGetVolume(path, true);
    if (!vd) {
        r->_errno = ENODEV;
        return -1;
    }

    // Short circuit cases were we don't actually have to do anything
    if (!buf)
        return 0;

    // Lock
    ntfsLock(vd);

    // Zero out the stat buffer
    memset(buf, 0, sizeof(struct statvfs));

    if (ntfs_volume_get_free_space(vd->vol)) {
        ntfsUnlock(vd);
        r->_errno = EIO;
        return -1;
    }

    // File system block size
    buf->f_bsize = vd->vol->cluster_size;

    // Fundamental file system block size
    buf->f_frsize = vd->vol->cluster_size;

    // Total number of blocks on file system in units of f_frsize
    buf->f_blocks = vd->vol->nr_clusters;

    // Free blocks available for all and for non-privileged processes
    size = MAX(vd->vol->free_clusters, 0);
    buf->f_bfree = buf->f_bavail = size;

    // Free inodes on the free space
    delta_bits = vd->vol->cluster_size_bits - vd->vol->mft_record_size_bits;
    if (delta_bits >= 0)
        size <<= delta_bits;
    else
        size >>= -delta_bits;

    // Number of inodes at this point in time
    buf->f_files = (vd->vol->mftbmp_na->allocated_size << 3) + size;

    // Free inodes available for all and for non-privileged processes
    size += vd->vol->free_mft_records;
    buf->f_ffree = buf->f_favail = MAX(size, 0);

    // File system id
    buf->f_fsid = vd->id;

    // Bit mask of f_flag values.
    buf->f_flag = (NVolReadOnly(vd->vol) ? ST_RDONLY : 0);

    // Maximum length of filenames
    buf->f_namemax = NTFS_MAX_NAME_LEN;

    // Unlock
    ntfsUnlock(vd);

    return 0;
}

/**
 * PRIVATE: Callback for directory walking
 */
int ntfs_readdir_filler (DIR_ITER *dirState, const ntfschar *name, const int name_len, const int name_type,
                         const s64 pos, const MFT_REF mref, const unsigned dt_type)
{
    ntfs_dir_state *dir = STATE(dirState);
    ntfs_dir_entry *entry = NULL;
    char *entry_name = NULL;

    // Sanity check
    if (!dir || !dir->vd) {
        errno = EINVAL;
        return -1;
    }

    // Ignore DOS file names
    if (name_type == FILE_NAME_DOS) {
        return 0;
    }

    // Convert the entry name to our current local
    if (ntfsUnicodeToLocal(name, name_len, &entry_name, 0) < 0) {
        return -1;
    }

    // Allocate a new directory entry
    entry = (ntfs_dir_entry *) ntfs_malloc(sizeof(ntfs_dir_entry));
    if (!entry) {
        ntfs_free(entry_name);
        return -1;
    }

    // Setup the entry
    entry->name = entry_name;
    entry->mref = mref;
    entry->type = dt_type;
    entry->next = NULL;

    // Link the entry to the directory
    if (!dir->first) {
        dir->first = entry;
    } else {
        ntfs_dir_entry *last = dir->first;
        while (last->next) last = last->next;
        last->next = entry;
    }

    return 0;
}

DIR_ITER *ntfs_diropen_r (struct _reent *r, DIR_ITER *dirState, const char *path)
{
    ntfs_log_trace("dirState %p, path %s\n", dirState, path);

    ntfs_dir_state* dir = STATE(dirState);
    s64 position = 0;

    // Get the volume descriptor for this path
    dir->vd = ntfsGetVolume(path, true);
    if (!dir->vd) {
        r->_errno = ENODEV;
        return NULL;
    }

    // Lock
    ntfsLock(dir->vd);

    // Find the directory
    dir->ni = ntfsOpenEntry(dir->vd, path);
    if (!dir->ni) {
        ntfsUnlock(dir->vd);
        r->_errno = ENOENT;
        return NULL;
    }

    // Ensure that this directory is indeed a directory
    if (!(dir->ni->mrec->flags && MFT_RECORD_IS_DIRECTORY)) {
        ntfsCloseEntry(dir->vd, dir->ni);
        ntfsUnlock(dir->vd);
        r->_errno = ENOTDIR;
        return NULL;
    }

    // Read the directory
    dir->first = dir->current = NULL;
    if (ntfs_readdir(dir->ni, &position, dirState, (ntfs_filldir_t)ntfs_readdir_filler)) {
        ntfsCloseDir(dir);
        ntfsUnlock(dir->vd);
        r->_errno = errno;
        return NULL;
    }

    // Move to the first entry in the directory
    dir->current = dir->first;

    // Update directory times
    ntfsUpdateTimes(dir->vd, dir->ni, NTFS_UPDATE_ATIME);

    // Insert the directory into the double-linked FILO list of open directories
    if (dir->vd->firstOpenDir) {
        dir->nextOpenDir = dir->vd->firstOpenDir;
        dir->vd->firstOpenDir->prevOpenDir = dir;
    } else {
        dir->nextOpenDir = NULL;
    }
    dir->prevOpenDir = NULL;
    dir->vd->firstOpenDir = dir;
    dir->vd->openDirCount++;

    // Unlock
    ntfsUnlock(dir->vd);

    return dirState;
}

int ntfs_dirreset_r (struct _reent *r, DIR_ITER *dirState)
{
    ntfs_log_trace("dirState %p\n", dirState);

    ntfs_dir_state* dir = STATE(dirState);

    // Sanity check
    if (!dir || !dir->vd || !dir->ni) {
        r->_errno = EBADF;
        return -1;
    }

    // Lock
    ntfsLock(dir->vd);

    // Move to the first entry in the directory
    dir->current = dir->first;

    // Update directory times
    ntfsUpdateTimes(dir->vd, dir->ni, NTFS_UPDATE_ATIME);

    // Unlock
    ntfsUnlock(dir->vd);

    return 0;
}

int ntfs_dirnext_r (struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    ntfs_log_trace("dirState %p, filename %p, filestat %p\n", dirState, filename, filestat);

    ntfs_dir_state* dir = STATE(dirState);

    // Sanity check
    if (!dir || !dir->vd || !dir->ni) {
        r->_errno = EBADF;
        return -1;
    }

    // Lock
    ntfsLock(dir->vd);

    // Check that there is a entry waiting to be fetched
    if (!dir->current) {
        ntfsUnlock(dir->vd);
        r->_errno = ENOENT;
        return -1;
    }

    // Fetch the current entry
    strncpy(filename, dir->current->name, NAME_MAX + 1);
    if(filestat != NULL) {
        memset(filestat, 0, sizeof(struct stat));
        filestat->st_ino = MREF(dir->current->mref);
        switch (dir->current->type) {
            case NTFS_DT_DIR:
                filestat->st_mode = S_IFDIR | (0777 & ~dir->vd->dmask);
                break;
            case NTFS_DT_LNK:
                filestat->st_mode = S_IFLNK | 0777;
                break;
            case NTFS_DT_FIFO:
                filestat->st_mode = S_IFIFO;
                break;
            case NTFS_DT_SOCK:
                filestat->st_mode = S_IFSOCK;
                break;
            case NTFS_DT_BLK:
                filestat->st_mode = S_IFBLK;
                break;
            case NTFS_DT_CHR:
                filestat->st_mode = S_IFCHR;
                break;
            case NTFS_DT_REPARSE:
                filestat->st_mode = S_IFLNK | 0777;
                break;
            default:
                filestat->st_mode = S_IFREG | (0777 & ~dir->vd->fmask);
                break;
        }
    }

    // Move to the next entry in the directory
    dir->current = dir->current->next;

    // Update directory times
    ntfsUpdateTimes(dir->vd, dir->ni, NTFS_UPDATE_ATIME);

    // Unlock
    ntfsUnlock(dir->vd);

    return 0;
}

int ntfs_dirclose_r (struct _reent *r, DIR_ITER *dirState)
{
    ntfs_log_trace("dirState %p\n", dirState);

    ntfs_dir_state* dir = STATE(dirState);

    // Sanity check
    if (!dir || !dir->vd) {
        r->_errno = EBADF;
        return -1;
    }

    // Lock
    ntfsLock(dir->vd);

    // Close the directory
    ntfsCloseDir(dir);

    // Remove the directory from the double-linked FILO list of open directories
    dir->vd->openDirCount--;
    if (dir->nextOpenDir)
        dir->nextOpenDir->prevOpenDir = dir->prevOpenDir;
    if (dir->prevOpenDir)
        dir->prevOpenDir->nextOpenDir = dir->nextOpenDir;
    else
        dir->vd->firstOpenDir = dir->nextOpenDir;

    // Unlock
    ntfsUnlock(dir->vd);

    return 0;
}
