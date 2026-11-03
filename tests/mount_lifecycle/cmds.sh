# Deterministic mount/unmount lifecycle and nested-topology checks.

mkdir /mnt
mkdir /fat
echo LOWER_MNT_VISIBLE > /mnt/lower.txt
echo LOWER_FAT_VISIBLE > /fat/lower.txt
umount /no_such_mount
umount /
umount /.
umount /..
mount ext2 /mnt 3
mount ext2 /mnt
rm /mnt
mkdir /mnt/nest
echo EXT2_CHILD_VISIBLE > /mnt/nest/lower.txt
mount fat32 /fat 2
mount fat32 /mnt/nest
echo FAT_NESTED_PERSIST > /mnt/nest/p.txt
rm /mnt/nest
umount /mnt
umount /mnt/nest
cat /mnt/nest/lower.txt
mount fat32 /mnt/nest
cat /mnt/nest/p.txt
umount /mnt/nest
umount /mnt
cat /mnt/lower.txt
mount ext2 /mnt
cat /mnt/nest/lower.txt
umount /mnt
mount fat32 /fat
cat /fat/p.txt
umount /fat
cat /fat/lower.txt
echo MOUNT_LIFECYCLE_DONE
