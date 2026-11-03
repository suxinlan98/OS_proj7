#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define SRC_FILENAME "hello.c"
#define DST_FILENAME "/mnt/hello.c"

int test_cross_fs_copy() {
  printf("将从 xv6 文件系统复制文件到 EXT2 文件系统\n\n");

  int src_fd = open(SRC_FILENAME, O_RDONLY);
  if(src_fd < 0) {
    printf("错误: 找不到源文件 %s\n", SRC_FILENAME);
    return 1;
  }

  int dst_fd = open(DST_FILENAME, O_WRONLY | O_CREATE);
  if(dst_fd < 0) {
    printf("错误: 无法创建目标文件 %s\n", DST_FILENAME);
    printf("请先执行 mount\n");
    close(src_fd);
    return 2;
  }

  char copy_buf[512];
  int copy_n;
  while((copy_n = read(src_fd, copy_buf, sizeof(copy_buf))) > 0) {
    if(write(dst_fd, copy_buf, copy_n) != copy_n) {
      printf("✗ 写入失败\n");
      close(src_fd);
      close(dst_fd);
      return 3;
    }
  }

  close(src_fd);
  close(dst_fd);

  if(copy_n < 0) {
    printf("✗ 读取失败\n");
    return 4;
  }

  printf("✓ 文件复制成功！\n\n");

  printf("验证复制结果:\n");
  int fd = open(DST_FILENAME, O_RDONLY);
  if(fd < 0) {
    printf("✗ 无法打开复制的文件\n");
    return 5;
  }

  static char verify_buf[256];
  int verify_n = read(fd, verify_buf, sizeof(verify_buf) - 1);
  if(verify_n < 0) {
    printf("✗ 读取复制的文件失败\n");
    close(fd);
    return 6;
  }
  verify_buf[verify_n] = '\0';
  close(fd);

  printf("✓ 复制成功！文件内容: \n%s\n", verify_buf);
  printf("现在您可以用以下命令查看复制的文件:\n");
  printf("cat %s\n", DST_FILENAME);

  printf("%s(): succeed\n", __func__);
  return 0;
}

int main(int argc, char *argv[]) {
  printf("\n--------- %s: begin... ---------------------{\n", argv[0]);
  test_cross_fs_copy();
  printf("--------- %s: finished -------------------}\n", argv[0]);
  return 0;
}
