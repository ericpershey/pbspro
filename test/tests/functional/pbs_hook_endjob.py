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
    job_time_success = 3
    job_time_qdel = 100
    job_array_num_subjobs = 3

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

        self.logger.info("**************** HOOK START ****************")
        hook_name = test_body_func.__name__

        attrs = {'event': 'endjob', 'enabled': 'True'}
        ret = self.server.create_hook(hook_name, attrs)
        self.assertEqual(ret, True, "Could not create hook %s" % hook_name)

        hook_body = generate_hook_body_from_func(endjob_hook, hook_name)
        ret = self.server.import_hook(hook_name, hook_body)
        self.assertEqual(ret, True, "Could not import hook %s" % hook_name)

        a = {'job_history_enable': 'True'}
        self.server.manager(MGR_CMD_SET, SERVER, a)

        self.start_time = int(time.time())
        test_body_func(*args, **kwargs)

        for jid in [self.job_id] + self.subjob_ids:
            msg_logged = jid in self.started_job_ids
            self.server.log_match(
                '%s;endjob hook started for test %s' % (jid, hook_name),
                starttime=self.start_time, max_attempts=3,
                existence=msg_logged)
            self.server.log_match(
                '%s;endjob hook finished for test %s' % (jid, hook_name),
                starttime=self.start_time, max_attempts=3,
                existence=msg_logged)

        ret = self.server.delete_hook(hook_name)
        self.assertEqual(ret, True, "Could not delete hook %s" % hook_name)
        self.logger.info("**************** HOOK END ****************")

    def submit_reservation(self, select, start, end, user, rrule=None,
                           place='free', extra_attrs=None):
        """
        helper method to submit a reservation
        """
        a = {'Resource_List.select': select,
             'Resource_List.place': place,
             'reserve_start': start,
             'reserve_end': end,
             }
        if rrule is not None:
            tzone = self.get_tz()
            a.update({ATTR_resv_rrule: rrule, ATTR_resv_timezone: tzone})

        if extra_attrs:
            a.update(extra_attrs)
        r = Reservation(user, a)

        return self.server.submit(r)

    def job_submit(
            self,
            job_sleep_time=job_time_success,
            job_attrs={}):
        self.job = Job(TEST_USER, attrs=job_attrs)
        self.job.set_sleep_time(job_sleep_time)
        self.job_id = self.server.submit(self.job)

    def job_verify_started(self, job_id=None):
        jid = job_id or self.job_id
        self.server.expect(JOB, {'job_state': 'R'}, id=jid)
        self.started_job_ids.append(jid)

    def job_verify_deleted(self, job_id=None):
        jid = job_id or self.job_id
        # check that the substate is set to 91 (TERMINATED) which indicates
        # that the job was deleted
        self.server.expect(JOB, {'job_state': 91}, extend='x', id=jid)

    def job_verify_ended(self, job_id=None):
        jid = job_id or self.job_id
        self.server.expect(JOB, {'job_state': 'F'}, extend='x', id=jid)

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

    def job_array_submit(
            self,
            job_sleep_time=job_time_success,
            avail_ncpus=job_array_num_subjobs,
            subjob_count=job_array_num_subjobs,
            subjob_ncpus=1,
            job_attrs={}):
        a = {'resources_available.ncpus': avail_ncpus}
        self.server.manager(MGR_CMD_SET, NODE, a, self.mom.shortname)

        a = {}
        a.update(job_attrs)
        a[ATTR_J] = '0-' + str(subjob_count - 1)
        a['Resource_List.select'] = 'ncpus=' + str(subjob_ncpus)
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

    def test_hook_endjob_resv(self):
        """
        Run a single job to completion and verify that the end job hook was
        executed.
        """
        def endjob_resv():
            a = {'reserve_retry_time': 5}
            self.server.manager(MGR_CMD_SET, SERVER, a)

            now = int(time.time())
            rid1 = self.submit_reservation(
                user=TEST_USER, select='1:ncpus=1', start=now + 30,
                end=now + 90)

            a = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
            self.server.expect(RESV, a, id=rid1)
            resv_queue = rid1.split('.')[0]
            self.server.status(RESV, 'resv_nodes')

            num_array_jobs = 3
            a = {'resources_available.ncpus': 1}
            self.server.manager(MGR_CMD_SET, NODE, a, self.mom.shortname)
        attr_j_str = '1-' + str(num_array_jobs)
        j = Job(TEST_USER, attrs={
            ATTR_J: attr_j_str, 'Resource_List.select': 'ncpus=1',
            ATTR_queue: resv_queue})

        j.set_sleep_time(4)
        jid = self.server.submit(j)

        subjid = []
        subjid.append(jid)
        #subjid.append(jid)
        for i in range (1,(num_array_jobs+1)):
            subjid.append( j.create_subjob_id(jid, i) )

        self.logger.info('Sleeping until reservation starts')
        self.server.expect(RESV,
                           {'reserve_state': (MATCH_RE, 'RESV_RUNNING|5')},
                           id=rid1, offset=self.start_time - int(time.time()))

        # 1. check job array has begun
        self.server.expect(JOB, {'job_state': 'B'}, jid)

        for i in range (1,(num_array_jobs+1)):
            self.server.expect(JOB, {'job_state': 'R'},
                               id=subjid[i], offset=4)

        self.server.expect(JOB, {'job_state': 'F'}, extend='x',
                                offset=4, id=jid, interval=5)

        ret = self.server.delete_hook(hook_name)
        self.assertEqual(ret, True, "Could not delete hook %s" % hook_name)
        self.server.log_match(hook_msg, starttime=start_time,
                              max_attempts=10)
        self.logger.info("**************** HOOK END ****************")

    def test_hook_endjob_reque(self):
        """
        By creating an import hook, it executes a job hook.
        """
        self.logger.info("**************** HOOK START ****************")
        hook_name = "hook_endjob_reque"
        hook_msg = 'running %s' % hook_name
        hook_body = generate_hook_body_from_func(endjob_hook, hook_msg)
        attrs = {'event': 'endjob', 'enabled': 'True'}
        start_time = time.time()

        ret = self.server.create_hook(hook_name, attrs)
        self.assertEqual(ret, True, "Could not create hook %s" % hook_name)
        ret = self.server.import_hook(hook_name, hook_body)
        self.assertEqual(ret, True, "Could not import hook %s" % hook_name)

        a = {'job_history_enable': 'True',
            'job_requeue_timeout': 1}
        self.server.manager(MGR_CMD_SET, SERVER, a)

        j = Job(TEST_USER, attrs={ATTR_N: 'job_requeue_timeout'})
        j.set_sleep_time(4)
        jid = self.server.submit(j)
        self.server.rerunjob(jid, extend='force')
        self.server.expect(JOB, {'job_state': 'R', 'substate': 42}, id=jid,
                                max_attempts=10, interval=2)

        self.server.expect(JOB, {'job_state': 'F'}, extend='x',
                                offset=4, id=jid, max_attempts=10,
                                interval=2)
        ret = self.server.delete_hook(hook_name)
        self.assertEqual(ret, True, "Could not delete hook %s" % hook_name)
        self.server.log_match(hook_msg, starttime=start_time)
        self.logger.info("**************** HOOK END ****************")

    def test_hook_endjob_delete(self):
        """
        By creating an import hook, it executes a job hook.
        """
        self.logger.info("**************** HOOK START ****************")
        hook_name = "hook_endjob_delete"
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
            self.server.delete(jid)
        except:
            self.logger.info("exception occurred during job delete attampt")

        # check that the substate is set to 91 (TERMINATED) which indicates
        # job was deleted
        self.server.expect(JOB, {'substate': 91}, extend='x',
                                offset=4, id=jid, max_attempts=10,
                                interval=2)
        ret = self.server.delete_hook(hook_name)
        self.assertEqual(ret, True, "Could not delete hook %s" % hook_name)
        self.server.log_match(hook_msg, starttime=start_time)
        self.logger.info("**************** HOOK END ****************")

    def test_hook_endjob_delete_running_array_job(self):
        """
        By creating an import hook, it executes a job hook.
        """

        def endjob_delete_running_array_job():
            self.job_array_submit(
                job_sleep_time=self.job_time_qdel)
            self.job_array_verify_started()

        num_array_jobs = 3
        a = {'job_history_enable': 'True'}
        self.server.manager(MGR_CMD_SET, SERVER, a)
        a = {'resources_available.ncpus': num_array_jobs}
        self.server.manager(MGR_CMD_SET, NODE, a, self.mom.shortname)
        attr_j_str = '1-' + str(num_array_jobs)
        j = Job(
            TEST_USER,
             attrs={ATTR_J: attr_j_str, 'Resource_List.select': 'ncpus=1'})
        j.set_sleep_time(20)
        jid = self.server.submit(j)

        subjid = []
        for i in range(num_array_jobs):
            subjid.append(j.create_subjob_id(jid, i+1))

        # check job array has begun
        self.server.expect(JOB, {'job_state': 'B'}, jid)

        # wait for the subjobs to begin running
        for i in range(num_array_jobs):
            self.server.expect(JOB, {'job_state': 'R'}, id=subjid[i])

        # delete array job
        self.server.delete(id=jid)

        for i in range(num_array_jobs):
            # check that the substate is set to 91 (TERMINATED) which
            # indicates job was deleted
            self.server.expect(JOB, {'substate': 91}, extend='x', id=subjid[i])

        self.server.expect(JOB, {'substate': 91}, extend='x', id=jid)

        ret = self.server.delete_hook(hook_name)
        self.assertEqual(ret, True, "Could not delete hook %s" % hook_name)
        self.server.log_match(hook_msg, starttime=start_time)
        self.logger.info("**************** HOOK END ****************")

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

    def test_hook_endjob_delete_force(self):
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
