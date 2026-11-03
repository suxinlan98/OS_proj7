// SPDX-License-Identifier: BSD-2-Clause
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "usage: umount path\n");
    exit(1);
  }

  if(umount(argv[1]) < 0){
    fprintf(2, "umount: busy or invalid\n");
    exit(1);
  }

  printf("umount: %s\n", argv[1]);
  exit(0);
}
