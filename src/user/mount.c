// SPDX-License-Identifier: BSD-2-Clause
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static int
default_dev(char *fstype)
{
  if(strcmp(fstype, "xv6") == 0)
    return 1;
  if(strcmp(fstype, "ext2") == 0)
    return 2;
  if(strcmp(fstype, "fat32") == 0)
    return 3;
  return 0;
}

int
main(int argc, char *argv[])
{
  int dev;

  if(argc < 3 || argc > 4){
    fprintf(2, "usage: mount fstype path [dev]\n");
    exit(1);
  }

  if(argc == 4)
    dev = atoi(argv[3]);
  else
    dev = default_dev(argv[1]);

  if(mount(argv[1], argv[2], dev) < 0){
    fprintf(2, "mount: failed\n");
    exit(1);
  }

  printf("mount: %s on %s dev %d\n", argv[1], argv[2], dev);
  exit(0);
}
