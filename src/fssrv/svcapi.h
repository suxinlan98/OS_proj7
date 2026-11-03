// System calls the server uses (same as user/user.h, which cannot be
// included together with defs.h).
int svc_register(int);
int svc_recv(int, void*);
int svc_reply(int, long, long, long);
int svc_xfer(int, int, uint64, void*, int);
int blk_rw(int, int, void*, int);
int exit(int) __attribute__((noreturn));
