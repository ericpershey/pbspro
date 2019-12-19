#!/bin/bash
export PBS_INSTALL_DIR=/opt/pbs
cd /src/pbspro
./autogen.sh
./configure --prefix=$PBS_INSTALL_DIR --enable-ptl
make dist
mkdir /root/rpmbuild /root/rpmbuild/SOURCES /root/rpmbuild/SPECS
cp pbspro-*.tar.gz /root/rpmbuild/SOURCES
cp pbspro-rpmlintrc /root/rpmbuild/SOURCES
cp pbspro.spec /root/rpmbuild/SPECS
cd /root/rpmbuild/SPECS
rpmbuild -ba pbspro.spec
