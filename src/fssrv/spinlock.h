// The file system server handles one request at a time, so its locks
// compile to nothing; the code is shared with the original kernel version.
struct spinlock { int locked; };
