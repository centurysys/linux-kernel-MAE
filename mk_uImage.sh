#!/bin/sh

mkimage -A arm64 -O linux -C gzip -T kernel -a 0x80080000 -e 0x80080000 -n LS1046ARDB -d arch/arm64/boot/Image.gz uImage

exit $?
