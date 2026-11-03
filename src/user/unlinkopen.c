// SPDX-License-Identifier: BSD-2-Clause
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  char *path = "/mnt/uop.txt";
  int wfd, rfd, n;
  char buf[8];

  if(argc > 1)
    path = argv[1];

  unlink(path);

  if((wfd = open(path, O_WRONLY | O_CREATE)) < 0){
    printf("unlinkopen: open-for-write %s failed\n", path);
    exit(1);
  }
  if(write(wfd, "hello", 5) != 5){
    printf("unlinkopen: write failed\n");
    exit(1);
  }

  if((rfd = open(path, O_RDONLY)) < 0){
    printf("unlinkopen: open-for-read %s failed\n", path);
    exit(1);
  }

  if(unlink(path) < 0){
    printf("unlinkopen: unlink %s failed\n", path);
    exit(1);
  }

  n = read(rfd, buf, 5);
  if(n != 5 || memcmp(buf, "hello", 5) != 0){
    printf("unlinkopen: FAIL (read %d bytes after unlink)\n", n);
    exit(1);
  }

  close(wfd);
  close(rfd);

  if(open(path, O_RDONLY) >= 0){
    printf("unlinkopen: FAIL (file resurrected after close)\n");
    exit(1);
  }

  printf("unlinkopen: ok\n");
  exit(0);
}
