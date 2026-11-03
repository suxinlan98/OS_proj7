// SPDX-License-Identifier: BSD-2-Clause
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAT32_BPS          512
#define FAT32_BLOCKS       65536
#define FAT32_TOTAL_SEC    (FAT32_BLOCKS * 2)
#define FAT32_RSVD_SEC     32
#define FAT32_NUM_FATS     2
#define FAT32_FAT_SEC      1024
#define FAT32_SEC_PER_CLUS 1
#define FAT32_ROOT_CLUS    2

static void
put16(unsigned char *p, int off, uint16_t v)
{
  p[off+0] = v & 0xff;
  p[off+1] = v >> 8;
}

static void
put32(unsigned char *p, int off, uint32_t v)
{
  p[off+0] = v & 0xff;
  p[off+1] = (v >> 8) & 0xff;
  p[off+2] = (v >> 16) & 0xff;
  p[off+3] = (v >> 24) & 0xff;
}

static void
write_sector(FILE *f, uint32_t sec, unsigned char *buf)
{
  if(fseek(f, (long)sec * FAT32_BPS, SEEK_SET) != 0 ||
     fwrite(buf, 1, FAT32_BPS, f) != FAT32_BPS){
    perror("mkfat32");
    exit(1);
  }
}

// ---- LFN fixture files ----
// Two pre-made files with long-name directory entries, used by the
// fat32 test point to verify the kernel's LFN read path:
//   longfilename.txt  (16 chars, two-fragment chain, alias lookup)
//   ShortLong.txt     (13 chars, single fragment, case-insensitive)

static unsigned char
lfn_checksum(const unsigned char *name83)
{
  unsigned char sum = 0;
  int i;

  for(i = 0; i < 11; i++)
    sum = ((sum & 1) << 7) + (sum >> 1) + name83[i];
  return sum;
}

// Write one LFN directory entry.  frag points at this fragment's slice
// of the full name, n is the number of characters in the slice (<=13).
static void
put_lfn(unsigned char *de, int seq, int last, unsigned char cksum,
        const char *frag, int n)
{
  static const unsigned char off[13] =
    { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
  int i;

  memset(de, 0, 32);
  de[0] = seq | (last ? 0x40 : 0);
  for(i = 0; i < 13; i++){
    unsigned short c = 0xffff;
    if(i < n)
      c = (unsigned char)frag[i];
    else if(i == n)
      c = 0x0000;
    de[off[i]] = c & 0xff;
    de[off[i] + 1] = c >> 8;
  }
  de[11] = 0x0f;    // LFN attribute
  de[13] = cksum;
}

static void
put_sfn(unsigned char *de, const char *name83, uint32_t clus, uint32_t size)
{
  memset(de, 0, 32);
  memcpy(de, name83, 11);
  de[11] = 0x20;    // archive
  put16(de, 26, clus);
  put32(de, 28, size);
}

static void
inject_lfn_fixtures(FILE *f, uint32_t data_start)
{
  static const char lfn1[] = "longfilename.txt";
  static const char lfn2[] = "ShortLong.txt";
  static const char content[] = "fat32-lfn-ok\n";
  unsigned char root[FAT32_BPS], data[FAT32_BPS];
  unsigned char ck1, ck2;

  ck1 = lfn_checksum((const unsigned char *)"LONGFI~1TXT");
  ck2 = lfn_checksum((const unsigned char *)"SHORTL~1TXT");

  memset(root, 0, sizeof(root));
  // fragments of one name are stored in reverse order, last first
  put_lfn(root + 0, 2, 1, ck1, lfn1 + 13, 3);       // "txt"
  put_lfn(root + 32, 1, 0, ck1, lfn1, 13);          // "longfilename."
  put_sfn(root + 64, "LONGFI~1TXT", 3, sizeof(content) - 1);
  put_lfn(root + 96, 1, 1, ck2, lfn2, 13);          // single fragment
  put_sfn(root + 128, "SHORTL~1TXT", 4, sizeof(content) - 1);
  write_sector(f, data_start, root);                // root dir is cluster 2

  memset(data, 0, sizeof(data));
  memcpy(data, content, sizeof(content) - 1);
  write_sector(f, data_start + 1, data);            // cluster 3
  write_sector(f, data_start + 2, data);            // cluster 4
}

int
main(int argc, char **argv)
{
  const char *path;
  FILE *f;
  unsigned char boot[FAT32_BPS];
  unsigned char fsinfo[FAT32_BPS];
  unsigned char fat[FAT32_BPS];
  uint32_t data_start, clusters;

  if(argc != 2){
    fprintf(stderr, "Usage: mkfat32 image\n");
    return 1;
  }
  path = argv[1];

  data_start = FAT32_RSVD_SEC + FAT32_NUM_FATS * FAT32_FAT_SEC;
  clusters = (FAT32_TOTAL_SEC - data_start) / FAT32_SEC_PER_CLUS;
  if((clusters + 2) * 4 > FAT32_FAT_SEC * FAT32_BPS){
    fprintf(stderr, "mkfat32: FAT too small\n");
    return 1;
  }

  f = fopen(path, "wb+");
  if(f == 0){
    perror(path);
    return 1;
  }
  if(fseek(f, (long)FAT32_BLOCKS * 1024 - 1, SEEK_SET) != 0 ||
     fputc(0, f) == EOF){
    perror(path);
    fclose(f);
    return 1;
  }

  memset(boot, 0, sizeof(boot));
  boot[0] = 0xeb;
  boot[1] = 0x58;
  boot[2] = 0x90;
  memcpy(boot + 3, "MSDOS5.0", 8);
  put16(boot, 11, FAT32_BPS);
  boot[13] = FAT32_SEC_PER_CLUS;
  put16(boot, 14, FAT32_RSVD_SEC);
  boot[16] = FAT32_NUM_FATS;
  put16(boot, 17, 0);
  put16(boot, 19, 0);
  boot[21] = 0xf8;
  put16(boot, 22, 0);
  put16(boot, 24, 63);
  put16(boot, 26, 255);
  put32(boot, 28, 0);
  put32(boot, 32, FAT32_TOTAL_SEC);
  put32(boot, 36, FAT32_FAT_SEC);
  put16(boot, 40, 0);
  put16(boot, 42, 0);
  put32(boot, 44, FAT32_ROOT_CLUS);
  put16(boot, 48, 1);
  put16(boot, 50, 6);
  boot[64] = 0x80;
  boot[66] = 0x29;
  put32(boot, 67, 0x20260624);
  memcpy(boot + 71, "XV6 FAT32  ", 11);
  memcpy(boot + 82, "FAT32   ", 8);
  boot[510] = 0x55;
  boot[511] = 0xaa;
  write_sector(f, 0, boot);
  write_sector(f, 6, boot);

  memset(fsinfo, 0, sizeof(fsinfo));
  put32(fsinfo, 0, 0x41615252);
  put32(fsinfo, 484, 0x61417272);
  put32(fsinfo, 488, clusters - 1);
  put32(fsinfo, 492, 3);
  put32(fsinfo, 508, 0xaa550000);
  write_sector(f, 1, fsinfo);
  write_sector(f, 7, fsinfo);

  memset(fat, 0, sizeof(fat));
  put32(fat, 0, 0x0ffffff8);
  put32(fat, 4, 0xffffffff);
  put32(fat, 8, 0x0fffffff);
  put32(fat, 12, 0x0fffffff);   // cluster 3: fixture file 1 data
  put32(fat, 16, 0x0fffffff);   // cluster 4: fixture file 2 data
  write_sector(f, FAT32_RSVD_SEC, fat);
  write_sector(f, FAT32_RSVD_SEC + FAT32_FAT_SEC, fat);

  inject_lfn_fixtures(f, data_start);

  fclose(f);
  return 0;
}
