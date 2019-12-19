#!/bin/bash
export PBS_INSTALL_DIR=/opt/pbs
cd /src/pbspro
./autogen.sh
./configure --prefix=$PBS_INSTALL_DIR --enable-ptl
make -j 7
make install
/opt/pbs/libexec/pbs_postinstall
chmod 4755 /opt/pbs/sbin/pbs_iff /opt/pbs/sbin/pbs_rcp
/opt/pbs/unsupported/pbs_config --make-ug
