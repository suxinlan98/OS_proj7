// SPDX-License-Identifier: BSD-2-Clause
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "vfs.h"

static struct {
  struct spinlock lock;
  struct sleeplock oplock;
  struct mount mount[NMOUNT];
} mtable;

// The default backend is the original xv6 filesystem.
static struct inode*
xv6_lookup(struct inode *dp, char *name)
{
  return dirlookup(dp, name, 0);
}

static int
xv6_link(struct inode *ip, struct inode *dp, char *name)
{
  ilock(ip);
  if(ip->type == T_DIR){
    iunlock(ip);
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlock(dp);
    ilock(ip);
    ip->nlink--;
    iupdate(ip);
    iunlock(ip);
    return -1;
  }
  iunlock(dp);

  return 0;
}

static struct inode_ops xv6_inode_ops = {
  .read = readi,
  .write = writei,
  .stat = stati,
  .trunc = itrunc,
  .lookup = xv6_lookup,
  .create = 0,
  .link = xv6_link,
};

static int
xv6_mount(struct mount *mnt)
{
  if(mnt == 0)
    return -1;
  mnt->root = 0;
  mnt->fs_private = 0;
  return 0;
}

static struct fs_ops xv6_fs_ops = {
  .mount = xv6_mount,
  .unmount = 0,
};

static enum fs_type
vfs_parse_fstype(char *fstype)
{
  if(strncmp(fstype, "xv6", 4) == 0)
    return FS_XV6;
  if(strncmp(fstype, "ext2", 5) == 0)
    return FS_EXT2;
  if(strncmp(fstype, "fat32", 6) == 0)
    return FS_FAT32;
  return FS_NONE;
}

static struct fs_ops*
vfs_fsops(enum fs_type fstype)
{
  switch(fstype){
  case FS_XV6:
    return &xv6_fs_ops;
  case FS_EXT2:
    return ext2_get_fs_ops();
  case FS_FAT32:
    return fat32_get_fs_ops();
  default:
    return 0;
  }
}

void
vfsinit(void)
{
  struct mount *root;

  initlock(&mtable.lock, "mtable");
  initsleeplock(&mtable.oplock, "mountop");

  root = &mtable.mount[0];
  root->state = MOUNTING;
  root->fstype = FS_XV6;
  root->dev = ROOTDEV;
  root->mountpoint = 0;
  root->root = 0;
  root->fsops = &xv6_fs_ops;
  root->fs_private = 0;

  if(root->fsops->mount(root) < 0)
    panic("vfsinit: xv6 mount");

  root->state = MOUNTED;
}

void
vfs_init_inode(struct inode *ip)
{
  ip->fstype = FS_XV6;
  ip->mnt = &mtable.mount[0];
  ip->ops = &xv6_inode_ops;
}

int
vfs_mount(char *fstype, char *path, int dev)
{
  enum fs_type type;
  struct fs_ops *ops;
  struct inode *mp, *parent;
  struct mount *mnt;
  char name[DIRSIZ];
  int free, i;

  if(fstype == 0 || path == 0 || dev <= 0)
    return -1;

  type = vfs_parse_fstype(fstype);
  ops = vfs_fsops(type);
  if(type == FS_NONE || ops == 0)
    return -1;

  // The xv6 root filesystem is mounted at boot and cannot be
  // mounted again.
  if(type == FS_XV6)
    return -1;

  if(path[0] == '/' && path[1] == '\0')
    return -1;

  // Serialize the complete path-resolution and publication sequence.
  // mtable.lock remains a short-held spinlock and never covers disk I/O.
  acquiresleep(&mtable.oplock);
  begin_op();

  if((parent = nameiparent(path, name)) == 0){
    end_op();
    releasesleep(&mtable.oplock);
    return -1;
  }

  // nameiparent() cannot provide the real parent for these aliases.
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0){
    iput(parent);
    end_op();
    releasesleep(&mtable.oplock);
    return -1;
  }

  // Resolve the final component in the parent we just found. Besides
  // avoiding a second path walk, this keeps parent and mp in one view.
  ilock(parent);
  mp = vfs_lookup(parent, name);
  iunlock(parent);
  if(mp == 0){
    iput(parent);
    end_op();
    releasesleep(&mtable.oplock);
    return -1;
  }

  // Keep mp locked until MOUNTING is visible. Unlink takes the same
  // ip->lock then mtable.lock order and will observe either outcome.
  ilock(mp);
  if((mp->dev == ROOTDEV && mp->inum == ROOTINO) ||
     mp->type != T_DIR || mp->nlink < 1){
    iunlockput(mp);
    iput(parent);
    end_op();
    releasesleep(&mtable.oplock);
    return -1;
  }

  free = -1;
  acquire(&mtable.lock);
  for(i = 1; i < NMOUNT; i++){
    mnt = &mtable.mount[i];
    if(mnt->state == MOUNT_FREE){
      if(free < 0)
        free = i;
      continue;
    }
    if(mnt->fstype == type ||
       (mnt->mountpoint != 0 &&
        mnt->mountpoint->dev == mp->dev &&
        mnt->mountpoint->inum == mp->inum) ||
       (mnt->state == MOUNTED && mnt->root != 0 &&
        mnt->root->dev == mp->dev &&
        mnt->root->inum == mp->inum)){
      free = -1;
      break;
    }
  }

  if(free < 0){
    release(&mtable.lock);
    iunlockput(mp);
    iput(parent);
    end_op();
    releasesleep(&mtable.oplock);
    return -1;
  }

  mnt = &mtable.mount[free];
  memset(mnt, 0, sizeof(*mnt));
  mnt->fstype = type;
  mnt->dev = dev;
  mnt->mountpoint = mp;
  mnt->parent = parent;
  mnt->root = 0;
  mnt->fsops = ops;
  mnt->fs_private = 0;
  mnt->state = MOUNTING;
  release(&mtable.lock);
  iunlock(mp);

  if(ops->mount(mnt) < 0){
    acquire(&mtable.lock);
    memset(mnt, 0, sizeof(*mnt));
    release(&mtable.lock);
    iput(mp);
    iput(parent);
    end_op();
    releasesleep(&mtable.oplock);
    return -1;
  }

  acquire(&mtable.lock);
  mnt->state = MOUNTED;
  release(&mtable.lock);

  end_op();
  releasesleep(&mtable.oplock);
  return 0;
}

int
vfs_umount(char *path)
{
  struct inode *mp = 0, *parent = 0;
  struct inode *root, *mountpoint, *mntparent;
  struct fs_ops *ops;
  struct mount *mnt = 0;
  char name[DIRSIZ];
  int i;

  if(path == 0 || (path[0] == '/' && path[1] == '\0'))
    return -1;

  acquiresleep(&mtable.oplock);
  begin_op();

  if((parent = nameiparent(path, name)) == 0)
    goto bad;
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  ilock(parent);
  mp = vfs_lookup(parent, name);
  iunlock(parent);
  if(mp == 0)
    goto bad;

  ilock(mp);
  acquire(&mtable.lock);
  for(i = 1; i < NMOUNT; i++){
    if(mtable.mount[i].state == MOUNTED &&
       mtable.mount[i].mountpoint == mp){
      mnt = &mtable.mount[i];
      break;
    }
  }
  if(mnt == 0 || mnt->readers != 0 || mnt->root == 0 || mnt->fsops == 0 ||
     mnt->fsops->unmount == 0){
    release(&mtable.lock);
    iunlock(mp);
    goto bad;
  }
  mnt->state = MOUNT_UNMOUNTING;
  release(&mtable.lock);

  // Once UNMOUNTING is visible, no new path can enter this mount.
  // Existing open files, cwd references and path walks make it busy.
  if(vfs_mount_busy(mnt)){
    acquire(&mtable.lock);
    mnt->state = MOUNTED;
    release(&mtable.lock);
    iunlock(mp);
    goto bad;
  }

  root = mnt->root;
  mountpoint = mnt->mountpoint;
  mntparent = mnt->parent;
  ops = mnt->fsops;

  iput(root);
  ops->unmount(mnt);

  acquire(&mtable.lock);
  memset(mnt, 0, sizeof(*mnt));
  release(&mtable.lock);
  iunlock(mp);

  // Drop the path walk's references and the mount table's references.
  iput(mp);
  iput(mountpoint);
  iput(parent);
  iput(mntparent);
  end_op();
  releasesleep(&mtable.oplock);
  return 0;

bad:
  if(mp != 0)
    iput(mp);
  if(parent != 0)
    iput(parent);
  end_op();
  releasesleep(&mtable.oplock);
  return -1;
}

// A MOUNTING entry is already busy: unlink must not invalidate its
// mount point while the backend is initialized. Mount calls are
// serialized separately by mtable.oplock.
struct mount*
vfs_mountpoint(struct inode *ip)
{
  struct mount *mnt;
  int i;

  if(ip == 0)
    return 0;

  acquire(&mtable.lock);
  for(i = 1; i < NMOUNT; i++){
    mnt = &mtable.mount[i];
    if(mnt->state != MOUNT_FREE &&
       mnt->mountpoint != 0 &&
       mnt->mountpoint->dev == ip->dev &&
       mnt->mountpoint->inum == ip->inum){
      release(&mtable.lock);
      return mnt;
    }
  }
  release(&mtable.lock);

  return 0;
}

// Return a referenced mounted root. MOUNTING entries remain hidden until
// their backend initialization has completed and the mount is published.
struct inode*
vfs_mounted_root(struct inode *ip)
{
  struct inode *root = 0;
  struct mount *mnt;
  int i;

  if(ip == 0)
    return 0;

  acquire(&mtable.lock);
  for(i = 1; i < NMOUNT; i++){
    mnt = &mtable.mount[i];
    if(mnt->state == MOUNTED &&
       mnt->mountpoint != 0 &&
       mnt->mountpoint->dev == ip->dev &&
       mnt->mountpoint->inum == ip->inum){
      root = mnt->root;
      mnt->readers++;
      break;
    }
  }
  release(&mtable.lock);

  if(root != 0){
    root = idup(root);
    acquire(&mtable.lock);
    mnt->readers--;
    release(&mtable.lock);
  }
  return root;
}

struct inode*
vfs_lookup(struct inode *dp, char *name)
{
  struct mount *mnt;
  struct inode *parent;
  int i;

  if(dp == 0 || dp->ops == 0 || dp->ops->lookup == 0)
    return 0;

  if(namecmp(name, "..") == 0){
    acquire(&mtable.lock);
    for(i = 1; i < NMOUNT; i++){
      mnt = &mtable.mount[i];
      if((mnt->state == MOUNTED || mnt->state == MOUNT_UNMOUNTING) &&
         mnt->root != 0 && mnt->parent != 0 &&
         mnt->root->dev == dp->dev && mnt->root->inum == dp->inum){
        parent = mnt->parent;
        mnt->readers++;
        release(&mtable.lock);
        parent = idup(parent);
        acquire(&mtable.lock);
        mnt->readers--;
        release(&mtable.lock);
        return parent;
      }
    }
    release(&mtable.lock);
  }

  return dp->ops->lookup(dp, name);
}

struct inode*
vfs_create(struct inode *dp, char *name, short type, short major, short minor)
{
  if(dp == 0 || dp->ops == 0 || dp->ops->create == 0)
    return 0;
  return dp->ops->create(dp, name, type, major, minor);
}

int
vfs_unlink(struct inode *dp, char *name)
{
  if(dp == 0 || dp->ops == 0 || dp->ops->unlink == 0)
    return -1;
  return dp->ops->unlink(dp, name);
}

int
vfs_link(struct inode *ip, struct inode *dp, char *name)
{
  if(ip == 0 || dp == 0 || ip->ops == 0 || ip->ops->link == 0)
    return -1;
  if(ip->fstype != dp->fstype || ip->dev != dp->dev)
    return -1;
  return ip->ops->link(ip, dp, name);
}

int
vfs_readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  if(ip == 0 || ip->ops == 0 || ip->ops->read == 0)
    return -1;
  return ip->ops->read(ip, user_dst, dst, off, n);
}

int
vfs_writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  if(ip == 0 || ip->ops == 0 || ip->ops->write == 0)
    return -1;
  return ip->ops->write(ip, user_src, src, off, n);
}

void
vfs_stati(struct inode *ip, struct stat *st)
{
  if(ip == 0 || ip->ops == 0 || ip->ops->stat == 0)
    return;
  ip->ops->stat(ip, st);
}

void
vfs_itrunc(struct inode *ip)
{
  if(ip == 0 || ip->ops == 0 || ip->ops->trunc == 0)
    return;
  ip->ops->trunc(ip);
}
