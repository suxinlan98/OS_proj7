# Commands sent to the xv6 shell for VFS mount/link semantics.

echo VFS_PARENT_OK > /parent.txt
mkdir /mnt
mkdir /fat
echo XV6_MNT_BASE > /mnt/base.txt
echo XV6_FAT_BASE > /fat/base.txt
mount ext2 /mnt
mount fat32 /fat
cat /echo > /mnt/echo
/mnt/echo EXT2_EXEC_OK
cat /echo > /fat/echo
/fat/echo FAT32_EXEC_OK
cat /mnt/../parent.txt
cat /fat/../parent.txt
ln /mnt/hello.c /mnt/hlink.c
cat /mnt/hlink.c
echo fat-link > /fat/a.txt
ln /fat/a.txt /fat/b.txt
unlinkopen /mnt/uop.txt
unlinkopen /fat/uop.txt
cd /mnt
umount /mnt
cd /
umount /mnt
cat /mnt/base.txt
mount ext2 /mnt
cat /mnt/hello.c
cd /fat
umount /fat
cd /
umount /fat
cat /fat/base.txt
mount fat32 /fat
/fat/echo FAT32_REMOUNT_OK
echo UMOUNT_SEMANTICS_OK
echo SEMANTICS_DONE
