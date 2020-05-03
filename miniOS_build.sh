#!/bin/sh

now=$(date)
echo $now

CURRENT_DIR=`pwd`
echo ${CURRENT_DIR}

which gcc
gcc --version

function prepare_diskImg()
{
    if [ -f "hd60M_80M_img.tar.gz" ]
    then
        tar -zxvf "hd60M_80M_img.tar.gz"
    else
        echo "## ERROR: file hd60M_80M_img.tar.gz is required !!! ##"
        exit
    fi
}

function build()
{
    make all
}

prepare_diskImg
build
echo "== success! =="
