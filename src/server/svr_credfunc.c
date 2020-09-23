/*
 * Copyright (C) 1994-2020 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of both the OpenPBS software ("OpenPBS")
 * and the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * OpenPBS is free software. You can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * OpenPBS is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * PBS Pro is commercially licensed software that shares a common core with
 * the OpenPBS software.  For a copy of the commercial license terms and
 * conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
 * Altair Legal Department.
 *
 * Altair's dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of OpenPBS and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair's trademarks, including but not limited to "PBS™",
 * "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
 * subject to Altair's trademark licensing policies.
 */


/**
 * @file	svr_credfunc.c
 *
 * @brief
 *	Routines for work task that takes care of renewing credentials for
 *	running jobs.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <errno.h>
#include <unistd.h>

#include "attribute.h"
#include "job.h"
#include "work_task.h"
#include "pbs_error.h"
#include "log.h"

#include <stdlib.h>

#define SVR_RENEW_CREDS_TM	300	/* each 5*60 seconds, reschedule the work task and spread renew within the 5*60 seconds */
#define SVR_RENEW_PERIOD_DEFAULT	3600	/* default renew creds 1 hour befor expiration */
#define SVR_RENEW_CACHE_PERIOD_DEFAULT	7200	/* default cred usable 2 hours befor expiration */

long svr_cred_renew_enable = 0; /*disable by default*/
long svr_cred_renew_period = SVR_RENEW_PERIOD_DEFAULT;
long svr_cred_renew_cache_period = SVR_RENEW_CACHE_PERIOD_DEFAULT;

extern time_t time_now;
extern pbs_list_head svr_alljobs;

extern int send_cred(job *pjob);

/* @brief
 *	The work task for particular job. This work task renew credentials for
 *	a job specified in the work task and sends the credentials to the
 *	superior mom.
 *
 * @param[in] pwt - work task structure
 *
 */
void
svr_renew_job_cred(struct work_task *pwt)
{
	char	*jobid = (char *)pwt->wt_parm1;
	job 	*pjob = NULL;
	int	rc;
	if ((pjob = find_job(jobid)) != NULL) {
		if (!check_job_state(pjob, JOB_STATE_LTR_RUNNING))
			return;

		/* job without cred id */
		if ((is_jattr_set(pjob, JOB_ATR_cred_id)) == 0)
			return;

		rc = send_cred(pjob);
		if (rc != 0) {
			log_eventf(PBSEVENT_ERROR, PBS_EVENTCLASS_SERVER,
				LOG_NOTICE, msg_daemonname,
				"svr_renew_job_cred %s renew failed, send_cred returned: %d", pjob->ji_qs.ji_jobid, rc);
		} else {
			log_eventf(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
				LOG_NOTICE, msg_daemonname,
				"svr_renew_job_cred %s renew was successful", pjob->ji_qs.ji_jobid);
		}
	} /* else job does not exists - job probably finished */
}

/* @brief
 *	This is the main credentials renew work task. This work task runs every
 *	SVR_RENEW_CREDS_TM and it checks all the running jobs and for running
 *	jobs it checks the validity of credentials. If the credentials are too
 *	old then svr_renew_job_cred() work task is planned for the particular
 *	job.
 *
 * @param[in] pwt - work task structure
 *
 */
void
svr_renew_creds(struct work_task *pwt)
{
	job 	*pjob = NULL;
	job 	*nxpjob = NULL;

	/* first, set up another work task for next time period */
	if (pwt && svr_cred_renew_enable) {
		if (!set_task(WORK_Timed,
			(time_now + SVR_RENEW_CREDS_TM),
			svr_renew_creds, NULL)) {
			log_err(errno,
				__func__,
				"Unable to set task for renew credentials");
		}
	}

	/*
	 * Traverse through the SERVER job list and set renew task if necessary.
	 * The renew tasks are spread within SVR_RENEW_CREDS_TM
	 */
	pjob = (job *)GET_NEXT(svr_alljobs);

	while (pjob) {
		/* save the next job */
		nxpjob = (job *)GET_NEXT(pjob->ji_alljobs);

		if ((is_jattr_set(pjob, JOB_ATR_cred_id)) &&
			check_job_state(pjob, JOB_STATE_LTR_RUNNING)) {

			if ((is_jattr_set(pjob, JOB_ATR_cred_validity)) &&
				(get_jattr_long(pjob,  JOB_ATR_cred_validity) - svr_cred_renew_period <= time_now)) {
				/* spread the renew tasks to the SVR_RENEW_CREDS_TM interval */
				if (!set_task(WORK_Timed, (time_now + (rand() % SVR_RENEW_CREDS_TM)), svr_renew_job_cred, pjob->ji_qs.ji_jobid)) {
					log_err(errno, __func__, "Unable to set task for renew job credential");
				}
			}
		}
		/* restore the saved next in pjob */
		pjob = nxpjob;
	}
}

/* @brief
 *	Enables renewing credentials for running jobs. It starts the renewing
 *	work task.
 *
 * @param[in]	pattr	-	pointer to attribute structure
 * @param[in]	pobject -	pointer to some parent object.(not used here)
 * @param[in]	actmode	-	the action to take (e.g. ATR_ACTION_ALTER)
 *
 * @return	int
 * @retval	PBSE_NONE on success
 * @retval	!= PBSE_NONE on error
 */
int
set_cred_renew_enable(attribute *pattr, void *pobject, int actmode)
{
#if defined(PBS_SECURITY) && (PBS_SECURITY == KRB5)
	if ((actmode == ATR_ACTION_ALTER) ||
		(actmode == ATR_ACTION_RECOV)) {

		svr_cred_renew_enable = pattr->at_val.at_long;
		if (svr_cred_renew_enable) {
			(void)set_task(WORK_Timed,
				(long)(time_now + SVR_RENEW_CREDS_TM),
				svr_renew_creds, 0);
		}
	}
#endif
	return (PBSE_NONE);
}

/* @brief
 *	Sets the svr_cred_renew_period.
 *
 * @param[in]	pattr	-	pointer to attribute structure
 * @param[in]	pobject -	pointer to some parent object.(not used here)
 * @param[in]	actmode	-	the action to take (e.g. ATR_ACTION_ALTER)
 *
 * @return	int
 * @retval	PBSE_NONE on success
 * @retval	!= PBSE_NONE on error
 */
int
set_cred_renew_period(attribute *pattr, void *pobject, int actmode)
{
	if ((actmode == ATR_ACTION_ALTER) ||
		(actmode == ATR_ACTION_RECOV)) {

		if ((pattr->at_val.at_long < SVR_RENEW_CREDS_TM)) {
			log_eventf(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
				LOG_NOTICE, msg_daemonname,
				"%s value to low, using: %ld",
				ATTR_cred_renew_period,
				svr_cred_renew_period);
			return PBSE_BADATVAL;
		}

		svr_cred_renew_period = pattr->at_val.at_long;

		if ((svr_cred_renew_period > svr_cred_renew_cache_period)) {
			/* warning */
			log_eventf(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
				LOG_NOTICE, msg_daemonname,
				"%s: %ld should be lower than %s: %ld",
				ATTR_cred_renew_period,
				pattr->at_val.at_long,
				ATTR_cred_renew_cache_period,
				svr_cred_renew_cache_period);
		}

		log_eventf(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
			LOG_NOTICE, msg_daemonname,
			"svr_cred_renew_period set to val %ld",
			svr_cred_renew_period);
	}
	return PBSE_NONE;
}

/* @brief
 *	Sets the svr_cred_renew_cache_period.
 *
 * @param[in]	pattr	-	pointer to attribute structure
 * @param[in]	pobject -	pointer to some parent object.(not used here)
 * @param[in]	actmode	-	the action to take (e.g. ATR_ACTION_ALTER)
 *
 * @return	int
 * @retval	PBSE_NONE on success
 * @retval	!= PBSE_NONE on error
 */
int
set_cred_renew_cache_period(attribute *pattr, void *pobject, int actmode)
{
	if ((actmode == ATR_ACTION_ALTER) ||
		(actmode == ATR_ACTION_RECOV)) {

		if ((pattr->at_val.at_long < SVR_RENEW_CREDS_TM)) {
			log_eventf(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
				LOG_NOTICE, msg_daemonname,
				"%s value to low, using: %ld",
				ATTR_cred_renew_cache_period,
				svr_cred_renew_cache_period);
			return PBSE_BADATVAL;
		}

		svr_cred_renew_cache_period = pattr->at_val.at_long;

		if ((svr_cred_renew_cache_period < svr_cred_renew_period)) {
			/* warning */
			log_eventf(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
				LOG_NOTICE, msg_daemonname,
				"%s: %ld should be greater than %s: %ld",
				ATTR_cred_renew_cache_period,
				pattr->at_val.at_long,
				ATTR_cred_renew_period,
				svr_cred_renew_period);
		}

		log_eventf(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
			LOG_NOTICE, msg_daemonname,
			"svr_cred_renew_cache_period set to val %ld",
			svr_cred_renew_cache_period);
	}
	return PBSE_NONE;
}
