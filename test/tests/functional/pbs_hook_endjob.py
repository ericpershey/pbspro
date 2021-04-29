# coding: utf-8

# Copyright (C) 1994-2021 Altair Engineering, Inc.
# For more information, contact Altair at www.altair.com.
#
# This file is part of both the OpenPBS software ("OpenPBS")
# and the PBS Professional ("PBS Pro") software.
#
# Open Source License Information:
#
# OpenPBS is free software. You can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# OpenPBS is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
# License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Commercial License Information:
#
# PBS Pro is commercially licensed software that shares a common core with
# the OpenPBS software.  For a copy of the commercial license terms and
# conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
# Altair Legal Department.
#
# Altair's dual-license business model allows companies, individuals, and
# organizations to create proprietary derivative works of OpenPBS and
# distribute them - whether embedded or bundled with other software -
# under a commercial license agreement.
#
# Use of Altair's trademarks, including but not limited to "PBS™",
# "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
# subject to Altair's trademark licensing policies.

import time

from tests.functional import *
from tests.functional import JOB, MGR_CMD_SET, SERVER, TEST_USER, ATTR_h, Job


def endjob_hook(hook_func_name):
    import pbs
    import sys

    pbs.logmsg(pbs.LOG_DEBUG, "pbs.__file__:" + pbs.__file__)

    try:
        e = pbs.event()
        job = e.job
        # print additional info
        pbs.logjobmsg(
            job.id, 'endjob hook started for test %s' % (hook_func_name,))
        if hasattr(job, "resv") and job.resv:
            pbs.logjobmsg(
                job.id, 'endjob hook, resv:%s' % (job.resv.resvid,))
            pbs.logjobmsg(
                job.id,
                'endjob hook, resv_nodes:%s' % (job.resv.resv_nodes,))
            pbs.logjobmsg(
                job.id,
                'endjob hook, resv_state:%s' % (job.resv.reserve_state,))
        else:
            pbs.logjobmsg(job.id, 'endjob hook, resv:(None)')
        pbs.logjobmsg(job.id, 'endjob hook, job endtime:%d' % (job.endtime,))
        # pbs.logjobmsg(pbs.REVERSE_JOB_STATE.get(job.state))
        pbs.logjobmsg(
            job.id, 'endjob hook finished for test %s' % (hook_func_name,))
    except Exception as err:
        ty, _, tb = sys.exc_info()
        pbs.logmsg(
            pbs.LOG_DEBUG, str(ty) + str(tb.tb_frame.f_code.co_filename) +
            str(tb.tb_lineno))
        e.reject()
    else:
        e.accept()


@tags('hooks')
class TestHookEndJob(TestFunctional):
    node_cpu_count = 4
    job_array_num_subjobs = node_cpu_count
    job_time_success = 5
    job_time_qdel = 120
    resv_retry_time = 5
    resv_start_delay = 20
    resv_duration = 180

    def run_test_func(self, test_body_func, *args, **kwargs):
        """
        Setup the environment for running end job hook related tests, execute
        the test function and then perform common checks and clean up.
        """
        self.job = None
        self.job_id = None
        self.subjob_count = 0
        self.subjob_ids = []
        self.started_job_ids = []
        self.resv_id = None
        self.resv_queue = None
        self.resv_start_time = None
        self.scheduling_enabled = True
        self.hook_name = test_body_func.__name__

        self.logger.info("**************** HOOK START ****************")

        a = {
            'resources_available.ncpus': self.node_cpu_count,
        }
        self.server.manager(MGR_CMD_SET, NODE, a, self.mom.shortname)

        attrs = {
            'event': 'endjob',
            'enabled': 'True',
        }
        ret = self.server.create_hook(self.hook_name, attrs)
        self.assertEqual(
            ret, True, "Could not create hook %s" % self.hook_name)

        hook_body = generate_hook_body_from_func(endjob_hook, self.hook_name)
        ret = self.server.import_hook(self.hook_name, hook_body)
        self.assertEqual(
            ret, True, "Could not import hook %s" % self.hook_name)

        a = {
            'job_history_enable': 'True',
            'job_requeue_timeout': 1,
            'reserve_retry_time': self.resv_retry_time,
        }
        self.server.manager(MGR_CMD_SET, SERVER, a)

        self.log_start_time = int(time.time())
        test_body_func(*args, **kwargs)
        self.check_log_for_endjob_hook_messages()

        ret = self.server.delete_hook(self.hook_name)
        self.assertEqual(
            ret, True, "Could not delete hook %s" % self.hook_name)
        self.logger.info("**************** HOOK END ****************")

    def check_log_for_endjob_hook_messages(self, job_ids=None):
        jids = job_ids or [self.job_id] + self.subjob_ids
        for jid in jids:
            msg_logged = jid in self.started_job_ids
            # FIXME: max_attempts should be set to one (1), but semi-frequently
            # (35-ish percent of the time) it misses earlier messages despite
            # having already matched the last message expected in the log first
            # (ex: from the job array).  this is possibly a bug in log_match()
            # but requires further investigation.
            #
            # the unfortunate part of this characteristic is that one cannot
            # guarantee that a message was never logged, which means that one
            # cannot detect with 100% certainty that the hook script was
            # executed when it should not have been.
            max_attempts = 3 if msg_logged else 10
            self.server.log_match(
                '%s;endjob hook started for test %s' % (jid, self.hook_name),
                starttime=self.log_start_time, max_attempts=max_attempts,
                existence=msg_logged)
            self.server.log_match(
                '%s;endjob hook, resv:%s' % (jid, self.resv_queue or "(None)"),
                starttime=self.log_start_time, max_attempts=max_attempts,
                existence=msg_logged)
            self.server.log_match(
                '%s;endjob hook finished for test %s' % (jid, self.hook_name),
                starttime=self.log_start_time, max_attempts=max_attempts,
                existence=msg_logged)
        self.log_start_time = int(time.time())
        self.started_job_ids = []

    def job_submit(self, job_sleep_time=job_time_success, job_attrs={}):
        self.job = Job(TEST_USER, attrs=job_attrs)
        self.job.set_sleep_time(job_sleep_time)
        self.job_id = self.server.submit(self.job)

    def job_requeue(self, job_id=None):
        if self.scheduling_enabled:
            self.server.manager(MGR_CMD_SET, SERVER, {'scheduling': 'False'})
            self.scheduling_enabled = False
        jid = job_id or self.job_id
        self.server.rerunjob(jid, extend='force')
        self.job_verify_queued()

    def job_delete(self, job_id=None):
        jid = job_id or self.job_id
        self.server.delete(jid)
        # check that the substate is set to 91 (TERMINATED) which indicates
        # that the job was deleted
        self.server.expect(JOB, {'substate': 91}, extend='x', id=jid)

    def job_verify_queued(self, job_id=None):
        jid = job_id or self.job_id
        self.server.expect(JOB, {'job_state': 'Q'}, id=jid)

    def job_verify_started(self, job_id=None):
        if not self.scheduling_enabled:
            self.server.manager(MGR_CMD_SET, SERVER, {'scheduling': 'True'})
            self.scheduling_enabled = True
        jid = job_id or self.job_id
        self.server.expect(JOB, {'job_state': 'R'}, id=jid)
        self.started_job_ids.append(jid)

    def job_verify_ended(self, job_id=None):
        jid = job_id or self.job_id
        self.server.expect(JOB, {'job_state': 'F'}, extend='x', id=jid)

    def job_array_submit(
            self,
            job_sleep_time=job_time_success,
            avail_ncpus=job_array_num_subjobs,
            subjob_count=job_array_num_subjobs,
            subjob_ncpus=1,
            job_attrs={}):
        a = {
            ATTR_J: '0-' + str(subjob_count - 1),
            'Resource_List.select': '1:ncpus=' + str(subjob_ncpus),
        }
        a.update(job_attrs)
        self.job_submit(job_sleep_time, job_attrs=a)

        self.subjob_count = subjob_count
        self.subjob_ids = [
            self.job.create_subjob_id(self.job_id, i)
            for i in range(self.subjob_count)]

    def job_array_verify_started(self):
        self.server.expect(JOB, {'job_state': 'B'}, self.job_id)
        self.started_job_ids.append(self.job_id)

    def job_array_verify_subjobs_started(self):
        for jid in self.subjob_ids:
            self.job_verify_started(job_id=jid)

    def job_array_verify_subjobs_ended(self):
        for jid in self.subjob_ids:
            self.job_verify_ended(job_id=jid)

    def job_array_verify_ended(self):
        self.job_verify_ended()

    def resv_submit(
            self, user, resources, start_time, end_time, place='free',
            resv_attrs={}):
        """
        helper method to submit a reservation
        """
        self.resv_start_time = start_time
        a = {
            'Resource_List.select': resources,
            'Resource_List.place': place,
            'reserve_start': start_time,
            'reserve_end': end_time,
        }
        a.update(resv_attrs)
        resv = Reservation(user, a)
        self.resv_id = self.server.submit(resv)

    def resv_verify_confirmed(self):
        a = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
        self.server.expect(RESV, a, id=self.resv_id)
        self.resv_queue = self.resv_id.split('.')[0]
        self.server.status(RESV, 'resv_nodes')

    def resv_verify_started(self):
        self.logger.info('Sleeping until reservation starts')
        self.server.expect(
            RESV, {'reserve_state': (MATCH_RE, 'RESV_RUNNING|5')},
            id=self.resv_id, offset=self.resv_start_time-int(time.time()+1))

    @tags('hooks', 'smoke')
    def test_hook_endjob_run_single_job(self):
        """
        Run a single job to completion and verify that the end job hook was
        executed.
        """
        def endjob_run_single_job():
            self.job_submit()
            self.job_verify_started()
            self.job_verify_ended()

        self.run_test_func(endjob_run_single_job)

    def test_hook_endjob_run_array_job(self):
        """
        Run an array of jobs to completion and verify that the end job hook was
        executed for all subjobs and the array job.
        """
        def endjob_run_array_job():
            self.job_array_submit()
            self.job_array_verify_started()
            self.job_array_verify_subjobs_started()
            self.job_array_verify_subjobs_ended()
            self.job_array_verify_ended()

        self.run_test_func(endjob_run_array_job)

    def test_hook_endjob_run_array_job_in_resv(self):
        """
        Run an array of jobs to completion within a reservation and verify
        that the end job hook was executed for all subjobs and the array job.
        """
        def endjob_run_array_job_in_resv():
            resources = '1:ncpus=' + str(self.node_cpu_count)
            start_time = int(time.time()) + self.resv_start_delay
            end_time = start_time + self.resv_duration
            self.resv_submit(TEST_USER, resources, start_time, end_time)
            self.resv_verify_confirmed()

            a = {ATTR_queue: self.resv_queue}
            self.job_array_submit(
                subjob_ncpus=int(self.node_cpu_count/2), job_attrs=a)

            self.resv_verify_started()

            self.job_array_verify_started()
            self.job_array_verify_subjobs_started()
            self.job_array_verify_subjobs_ended()
            self.job_array_verify_ended()

        self.run_test_func(endjob_run_array_job_in_resv)

    def test_hook_endjob_rerun_single_job(self):
        """
        Start a single job, issue a rerun, and verify that the end job hook was
        executed for both runs.
        """
        def endjob_rerun_single_job():
            self.job_submit()
            self.job_verify_started()
            self.job_requeue()
            self.check_log_for_endjob_hook_messages()
            self.job_verify_started()
            self.job_verify_ended()

        self.run_test_func(endjob_rerun_single_job)

    def test_hook_endjob_rerun_array_job(self):
        # TODO: add code and description, or remove
        pass

    def test_hook_endjob_delete_running_single_job(self):
        """
        Run a single job, but delete before completion.  Verify that the end
        job hook was executed.
        """
        def endjob_delete_running_single_job():
            self.job_submit(job_sleep_time=self.job_time_qdel)
            self.job_verify_started()
            self.job_delete()
            self.job_verify_ended()

        self.run_test_func(endjob_delete_running_single_job)

    def test_hook_endjob_delete_running_array_job(self):
        """
        Run an array job, where all jobs get started but also deleted before
        # completion.  Verify that the end job hook was executed for all
        # subjobs and the array job.
        """
        # TODO: need to delete job array and maybe subjobs
        def endjob_delete_running_array_job():
            self.job_array_submit(job_sleep_time=self.job_time_qdel)
            self.job_array_verify_started()
            self.job_array_verify_subjobs_started()
            self.job_array_verify_subjobs_ended()
            self.job_array_verify_ended()

        self.run_test_func(endjob_delete_running_array_job)

#        num_array_jobs = 3
#        a = {'job_history_enable': 'True'}
#        self.server.manager(MGR_CMD_SET, SERVER, a)
#        a = {'resources_available.ncpus': num_array_jobs}
#        self.server.manager(MGR_CMD_SET, NODE, a, self.mom.shortname)
#        attr_j_str = '1-' + str(num_array_jobs)
#        j = Job(
#            TEST_USER,
#             attrs={ATTR_J: attr_j_str, 'Resource_List.select': 'ncpus=1'})
#        j.set_sleep_time(20)
#        jid = self.server.submit(j)
#
#        subjid = []
#        for i in range(num_array_jobs):
#            subjid.append(j.create_subjob_id(jid, i+1))
#
#        # check job array has begun
#        self.server.expect(JOB, {'job_state': 'B'}, jid)
#
#        # wait for the subjobs to begin running
#        for i in range(num_array_jobs):
#            self.server.expect(JOB, {'job_state': 'R'}, id=subjid[i])
#
#        # delete array job
#        self.server.delete(id=jid)
#
#        for i in range(num_array_jobs):
#            # check that the substate is set to 91 (TERMINATED) which
#            # indicates job was deleted
#            self.server.expect(JOB, {'substate': 91}, extend='x', id=subjid[i])
#
#        self.server.expect(JOB, {'substate': 91}, extend='x', id=jid)
#
#        ret = self.server.delete_hook(hook_name)
#        self.assertEqual(ret, True, "Could not delete hook %s" % hook_name)
#        self.server.log_match(hook_msg, starttime=start_time)
#        self.logger.info("**************** HOOK END ****************")

    def test_hook_endjob_delete_partial_running_array_job(self):
        """
        Run a single job, but delete before completion.  Verify that the end
        job hook was executed.
        """
        # TODO: need to delete job array and maybe started subjobs
        def endjob_delete_partial_running_array_job():
            a = {ATTR_queue: self.resv_queue}
            self.job_array_submit(
                job_sleep_time=self.job_time_qdel, job_attrs=a)
            self.job_array_verify_started()
            self.job_array_verify_subjobs_started()
            self.job_array_verify_subjobs_ended()
            self.job_array_verify_ended()

    def test_hook_endjob_delete_unstarted_single_job(self):
        # TODO: add code and description, or remove
        pass

    def test_hook_endjob_delete_unstarted_array_job(self):
        # TODO: add code and description, or remove
        pass

    def test_hook_endjob_delete_array_subjobs(self):
        """
        By creating an import hook, it executes a job hook.
        """
        self.logger.info("**************** HOOK START ****************")
        hook_name = "hook_endjob_delete_array"
        hook_msg = 'running %s' % hook_name
        hook_body = generate_hook_body_from_func(endjob_hook, hook_msg)
        attrs = {'event': 'endjob', 'enabled': 'True'}
        start_time = time.time()

        ret = self.server.create_hook(hook_name, attrs)
        self.assertEqual(ret, True, "Could not create hook %s" % hook_name)
        ret = self.server.import_hook(hook_name, hook_body)
        self.assertEqual(ret, True, "Could not import hook %s" % hook_name)

        a = {'job_history_enable': 'True'}
        self.server.manager(MGR_CMD_SET, SERVER, a)
        a = {'resources_available.ncpus': 1}
        self.server.manager(MGR_CMD_SET, NODE, a, self.mom.shortname)
        num_array_jobs = 3
        attr_j_str = '1-' + str(num_array_jobs)
        j = Job(TEST_USER, attrs={
            ATTR_J: attr_j_str, 'Resource_List.select': 'ncpus=1'})

        j.set_sleep_time(20)

        jid = self.server.submit(j)

        subjid = []
        subjid.append(jid)
        # subjid.append(jid)
        for i in range (1,(num_array_jobs+1)):
            subjid.append( j.create_subjob_id(jid, i) )

        # 1. check job array has begun
        self.server.expect(JOB, {'job_state': 'B'}, jid)

        for i in range (1,(num_array_jobs+1)):
            self.server.expect(JOB, {'job_state': 'R'},
                               id=subjid[i])
            # delete subjob
            self.server.delete(id=subjid[i])
            # check that the substate is set to 91 (TERMINATED) which
            # indicates job was deleted
            self.server.expect(JOB, {'substate': 91}, extend='x',
                               id=subjid[i])

        ret = self.server.delete_hook(hook_name)
        self.assertEqual(ret, True, "Could not delete hook %s" % hook_name)
        self.server.log_match(hook_msg, starttime=start_time)
        self.logger.info("**************** HOOK END ****************")

    def test_hook_endjob_force_delete_single_job(self):
        """
        By creating an import hook, it executes a job hook.
        """
        self.logger.info("**************** HOOK START ****************")
        hook_name = "hook_endjob_delete_force"
        hook_msg = 'running %s' % hook_name
        hook_body = generate_hook_body_from_func(endjob_hook, hook_msg)
        attrs = {'event': 'endjob', 'enabled': 'True'}
        start_time = time.time()

        ret = self.server.create_hook(hook_name, attrs)
        self.assertEqual(ret, True, "Could not create hook %s" % hook_name)
        ret = self.server.import_hook(hook_name, hook_body)
        self.assertEqual(ret, True, "Could not import hook %s" % hook_name)

        a = {'job_history_enable': 'True'}
        self.server.manager(MGR_CMD_SET, SERVER, a)

        j = Job(TEST_USER)
        j.set_sleep_time(4)
        jid = self.server.submit(j)

        # check job array is running
        self.server.expect(JOB, {'job_state': 'R'}, id=jid,
                                max_attempts=10)
        # delete job
        try:
            self.server.delete(jid, extend='force')
        except:
            self.logger.info("exception occurred during job delete attempt")

        # check that the substate is set to 91 (TERMINATED) which indicates
        # job was deleted
        self.server.expect(JOB, {'substate': 91}, extend='x',
                                offset=4, id=jid, max_attempts=10,
                                interval=2)
        ret = self.server.delete_hook(hook_name)

        # check that the hook was not ran since the job was deleted by force
        self.assertEqual(ret, True, "Could not delete hook %s" % hook_name)
        self.server.log_match(hook_msg, starttime=start_time)
        self.logger.info("**************** HOOK END ****************")

    def test_hook_endjob_force_delete_array_job(self):
        # TODO: write code
        pass
