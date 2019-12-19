#!/bin/bash
cd /home/pbsuser/pbspro
git checkout docker_builds
export PBS_INSTALL_DIR=/opt/pbs
cd /src/pbspro
./autogen.sh
./configure --prefix=$PBS_INSTALL_DIR --enable-ptl
make -j 7
sudo make install
sudo /opt/pbs/libexec/pbs_postinstall
sudo chmod 4755 /opt/pbs/sbin/pbs_iff /opt/pbs/sbin/pbs_rcp
sudo /opt/pbs/unsupported/pbs_config --make-ug
