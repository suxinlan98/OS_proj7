// fsfault: the file system service is an ordinary user process, so its
// failure must be contained.  This test kills it (and crashes it from the
// inside) while clients are working, and checks that
//   * clients get an error instead of hanging or crashing the kernel,
//   * handles that belonged to the dead server are rejected,
//   * init starts a new server and data committed before the crash is intact,
//   * the system keeps going through repeated failures.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define SVC_FS 1

static int failed;

static void
check(int ok, char *what)
{
  if(ok){
    printf("fsfault: %s: OK\n", what);
  } else {
    printf("fsfault: %s: FAIL\n", what);
    failed = 1;
  }
}

// wait until a server other than oldpid is serving; returns its pid
static int
wait_new_server(int oldpid)
{
  int i, p;
  for(i = 0; i < 500; i++){
    p = svc_pid(SVC_FS);
    if(p > 0 && p != oldpid){
      // alive and answering?
      int fd = open("/README", O_RDONLY);
      if(fd >= 0){
        close(fd);
        return p;
      }
    }
    pause(1);
  }
  return 0;
}

static int
write_file(char *path, char *s)
{
  int fd = open(path, O_CREATE | O_RDWR | O_TRUNC);
  if(fd < 0)
    return -1;
  int n = strlen(s);
  if(write(fd, s, n) != n){
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

static int
file_is(char *path, char *s)
{
  char buf[64];
  int n, fd = open(path, O_RDONLY);
  if(fd < 0)
    return 0;
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n < 0)
    return 0;
  buf[n] = 0;
  return strcmp(buf, s) == 0;
}

static void
kill_server(int how)
{
  if(how == 0){
    kill(svc_pid(SVC_FS));
  } else {
    // crash from the inside: null dereference in the server
    mount("__crash__", "/", 1);
  }
}

int
main(int argc, char *argv[])
{
  int spid, npid, fd, fd2, i, n;
  char buf[16];
  int p[2];
#ifdef P0_TEST_HOOKS
  int rspid, rnpid;
#endif

  spid = svc_pid(SVC_FS);
  check(spid > 0, "file system server is a separate process");
  check(spid != getpid(), "server pid differs from client");

  // Committed state, an open file, and a current directory that belong to
  // the server instance about to die.
  mkdir("fdir");
  check(write_file("/fdir/keep", "committed-data") == 0, "write before crash");
  chdir("fdir");
  fd = open("/fdir/keep", O_RDONLY);
  check(fd >= 0, "open before crash");
  fd2 = open("/fdir/open2", O_CREATE | O_RDWR);
  check(fd2 >= 0, "create before crash");

#ifdef P0_TEST_HOOKS
  // --- 0. restart is init-only --------------------------------------
  // Hold init inside svc_restart so the service stays down and the denial
  // can be observed without racing the supervisor's restart.
  rspid = svc_pid(SVC_FS);
  check(svc_restart(-2) == 0, "test hook armed");
  kill(rspid);
  for(i = 0; i < 500 && svc_pid(SVC_FS) != 0; i++)
    pause(1);
  check(svc_pid(SVC_FS) == 0, "service stays down while init is held");
  check(svc_restart(SVC_FS) < 0, "restart denied while service down");
  pause(5);
  check(svc_pid(SVC_FS) == 0, "no server created by unauthorized restart");
  check(svc_restart(-1) == 0, "test hook released");
  rnpid = wait_new_server(rspid);
  check(rnpid > 0 && rnpid != rspid, "init restart still works");
#endif

  // --- 1. kill the server -------------------------------------------
  // section 0 restarted the server, so re-read the pid of the one to kill
  spid = svc_pid(SVC_FS);
  kill_server(0);
  // The kernel and unrelated services must not care: pipes, fork, console.
  pipe(p);
  if(fork() == 0){
    write(p[1], "ping", 4);
    exit(0);
  }
  n = read(p[0], buf, 4);
  wait(0);
  int pipeok = (n == 4 && memcmp(buf, "ping", 4) == 0);
  close(p[0]);
  close(p[1]);

  npid = wait_new_server(spid);
  // reported only now: init and the server print while they restart, and
  // concurrent console output would interleave with this line
  check(pipeok, "pipes/fork work while file system is down");
  check(npid > 0, "init restarted the file system server");
  check(npid != spid, "new server is a new process");

  // --- 2. state after recovery --------------------------------------
  check(read(fd, buf, sizeof(buf)) < 0, "stale open file is rejected");
  check(write(fd2, "x", 1) < 0, "stale file write is rejected");
  struct stat st;
  check(fstat(fd, &st) < 0, "stale fstat is rejected");
  close(fd);          // must not hurt the new server
  close(fd2);
  // cwd handle is stale -> relative names resolve from "/"
  check(open("keep", O_RDONLY) < 0, "stale cwd no longer resolves relative names");
  chdir("/");
  check(file_is("/fdir/keep", "committed-data"), "data committed before the crash survived");
  check(write_file("/after", "new-server") == 0 && file_is("/after", "new-server"),
        "new server serves reads and writes");
  check(open("/fdir/open2", O_RDONLY) >= 0, "file created before the crash exists");

  // --- 3. crash from inside the server (null dereference) -----------
  spid = svc_pid(SVC_FS);
  kill_server(1);
  npid = wait_new_server(spid);
  check(npid > 0 && npid != spid, "recovered from a crash inside the server");
  check(file_is("/after", "new-server"), "data intact after internal crash");

  // --- 4. repeated failures under load ------------------------------
  // A child hammers the file system; the server is killed 6 times.
  int child = fork();
  if(child == 0){
    char name[16];
    for(i = 0; i < 2000; i++){
      name[0] = '/'; name[1] = 'l'; name[2] = '0' + (i % 8); name[3] = 0;
      int f = open(name, O_CREATE | O_RDWR);
      if(f >= 0){
        write(f, "0123456789abcdef", 16);
        close(f);
      }
      if(i % 7 == 0)
        unlink(name);
    }
    exit(0);
  }
  for(i = 0; i < 6; i++){
    pause(3);
    spid = svc_pid(SVC_FS);
    if(spid > 0)
      kill(spid);
    npid = wait_new_server(spid);
    if(npid <= 0)
      break;
  }
  check(i == 6, "survived 6 server crashes under load");
  wait(0);
  check(file_is("/fdir/keep", "committed-data"), "old data intact after repeated crashes");
  check(write_file("/final", "ok") == 0 && file_is("/final", "ok"), "file system fully usable at the end");

  // --- 5. mounts are server state: gone after restart, redo works ---
  mkdir("/mnt2");
  check(mount("ext2", "/mnt2", 2) == 0, "mount ext2");
  spid = svc_pid(SVC_FS);
  kill(spid);
  npid = wait_new_server(spid);
  check(npid > 0, "restart with ext2 mounted");
  check(mount("ext2", "/mnt2", 2) == 0, "remount ext2 after restart");
  fd = open("/mnt2/hello.c", O_RDONLY);
  check(fd >= 0 && read(fd, buf, 4) == 4, "read ext2 file after restart");
  if(fd >= 0)
    close(fd);
  check(umount("/mnt2") == 0, "umount ext2");

  if(failed){
    printf("FSFAULT_FAIL\n");
    exit(1);
  }
  printf("FSFAULT_DONE\n");
  exit(0);
}
