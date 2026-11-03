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

#define FAT32_SIG0       0x55
#define FAT32_SIG1       0xAA
#define FAT32_ROOT_INO   2
#define FAT32_EOC        0x0ffffff8
#define FAT32_MASK       0x0fffffff

#define FAT32_ATTR_VOL   0x08
#define FAT32_ATTR_DIR   0x10
#define FAT32_ATTR_ARCH  0x20
#define FAT32_ATTR_LFN   0x0f

#define BPB_BYTSPERSEC   11
#define BPB_SECPERCLUS   13
#define BPB_RSVDSECCNT   14
#define BPB_NUMFATS      16
#define BPB_ROOTENTCNT   17
#define BPB_TOTSEC16     19
#define BPB_FATSZ16      22
#define BPB_TOTSEC32     32
#define BPB_FATSZ32      36
#define BPB_ROOTCLUS     44
#define BPB_FSTYPE       82
#define BPB_BOOTSIG      510

struct fat32_fs {
  int used;
  uint dev;
  uint bytes_per_sec;
  uint sec_per_clus;
  uint cluster_size;
  uint rsvd_sec;
  uint num_fats;
  uint fat_sz;
  uint total_sec;
  uint root_clus;
  uint data_sec;
  uint data_start_sec;
  uint nclus;
  uint next_free;
  // Serializes FAT allocation and free paths (including the next_free
  // hint) across CPUs.  Only ever taken after inode locks and before
  // buffer locks.
  struct sleeplock lock;
};

struct fat32_dirent {
  uchar name[11];
  uchar attr;
  uchar ntres;
  uchar crt_time_tenth;
  ushort crt_time;
  ushort crt_date;
  ushort access_date;
  ushort clus_hi;
  ushort write_time;
  ushort write_date;
  ushort clus_lo;
  uint size;
} __attribute__((packed));

static struct fat32_fs fat32fs;
static struct inode_ops fat32_inode_ops;
static struct fs_ops fat32_fs_ops;

static ushort
fat32_get16(uchar *p)
{
  return p[0] | (p[1] << 8);
}

static uint
fat32_get32(uchar *p)
{
  return (uint)p[0] | ((uint)p[1] << 8) |
         ((uint)p[2] << 16) | ((uint)p[3] << 24);
}

static void
fat32_put32(uchar *p, uint v)
{
  p[0] = v & 0xff;
  p[1] = (v >> 8) & 0xff;
  p[2] = (v >> 16) & 0xff;
  p[3] = (v >> 24) & 0xff;
}

static uint
fat32_min(uint a, uint b)
{
  return a < b ? a : b;
}

static int
fat32_is_power2(uint n)
{
  return n != 0 && (n & (n - 1)) == 0;
}

static int
fat32_check_type(uchar *boot)
{
  return boot[BPB_FSTYPE+0] == 'F' &&
         boot[BPB_FSTYPE+1] == 'A' &&
         boot[BPB_FSTYPE+2] == 'T' &&
         boot[BPB_FSTYPE+3] == '3' &&
         boot[BPB_FSTYPE+4] == '2';
}

static uint
fat32_clus_offset(struct fat32_fs *fs, uint clus)
{
  return (fs->data_start_sec + (clus - 2) * fs->sec_per_clus) *
         fs->bytes_per_sec;
}

static uint
fat32_fat_offset(struct fat32_fs *fs, int fatno, uint clus)
{
  return (fs->rsvd_sec + fatno * fs->fat_sz) * fs->bytes_per_sec + clus * 4;
}

static int
fat32_is_eoc(uint v)
{
  return (v & FAT32_MASK) >= FAT32_EOC;
}

static uint
fat32_first_cluster(struct fat32_dirent *de)
{
  return ((uint)de->clus_hi << 16) | de->clus_lo;
}

static void
fat32_set_cluster(struct fat32_dirent *de, uint clus)
{
  de->clus_hi = clus >> 16;
  de->clus_lo = clus & 0xffff;
}

static int
fat32_read_fat(struct fat32_fs *fs, uint clus, uint *val)
{
  uchar buf[4];

  if(clus < 2 || clus > fs->nclus + 1)
    return -1;
  if(block_read_bytes(fs->dev, fat32_fat_offset(fs, 0, clus),
                      buf, sizeof(buf)) != sizeof(buf))
    return -1;
  *val = fat32_get32(buf) & FAT32_MASK;
  return 0;
}

static int
fat32_write_fat(struct fat32_fs *fs, uint clus, uint val)
{
  uchar buf[4];
  int i;

  if(clus < 2 || clus > fs->nclus + 1)
    return -1;
  fat32_put32(buf, val & FAT32_MASK);
  for(i = 0; i < fs->num_fats; i++){
    if(block_write_bytes(fs->dev, fat32_fat_offset(fs, i, clus),
                         buf, sizeof(buf)) != sizeof(buf))
      return -1;
  }
  return 0;
}

static int
fat32_zero_cluster(struct fat32_fs *fs, uint clus)
{
  uchar zero[BSIZE];
  uint off, n;

  memset(zero, 0, sizeof(zero));
  for(off = 0; off < fs->cluster_size; off += n){
    n = fat32_min(BSIZE, fs->cluster_size - off);
    if(block_write_bytes(fs->dev, fat32_clus_offset(fs, clus) + off,
                         zero, n) != n)
      return -1;
  }
  return 0;
}

static uint
fat32_alloc_cluster(struct fat32_fs *fs)
{
  uint clus, val, start;

  acquiresleep(&fs->lock);

  // Next-free hint: continue where the last allocation stopped
  // instead of rescanning the FAT from cluster 2 every time.
  start = fs->next_free;
  if(start < 2 || start > fs->nclus + 1)
    start = 2;
  clus = start;
  for(;;){
    if(fat32_read_fat(fs, clus, &val) < 0){
      releasesleep(&fs->lock);
      return 0;
    }
    if(val == 0){
      if(fat32_write_fat(fs, clus, FAT32_EOC) < 0){
        releasesleep(&fs->lock);
        return 0;
      }
      if(fat32_zero_cluster(fs, clus) < 0){
        fat32_write_fat(fs, clus, 0);
        releasesleep(&fs->lock);
        return 0;
      }
      fs->next_free = clus + 1;
      releasesleep(&fs->lock);
      return clus;
    }
    clus++;
    if(clus > fs->nclus + 1)
      clus = 2;
    if(clus == start){
      releasesleep(&fs->lock);
      return 0;
    }
  }
}

static void
fat32_free_after(struct fat32_fs *fs, uint first)
{
  uint next, cur;

  if(first < 2)
    return;
  if(fat32_read_fat(fs, first, &cur) < 0)
    return;
  fat32_write_fat(fs, first, FAT32_EOC);

  while(!fat32_is_eoc(cur) && cur >= 2){
    if(fat32_read_fat(fs, cur, &next) < 0)
      break;
    fat32_write_fat(fs, cur, 0);
    if(cur < fs->next_free)
      fs->next_free = cur;
    cur = next;
  }
}

static void
fat32_free_chain(struct fat32_fs *fs, uint first)
{
  acquiresleep(&fs->lock);
  fat32_free_after(fs, first);
  if(first >= 2){
    fat32_write_fat(fs, first, 0);
    if(first < fs->next_free)
      fs->next_free = first;
  }
  releasesleep(&fs->lock);
}

static uint
fat32_cluster_at(struct inode *ip, uint index, int alloc)
{
  struct fat32_fs *fs = (struct fat32_fs*)ip->mnt->fs_private;
  uint clus, next, cur;

  // Walk the chain from the cached position when possible; rescanning
  // from the head for every block makes large files quadratic.
  if(ip->addrs[3] >= 2 && ip->addrs[2] <= index){
    clus = ip->addrs[3];
    cur = ip->addrs[2];
  } else {
    clus = ip->addrs[0];
    cur = 0;
  }
  if(clus < 2)
    return 0;

  while(cur < index){
    if(fat32_read_fat(fs, clus, &next) < 0)
      return 0;
    if(fat32_is_eoc(next)){
      if(!alloc)
        return 0;
      next = fat32_alloc_cluster(fs);
      if(next == 0)
        return 0;
      if(fat32_write_fat(fs, clus, next) < 0){
        fat32_write_fat(fs, next, 0);
        return 0;
      }
    }
    clus = next;
    cur++;
  }

  ip->addrs[2] = cur;
  ip->addrs[3] = clus;
  return clus;
}

static int
fat32_is_valid_entry(struct fat32_dirent *de)
{
  if(de->name[0] == 0x00 || de->name[0] == 0xe5)
    return 0;
  if((de->attr & FAT32_ATTR_LFN) == FAT32_ATTR_LFN)
    return 0;
  if(de->attr & FAT32_ATTR_VOL)
    return 0;
  return 1;
}

static char
fat32_toupper(char c)
{
  if(c >= 'a' && c <= 'z')
    return c - 'a' + 'A';
  return c;
}

static char
fat32_tolower(char c)
{
  if(c >= 'A' && c <= 'Z')
    return c - 'A' + 'a';
  return c;
}

static int
fat32_name83(char *name, uchar out[11])
{
  int i, j;

  memset(out, ' ', 11);
  if(name[0] == 0)
    return -1;

  for(i = 0; name[i] && name[i] != '.'; i++){
    if(i >= 8 || name[i] == '/' || name[i] == ' ')
      return -1;
    out[i] = fat32_toupper(name[i]);
  }
  if(i == 0)
    return -1;

  if(name[i] == '.'){
    i++;
    for(j = 0; name[i]; i++, j++){
      if(j >= 3 || name[i] == '/' || name[i] == '.' || name[i] == ' ')
        return -1;
      out[8+j] = fat32_toupper(name[i]);
    }
    if(j == 0)
      return -1;
  }
  return 0;
}

static void
fat32_name_from83(struct fat32_dirent *de, char *name)
{
  int i, n;

  n = 0;
  for(i = 0; i < 8 && de->name[i] != ' '; i++)
    name[n++] = fat32_tolower(de->name[i]);
  if(de->name[8] != ' '){
    name[n++] = '.';
    for(i = 8; i < 11 && de->name[i] != ' '; i++)
      name[n++] = fat32_tolower(de->name[i]);
  }
  name[n] = 0;
}

static void
fat32_fill_inode(struct inode *ip, struct mount *mnt, struct fat32_dirent *de,
                 uint entry_off)
{
  uint clus;

  clus = fat32_first_cluster(de);
  ip->fstype = FS_FAT32;
  ip->mnt = mnt;
  ip->ops = &fat32_inode_ops;
  ip->type = (de->attr & FAT32_ATTR_DIR) ? T_DIR : T_FILE;
  ip->major = 0;
  ip->minor = 0;
  ip->nlink = 1;
  ip->size = (ip->type == T_DIR) ? 0 : de->size;
  ip->addrs[0] = clus;
  ip->addrs[1] = entry_off;
  ip->addrs[2] = 0;   // chain-walk cache
  ip->addrs[3] = 0;
  __atomic_store_n(&ip->valid, 1, __ATOMIC_RELEASE);
}

static struct inode*
fat32_iget(struct mount *mnt, struct fat32_dirent *de, uint entry_off)
{
  struct fat32_fs *fs = (struct fat32_fs*)mnt->fs_private;
  struct inode *ip;
  uint clus;

  clus = fat32_first_cluster(de);
  if(clus < 2)
    return 0;
  if(mnt->root != 0 && clus == fs->root_clus)
    return idup(mnt->root);
  ip = iget(mnt->dev, clus);
  // This reference prevents iput() from clearing valid. The acquire
  // fast path also avoids nesting inode locks during pathname lookup.
  if(__atomic_load_n(&ip->valid, __ATOMIC_ACQUIRE))
    return ip;

  acquiresleep(&ip->lock);
  // Refill only on a cache miss; a live inode may hold in-memory
  // state that is newer than the on-disk directory entry.
  if(!ip->valid)
    fat32_fill_inode(ip, mnt, de, entry_off);
  releasesleep(&ip->lock);
  return ip;
}

static struct inode*
fat32_root_inode(struct mount *mnt)
{
  struct inode *ip;

  ip = iget(mnt->dev, FAT32_ROOT_INO);
  acquiresleep(&ip->lock);
  if(!ip->valid){
    ip->fstype = FS_FAT32;
    ip->mnt = mnt;
    ip->ops = &fat32_inode_ops;
    ip->type = T_DIR;
    ip->major = 0;
    ip->minor = 0;
    ip->nlink = 1;
    ip->size = 0;
    ip->addrs[0] = fat32fs.root_clus;
    ip->addrs[1] = 0;
    ip->addrs[2] = 0;   // chain-walk cache
    ip->addrs[3] = 0;
    __atomic_store_n(&ip->valid, 1, __ATOMIC_RELEASE);
  }
  releasesleep(&ip->lock);
  return ip;
}

static int
fat32_read_entry(struct fat32_fs *fs, uint off, struct fat32_dirent *de)
{
  return block_read_bytes(fs->dev, off, (uchar*)de, sizeof(*de)) ==
         sizeof(*de) ? 0 : -1;
}

static int
fat32_write_entry(struct fat32_fs *fs, uint off, struct fat32_dirent *de)
{
  return block_write_bytes(fs->dev, off, (uchar*)de, sizeof(*de)) ==
         sizeof(*de) ? 0 : -1;
}

static int
fat32_find_entry(struct inode *dp, uchar name83[11],
                 struct fat32_dirent *out, uint *entry_off)
{
  struct fat32_fs *fs = (struct fat32_fs*)dp->mnt->fs_private;
  struct fat32_dirent de;
  uint clus, next, pos, off;

  if(dp->type != T_DIR)
    return -1;

  clus = dp->addrs[0];
  while(clus >= 2){
    for(pos = 0; pos < fs->cluster_size; pos += sizeof(de)){
      off = fat32_clus_offset(fs, clus) + pos;
      if(fat32_read_entry(fs, off, &de) < 0)
        return -1;
      if(de.name[0] == 0x00)
        return -1;
      if(fat32_is_valid_entry(&de) && memcmp(de.name, name83, 11) == 0){
        if(out)
          *out = de;
        if(entry_off)
          *entry_off = off;
        return 0;
      }
    }
    if(fat32_read_fat(fs, clus, &next) < 0 || fat32_is_eoc(next))
      break;
    clus = next;
  }
  return -1;
}

static int
fat32_find_free_entry(struct inode *dp, uint *entry_off)
{
  struct fat32_fs *fs = (struct fat32_fs*)dp->mnt->fs_private;
  struct fat32_dirent de;
  uint clus, next, pos, off, newclus;

  clus = dp->addrs[0];
  while(clus >= 2){
    for(pos = 0; pos < fs->cluster_size; pos += sizeof(de)){
      off = fat32_clus_offset(fs, clus) + pos;
      if(fat32_read_entry(fs, off, &de) < 0)
        return -1;
      if(de.name[0] == 0x00 || de.name[0] == 0xe5){
        *entry_off = off;
        return 0;
      }
    }
    if(fat32_read_fat(fs, clus, &next) < 0)
      return -1;
    if(fat32_is_eoc(next))
      break;
    clus = next;
  }

  newclus = fat32_alloc_cluster(fs);
  if(newclus == 0)
    return -1;
  if(fat32_write_fat(fs, clus, newclus) < 0){
    fat32_write_fat(fs, newclus, 0);
    return -1;
  }
  *entry_off = fat32_clus_offset(fs, newclus);
  return 0;
}

static void
fat32_remove_entry(struct fat32_fs *fs, uint entry_off)
{
  struct fat32_dirent de;

  if(entry_off == 0)
    return;
  if(fat32_read_entry(fs, entry_off, &de) < 0)
    return;
  de.name[0] = 0xe5;
  fat32_write_entry(fs, entry_off, &de);
}

static int
fat32_add_entry(struct inode *dp, uchar name83[11], uchar attr,
                uint clus, uint size, uint *entry_off)
{
  struct fat32_fs *fs = (struct fat32_fs*)dp->mnt->fs_private;
  struct fat32_dirent de;

  if(fat32_find_free_entry(dp, entry_off) < 0)
    return -1;

  memset(&de, 0, sizeof(de));
  memmove(de.name, name83, 11);
  de.attr = attr;
  fat32_set_cluster(&de, clus);
  de.size = size;
  return fat32_write_entry(fs, *entry_off, &de);
}

static int
fat32_sync_inode(struct inode *ip)
{
  struct fat32_fs *fs = (struct fat32_fs*)ip->mnt->fs_private;
  struct fat32_dirent de;

  if(ip->addrs[1] == 0)
    return 0;
  if(fat32_read_entry(fs, ip->addrs[1], &de) < 0)
    return -1;
  de.size = ip->type == T_DIR ? 0 : ip->size;
  fat32_set_cluster(&de, ip->addrs[0]);
  return fat32_write_entry(fs, ip->addrs[1], &de);
}

static int
fat32_mount(struct mount *mnt)
{
  uchar boot[BSIZE];
  uint root_entries, fat16, total16;
  uint fat_bytes, needed;

  if(fat32fs.used)
    return -1;

  if(block_read_bytes(mnt->dev, 0, boot, sizeof(boot)) < 0)
    return -1;

  if(boot[BPB_BOOTSIG] != FAT32_SIG0 || boot[BPB_BOOTSIG+1] != FAT32_SIG1)
    return -1;
  if(!fat32_check_type(boot))
    return -1;

  fat32fs.bytes_per_sec = fat32_get16(boot + BPB_BYTSPERSEC);
  fat32fs.sec_per_clus = boot[BPB_SECPERCLUS];
  fat32fs.rsvd_sec = fat32_get16(boot + BPB_RSVDSECCNT);
  fat32fs.num_fats = boot[BPB_NUMFATS];
  fat32fs.total_sec = fat32_get32(boot + BPB_TOTSEC32);
  fat32fs.fat_sz = fat32_get32(boot + BPB_FATSZ32);
  fat32fs.root_clus = fat32_get32(boot + BPB_ROOTCLUS);

  root_entries = fat32_get16(boot + BPB_ROOTENTCNT);
  total16 = fat32_get16(boot + BPB_TOTSEC16);
  fat16 = fat32_get16(boot + BPB_FATSZ16);

  if(fat32fs.bytes_per_sec != 512)
    return -1;
  if(!fat32_is_power2(fat32fs.sec_per_clus))
    return -1;
  if(fat32fs.rsvd_sec == 0 || fat32fs.num_fats == 0 ||
     fat32fs.fat_sz == 0 || fat32fs.total_sec == 0 ||
     fat32fs.root_clus < FAT32_ROOT_INO)
    return -1;
  if(root_entries != 0 || total16 != 0 || fat16 != 0)
    return -1;

  fat32fs.data_start_sec = fat32fs.rsvd_sec + fat32fs.num_fats * fat32fs.fat_sz;
  if(fat32fs.total_sec <= fat32fs.data_start_sec)
    return -1;
  fat32fs.data_sec = fat32fs.total_sec - fat32fs.data_start_sec;
  fat32fs.cluster_size = fat32fs.bytes_per_sec * fat32fs.sec_per_clus;
  fat32fs.nclus = fat32fs.data_sec / fat32fs.sec_per_clus;
  if(fat32fs.cluster_size == 0 || fat32fs.nclus == 0)
    return -1;

  fat_bytes = fat32fs.fat_sz * fat32fs.bytes_per_sec;
  needed = (fat32fs.nclus + 2) * 4;
  if(fat_bytes < needed)
    return -1;

  fat32fs.used = 1;
  fat32fs.dev = mnt->dev;
  fat32fs.next_free = 2;
  initsleeplock(&fat32fs.lock, "fat32fs");
  mnt->fs_private = &fat32fs;
  mnt->root = fat32_root_inode(mnt);
  if(mnt->root == 0){
    memset(&fat32fs, 0, sizeof(fat32fs));
    mnt->fs_private = 0;
    return -1;
  }

  return 0;
}

static void
fat32_unmount(struct mount *mnt)
{
  memset(&fat32fs, 0, sizeof(fat32fs));
  mnt->fs_private = 0;
}

// ---- FAT32 long file name (LFN) read support ----
// Only lookup and directory listing understand LFN chains; create and
// unlink stay on 8.3 short names.  The xv6 namespace truncates path
// components to DIRSIZ characters, so assembling the first DIRSIZ
// characters of a long name is sufficient: names that fit match
// exactly (case-insensitive, per the FAT rules), longer ones follow
// the same truncated-alias rule as the ext2 backend.

struct fat32_lfn {
  int expect;            // next fragment sequence expected, 0 = no chain
  int ready;             // a complete chain waits for its short entry
  uchar cksum;           // checksum shared by all fragments
  int endpos;            // name length; > DIRSIZ means "longer than DIRSIZ"
  char name[DIRSIZ];
};

static uchar
fat32_lfn_checksum(uchar *name83)
{
  uchar sum = 0;
  int i;

  for(i = 0; i < 11; i++)
    sum = ((sum & 1) << 7) + (sum >> 1) + name83[i];
  return sum;
}

static ushort
fat32_lfn_char(struct fat32_dirent *de, int i)
{
  // the 13 UTF-16LE characters of a fragment live at byte offsets
  // 1..10, 14..25 and 28..31
  static const uchar off[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
  uchar *p = (uchar*)de;

  return p[off[i]] | (p[off[i] + 1] << 8);
}

static void
fat32_lfn_reset(struct fat32_lfn *st)
{
  st->expect = 0;
  st->ready = 0;
}

// Feed one LFN fragment (attr 0x0F directory entry) into the assembly
// state.  Fragments of one name are stored in reverse order on disk.
static void
fat32_lfn_feed(struct fat32_lfn *st, struct fat32_dirent *de)
{
  int seq, i, pos;
  ushort w;

  seq = de->name[0] & 0x3F;
  if(de->name[0] & 0x40){
    // logically last fragment, stored first: starts a new chain
    st->expect = seq;
    st->ready = 0;
    st->cksum = de->name[13];
    st->endpos = seq * 13;
    memset(st->name, 0, sizeof(st->name));
  } else if(st->expect == 0 || seq != st->expect || de->name[13] != st->cksum){
    fat32_lfn_reset(st);        // out of order or checksum mismatch
    return;
  }

  for(i = 0; i < 13; i++){
    pos = (seq - 1) * 13 + i;
    w = fat32_lfn_char(de, i);
    if(w == 0x0000 || w == 0xFFFF){
      if(pos < st->endpos)
        st->endpos = pos;       // terminator or padding
      continue;
    }
    if(pos < DIRSIZ && pos < st->endpos)
      st->name[pos] = w < 0x80 ? w : '?';
  }

  if(seq == 1)
    st->ready = 1;              // chain complete
  else
    st->expect = seq - 1;
}

// Consult the assembled chain when its short-name entry shows up.
// Returns 1 and copies the (truncated) long name when the chain is
// complete and its checksum matches this entry's 8.3 name.
static int
fat32_lfn_take(struct fat32_lfn *st, struct fat32_dirent *de,
               char *name, int *len)
{
  int ok;

  ok = st->ready && st->cksum == fat32_lfn_checksum(de->name);
  if(ok){
    memmove(name, st->name, DIRSIZ);
    *len = st->endpos;
  }
  fat32_lfn_reset(st);
  return ok;
}

static int
fat32_lfn_match(char *query, int qlen, char *lname, int llen)
{
  int i;

  if(llen <= DIRSIZ){
    if(qlen != llen)            // exact match for names that fit
      return 0;
  } else {
    if(qlen != DIRSIZ)          // longer name: truncated-alias only
      return 0;
    llen = DIRSIZ;
  }
  for(i = 0; i < llen; i++)
    if(fat32_tolower(query[i]) != fat32_tolower(lname[i]))
      return 0;
  return 1;
}

static int
fat32_namelen(char *name)
{
  int len;

  for(len = 0; len < DIRSIZ && name[len]; len++)
    ;
  return len;
}

// Long-name fallback of fat32_lookup: scan the directory for an LFN
// chain whose assembled name matches the query.
static struct inode*
fat32_lookup_lfn(struct inode *dp, char *name)
{
  struct fat32_fs *fs = (struct fat32_fs*)dp->mnt->fs_private;
  struct fat32_dirent de;
  struct fat32_lfn st;
  char lname[DIRSIZ];
  uint clus, next, pos, off;
  int llen, qlen;

  qlen = fat32_namelen(name);
  memset(&st, 0, sizeof(st));
  clus = dp->addrs[0];
  while(clus >= 2){
    for(pos = 0; pos < fs->cluster_size; pos += sizeof(de)){
      off = fat32_clus_offset(fs, clus) + pos;
      if(fat32_read_entry(fs, off, &de) < 0)
        return 0;
      if(de.name[0] == 0x00)
        return 0;
      if(de.name[0] == 0xe5){
        fat32_lfn_reset(&st);   // deleted entry breaks a chain
        continue;
      }
      if((de.attr & FAT32_ATTR_LFN) == FAT32_ATTR_LFN){
        fat32_lfn_feed(&st, &de);
        continue;
      }
      if(de.attr & FAT32_ATTR_VOL){
        fat32_lfn_reset(&st);
        continue;
      }
      if(fat32_lfn_take(&st, &de, lname, &llen) &&
         fat32_lfn_match(name, qlen, lname, llen))
        return fat32_iget(dp->mnt, &de, off);
    }
    if(fat32_read_fat(fs, clus, &next) < 0 || fat32_is_eoc(next))
      break;
    clus = next;
  }
  return 0;
}

static int
fat32_readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  struct fat32_fs *fs = (struct fat32_fs*)ip->mnt->fs_private;
  struct dirent xde;
  struct fat32_dirent de;
  struct fat32_lfn lst;
  char name[DIRSIZ];
  char lname[DIRSIZ];
  uchar buf[BSIZE];
  uint want, seen, clus, next, pos, deoff, m;
  int llen, has_lfn;

  if(ip->type == T_DIR){
    if(n < sizeof(xde) || off % sizeof(xde) != 0)
      return 0;
    want = off / sizeof(xde);
    seen = 0;
    memset(&lst, 0, sizeof(lst));
    clus = ip->addrs[0];
    while(clus >= 2){
      for(pos = 0; pos < fs->cluster_size; pos += sizeof(de)){
        deoff = fat32_clus_offset(fs, clus) + pos;
        if(fat32_read_entry(fs, deoff, &de) < 0)
          return -1;
        if(de.name[0] == 0x00)
          return 0;
        if((de.attr & FAT32_ATTR_LFN) == FAT32_ATTR_LFN &&
           de.name[0] != 0xe5){
          fat32_lfn_feed(&lst, &de);
          continue;
        }
        if(!fat32_is_valid_entry(&de)){
          fat32_lfn_reset(&lst);        // deleted/volume entry breaks a chain
          continue;
        }
        has_lfn = fat32_lfn_take(&lst, &de, lname, &llen);
        if(seen++ == want){
          memset(&xde, 0, sizeof(xde));
          xde.inum = fat32_first_cluster(&de);
          if(has_lfn)
            memmove(xde.name, lname, fat32_min(llen, DIRSIZ));
          else {
            fat32_name_from83(&de, name);
            memmove(xde.name, name, fat32_min(strlen(name), DIRSIZ));
          }
          if(either_copyout(user_dst, dst, (char*)&xde, sizeof(xde)) < 0)
            return -1;
          return sizeof(xde);
        }
      }
      if(fat32_read_fat(fs, clus, &next) < 0 || fat32_is_eoc(next))
        break;
      clus = next;
    }
    return 0;
  }

  if(off > ip->size || off + n < off)
    return 0;
  if(off + n > ip->size)
    n = ip->size - off;

  for(want = 0; want < n; want += m, off += m, dst += m){
    clus = fat32_cluster_at(ip, off / fs->cluster_size, 0);
    m = fat32_min(n - want, fs->cluster_size - off % fs->cluster_size);
    m = fat32_min(m, BSIZE);
    if(clus == 0){
      memset(buf, 0, m);
    } else if(block_read_bytes(fs->dev,
                               fat32_clus_offset(fs, clus) + off % fs->cluster_size,
                               buf, m) != m){
      return -1;
    }
    if(either_copyout(user_dst, dst, (char*)buf, m) < 0)
      return -1;
  }

  return want;
}

static int
fat32_writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  struct fat32_fs *fs = (struct fat32_fs*)ip->mnt->fs_private;
  uchar buf[BSIZE];
  uint tot, m, clus;

  if(ip->type != T_FILE || off + n < off)
    return -1;

  for(tot = 0; tot < n; tot += m, off += m, src += m){
    clus = fat32_cluster_at(ip, off / fs->cluster_size, 1);
    if(clus == 0)
      break;
    m = fat32_min(n - tot, fs->cluster_size - off % fs->cluster_size);
    m = fat32_min(m, BSIZE);
    if(either_copyin((char*)buf, user_src, src, m) < 0)
      return -1;
    if(block_write_bytes(fs->dev,
                         fat32_clus_offset(fs, clus) + off % fs->cluster_size,
                         buf, m) != m)
      return -1;
  }

  if(off > ip->size)
    ip->size = off;
  fat32_sync_inode(ip);
  return tot;
}

static void
fat32_stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

static void
fat32_itrunc(struct inode *ip)
{
  struct fat32_fs *fs = (struct fat32_fs*)ip->mnt->fs_private;

  if(ip->nlink == 0){
    // Called from iput() after the last close of an unlinked file
    // or empty directory: the directory entry is already gone, so
    // release the whole cluster chain instead of keeping the first
    // cluster around.
    fat32_free_chain(fs, ip->addrs[0]);
    ip->addrs[0] = 0;
    ip->addrs[2] = 0;
    ip->addrs[3] = 0;
    ip->size = 0;
    return;
  }

  if(ip->type != T_FILE)
    return;
  acquiresleep(&fs->lock);
  fat32_free_after(fs, ip->addrs[0]);
  releasesleep(&fs->lock);
  fat32_zero_cluster(fs, ip->addrs[0]);
  ip->addrs[2] = 0;
  ip->addrs[3] = 0;
  ip->size = 0;
  fat32_sync_inode(ip);
}

static struct inode*
fat32_lookup(struct inode *dp, char *name)
{
  uchar name83[11];
  struct fat32_dirent de;
  uint entry_off;

  if(namecmp(name, ".") == 0)
    return idup(dp);
  if(namecmp(name, "..") == 0){
    memset(name83, ' ', sizeof(name83));
    name83[0] = '.';
    name83[1] = '.';
    if(fat32_find_entry(dp, name83, &de, &entry_off) == 0)
      return fat32_iget(dp->mnt, &de, entry_off);
    return idup(dp);
  }
  if(fat32_name83(name, name83) == 0 &&
     fat32_find_entry(dp, name83, &de, &entry_off) == 0)
    return fat32_iget(dp->mnt, &de, entry_off);
  // Not found by the 8.3 name (or no 8.3 form): try long names.
  return fat32_lookup_lfn(dp, name);
}

static struct inode*
fat32_create(struct inode *dp, char *name, short type, short major, short minor)
{
  struct fat32_fs *fs = (struct fat32_fs*)dp->mnt->fs_private;
  struct fat32_dirent de;
  struct inode tmp;
  uchar name83[11], dot[11], dotdot[11];
  uchar attr;
  uint clus, entry_off, dot_off;

  (void)major;
  (void)minor;

  if(type != T_FILE && type != T_DIR)
    return 0;
  if(fat32_name83(name, name83) < 0)
    return 0;
  if(fat32_find_entry(dp, name83, 0, 0) == 0)
    return 0;

  clus = fat32_alloc_cluster(fs);
  if(clus == 0)
    return 0;

  attr = type == T_DIR ? FAT32_ATTR_DIR : FAT32_ATTR_ARCH;
  if(fat32_add_entry(dp, name83, attr, clus, 0, &entry_off) < 0){
    fat32_write_fat(fs, clus, 0);
    return 0;
  }

  if(type == T_DIR){
    memset(&tmp, 0, sizeof(tmp));
    tmp.mnt = dp->mnt;
    tmp.type = T_DIR;
    tmp.addrs[0] = clus;

    memset(dot, ' ', sizeof(dot));
    memset(dotdot, ' ', sizeof(dotdot));
    dot[0] = '.';
    dotdot[0] = '.';
    dotdot[1] = '.';
    if(fat32_add_entry(&tmp, dot, FAT32_ATTR_DIR, clus, 0, &dot_off) < 0){
      fat32_remove_entry(fs, entry_off);
      fat32_write_fat(fs, clus, 0);
      return 0;
    }
    if(fat32_add_entry(&tmp, dotdot, FAT32_ATTR_DIR, dp->addrs[0], 0,
                       &dot_off) < 0){
      fat32_remove_entry(fs, entry_off);
      fat32_write_fat(fs, clus, 0);
      return 0;
    }
  }

  memset(&de, 0, sizeof(de));
  memmove(de.name, name83, sizeof(name83));
  de.attr = attr;
  fat32_set_cluster(&de, clus);
  de.size = 0;
  return fat32_iget(dp->mnt, &de, entry_off);
}

static int
fat32_is_dir_empty(struct inode *ip)
{
  struct fat32_fs *fs = (struct fat32_fs*)ip->mnt->fs_private;
  struct fat32_dirent de;
  uint clus, next, pos, off;

  clus = ip->addrs[0];
  while(clus >= 2){
    for(pos = 0; pos < fs->cluster_size; pos += sizeof(de)){
      off = fat32_clus_offset(fs, clus) + pos;
      if(fat32_read_entry(fs, off, &de) < 0)
        return 0;
      if(de.name[0] == 0x00)
        return 1;
      if(!fat32_is_valid_entry(&de))
        continue;
      if(!(de.name[0] == '.' && de.name[1] == ' ') &&
         !(de.name[0] == '.' && de.name[1] == '.' && de.name[2] == ' '))
        return 0;
    }
    if(fat32_read_fat(fs, clus, &next) < 0 || fat32_is_eoc(next))
      break;
    clus = next;
  }

  return 1;
}

static int
fat32_unlink(struct inode *dp, char *name)
{
  struct fat32_fs *fs = (struct fat32_fs*)dp->mnt->fs_private;
  struct fat32_dirent de;
  struct inode *ip;
  uchar name83[11];
  uint entry_off;

  if(fat32_name83(name, name83) < 0)
    return -1;
  if(fat32_find_entry(dp, name83, &de, &entry_off) < 0)
    return -1;

  ip = fat32_iget(dp->mnt, &de, entry_off);
  if(ip == 0)
    return -1;

  ilock(ip);
  if(ip->type == T_DIR && !fat32_is_dir_empty(ip)){
    iunlockput(ip);
    return -1;
  }

  if(vfs_mountpoint(ip) != 0){
    // Cannot unlink a directory that is used as a mount point.
    iunlockput(ip);
    return -1;
  }

  fat32_remove_entry(fs, entry_off);

  // Only drop the link count here. The cluster chain is released by
  // iput() once the last open reference goes away.
  ip->nlink = 0;
  iunlockput(ip);
  return 0;
}

static int
fat32_link(struct inode *ip, struct inode *dp, char *name)
{
  (void)ip;
  (void)dp;
  (void)name;
  return -1;
}

static struct inode_ops fat32_inode_ops = {
  .read = fat32_readi,
  .write = fat32_writei,
  .stat = fat32_stati,
  .trunc = fat32_itrunc,
  .lookup = fat32_lookup,
  .create = fat32_create,
  .unlink = fat32_unlink,
  .link = fat32_link,
};

static struct fs_ops fat32_fs_ops = {
  .mount = fat32_mount,
  .unmount = fat32_unmount,
};

struct fs_ops*
fat32_get_fs_ops(void)
{
  return &fat32_fs_ops;
}
