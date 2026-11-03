# Commands sent to the xv6 shell for the ext2 smoke test.

mkdir /mnt
mount ext2 /mnt
cat /mnt/hello.c
cat /mnt/verylongfilename.txt
echo ext2-first > /mnt/a.txt
cat /mnt/a.txt
echo ext2-second > /mnt/a.txt
cat /mnt/a.txt
rm /mnt/a.txt
mkdir /mnt/d1
mkdir /mnt/d1/d2
echo ext2-nested > /mnt/d1/d2/f.txt
cat /mnt/d1/d2/f.txt
bigfile /mnt/bigfile
wc /mnt/hostbig.bin
echo EXT2_DONE
