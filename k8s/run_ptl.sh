#!/bin/bash -x
cd $PBS_SOURCE_DIR
sudo -u $PBS_SOURCE_USER make maintainer-clean
sudo -u $PBS_SOURCE_USER ./autogen.sh
export PBS_BUILD_DIR=/tmp/build-pbspro
export PBS_INSTALL_BASE=/opt
export PBS_INSTALL_DIR=${PBS_INSTALL_BASE}/pbs
export PTL_INSTALL_DIR=${PBS_INSTALL_BASE}/ptl
export PATH=${PBS_INSTALL_DIR}/bin:${PTL_INSTALL_DIR}/bin:$PATH
export PYTHON3_MAJOR_VERSION=$(python3 --version | \
  sed -r -e 's/Python ([[:digit:]]+)\.([[:digit:]]+)\.[[:digit:]]+/\1.\2/')
export PYTHONPATH=${PTL_INSTALL_DIR}/lib/python${PYTHON3_MAJOR_VERSION}/site-packages:${PYTHONPATH}
sudo rm -rf ${PBS_BUILD_DIR} && sudo mkdir -p ${PBS_BUILD_DIR} && cd ${PBS_BUILD_DIR}
sudo ${PBS_SOURCE_DIR}/configure --prefix=${PBS_INSTALL_DIR} --enable-ptl
cd ${PBS_BUILD_DIR}
sudo make -j 8
cd ${PBS_BUILD_DIR}
sudo /etc/init.d/pbs stop
cd ${PBS_BUILD_DIR}
sudo make install
sudo /opt/pbs/libexec/pbs_postinstall
sudo chmod 4755 /opt/pbs/sbin/pbs_iff /opt/pbs/sbin/pbs_rcp
sudo /opt/pbs/unsupported/pbs_config --make-ug
sudo /etc/init.d/pbs start
cd ${PTL_INSTALL_DIR}/tests
time ${PTL_INSTALL_DIR}/bin/pbs_benchpress \
	--db-name ${PBS_BUILD_DIR}/test/tests/ptl_test_results.json \
	-f ${PTL_INSTALL_DIR}/tests/pbs_smoketest.py \
	-o ${PBS_BUILD_DIR}/test/tests/ptl_output_smoke_test_finished_jobs.txt \
	-t SmokeTest.test_round_robin

#time $PTL_INSTALL_DIR/bin/pbs_benchpress --db-name $PBS_BUILD_DIR/test/tests/ptl_test_results.json -f $PTL_INSTALL_DIR/tests/functional/pbs_hook_management.py
#time $PTL_INSTALL_DIR/bin/pbs_benchpress --db-name $PBS_BUILD_DIR/test/tests/ptl_test_results.json -f $PTL_INSTALL_DIR/tests/functional/pbs_hooksmoketest.py
#time $PTL_INSTALL_DIR/bin/pbs_benchpress --db-name $PBS_BUILD_DIR/test/tests/ptl_test_results.json -f $PTL_INSTALL_DIR/tests/pbs_smoketest.py -o $PBS_BUILD_DIR/test/tests/ptl_output_smoke_test_finished_jobs.txt -t SmokeTest.test_finished_jobs
#time $PTL_INSTALL_DIR/bin/pbs_benchpress --db-name $PBS_BUILD_DIR/test/tests/ptl_test_results.json -f $PTL_INSTALL_DIR/tests/pbs_smoketest.py -o $PBS_BUILD_DIR/test/tests/ptl_output_smoke_test_add_server_dyn_res.txt -t SmokeTest.test_add_server_dyn_res
#time $PTL_INSTALL_DIR/bin/pbs_benchpress --db-name $PBS_BUILD_DIR/test/tests/ptl_test_results.json -f $PTL_INSTALL_DIR/tests/pbs_smoketest.py -o $PBS_BUILD_DIR/test/tests/ptl_output_smoke_test_resume_job_array_with_preempt.txt -t SmokeTest.test_resume_job_array_with_preempt
#time $PTL_INSTALL_DIR/bin/pbs_benchpress --db-name $PBS_BUILD_DIR/test/tests/ptl_test_results.json -f $PTL_INSTALL_DIR/tests/pbs_smoketest.py -o $PBS_BUILD_DIR/test/tests/ptl_output_smoke_test_shrink_to_fit_resv_barrier.txt -t SmokeTest.test_shrink_to_fit_resv_barrier
#time $PTL_INSTALL_DIR/bin/pbs_benchpress --db-name $PBS_BUILD_DIR/test/tests/ptl_test_results.json -f $PTL_INSTALL_DIR/tests/pbs_smoketest.py -o $PBS_BUILD_DIR/test/tests/ptl_output_smoke_test_submit_job_with_script.txt -t SmokeTest.test_submit_job_with_script
#time $PTL_INSTALL_DIR/bin/pbs_benchpress --db-name $PBS_BUILD_DIR/test/tests/ptl_test_results.json -f $PTL_INSTALL_DIR/tests/pbs_smoketest.py -o $PBS_BUILD_DIR/test/tests/ptl_output_smoke_test_resource_delete.txt -t SmokeTest.test_resource_delete
#time $PTL_INSTALL_DIR/bin/pbs_benchpress --db-name $PBS_BUILD_DIR/test/tests/ptl_test_results.json -f $PTL_INSTALL_DIR/tests/pbs_smoketest.py -o $PBS_BUILD_DIR/test/tests/ptl_output_smoke.txt |& tee $PBS_BUILD_DIR/test/tests/out.txt
#time sudo PYTHONPATH=$PYTHONPATH $PTL_INSTALL_DIR/bin/pbs_benchpress \
#  --tags smoke |& \
#  tee test-`date '+%Y%m%d%H%M%S'`.log
#time sudo PYTHONPATH=$PYTHONPATH $PTL_INSTALL_DIR/bin/pbs_benchpress \
#  -f $PTL_INSTALL_DIR/tests/pbs_smoketest.py \
#  -t SmokeTest.test_resource_delete |& \
#  tee test-`date '+%Y%m%d%H%M%S'`.log
#time sudo PYTHONPATH=$PYTHONPATH $PTL_INSTALL_DIR/bin/pbs_benchpress \
#  --tags smoke,hooks |& \
#  tee test-`date '+%Y%m%d%H%M%S'`.log
