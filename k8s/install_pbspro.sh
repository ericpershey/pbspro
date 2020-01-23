#!/bin/bash
cd /home/pbsuser/pbspro
make install
/opt/pbs/libexec/pbs_postinstall
chmod 4755 /opt/pbs/sbin/pbs_iff /opt/pbs/sbin/pbs_rcp
/opt/pbs/unsupported/pbs_config --make-ug