# Large-file verification across repeated unmount/remount cycles.

mkdir /mnt
mkdir /fat
echo EXT2_LOWER_LAYER > /mnt/lower.txt
echo FAT_LOWER_LAYER > /fat/lower.txt
mount ext2 /mnt
mount fat32 /fat
bigfile /mnt/large.bin keep
bigfile /fat/large.bin keep
umount /mnt
umount /fat
cat /mnt/lower.txt
cat /fat/lower.txt
mount ext2 /mnt
mount fat32 /fat
bigfile /mnt/large.bin verify
bigfile /fat/large.bin verify
umount /mnt
umount /fat
cat /mnt/lower.txt
cat /fat/lower.txt
mount ext2 /mnt
mount fat32 /fat
bigfile /mnt/large.bin verify
bigfile /fat/large.bin verify
umount /mnt
umount /fat
cat /mnt/lower.txt
cat /fat/lower.txt
mount ext2 /mnt
mount fat32 /fat
bigfile /mnt/large.bin verify
bigfile /fat/large.bin verify
echo REMOUNT_IO_DONE
