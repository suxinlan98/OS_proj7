# Commands sent to the xv6 shell for the FAT32 smoke test.
# FAT32 support is intentionally tested with 8.3 short names.

mkdir /fat
mount fat32 /fat
ls /fat
echo fat32-one > /fat/a.txt
cat /fat/a.txt
echo fat32-two > /fat/a.txt
cat /fat/a.txt
echo > /fat/e.txt
cat /fat/e.txt
rm /fat/a.txt
rm /fat/e.txt
mkdir /fat/d1
mkdir /fat/d1/d2
echo fat-nested > /fat/d1/d2/f.txt
cat /fat/d1/d2/f.txt
ls /fat
cat /fat/longfilename.txt
cat /fat/shortlong.txt
cat /fat/longfi~1.txt
echo FAT32_DONE
