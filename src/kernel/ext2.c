// SPDX-License-Identifier: BSD-2-Clause
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"
#include "vfs.h"

#define EXT2_SUPER_OFFSET 1024
#define EXT2_SUPER_MAGIC  0xEF53
#define EXT2_ROOT_INO     2
#define EXT2_N_BLOCKS     15
#define EXT2_NAME_MAX     255

#define EXT2_S_IFMT       0xF000
#define EXT2_S_IFREG      0x8000
#define EXT2_S_IFDIR      0x4000

#define EXT2_FT_REG_FILE  1
#define EXT2_FT_DIR       2

struct ext2_super {
  uint s_inodes_count;
  uint s_blocks_count;
  uint s_r_blocks_count;
  uint s_free_blocks_count;
  uint s_free_inodes_count;
  uint s_first_data_block;
  uint s_log_block_size;
  int  s_log_frag_size;
  uint s_blocks_per_group;
  uint s_frags_per_group;
  uint s_inodes_per_group;
  uint s_mtime;
  uint s_wtime;
  ushort s_mnt_count;
  ushort s_max_mnt_count;
  ushort s_magic;
  ushort s_state;
  ushort s_errors;
  ushort s_minor_rev_level;
  uint s_lastcheck;
  uint s_checkinterval;
  uint s_creator_os;
  uint s_rev_level;
  ushort s_def_resuid;
  ushort s_def_resgid;
  uint s_first_ino;
  ushort s_inode_size;
  ushort s_block_group_nr;
} __attribute__((packed));

struct ext2_group_desc {
  uint bg_block_bitmap;
  uint bg_inode_bitmap;
  uint bg_inode_table;
  ushort bg_free_blocks_count;
  ushort bg_free_inodes_count;
} __attribute__((packed));

struct ext2_dinode {
  ushort i_mode;
  ushort i_uid;
  uint i_size;
  uint i_atime;
  uint i_ctime;
  uint i_mtime;
  uint i_dtime;
  ushort i_gid;
  ushort i_links_count;
  uint i_blocks;
  uint i_flags;
  uint i_osd1;
  uint i_block[EXT2_N_BLOCKS];
  uint i_generation;
  uint i_file_acl;
  uint i_dir_acl;
  uint i_faddr;
  uchar i_osd2[12];
} __attribute__((packed));

struct ext2_dirent {
  uint inode;
  ushort rec_len;
  uchar name_len;
  uchar file_type;
  char name[EXT2_NAME_MAX];
} __attribute__((packed));

struct ext2_fs {
  int used;
  uint dev;
  uint block_size;
  uint inode_size;
  struct ext2_super sb;
  struct ext2_group_desc gd;
  // Serializes allocator and free paths (bitmap + in-memory free
  // counters) across CPUs.  Only ever taken after inode locks and
  // before buffer locks.
  struct sleeplock lock;
};

static struct ext2_fs ext2fs;

static struct inode_ops ext2_inode_ops;
static struct fs_ops ext2_fs_ops;
static void ext2_itrunc(struct inode *ip);

static uint
ext2_min(uint a, uint b)
{
  return a < b ? a : b;
}

static uint
ext2_block_offset(struct ext2_fs *fs, uint blockno)
{
  return blockno * fs->block_size;
}

static uint
ext2_align4(uint n)
{
  return (n + 3) & ~3;
}

static int
ext2_namelen(char *name)
{
  int len;

  for(len = 0; len < DIRSIZ && name[len]; len++)
    ;
  return len;
}

static int
ext2_nameeq(char *a, char *b, int blen)
{
  int alen = ext2_namelen(a);

  if(alen != blen)
    return 0;
  return memcmp(a, b, alen) == 0;
}

static int
ext2_read_dinode(struct ext2_fs *fs, uint inum, struct ext2_dinode *di)
{
  uint index, off;

  if(inum == 0 || inum > fs->sb.s_inodes_count)
    return -1;

  index = inum - 1;
  off = ext2_block_offset(fs, fs->gd.bg_inode_table) + index * fs->inode_size;
  return block_read_bytes(fs->dev, off, (uchar*)di, sizeof(*di));
}

static int
ext2_write_dinode(struct ext2_fs *fs, uint inum, struct ext2_dinode *di)
{
  uint index, off;

  if(inum == 0 || inum > fs->sb.s_inodes_count)
    return -1;

  index = inum - 1;
  off = ext2_block_offset(fs, fs->gd.bg_inode_table) + index * fs->inode_size;
  return block_write_bytes(fs->dev, off, (uchar*)di, sizeof(*di));
}

static short
ext2_mode_to_type(ushort mode)
{
  if((mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
    return T_DIR;
  return T_FILE;
}

static int
ext2_supported_mode(ushort mode)
{
  mode &= EXT2_S_IFMT;
  return mode == EXT2_S_IFDIR || mode == EXT2_S_IFREG;
}

static ushort
ext2_type_to_mode(short type)
{
  if(type == T_DIR)
    return EXT2_S_IFDIR | 0755;
  return EXT2_S_IFREG | 0644;
}

static void
ext2_fill_inode(struct inode *ip, struct mount *mnt, struct ext2_dinode *di)
{
  int i;

  ip->fstype = FS_EXT2;
  ip->mnt = mnt;
  ip->ops = &ext2_inode_ops;
  ip->type = ext2_mode_to_type(di->i_mode);
  ip->major = 0;
  ip->minor = 0;
  ip->nlink = di->i_links_count;
  ip->size = di->i_size;
  for(i = 0; i < NDIRECT; i++)
    ip->addrs[i] = di->i_block[i];
  ip->addrs[NDIRECT] = di->i_block[12];
  ip->addrs[NDIRECT+1] = di->i_block[13];
  __atomic_store_n(&ip->valid, 1, __ATOMIC_RELEASE);
}

static struct inode*
ext2_iget(struct mount *mnt, uint inum)
{
  struct ext2_fs *fs = (struct ext2_fs*)mnt->fs_private;
  struct ext2_dinode di;
  struct inode *ip;

  ip = iget(mnt->dev, inum);
  // This reference prevents iput() from clearing valid. The acquire
  // fast path also avoids nesting inode locks during pathname lookup.
  if(__atomic_load_n(&ip->valid, __ATOMIC_ACQUIRE))
    return ip;

  acquiresleep(&ip->lock);
  if(ip->valid){
    releasesleep(&ip->lock);
    return ip;
  }

  // Only a cache miss touches the disk; refilling a live inode
  // would clobber in-memory state that is newer than the dinode.
  if(ext2_read_dinode(fs, inum, &di) < 0)
    goto bad;
  if(di.i_mode == 0)
    goto bad;
  if(!ext2_supported_mode(di.i_mode))
    goto bad;
  // Triple-indirect files are out of scope and refused.
  if(di.i_block[14])
    goto bad;

  ext2_fill_inode(ip, mnt, &di);
  releasesleep(&ip->lock);
  return ip;

bad:
  releasesleep(&ip->lock);
  iput(ip);
  return 0;
}

static int
ext2_sync_inode(struct inode *ip)
{
  struct ext2_fs *fs = (struct ext2_fs*)ip->mnt->fs_private;
  struct ext2_dinode di;
  struct buf *bp;
  uint *indirect;
  uint blocks;
  int i;

  if(ext2_read_dinode(fs, ip->inum, &di) < 0)
    return -1;

  di.i_mode = ext2_type_to_mode(ip->type);
  di.i_size = ip->size;
  di.i_links_count = ip->nlink;
  blocks = 0;
  for(i = 0; i < NDIRECT; i++){
    di.i_block[i] = ip->addrs[i];
    if(ip->addrs[i])
      blocks++;
  }
  di.i_block[12] = ip->addrs[NDIRECT];
  if(ip->addrs[NDIRECT]){
    blocks++;
    bp = bread(fs->dev, ip->addrs[NDIRECT]);
    indirect = (uint*)bp->data;
    for(i = 0; i < fs->block_size / sizeof(uint); i++)
      if(indirect[i])
        blocks++;
    brelse(bp);
  }
  di.i_block[13] = ip->addrs[NDIRECT+1];
  if(ip->addrs[NDIRECT+1]){
    blocks++;
    bp = bread(fs->dev, ip->addrs[NDIRECT+1]);
    indirect = (uint*)bp->data;
    for(i = 0; i < fs->block_size / sizeof(uint); i++){
      if(indirect[i]){
        struct buf *ibp;
        uint *inner;
        int j;

        blocks++;
        ibp = bread(fs->dev, indirect[i]);
        inner = (uint*)ibp->data;
        for(j = 0; j < fs->block_size / sizeof(uint); j++)
          if(inner[j])
            blocks++;
        brelse(ibp);
      }
    }
    brelse(bp);
  }
  di.i_blocks = blocks * (fs->block_size / 512);

  if(ext2_write_dinode(fs, ip->inum, &di) < 0)
    return -1;
  return 0;
}

static int
ext2_write_super(struct ext2_fs *fs)
{
  return block_write_bytes(fs->dev, EXT2_SUPER_OFFSET, (uchar*)&fs->sb,
                           sizeof(fs->sb));
}

static int
ext2_write_group(struct ext2_fs *fs)
{
  uint gd_block = fs->sb.s_first_data_block + 1;

  return block_write_bytes(fs->dev, ext2_block_offset(fs, gd_block),
                           (uchar*)&fs->gd, sizeof(fs->gd));
}

static int
ext2_test_bit(uchar *buf, uint bit)
{
  return (buf[bit / 8] & (1 << (bit % 8))) != 0;
}

static void
ext2_set_bit(uchar *buf, uint bit)
{
  buf[bit / 8] |= 1 << (bit % 8);
}

static void
ext2_clear_bit(uchar *buf, uint bit)
{
  buf[bit / 8] &= ~(1 << (bit % 8));
}

static uint
ext2_alloc_block(struct ext2_fs *fs)
{
  struct buf *bp, *dbp;
  uchar *bitmap;
  uint i, blockno, start, nblocks;

  acquiresleep(&fs->lock);
  bp = bread(fs->dev, fs->gd.bg_block_bitmap);
  bitmap = (uchar*)bp->data;

  // Never allocate beyond the last block that exists in the image.
  start = fs->sb.s_first_data_block;
  nblocks = ext2_min(fs->sb.s_blocks_per_group,
                     fs->sb.s_blocks_count - start);
  for(i = 0; i < nblocks; i++){
    if(!ext2_test_bit(bitmap, i)){
      ext2_set_bit(bitmap, i);
      blockno = start + i;
      bwrite(bp);
      brelse(bp);
      if(fs->sb.s_free_blocks_count)
        fs->sb.s_free_blocks_count--;
      if(fs->gd.bg_free_blocks_count)
        fs->gd.bg_free_blocks_count--;
      ext2_write_super(fs);
      ext2_write_group(fs);
      dbp = bread(fs->dev, blockno);
      memset(dbp->data, 0, BSIZE);
      bwrite(dbp);
      brelse(dbp);
      releasesleep(&fs->lock);
      return blockno;
    }
  }

  brelse(bp);
  releasesleep(&fs->lock);
  return 0;
}

static void
ext2_free_block(struct ext2_fs *fs, uint blockno)
{
  struct buf *bp;
  uchar *bitmap;
  uint bit;

  if(blockno == 0)
    return;
  acquiresleep(&fs->lock);
  bp = bread(fs->dev, fs->gd.bg_block_bitmap);
  bitmap = (uchar*)bp->data;
  bit = blockno - fs->sb.s_first_data_block;
  if(ext2_test_bit(bitmap, bit)){
    ext2_clear_bit(bitmap, bit);
    bwrite(bp);
    fs->sb.s_free_blocks_count++;
    fs->gd.bg_free_blocks_count++;
    ext2_write_super(fs);
    ext2_write_group(fs);
  }
  brelse(bp);
  releasesleep(&fs->lock);
}

static uint
ext2_alloc_inode(struct ext2_fs *fs)
{
  struct buf *bp;
  uchar *bitmap;
  uint i, inum, first, ninodes;

  acquiresleep(&fs->lock);
  bp = bread(fs->dev, fs->gd.bg_inode_bitmap);
  bitmap = (uchar*)bp->data;

  first = fs->sb.s_first_ino;
  if(first == 0)
    first = 11;

  // Never allocate beyond the last inode that exists in the image.
  ninodes = ext2_min(fs->sb.s_inodes_per_group, fs->sb.s_inodes_count);
  for(i = first - 1; i < ninodes; i++){
    if(!ext2_test_bit(bitmap, i)){
      ext2_set_bit(bitmap, i);
      inum = i + 1;
      bwrite(bp);
      brelse(bp);
      if(fs->sb.s_free_inodes_count)
        fs->sb.s_free_inodes_count--;
      if(fs->gd.bg_free_inodes_count)
        fs->gd.bg_free_inodes_count--;
      ext2_write_super(fs);
      ext2_write_group(fs);
      releasesleep(&fs->lock);
      return inum;
    }
  }

  brelse(bp);
  releasesleep(&fs->lock);
  return 0;
}

static void
ext2_free_inode(struct ext2_fs *fs, uint inum)
{
  struct buf *bp;
  uchar *bitmap;
  struct ext2_dinode di;
  uint bit;

  if(inum == 0)
    return;
  acquiresleep(&fs->lock);
  bp = bread(fs->dev, fs->gd.bg_inode_bitmap);
  bitmap = (uchar*)bp->data;
  bit = inum - 1;
  if(ext2_test_bit(bitmap, bit)){
    ext2_clear_bit(bitmap, bit);
    bwrite(bp);
    fs->sb.s_free_inodes_count++;
    fs->gd.bg_free_inodes_count++;
    ext2_write_super(fs);
    ext2_write_group(fs);
  }
  brelse(bp);

  memset(&di, 0, sizeof(di));
  ext2_write_dinode(fs, inum, &di);
  releasesleep(&fs->lock);
}

// Double-indirect branch of ext2_bmap: outer table in
// addrs[NDIRECT+1], up to (block_size/4) inner single-indirect tables.
static uint
ext2_bmap_double(struct inode *ip, uint bn, int alloc)
{
  struct ext2_fs *fs = (struct ext2_fs*)ip->mnt->fs_private;
  struct buf *bp;
  uint *indirect;
  uint nind, inner, blockno;

  nind = fs->block_size / sizeof(uint);
  if(bn >= nind * nind)
    return 0;

  if(ip->addrs[NDIRECT+1] == 0){
    if(!alloc)
      return 0;
    ip->addrs[NDIRECT+1] = ext2_alloc_block(fs);
    if(ip->addrs[NDIRECT+1] == 0)
      return 0;
  }

  bp = bread(fs->dev, ip->addrs[NDIRECT+1]);
  indirect = (uint*)bp->data;
  inner = indirect[bn / nind];
  if(inner == 0){
    if(!alloc){
      brelse(bp);
      return 0;
    }
    inner = ext2_alloc_block(fs);
    if(inner == 0){
      brelse(bp);
      return 0;
    }
    indirect[bn / nind] = inner;
    bwrite(bp);
  }
  brelse(bp);

  bp = bread(fs->dev, inner);
  indirect = (uint*)bp->data;
  blockno = indirect[bn % nind];
  if(blockno == 0 && alloc){
    blockno = ext2_alloc_block(fs);
    if(blockno == 0){
      brelse(bp);
      return 0;
    }
    indirect[bn % nind] = blockno;
    bwrite(bp);
  }
  brelse(bp);
  return blockno;
}

static uint
ext2_bmap(struct inode *ip, uint bn, int alloc)
{
  struct ext2_fs *fs = (struct ext2_fs*)ip->mnt->fs_private;
  struct buf *bp;
  uint *indirect;
  uint blockno;

  if(bn < NDIRECT){
    if(ip->addrs[bn] == 0 && alloc)
      ip->addrs[bn] = ext2_alloc_block(fs);
    return ip->addrs[bn];
  }

  bn -= NDIRECT;
  if(bn >= fs->block_size / sizeof(uint))
    return ext2_bmap_double(ip, bn - fs->block_size / sizeof(uint), alloc);

  if(ip->addrs[NDIRECT] == 0){
    if(!alloc)
      return 0;
    ip->addrs[NDIRECT] = ext2_alloc_block(fs);
    if(ip->addrs[NDIRECT] == 0)
      return 0;
  }

  bp = bread(fs->dev, ip->addrs[NDIRECT]);

  indirect = (uint*)bp->data;
  blockno = indirect[bn];
  if(blockno == 0 && alloc){
    blockno = ext2_alloc_block(fs);
    if(blockno == 0){
      brelse(bp);
      return 0;
    }
    indirect[bn] = blockno;
    bwrite(bp);
  }

  brelse(bp);
  return blockno;
}

static struct inode*
ext2_lookup(struct inode *dp, char *name)
{
  struct ext2_fs *fs = (struct ext2_fs*)dp->mnt->fs_private;
  uchar buf[BSIZE];
  struct ext2_dirent *de;
  uint off, bno, pos, alias;

  if(dp->type != T_DIR)
    return 0;

  // Path components are truncated to DIRSIZ characters by the namei
  // layer, so a query of exactly DIRSIZ chars may be the truncation of
  // a longer on-disk name.  Remember the first such alias candidate;
  // an exact short match always wins.
  alias = 0;
  for(off = 0; off < dp->size; off += fs->block_size){
    bno = ext2_bmap(dp, off / fs->block_size, 0);
    if(bno == 0)
      continue;
    if(block_read_bytes(fs->dev, ext2_block_offset(fs, bno),
                        buf, fs->block_size) < 0)
      return 0;
    for(pos = 0; pos < fs->block_size; ){
      de = (struct ext2_dirent*)(buf + pos);
      if(de->rec_len < 8 || pos + de->rec_len > fs->block_size)
        break;
      if(de->inode != 0){
        if(de->name_len <= DIRSIZ){
          if(ext2_nameeq(name, de->name, de->name_len))
            return ext2_iget(dp->mnt, de->inode);
        } else if(alias == 0 && ext2_namelen(name) == DIRSIZ &&
                  memcmp(name, de->name, DIRSIZ) == 0){
          alias = de->inode;
        }
      }
      pos += de->rec_len;
    }
  }

  if(alias)
    return ext2_iget(dp->mnt, alias);

  return 0;
}

static int
ext2_find_dirent(struct inode *dp, char *name, struct ext2_dirent *out,
                 uint *entry_off)
{
  struct ext2_fs *fs = (struct ext2_fs*)dp->mnt->fs_private;
  uchar buf[BSIZE];
  struct ext2_dirent *de;
  uint off, bno, pos;

  if(dp->type != T_DIR)
    return -1;

  for(off = 0; off < dp->size; off += fs->block_size){
    bno = ext2_bmap(dp, off / fs->block_size, 0);
    if(bno == 0)
      continue;
    if(block_read_bytes(fs->dev, ext2_block_offset(fs, bno),
                        buf, fs->block_size) < 0)
      return -1;
    for(pos = 0; pos < fs->block_size; ){
      de = (struct ext2_dirent*)(buf + pos);
      if(de->rec_len < 8 || pos + de->rec_len > fs->block_size)
        break;
      if(de->inode != 0 && de->name_len <= DIRSIZ &&
         ext2_nameeq(name, de->name, de->name_len)){
        if(out)
          *out = *de;
        if(entry_off)
          *entry_off = ext2_block_offset(fs, bno) + pos;
        return 0;
      }
      pos += de->rec_len;
    }
  }

  return -1;
}

static int
ext2_remove_dirent(struct ext2_fs *fs, uint entry_off)
{
  struct ext2_dirent de;

  if(block_read_bytes(fs->dev, entry_off, (uchar*)&de, 8) < 0)
    return -1;
  de.inode = 0;
  return block_write_bytes(fs->dev, entry_off, (uchar*)&de, 8) == 8 ? 0 : -1;
}

static int
ext2_is_dir_empty(struct inode *ip)
{
  struct ext2_fs *fs = (struct ext2_fs*)ip->mnt->fs_private;
  uchar buf[BSIZE];
  struct ext2_dirent *de;
  uint off, bno, pos;

  for(off = 0; off < ip->size; off += fs->block_size){
    bno = ext2_bmap(ip, off / fs->block_size, 0);
    if(bno == 0)
      continue;
    if(block_read_bytes(fs->dev, ext2_block_offset(fs, bno),
                        buf, fs->block_size) < 0)
      return 0;
    for(pos = 0; pos < fs->block_size; ){
      de = (struct ext2_dirent*)(buf + pos);
      if(de->rec_len < 8 || pos + de->rec_len > fs->block_size)
        break;
      if(de->inode != 0 &&
         !ext2_nameeq(".", de->name, de->name_len) &&
         !ext2_nameeq("..", de->name, de->name_len))
        return 0;
      pos += de->rec_len;
    }
  }

  return 1;
}

static int
ext2_dir_add(struct inode *dp, char *name, uint inum, uchar file_type)
{
  struct ext2_fs *fs = (struct ext2_fs*)dp->mnt->fs_private;
  struct buf *bp;
  struct ext2_dirent *de, *newde;
  uint off, bno, pos, namelen, need, actual;
  int bad;

  namelen = ext2_namelen(name);
  if(namelen == 0 || namelen > EXT2_NAME_MAX)
    return -1;
  need = ext2_align4(8 + namelen);

  for(off = 0; off < dp->size; off += fs->block_size){
    bno = ext2_bmap(dp, off / fs->block_size, 0);
    if(bno == 0)
      continue;
    bp = bread(fs->dev, bno);
    bad = 0;
    for(pos = 0; pos < fs->block_size; ){
      de = (struct ext2_dirent*)(bp->data + pos);
      if(de->rec_len < 8 || pos + de->rec_len > fs->block_size){
        bad = 1;
        break;
      }
      actual = ext2_align4(8 + de->name_len);
      if(actual > de->rec_len){
        bad = 1;
        break;
      }
      if(de->inode == 0 && de->rec_len >= need){
        de->inode = inum;
        de->name_len = namelen;
        de->file_type = file_type;
        memmove(de->name, name, namelen);
        bwrite(bp);
        brelse(bp);
        return 0;
      }
      if(de->rec_len >= actual + need){
        newde = (struct ext2_dirent*)(bp->data + pos + actual);
        newde->inode = inum;
        newde->rec_len = de->rec_len - actual;
        newde->name_len = namelen;
        newde->file_type = file_type;
        memmove(newde->name, name, namelen);
        de->rec_len = actual;
        bwrite(bp);
        brelse(bp);
        return 0;
      }
      pos += de->rec_len;
    }
    brelse(bp);
    if(bad)
      return -1;
  }

  bno = ext2_bmap(dp, dp->size / fs->block_size, 1);
  if(bno == 0)
    return -1;
  bp = bread(fs->dev, bno);
  memset(bp->data, 0, BSIZE);
  de = (struct ext2_dirent*)bp->data;
  de->inode = inum;
  de->rec_len = fs->block_size;
  de->name_len = namelen;
  de->file_type = file_type;
  memmove(de->name, name, namelen);
  bwrite(bp);
  brelse(bp);
  dp->size += fs->block_size;
  ext2_sync_inode(dp);
  return 0;
}

static int
ext2_init_dir(struct ext2_fs *fs, uint inum, uint parent, uint bno)
{
  struct buf *bp;
  struct ext2_dirent *de;
  uint dotlen;

  bp = bread(fs->dev, bno);
  memset(bp->data, 0, BSIZE);

  dotlen = ext2_align4(8 + 1);
  de = (struct ext2_dirent*)bp->data;
  de->inode = inum;
  de->rec_len = dotlen;
  de->name_len = 1;
  de->file_type = EXT2_FT_DIR;
  memmove(de->name, ".", 1);

  de = (struct ext2_dirent*)(bp->data + dotlen);
  de->inode = parent;
  de->rec_len = fs->block_size - dotlen;
  de->name_len = 2;
  de->file_type = EXT2_FT_DIR;
  memmove(de->name, "..", 2);

  bwrite(bp);
  brelse(bp);
  return 0;
}

static struct inode*
ext2_create(struct inode *dp, char *name, short type, short major, short minor)
{
  struct ext2_fs *fs = (struct ext2_fs*)dp->mnt->fs_private;
  struct ext2_dinode di;
  uint inum, bno = 0;

  (void)major;
  (void)minor;

  if(type != T_FILE && type != T_DIR)
    return 0;

  inum = ext2_alloc_inode(fs);
  if(inum == 0)
    return 0;

  memset(&di, 0, sizeof(di));
  di.i_mode = ext2_type_to_mode(type);
  di.i_links_count = type == T_DIR ? 2 : 1;

  if(type == T_DIR){
    bno = ext2_alloc_block(fs);
    if(bno == 0){
      ext2_free_inode(fs, inum);
      return 0;
    }
    if(ext2_init_dir(fs, inum, dp->inum, bno) < 0){
      ext2_free_block(fs, bno);
      ext2_free_inode(fs, inum);
      return 0;
    }
    di.i_size = fs->block_size;
    di.i_blocks = fs->block_size / 512;
    di.i_block[0] = bno;
  }

  if(ext2_write_dinode(fs, inum, &di) < 0){
    if(type == T_DIR)
      ext2_free_block(fs, bno);
    ext2_free_inode(fs, inum);
    return 0;
  }

  if(ext2_dir_add(dp, name, inum,
                  type == T_DIR ? EXT2_FT_DIR : EXT2_FT_REG_FILE) < 0){
    if(type == T_DIR)
      ext2_free_block(fs, bno);
    ext2_free_inode(fs, inum);
    return 0;
  }

  if(type == T_DIR){
    dp->nlink++;
    ext2_sync_inode(dp);
  }

  return ext2_iget(dp->mnt, inum);
}

static int
ext2_unlink(struct inode *dp, char *name)
{
  struct ext2_fs *fs = (struct ext2_fs*)dp->mnt->fs_private;
  struct ext2_dirent de;
  struct inode *ip;
  uint entry_off;

  if(ext2_find_dirent(dp, name, &de, &entry_off) < 0)
    return -1;

  ip = ext2_iget(dp->mnt, de.inode);
  if(ip == 0)
    return -1;

  ilock(ip);
  if(ip->type == T_DIR && !ext2_is_dir_empty(ip)){
    iunlockput(ip);
    return -1;
  }

  if(vfs_mountpoint(ip) != 0){
    // Cannot unlink a directory that is used as a mount point.
    iunlockput(ip);
    return -1;
  }

  if(ext2_remove_dirent(fs, entry_off) < 0){
    iunlockput(ip);
    return -1;
  }

  if(ip->type == T_DIR){
    dp->nlink--;
    ext2_sync_inode(dp);
    ip->nlink = 0;
  } else if(ip->nlink > 0){
    ip->nlink--;
  }

  // Only drop the link count here. Blocks and the inode itself are
  // released by iput() once the last open reference goes away.
  ext2_sync_inode(ip);
  iunlockput(ip);
  return 0;
}

static int
ext2_link(struct inode *ip, struct inode *dp, char *name)
{
  struct inode *old;

  if(ip->type != T_FILE || dp->type != T_DIR)
    return -1;

  ilock(ip);
  ip->nlink++;
  if(ext2_sync_inode(ip) < 0){
    ip->nlink--;
    iunlock(ip);
    return -1;
  }
  iunlock(ip);

  ilock(dp);
  if((old = ext2_lookup(dp, name)) != 0){
    iput(old);
    iunlock(dp);
    ilock(ip);
    ip->nlink--;
    ext2_sync_inode(ip);
    iunlock(ip);
    return -1;
  }
  if(ext2_dir_add(dp, name, ip->inum, EXT2_FT_REG_FILE) < 0){
    iunlock(dp);
    ilock(ip);
    ip->nlink--;
    ext2_sync_inode(ip);
    iunlock(ip);
    return -1;
  }
  iunlock(dp);

  return 0;
}

static int
ext2_readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  struct ext2_fs *fs = (struct ext2_fs*)ip->mnt->fs_private;
  struct dirent xde;
  uchar buf[BSIZE];
  struct ext2_dirent *de;
  uint tot, m, bno, want, seen, pos;

  if(ip->type == T_DIR){
    if(n < sizeof(xde) || off % sizeof(xde) != 0)
      return 0;
    want = off / sizeof(xde);
    seen = 0;
    for(off = 0; off < ip->size; off += fs->block_size){
      bno = ext2_bmap(ip, off / fs->block_size, 0);
      if(bno == 0)
        continue;
      if(block_read_bytes(fs->dev, ext2_block_offset(fs, bno),
                          buf, fs->block_size) < 0)
        return -1;
      for(pos = 0; pos < fs->block_size; ){
        de = (struct ext2_dirent*)(buf + pos);
        if(de->rec_len < 8 || pos + de->rec_len > fs->block_size)
          break;
        if(de->inode != 0){
          if(seen++ == want){
            memset(&xde, 0, sizeof(xde));
            xde.inum = de->inode;
            // Names longer than DIRSIZ are shown truncated; they can
            // still be looked up through the alias rule in ext2_lookup.
            memmove(xde.name, de->name, ext2_min(de->name_len, DIRSIZ));
            if(either_copyout(user_dst, dst, (char*)&xde, sizeof(xde)) < 0)
              return -1;
            return sizeof(xde);
          }
        }
        pos += de->rec_len;
      }
    }
    return 0;
  }

  if(off > ip->size || off + n < off)
    return 0;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot = 0; tot < n; tot += m, off += m, dst += m){
    bno = ext2_bmap(ip, off / fs->block_size, 0);
    m = ext2_min(n - tot, fs->block_size - off % fs->block_size);
    if(bno == 0){
      memset(buf, 0, fs->block_size);
    } else if(block_read_bytes(fs->dev, ext2_block_offset(fs, bno),
                               buf, fs->block_size) < 0){
      return -1;
    }
    if(either_copyout(user_dst, dst, buf + off % fs->block_size, m) < 0)
      return -1;
  }

  return tot;
}

static int
ext2_writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  struct ext2_fs *fs = (struct ext2_fs*)ip->mnt->fs_private;
  uchar buf[BSIZE];
  uint tot, m, bno;

  if(off + n < off)
    return -1;

  for(tot = 0; tot < n; tot += m, off += m, src += m){
    bno = ext2_bmap(ip, off / fs->block_size, 1);
    if(bno == 0)
      break;
    if(block_read_bytes(fs->dev, ext2_block_offset(fs, bno),
                        buf, fs->block_size) < 0)
      return -1;
    m = ext2_min(n - tot, fs->block_size - off % fs->block_size);
    if(either_copyin(buf + off % fs->block_size, user_src, src, m) < 0)
      return -1;
    if(block_write_bytes(fs->dev, ext2_block_offset(fs, bno),
                         buf, fs->block_size) < 0)
      return -1;
  }

  if(off > ip->size)
    ip->size = off;
  ext2_sync_inode(ip);

  return tot;
}

static void
ext2_stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

static void
ext2_itrunc(struct inode *ip)
{
  struct ext2_fs *fs = (struct ext2_fs*)ip->mnt->fs_private;
  struct buf *bp;
  uint *indirect;
  int i;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      ext2_free_block(fs, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(fs->dev, ip->addrs[NDIRECT]);
    indirect = (uint*)bp->data;
    for(i = 0; i < fs->block_size / sizeof(uint); i++)
      if(indirect[i])
        ext2_free_block(fs, indirect[i]);
    brelse(bp);
    ext2_free_block(fs, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  if(ip->addrs[NDIRECT+1]){
    bp = bread(fs->dev, ip->addrs[NDIRECT+1]);
    indirect = (uint*)bp->data;
    for(i = 0; i < fs->block_size / sizeof(uint); i++){
      if(indirect[i]){
        struct buf *ibp;
        uint *inner;
        int j;

        ibp = bread(fs->dev, indirect[i]);
        inner = (uint*)ibp->data;
        for(j = 0; j < fs->block_size / sizeof(uint); j++)
          if(inner[j])
            ext2_free_block(fs, inner[j]);
        brelse(ibp);
        ext2_free_block(fs, indirect[i]);
      }
    }
    brelse(bp);
    ext2_free_block(fs, ip->addrs[NDIRECT+1]);
    ip->addrs[NDIRECT+1] = 0;
  }

  ip->size = 0;
  ext2_sync_inode(ip);

  // Truncating an inode whose link count reached zero (called from
  // iput() after the last close of an unlinked file) also releases
  // the inode itself; O_TRUNC on a linked file keeps it.
  if(ip->nlink == 0)
    ext2_free_inode(fs, ip->inum);
}

static int
ext2_mount(struct mount *mnt)
{
  struct ext2_super sb;
  struct ext2_group_desc gd;
  uint block_size, inode_size, gd_block;

  if(ext2fs.used)
    return -1;

  if(block_read_bytes(mnt->dev, EXT2_SUPER_OFFSET, (uchar*)&sb,
                      sizeof(sb)) < 0)
    return -1;
  if(sb.s_magic != EXT2_SUPER_MAGIC)
    return -1;

  block_size = 1024 << sb.s_log_block_size;
  if(block_size != BSIZE)
    return -1;
  if(sb.s_blocks_count > sb.s_blocks_per_group ||
     sb.s_inodes_count > sb.s_inodes_per_group)
    return -1;

  inode_size = sb.s_inode_size;
  if(inode_size == 0)
    inode_size = 128;
  if(inode_size < sizeof(struct ext2_dinode))
    return -1;

  gd_block = sb.s_first_data_block + 1;
  if(block_read_bytes(mnt->dev, gd_block * block_size, (uchar*)&gd,
                      sizeof(gd)) < 0)
    return -1;

  memset(&ext2fs, 0, sizeof(ext2fs));
  ext2fs.used = 1;
  ext2fs.dev = mnt->dev;
  ext2fs.block_size = block_size;
  ext2fs.inode_size = inode_size;
  ext2fs.sb = sb;
  ext2fs.gd = gd;
  initsleeplock(&ext2fs.lock, "ext2fs");

  mnt->fs_private = &ext2fs;
  mnt->root = ext2_iget(mnt, EXT2_ROOT_INO);
  if(mnt->root == 0){
    memset(&ext2fs, 0, sizeof(ext2fs));
    mnt->fs_private = 0;
    return -1;
  }

  return 0;
}

static void
ext2_unmount(struct mount *mnt)
{
  memset(&ext2fs, 0, sizeof(ext2fs));
  mnt->fs_private = 0;
}

static struct inode_ops ext2_inode_ops = {
  .read = ext2_readi,
  .write = ext2_writei,
  .stat = ext2_stati,
  .trunc = ext2_itrunc,
  .lookup = ext2_lookup,
  .create = ext2_create,
  .unlink = ext2_unlink,
  .link = ext2_link,
};

static struct fs_ops ext2_fs_ops = {
  .mount = ext2_mount,
  .unmount = ext2_unmount,
};

struct fs_ops*
ext2_get_fs_ops(void)
{
  return &ext2_fs_ops;
}
