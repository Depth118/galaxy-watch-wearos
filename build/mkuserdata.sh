set -e
I=/root/android/userdata-blank.img; rm -f $I; truncate -s 220M $I
mke2fs -q -t ext4 -b 4096 -O ^orphan_file,^metadata_csum_seed,^64bit -E root_owner=1000:1000 -L data $I
gzip -9 -c $I > /mnt/d/r800/data_zram/userdata-blank.img.gz
ls -l /mnt/d/r800/data_zram/userdata-blank.img.gz; md5sum /mnt/d/r800/data_zram/userdata-blank.img.gz
