# Commands sent to the xv6 shell for VFS error-path stability.

mount ext2 /no_such_dir
mount ext2 /.
mount fat32 /..
mkdir /mnt
mkdir /mnt2
mkdir /fat
mount ext2 /mnt &
mount ext2 /mnt
mount ext2 /mnt2
mount fat32 /mnt
mount fat32 /fat &
mount fat32 /fat
ln /mnt/hello.c /fat/cross.txt
rm /mnt/no_such_file
rm /fat/no_such.txt
echo ERROR_PATH_DONE
