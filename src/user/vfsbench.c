#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define BUFSIZE 512
#define TOTAL (1024 * 1024)

static int total = TOTAL;

int
main(int argc, char *argv[])
{
  char *path = "/vfsbench.tmp";
  int iters = 3;
  char buf[BUFSIZE];
  int fd, i, off, n, t0, t1;

  if(argc > 1)
    path = argv[1];
  if(argc > 2)
    iters = atoi(argv[2]);
  if(argc > 3)
    total = atoi(argv[3]) * 1024;

  memset(buf, 'x', sizeof(buf));

  t0 = uptime();
  for(i = 0; i < iters; i++){
    if((fd = open(path, O_WRONLY | O_CREATE | O_TRUNC)) < 0){
      printf("vfsbench: open-for-write %s failed\n", path);
      exit(1);
    }
    for(off = 0; off < total; off += BUFSIZE){
      if(write(fd, buf, BUFSIZE) != BUFSIZE){
        printf("vfsbench: write failed at %d\n", off);
        exit(1);
      }
    }
    close(fd);
    unlink(path);
  }
  t1 = uptime();
  printf("vfsbench: write %d x %d bytes = %d ticks\n", iters, total, t1 - t0);

  if((fd = open(path, O_WRONLY | O_CREATE | O_TRUNC)) < 0){
    printf("vfsbench: prepare %s failed\n", path);
    exit(1);
  }
  for(off = 0; off < total; off += BUFSIZE){
    if(write(fd, buf, BUFSIZE) != BUFSIZE){
      printf("vfsbench: prepare write failed at %d\n", off);
      exit(1);
    }
  }
  close(fd);

  t0 = uptime();
  for(i = 0; i < iters; i++){
    if((fd = open(path, O_RDONLY)) < 0){
      printf("vfsbench: open-for-read %s failed\n", path);
      exit(1);
    }
    while((n = read(fd, buf, BUFSIZE)) > 0)
      ;
    if(n < 0){
      printf("vfsbench: read failed\n");
      exit(1);
    }
    close(fd);
  }
  t1 = uptime();
  printf("vfsbench: read %d x %d bytes = %d ticks\n", iters, total, t1 - t0);

  unlink(path);
  exit(0);
}
