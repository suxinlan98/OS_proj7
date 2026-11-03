# Commands sent to the xv6 shell for the public VFS test.
# Reproduces the pinned upstream evaluation flow (see ../VFS_TESTS_PIN.txt):
# mount ext2 on /mnt, then run the upstream cases from /tests.

mkdir /mnt
mount ext2 /mnt
cd tests
ls
cat cmds.sh | sh
echo PUBLIC_VFS_DONE
