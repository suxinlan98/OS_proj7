# CPUS=3 lookup/open versus mount/unmount stress for both backends.

mkdir /mnt
mkdir /fat
mount ext2 /mnt
mount fat32 /fat
echo EXT2_STORM_PROBE > /mnt/probe.txt
echo FAT_STORM_PROBE > /fat/probe.txt
umountstress ext2 /mnt 2 /mnt/probe.txt 256
cat /mnt/probe.txt
umountstress fat32 /fat 3 /fat/probe.txt 256
cat /fat/probe.txt
echo MOUNT_STORM_DONE
