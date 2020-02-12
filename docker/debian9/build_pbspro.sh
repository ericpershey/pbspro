#!/bin/bash
cd /home/pbsuser/pbspro
echo `pwd`
ls
export PBS_INSTALL_DIR=/opt/pbs
./autogen.sh
./configure --prefix=$PBS_INSTALL_DIR --enable-ptl
make -j 7
