# First QEMU session: write files to ext2 and FAT32.

mkdir /mnt
mkdir /fat
mount ext2 /mnt
mount fat32 /fat
echo persist-ext2 > /mnt/persist.txt
cat /mnt/persist.txt
echo persist-fat > /fat/p.txt
cat /fat/p.txt
bigfile /mnt/bigfile keep
echo PERSIST_WRITE_DONE
