#!/bin/bash
cd /home/pbsuser/pbspro
git checkout docker_builds
export PBS_INSTALL_DIR=/opt/pbs
cd /src/pbspro
./autogen.sh
./configure --prefix=$PBS_INSTALL_DIR --enable-ptl
make -j 7
