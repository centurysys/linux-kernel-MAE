#!/bin/sh

MOD_PATH=TARGET

#VERSION=$(grep VERSION Makefile | awk 'NR == 1 {printf $3}')
#PATCHLEVEL=$(grep PATCHLEVEL  Makefile | awk 'NR == 1 {printf $3}')
#SUBLEVEL=$(grep SUBLEVEL Makefile | awk 'NR == 1 {printf $3}')

#KERNEL_VERSION=${VERSION}.${PATCHLEVEL}.${SUBLEVEL}

INSTALL_MOD_PATH=$MOD_PATH make modules_install

COUNT=$(ls $MOD_PATH/lib/modules | wc -l)
if [ $COUNT -eq 1 ]; then
    KERNEL_VERSION=$(ls $MOD_PATH/lib/modules)
else
    echo "There are multiple versions as follows:"
    ls $MOD_PATH/lib/modules
    exit 1
fi

(cd $MOD_PATH/lib/modules && tar cvzf ${KERNEL_VERSION}.tar.gz $KERNEL_VERSION)
mv $MOD_PATH/lib/modules/${KERNEL_VERSION}.tar.gz ./

exit 0
