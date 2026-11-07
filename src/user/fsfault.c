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
  // Root decoy: a stale cwd must not silently resolve relative names here.
  check(write_file("/keep", "root-keep") == 0, "write root decoy");
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
  int pchild, pst = -1, pwp;
  pipe(p);
  pchild = fork();
  if(pchild == 0){
    write(p[1], "ping", 4);
    exit(0);
  }
  n = read(p[0], buf, 4);
  pwp = wait(&pst);
  int pipeok = (n == 4 && memcmp(buf, "ping", 4) == 0 && pwp == pchild && pst == 0);
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
  // cwd handle is stale -> relative names must fail, even though "/keep" exists
  fd = open("keep", O_RDONLY);
  check(fd < 0, "stale cwd no longer resolves relative names");
  if(fd >= 0)
    close(fd);
  chdir("/");
  check(file_is("/fdir/keep", "committed-data"), "data committed before the crash survived");
  check(write_file("/after", "new-server") == 0 && file_is("/after", "new-server"),
        "new server serves reads and writes");
  check(open("/fdir/open2", O_RDONLY) >= 0, "file created before the crash exists");

  // --- 2b. a stale cwd fails relative paths, absolute paths still work ---
  // The order matters: the relative chdir below must stay last, because on
  // the buggy kernel it "cures" the cwd and would hide the fork bug.
  int kst = -1, cp, wpid = -1;

  check(write_file("/linkdir", "root-link") == 0, "write root link decoy");
  check(write_file("/fdir/link2", "relative-link") == 0, "write relative decoy");
  mkdir("/umnt");
  mkdir("/x");
  check(link("/hello", "/x/prog") == 0, "absolute link to /hello");
  check(chdir("/fdir") == 0 && file_is("keep", "committed-data"),
        "relative access works before the crash");

  spid = svc_pid(SVC_FS);
  kill_server(0);
  npid = wait_new_server(spid);
  check(npid > 0 && npid != spid, "server restarted for stale cwd test");

  // A. relative operations must fail and must not touch the root directory.
  fd = open("keep", O_RDONLY);
  check(fd < 0, "stale cwd rejects relative open");
  if(fd >= 0)
    close(fd);
  check(mkdir("newdir") < 0 && open("/newdir", O_RDONLY) < 0,
        "stale cwd rejects relative mkdir");
  check(unlink("linkdir") < 0 && file_is("/linkdir", "root-link"),
        "stale cwd rejects relative unlink");
  check(link("keep", "/linkabs") < 0 && open("/linkabs", O_RDONLY) < 0,
        "stale cwd rejects relative link source");
  check(link("/fdir/keep", "linkrel") < 0 && open("/linkrel", O_RDONLY) < 0,
        "stale cwd rejects relative link target");
  check(mknod("devx", 1, 1) < 0 && unlink("/devx") < 0,
        "stale cwd rejects relative mknod");
  check(mount("ext2", "umnt", 2) < 0, "stale cwd rejects relative mount");

  fd = open("/keep", O_RDONLY);
  check(fd >= 0, "absolute open works with stale cwd");
  if(fd >= 0)
    close(fd);
  check(file_is("/keep", "root-keep"), "absolute read works with stale cwd");
  check(link("/fdir/keep", "/linkabs2") == 0 &&
        file_is("/linkabs2", "committed-data"),
        "absolute link works with stale cwd");
  check(mount("ext2", "/umnt", 2) == 0, "absolute mount works with stale cwd");
  fd = open("/umnt/hello.c", O_RDONLY);
  check(fd >= 0 && read(fd, buf, 4) == 4, "absolute path into mount works");
  if(fd >= 0)
    close(fd);
  check(umount("umnt") < 0, "stale cwd rejects relative umount");

  // B. fork while the parent cwd is still stale: no root cwd for the child.
  cp = fork();
  if(cp == 0){
    fd = open("keep", O_RDONLY);
    check(fd < 0, "fork child does not inherit a root cwd");
    if(fd >= 0)
      close(fd);
    check(file_is("/keep", "root-keep"), "fork child absolute access works");
    check(chdir("/fdir") == 0 && file_is("keep", "committed-data"),
          "fork child recovers with an absolute chdir");
    exit(failed ? 1 : 0);
  }
  kst = -1;
  wpid = wait(&kst);
  check(cp > 0 && wpid == cp && kst == 0, "fork child cwd checks passed");

  cp = fork();
  if(cp == 0){
    char *av[] = { "x/prog", 0 };
    exec("x/prog", av);
    exit(42);
  }
  kst = -1;
  wpid = wait(&kst);
  check(cp > 0 && wpid == cp && kst == 42, "stale cwd rejects relative exec");

  cp = fork();
  if(cp == 0){
    char *av[] = { "hello", 0 };
    exec("hello", av);
    exit(43);
  }
  kst = -1;
  wpid = wait(&kst);
  check(cp > 0 && wpid == cp && kst == 0, "bare-name exec still falls back to root");

  // C. last stale operation: on the buggy kernel this rewrites the cwd.
  check(chdir("fdir") < 0, "stale cwd rejects relative chdir");

  // D. an absolute chdir restores relative access.
  check(chdir("/fdir") == 0 && file_is("keep", "committed-data"),
        "absolute chdir restores relative access");
  check(link("link2", "/linkabs3") == 0 &&
        file_is("/linkabs3", "relative-link"),
        "relative link works after recovery");
  check(chdir("/") == 0, "chdir back to root");
  check(umount("/umnt") == 0, "absolute umount succeeds");
  fd = open("/umnt/hello.c", O_RDONLY);
  check(fd < 0, "rejected relative umount had no effect");
  if(fd >= 0)
    close(fd);

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
  int lst = -1;
  int lwp = wait(&lst);
  check(i == 6 && lwp == child && lst == 0, "survived 6 server crashes under load");
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

#ifdef P0_TEST_HOOKS
  // --- 6. svc_xfer direction whitelist ------------------------------
  // The server runs the invalid-direction probes on the next read from
  // this process; both legal directions are exercised around them.
  {
    char xb[8];
    int xn, xfd;

    check(write_file("/xferlegal", "abcdefgh") == 0,
          "legal xfer from client works");
    check(file_is("/xferlegal", "abcdefgh"),
          "legal xfer to client works");
    xfd = open("/xferlegal", O_RDONLY);
    check(xfd >= 0, "open for xfer direction test");
    if(xfd >= 0){
      memset(xb, 'Z', sizeof(xb));
      xn = read(xfd, xb, 4);
      check(xn == 4 && memcmp(xb, "abcd", 4) == 0,
            "legal xfer before direction test");
      check(mount("__xferdir__", "/", 1) == 0,
            "xfer direction hook armed");
      memset(xb, 'Z', sizeof(xb));
      xn = read(xfd, xb, 4);
      check(xn == -1 && memcmp(xb, "ZZZZ", 4) == 0,
            "invalid xfer directions rejected without copying");
      memset(xb, 0, sizeof(xb));
      xn = read(xfd, xb, 4);
      check(xn == 4 && memcmp(xb, "efgh", 4) == 0,
            "legal xfer resumes after rejection");
      close(xfd);
    }
  }
#endif

  if(failed){
    printf("FSFAULT_FAIL\n");
    exit(1);
  }
  printf("FSFAULT_DONE\n");
  exit(0);
}
