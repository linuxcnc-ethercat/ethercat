#!/bin/bash

if [ $# -ne 3 ]; then
    echo "Need 3 arguments: 1) kernel source dir, 2) previous version, 3) version to add"
    exit 1
fi

KERNELDIR=$1
PREVER=$2
KERNELVER=$3

MACBDIR=drivers/net/ethernet/cadence

FILES="macb_main.c macb.h macb_ptp.c"

for f in $FILES; do
    echo $f
    o=${f/\./-$KERNELVER-orig.}
    e=${f/\./-$KERNELVER-ethercat.}
    cp -v $KERNELDIR/$MACBDIR/$f $o
    chmod 644 $o
    cp -v $o $e
    op=${f/\./-$PREVER-orig.}
    ep=${f/\./-$PREVER-ethercat.}
    diff -up $op $ep | patch -p1 --no-backup-if-mismatch $e
    sed -i s/$PREVER-ethercat.h/$KERNELVER-ethercat.h/ $e
    git add $o $e
    echo -e "\t$e \\" >> Makefile.am
    echo -e "\t$o \\" >> Makefile.am
done

echo "Don't forget to update Makefile.am!"
