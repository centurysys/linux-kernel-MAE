#!/bin/sh

TARGET=$1

usage()
{
    echo "Usage: $0 [LS1012ARDB|LS1012AFRDM|LS1012AFRWY|LS1046ARDB|ACORN|PINECONE]" >&2
    exit 1
}

case $TARGET in
    LS1012ARDB | LS1012AFRDM | LS1012AFRWY | LS1046ARDB | ACORN | PINECONE)
        mkimage -A arm64 -O linux -C gzip -T kernel -a 0x80080000 -e 0x80080000 -n $TARGET -d arch/arm64/boot/Image.gz uImage
        ;;
    *)
        usage
        ;;
esac

exit $?
