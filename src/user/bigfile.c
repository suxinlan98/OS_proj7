// SPDX-License-Identifier: BSD-2-Clause
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define TOTAL (1024 * 1024)
#define CHUNK 512

static uchar
pattern(int i)
{
  return (i * 7 + 13) & 0xFF;
}

static int
write_file(char *path)
{
  uchar buf[CHUNK];
  int fd, i, n, off;

  if((fd = open(path, O_WRONLY | O_CREATE | O_TRUNC)) < 0){
    printf("bigfile: open-for-write %s failed\n", path);
    return -1;
  }
  for(off = 0; off < TOTAL; off += n){
    n = TOTAL - off < CHUNK ? TOTAL - off : CHUNK;
    for(i = 0; i < n; i++)
      buf[i] = pattern(off + i);
    if(write(fd, buf, n) != n){
      printf("bigfile: write failed at %d\n", off);
      close(fd);
      return -1;
    }
  }
  close(fd);
  return 0;
}

static int
verify_file(char *path)
{
  uchar buf[CHUNK];
  int fd, i, n, off;

  if((fd = open(path, O_RDONLY)) < 0){
    printf("bigfile: open-for-read %s failed\n", path);
    return -1;
  }
  for(off = 0; off < TOTAL; off += n){
    n = read(fd, buf, CHUNK);
    if(n <= 0){
      printf("bigfile: FAIL short read at %d (got %d)\n", off, n);
      close(fd);
      return -1;
    }
    for(i = 0; i < n; i++){
      if(buf[i] != pattern(off + i)){
        printf("bigfile: FAIL mismatch at byte %d\n", off + i);
        close(fd);
        return -1;
      }
    }
  }
  if(read(fd, buf, 1) != 0){
    printf("bigfile: FAIL trailing data past %d bytes\n", TOTAL);
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

int
main(int argc, char *argv[])
{
  char *path = "/mnt/bigfile";
  char *mode = "full";

  if(argc > 1)
    path = argv[1];
  if(argc > 2)
    mode = argv[2];

  if(strcmp(mode, "verify") != 0){
    if(write_file(path) < 0)
      exit(1);
  }
  if(verify_file(path) < 0)
    exit(1);

  if(strcmp(mode, "full") == 0){
    if(unlink(path) < 0){
      printf("bigfile: unlink %s failed\n", path);
      exit(1);
    }
  }

  printf("bigfile: ok (%d bytes, %s)\n", TOTAL, mode);
  exit(0);
}
