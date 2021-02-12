/*
 * Copyright (C) 1994-2021 Altair Engineering, Inc.
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

/*
 * array_func.c - Functions which provide basic Job Array functions
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pbs_ifl.h"
#include "libpbs.h"
#include "list_link.h"
#include "attribute.h"
#include "resource.h"
#include "server_limits.h"
#include "server.h"
#include "job.h"
#include "log.h"
#include "pbs_error.h"
#include "batch_request.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include "acct.h"
#include <sys/time.h>
#include "range.h"


/* External data */
extern char *msg_job_end_stat;
extern int   resc_access_perm;
extern time_t time_now;

/*
 * list of job attributes to copy from the parent Array job
 * when creating a sub job.
 */
static enum job_atr attrs_to_copy[] = {
	JOB_ATR_jobname,
	JOB_ATR_job_owner,
	JOB_ATR_resc_used,
	JOB_ATR_state,
	JOB_ATR_in_queue,
	JOB_ATR_at_server,
	JOB_ATR_account,
	JOB_ATR_ctime,
	JOB_ATR_errpath,
	JOB_ATR_grouplst,
	JOB_ATR_join,
	JOB_ATR_keep,
	JOB_ATR_mtime,
	JOB_ATR_mailpnts,
	JOB_ATR_mailuser,
	JOB_ATR_nodemux,
	JOB_ATR_outpath,
	JOB_ATR_priority,
	JOB_ATR_qtime,
	JOB_ATR_remove,
	JOB_ATR_rerunable,
	JOB_ATR_resource,
	JOB_ATR_session_id,
	JOB_ATR_shell,
	JOB_ATR_sandbox,
	JOB_ATR_jobdir,
	JOB_ATR_stagein,
	JOB_ATR_stageout,
	JOB_ATR_substate,
	JOB_ATR_userlst,
	JOB_ATR_variables,
	JOB_ATR_euser,
	JOB_ATR_egroup,
	JOB_ATR_hashname,
	JOB_ATR_hopcount,
	JOB_ATR_queuetype,
	JOB_ATR_security,
	JOB_ATR_etime,
	JOB_ATR_refresh,
	JOB_ATR_gridname,
	JOB_ATR_umask,
	JOB_ATR_cred,
	JOB_ATR_runcount,
	JOB_ATR_pset,
	JOB_ATR_eligible_time,
	JOB_ATR_sample_starttime,
	JOB_ATR_executable,
	JOB_ATR_Arglist,
	JOB_ATR_reserve_ID,
	JOB_ATR_project,
	JOB_ATR_run_version,
	JOB_ATR_tolerate_node_failures,
#if defined(PBS_SECURITY) && (PBS_SECURITY == KRB5)
	JOB_ATR_cred_id,
#endif
	JOB_ATR_submit_host,
	JOB_ATR_LAST /* This MUST be LAST	*/
};

/**
 * @brief
 * 			is_job_array - determines if the job id indicates
 *
 * @par	Functionality:
 * Note - subjob index or range may be invalid and not detected as such
 *
 * @param[in]	id - Job Id.
 *
 * @return      Job Type
 * @retval	 0  - A regular job
 * @retval	-1  - A ArrayJob
 * @retval	 2  - A single subjob
 * @retval	-3  - A range of subjobs
 */
int
is_job_array(char *id)
{
	char *pc;

	if ((pc = strchr(id, (int)'[')) == NULL)
		return IS_ARRAY_NO; /* not an ArrayJob nor a subjob (range) */
	if (*++pc == ']')
		return IS_ARRAY_ArrayJob;	/* an ArrayJob */

	/* know it is either a single subjob or an range there of */

	while (isdigit((int)*pc))
		++pc;
	if ((*pc == '-') || (*pc == ','))
		return IS_ARRAY_Range;	/* a range of subjobs */
	else
		return IS_ARRAY_Single;
}

/**
 * @brief
 * 		get_queued_subjobs_ct	-	get the number of queued subjobs if pjob is job array else return 1
 *
 * @param[in]	pjob	-	pointer to job structure
 *
 * @return	int
 * @retval	-1	: parse error
 * @retval	positive	: count of subjobs in JOB_ATR_array_indices_remaining if job array else 1
 */
int
get_queued_subjobs_ct(job *pjob)
{
	if (NULL == pjob)
		return -1;

	if (pjob->ji_qs.ji_svrflags & JOB_SVFLG_ArrayJob) {
		if (NULL == pjob->ji_ajinfo)
			return -1;

		return pjob->ji_ajinfo->tkm_subjsct[JOB_STATE_QUEUED];
	}

	return 1;
}

/**
 * @brief
 * 		find_arrayparent - find and return a pointer to the job that is or will be
 * 		the parent of the subjob id string
 *
 * @param[in]	subjobid - sub job id.
 *
 *	@return	parent job
 */
job *
find_arrayparent(char *subjobid)
{
	int   i;
	char  idbuf[PBS_MAXSVRJOBID+1];
	char *pc;

	for (i=0; i<PBS_MAXSVRJOBID; i++) {
		idbuf[i] = *(subjobid + i);
		if (idbuf[i] == '[')
			break;
	}
	idbuf[++i] = ']';
	idbuf[++i] = '\0';
	pc = strchr(subjobid, (int)'.');
	if (pc)
		strcat(idbuf, pc);
	return (find_job(idbuf));
}

/**
 * @brief
 * 		update_array_indices_remaining_attr - updates array_indices_remaining attribute
 *
 * @param[in,out]	parent - pointer to parent job.
 *
 * @return	void
 */
static void
update_array_indices_remaining_attr(job *parent)
{
	char *pnewstr = range_to_str(parent->ji_ajinfo->trm_quelist);

	if (pnewstr == NULL || *pnewstr == '\0')
		pnewstr = "-";
	set_jattr_str_slim(parent, JOB_ATR_array_indices_remaining, pnewstr, NULL);
	update_subjob_state_ct(parent);
}

/**
 * @brief
 * 	update state counts of subjob based on given information
 *
 * @param[in]	parent - pointer to parent job.
 * @param[in]	sj     - pointer to subjob (can be NULL)
 * @param[in]	offset - sub job index.
 * @param[in]	newstate - newstate of the sub job.
 *
 * @return void
 */
void
update_sj_parent(job *parent, job *sj, char *sjid, char oldstate, char newstate)
{
	ajinfo_t *ptbl;
	int idx;
	int ostatenum;
	int nstatenum;

	if (oldstate == newstate)
		return;

	if (parent == NULL || sjid == NULL || sjid[0] == '\0' || (idx = get_index_from_jid(sjid)) == -1)
		return;

	ptbl = parent->ji_ajinfo;
	if (ptbl == NULL)
		return;

	ostatenum = state_char2int(oldstate);
	nstatenum = state_char2int(newstate);
	if (ostatenum == -1 || nstatenum == -1)
		return;

	ptbl->tkm_subjsct[ostatenum]--;
	ptbl->tkm_subjsct[nstatenum]++;

	if (oldstate == JOB_STATE_LTR_QUEUED)
		range_remove_value(&ptbl->trm_quelist, idx);
	if (newstate == JOB_STATE_LTR_QUEUED)
		range_add_value(&ptbl->trm_quelist, idx, ptbl->tkm_step);
	update_array_indices_remaining_attr(parent);

	if (sj && newstate != JOB_STATE_LTR_QUEUED) {
		if (is_jattr_set(sj, JOB_ATR_exit_status)) {
			int e = get_jattr_long(sj, JOB_ATR_exit_status);
			int pe = 0;
			if (is_jattr_set(parent, JOB_ATR_exit_status))
				pe = get_jattr_long(parent, JOB_ATR_exit_status);
			if (pe != 2) {
				if (e > 0)
					pe = 1;
				else if (e < 0)
					pe = 2;
				else
					pe = 0;
			}
			set_jattr_l_slim(parent, JOB_ATR_exit_status, pe, SET);
		}
		if (is_jattr_set(sj, JOB_ATR_stageout_status)) {
			int pe = -1;
			int e = get_jattr_long(sj, JOB_ATR_stageout_status);
			if (is_jattr_set(parent, JOB_ATR_stageout_status))
				pe = get_jattr_long(parent, JOB_ATR_stageout_status);
			if (e > 0 && pe != 0)
				set_jattr_l_slim(parent, JOB_ATR_stageout_status, e, SET);
		}
	}
	job_save_db(parent);
}

/**
 * @brief
 * 		chk_array_doneness - check if all subjobs are expired and if so,
 *		purge the Array Job itself
 *
 * @param[in,out]	parent - pointer to parent job.
 *
 *	@return	void
 */
void
chk_array_doneness(job *parent)
{
	struct batch_request *preq;
	char hook_msg[HOOK_MSG_SIZE] = {0};
	int rc;
	ajinfo_t *ptbl = NULL;

	if (parent == NULL || parent->ji_ajinfo == NULL)
		return;

	ptbl = parent->ji_ajinfo;
	if (ptbl->tkm_flags & (TKMFLG_NO_DELETE | TKMFLG_CHK_ARRAY))
		return;	/* delete of subjobs in progress, or re-entering, so return here */

	if (ptbl->tkm_subjsct[JOB_STATE_QUEUED] + ptbl->tkm_subjsct[JOB_STATE_RUNNING]
			+ ptbl->tkm_subjsct[JOB_STATE_HELD] + ptbl->tkm_subjsct[JOB_STATE_EXITING] == 0) {

		/* Array Job all done, do simple eoj processing */
		parent->ji_qs.ji_un_type = JOB_UNION_TYPE_EXEC;
		parent->ji_qs.ji_un.ji_exect.ji_momaddr = 0;
		parent->ji_qs.ji_un.ji_exect.ji_momport = 0;

		parent->ji_qs.ji_un.ji_exect.ji_exitstat = get_jattr_long(parent, JOB_ATR_exit_status);		

		check_block(parent, "");
		if (check_job_state(parent, JOB_STATE_LTR_BEGUN)) {
			char acctbuf[40];

			/* set parent endtime to time_now */
			parent->ji_qs.ji_endtime = time_now;
			set_jattr_l_slim(parent, JOB_ATR_endtime, parent->ji_qs.ji_endtime, SET);

			/* Allocate space for the endjob hook event params */
			preq = alloc_br(PBS_BATCH_EndJob);
			if (preq) {
				(preq->rq_ind.rq_end).rq_pjob = parent;

				/* update parent job state to 'F' */
				sprintf(log_buffer, "rq_endjob svr_setjobstate update parent job state to 'F'");
				log_err(-1, __func__, log_buffer);
				svr_setjobstate(parent, JOB_STATE_LTR_FINISHED, JOB_SUBSTATE_FINISHED);

				/*
				* Call process_hooks
				*/
				rc = process_hooks(preq, hook_msg, sizeof(hook_msg), pbs_python_set_interrupt);
				if (rc == -1) {
					sprintf(log_buffer, "rq_endjob process_hooks call failed");
					log_err(-1, __func__, log_buffer);
				} else {
					sprintf(log_buffer, "rq_endjob process_hooks call succeeded");
					log_err(-1, __func__, log_buffer);
				}
				free_br(preq);
			} else {
				log_err(PBSE_INTERNAL, __func__, "rq_endjob alloc failed");
			}

			/* if BEGUN, issue 'E' account record */
			sprintf(acctbuf, msg_job_end_stat, parent->ji_qs.ji_un.ji_exect.ji_exitstat);
			account_job_update(parent, PBS_ACCT_LAST);
			account_jobend(parent, acctbuf, PBS_ACCT_END);

			svr_mailowner(parent, MAIL_END, MAIL_NORMAL, acctbuf);
		}
		if (is_jattr_set(parent, JOB_ATR_depend))
			depend_on_term(parent);

		/*
		 * Check if the history of the finished job can be saved or it needs to be purged .
		 */
		ptbl->tkm_flags |= TKMFLG_CHK_ARRAY;
		svr_saveorpurge_finjobhist(parent);
	}
}

/**
 * @brief
 * 	find subjob and its state and substate
 *
 * @param[in]     parent    - pointer to the parent job
 * @param[in]     sjidx     - subjob index
 * @param[out]    state     - put state of subjob if not null
 * @param[out]    substate  - put substate of subjob if not null
 *
 * @return job *
 * @retval !NULL - if subjob found
 * @return NULL  - if subjob not found
 */
job *
get_subjob_and_state(job *parent, int sjidx, char *state, int *substate)
{
	job *sj;

	if (state)
		*state = JOB_STATE_LTR_UNKNOWN;
	if (substate)
		*substate = JOB_SUBSTATE_UNKNOWN;

	if (parent == NULL || sjidx < 0)
		return NULL;

	if (sjidx < parent->ji_ajinfo->tkm_start || sjidx > parent->ji_ajinfo->tkm_end)
		return NULL;

	if (((sjidx - parent->ji_ajinfo->tkm_start) % parent->ji_ajinfo->tkm_step) != 0)
		return NULL;

	sj = find_job(create_subjob_id(parent->ji_qs.ji_jobid, sjidx));
	if (sj == NULL) {
		if (range_contains(parent->ji_ajinfo->trm_quelist, sjidx)) {
			if (state)
				*state = JOB_STATE_LTR_QUEUED;
			if (substate)
				*substate = JOB_SUBSTATE_QUEUED;
		} else {
			if (state) {
				char pjs = get_job_state(parent);
				if (pjs == JOB_STATE_LTR_FINISHED)
					*state = JOB_STATE_LTR_FINISHED;
				else
					*state = JOB_STATE_LTR_EXPIRED;
			}
			if (substate)
				*substate = JOB_SUBSTATE_FINISHED;
		}
		return NULL;
	}

	if (state)
		*state = get_job_state(sj);
	if (substate)
		*substate = get_job_substate(sj);

	return sj;
}
/**
 * @brief
 * 		update_subjob_state_ct - update the "array_state_count" attribute of an
 * 		array job
 *
 * @param[in]	pjob - pointer to the job
 *
 * @return	void
 */
void
update_subjob_state_ct(job *pjob)
{
	char buf[BUF_SIZE];
	const char *statename[] = {"Transit", "Queued", "Held", "Waiting", "Running",
				   "Exiting", "Expired", "Beginning", "Moved", "Finished"};

	buf[0] = '\0';
	snprintf(buf, sizeof(buf), "%s:%d %s:%d %s:%d %s:%d",
		 statename[JOB_STATE_QUEUED],
		 pjob->ji_ajinfo->tkm_subjsct[JOB_STATE_QUEUED],
		 statename[JOB_STATE_RUNNING],
		 pjob->ji_ajinfo->tkm_subjsct[JOB_STATE_RUNNING],
		 statename[JOB_STATE_EXITING],
		 pjob->ji_ajinfo->tkm_subjsct[JOB_STATE_EXITING],
		 statename[JOB_STATE_EXPIRED],
		 pjob->ji_ajinfo->tkm_subjsct[JOB_STATE_EXPIRED]);

	set_jattr_str_slim(pjob, JOB_ATR_array_state_count, buf, NULL);
}
/**
 * @brief
 * 		subst_array_index - Substitute the actual index into the file name
 * 		if this is a sub job and if the array index substitution
 * 		string is in the specified file path.  If, not the original string
 * 		is returned unchanged.
 *
 * @param[in]	pjob - pointer to the job
 * @param[in]	path - name of local or destination
 *
 * @return	path
 */
char *
subst_array_index(job *pjob, char *path)
{
	char *pindorg;
	char *cvt;
	char trail[MAXPATHLEN + 1];
	job *ppjob = pjob->ji_parentaj;

	if (ppjob == NULL)
		return path;
	if ((pindorg = strstr(path, PBS_FILE_ARRAY_INDEX_TAG)) == NULL)
		return path; /* unchanged */

	cvt = get_range_from_jid(pjob->ji_qs.ji_jobid);
	if (cvt == NULL)
		return path;
	*pindorg = '\0';
	strcpy(trail, pindorg + strlen(PBS_FILE_ARRAY_INDEX_TAG));
	strcat(path, cvt);
	strcat(path, trail);
	return path;
}

/**
 * @brief
 * 		mk_subjob_index_tbl - make the subjob index tracking table
 *		(ajinfo_t) based on the number of indexes in the "range"
 *
 * @param[in]	range - subjob index range
 * @param[out]	pbserror - PBSError to return
 * @param[in]	mode - "actmode" parameter to action function of "array_indices_submitted"
 *
 * @return	ptr to table
 * @retval  NULL	- error
 */
static int
setup_ajinfo(job *pjob, int mode)
{
	int i;
	int limit;
	int start;
	int end;
	int step;
	int count;
	char *eptr;
	char *range;
	ajinfo_t *trktbl;

	if (pjob->ji_ajinfo) {
		free_range_list(pjob->ji_ajinfo->trm_quelist);
		free(pjob->ji_ajinfo);
	}
	pjob->ji_ajinfo = NULL;
	range = get_jattr_str(pjob, JOB_ATR_array_indices_submitted);
	if (range == NULL)
		return PBSE_BADATVAL;
	i = parse_subjob_index(range, &eptr, &start, &end, &step, &count);
	if (i != 0)
		return PBSE_BADATVAL;

	if ((mode == ATR_ACTION_NEW) || (mode == ATR_ACTION_ALTER)) {
		if (server.sv_attr[(int) SVR_ATR_maxarraysize].at_flags & ATR_VFLAG_SET)
			limit = server.sv_attr[(int) SVR_ATR_maxarraysize].at_val.at_long;
		else
			limit = PBS_MAX_ARRAY_JOB_DFL; /* default limit 10000 */

		if (count > limit)
			return PBSE_MaxArraySize;
	}

	trktbl = (ajinfo_t *) malloc(sizeof(ajinfo_t));
	if (trktbl == NULL)
		return PBSE_SYSTEM;
	for (i = 0; i < PBS_NUMJOBSTATE; i++)
		trktbl->tkm_subjsct[i] = 0;
	if (mode == ATR_ACTION_RECOV || mode == ATR_ACTION_ALTER)
		trktbl->trm_quelist = NULL;
	else {
		trktbl->trm_quelist = new_range(start, end, step, count, NULL);
		if (trktbl->trm_quelist == NULL) {
			free(trktbl);
			return PBSE_SYSTEM;
		}
		trktbl->tkm_subjsct[JOB_STATE_QUEUED] = count;
	}
	trktbl->tkm_dsubjsct = 0;
	trktbl->tkm_ct = count;
	trktbl->tkm_start = start;
	trktbl->tkm_end = end;
	trktbl->tkm_step = step;
	trktbl->tkm_flags = 0;
	pjob->ji_ajinfo = trktbl;
	return PBSE_NONE;
}

/**
 * @brief
 * 		setup_arrayjob_attrs - set up the special attributes of an Array Job
 *		Called as "action" routine for the attribute array_indices_submitted
 *
 * @param[in]	pattr - pointer to special attributes of an Array Job
 * @param[in]	pobj -  pointer to job structure
 * @param[in]	mode -  actmode
 *
 * @return	PBS error
 * @retval  0	- success
 */
int
setup_arrayjob_attrs(attribute *pattr, void *pobj, int mode)
{
	job *pjob = pobj;

	if (mode != ATR_ACTION_ALTER && mode != ATR_ACTION_NEW && mode != ATR_ACTION_RECOV)
		return PBSE_BADATVAL;

	if (is_job_array(pjob->ji_qs.ji_jobid) != IS_ARRAY_ArrayJob)
		return PBSE_BADATVAL;	/* not an Array Job */

	if (mode == ATR_ACTION_ALTER && !check_job_state(pjob, JOB_STATE_LTR_QUEUED))
		return PBSE_MODATRRUN;	/* cannot modify once begun */

	/* set attribute "array" True  and clear "array_state_count" */
	pjob->ji_qs.ji_svrflags |= JOB_SVFLG_ArrayJob;
	set_jattr_b_slim(pjob, JOB_ATR_array, 1, SET);
	free_jattr(pjob, JOB_ATR_array_state_count);

	if ((mode == ATR_ACTION_NEW) || (mode == ATR_ACTION_RECOV)) {
		int rc = PBSE_BADATVAL;
		if ((rc = setup_ajinfo(pjob, mode)) != PBSE_NONE)
			return rc;
	}

	if (mode == ATR_ACTION_RECOV)
		return PBSE_NONE;

	update_array_indices_remaining_attr(pjob);

	return PBSE_NONE;
}

/**
 * @brief
 * 		fixup_arrayindicies - set state of subjobs based on array_indicies_remaining
 * @par	Functionality:
 * 		This is used when a job is being qmoved into this server.
 * 		It is necessary that the indices_submitted be first to cause the
 * 		creation of the tracking tbl. If the job is created here, no need of fix indicies
 *
 * @param[in]	pattr - pointer to special attributes of an Array Job
 * @param[in]	pobj -  pointer to job structure
 * @param[in]	mode -  actmode
 * @return	PBS error
 * @retval  0	- success
 */
int
fixup_arrayindicies(attribute *pattr, void *pobj, int mode)
{
	job *pjob = pobj;
	char *range;
	int qcount;

	if (!pjob || !(pjob->ji_qs.ji_svrflags & JOB_SVFLG_ArrayJob) || !pjob->ji_ajinfo)
		return PBSE_BADATVAL;

	if (mode == ATR_ACTION_NEW && (pjob->ji_qs.ji_svrflags & JOB_SVFLG_HERE))
		return PBSE_NONE;

	if (pjob->ji_ajinfo->trm_quelist != NULL)
		return PBSE_BADATVAL;

	range = get_jattr_str(pjob, JOB_ATR_array_indices_remaining);
	pjob->ji_ajinfo->trm_quelist = range_parse(range);
	if (pjob->ji_ajinfo->trm_quelist == NULL) {
		if (range && range[0] == '-') {
			pjob->ji_ajinfo->tkm_subjsct[JOB_STATE_QUEUED] = 0;
			pjob->ji_ajinfo->tkm_subjsct[JOB_STATE_EXPIRED] = pjob->ji_ajinfo->tkm_ct;
			update_subjob_state_ct(pjob);
			return PBSE_NONE;
		}
		return PBSE_BADATVAL;
	}

	qcount = range_count(pjob->ji_ajinfo->trm_quelist);
	pjob->ji_ajinfo->tkm_subjsct[JOB_STATE_QUEUED] = qcount;
	pjob->ji_ajinfo->tkm_subjsct[JOB_STATE_EXPIRED] = pjob->ji_ajinfo->tkm_ct - qcount;
	update_subjob_state_ct(pjob);
	return PBSE_NONE;
}

/**
 * @brief
 * 		create_subjob - create a Subjob from the parent Array Job
 * 		Certain attributes are changed or left out
 * @param[in]	parent - pointer to parent Job
 * @param[in]	newjid -  new job id
 * @param[in]	rc -  return code
 * @return	pointer to new job
 * @retval  NULL	- error
 */
job *
create_subjob(job *parent, char *newjid, int *rc)
{
	pbs_list_head  attrl;
	int	   i;
	int	   j;
	char	  *index;
	attribute_def *pdef;
	attribute *ppar;
	attribute *psub;
	svrattrl  *psatl;
	job 	  *subj;
	long	   eligibletime;
	long	    time_msec;
	struct timeval	    tval;
	char path[MAXPATHLEN + 1];

	if (newjid == NULL) {
		*rc = PBSE_IVALREQ;
		return NULL;
	}

	if ((parent->ji_qs.ji_svrflags & JOB_SVFLG_ArrayJob) == 0) {
		*rc = PBSE_IVALREQ;
		return NULL;	/* parent not an array job */
	}

	/* find and copy the index */
	if ((index = get_range_from_jid(newjid)) == NULL) {
		*rc = PBSE_IVALREQ;
		return NULL;
	}

	/*
	 * allocate and clear basic structure
	 * cannot copy job attributes because cannot share strings and other
	 * malloc-ed data,  so copy ji_qs as a whole and then copy the
	 * non-saved items before ji_qs.
	 */

	if ((subj = job_alloc()) == NULL) {
		*rc = PBSE_SYSTEM;
		return NULL;
	}
	subj->ji_qs = parent->ji_qs;	/* copy the fixed save area */
	subj->ji_qhdr     = parent->ji_qhdr;
	subj->ji_myResv   = parent->ji_myResv;
	subj->ji_parentaj = parent;
	strcpy(subj->ji_qs.ji_jobid, newjid);	/* replace job id */
	*subj->ji_qs.ji_fileprefix = '\0';

	/*
	 * now that is all done, copy the required attributes by
	 * encoding and then decoding into the new array.  Then add the
	 * subjob specific attributes.
	 */

	resc_access_perm = ATR_DFLAG_ACCESS;
	CLEAR_HEAD(attrl);
	for (i = 0; attrs_to_copy[i] != JOB_ATR_LAST; i++) {
		j    = (int)attrs_to_copy[i];
		ppar = &parent->ji_wattr[j];
		psub = &subj->ji_wattr[j];
		pdef = &job_attr_def[j];

		if (pdef->at_encode(ppar, &attrl, pdef->at_name, NULL,
			ATR_ENCODE_MOM, &psatl) > 0) {
			for (psatl = (svrattrl *)GET_NEXT(attrl); psatl;
				psatl = ((svrattrl *)GET_NEXT(psatl->al_link))) {
				pdef->at_decode(psub, psatl->al_name, psatl->al_resc,
					psatl->al_value);
			}
			/* carry forward the default bit if set */
			psub->at_flags |= (ppar->at_flags & ATR_VFLAG_DEFLT);
			free_attrlist(&attrl);
		}
	}

	set_jattr_generic(subj, JOB_ATR_array_id, parent->ji_qs.ji_jobid, NULL, INTERNAL);
	set_jattr_generic(subj, JOB_ATR_array_index, index, NULL, INTERNAL);

	/* Lastly, set or clear a few flags and link in the structure */

	subj->ji_qs.ji_svrflags &= ~JOB_SVFLG_ArrayJob;
	subj->ji_qs.ji_svrflags |=  JOB_SVFLG_SubJob;
	set_job_substate(subj, JOB_SUBSTATE_TRANSICM);
	svr_setjobstate(subj, JOB_STATE_LTR_QUEUED, JOB_SUBSTATE_QUEUED);

	/* subjob needs to borrow eligible time from parent job array.
	 * expecting only to accrue eligible_time and nothing else.
	 */
	if (server.sv_attr[(int)SVR_ATR_EligibleTimeEnable].at_val.at_long == 1) {

		eligibletime = get_jattr_long(parent, JOB_ATR_eligible_time);

		if (get_jattr_long(parent, JOB_ATR_accrue_type) == JOB_ELIGIBLE)
			eligibletime += get_jattr_long(subj, JOB_ATR_sample_starttime) - get_jattr_long(parent, JOB_ATR_sample_starttime);

		set_jattr_l_slim(subj, JOB_ATR_eligible_time, eligibletime, SET);
	}

	gettimeofday(&tval, NULL);
	time_msec = (tval.tv_sec * 1000L) + (tval.tv_usec/1000L);
	/* set the queue rank attribute */
	set_jattr_l_slim(subj, JOB_ATR_qrank, time_msec, SET);
	if (svr_enquejob(subj, NULL) != 0) {
		job_purge(subj);
		*rc = PBSE_IVALREQ;
		return NULL;
	}

	pbs_strncpy(path, get_jattr_str(subj, JOB_ATR_outpath), sizeof(path));
	subst_array_index(subj, path);
	set_jattr_str_slim(subj, JOB_ATR_outpath, path, NULL);
	pbs_strncpy(path, get_jattr_str(subj, JOB_ATR_errpath), sizeof(path));
	subst_array_index(subj, path);
	set_jattr_str_slim(subj, JOB_ATR_errpath, path, NULL);

	*rc = PBSE_NONE;
	return subj;
}

/**
 * @brief
 *	 	Duplicate the existing batch request for a running subjob
 *
 * @param[in]	opreq	- the batch status request structure to duplicate
 * @param[in]	pjob	- the parent job structure of the subjob
 * @param[in]	func	- the function to call after duplicating the batch
 *			   structure.
 * @par
 *		1. duplicate the batch request
 *		2. replace the job id with the one from the running subjob
 *		3. link the new batch request to the original and incr its ref ct
 *		4. call the "func" with the new batch request and job
 * @note
 *		Currently, this is called in PBS_Batch_DeleteJob, PBS_Batch_SignalJob,
 *		PBS_Batch_Rerun, and PBS_Batch_RunJob subjob requests.
 *		For any other request types, be sure to add another switch case below
 *		(matching request type).
 */
void
dup_br_for_subjob(struct batch_request *opreq, job *pjob, void (*func)(struct batch_request *, job *))
{
	struct batch_request  *npreq;

	npreq = alloc_br(opreq->rq_type);
	if (npreq == NULL)
		return;

	npreq->rq_perm    = opreq->rq_perm;
	npreq->rq_fromsvr = opreq->rq_fromsvr;
	npreq->rq_conn = opreq->rq_conn;
	npreq->rq_orgconn = opreq->rq_orgconn;
	npreq->rq_time    = opreq->rq_time;
	strcpy(npreq->rq_user, opreq->rq_user);
	strcpy(npreq->rq_host, opreq->rq_host);
	npreq->rq_extend  = opreq->rq_extend;
	npreq->rq_reply.brp_choice = BATCH_REPLY_CHOICE_NULL;
	npreq->rq_refct   = 0;

	/* for each type, update the job id with the one from the new job */

	switch (opreq->rq_type) {
		case PBS_BATCH_DeleteJobList:
			npreq->rq_ind.rq_deletejoblist = opreq->rq_ind.rq_deletejoblist;
			npreq->rq_ind.rq_deletejoblist.rq_count = 1;
			npreq->rq_ind.rq_deletejoblist.rq_jobslist = break_comma_list(pjob->ji_qs.ji_jobid);
			break;
		case PBS_BATCH_DeleteJob:
			npreq->rq_ind.rq_delete = opreq->rq_ind.rq_delete;
			strcpy(npreq->rq_ind.rq_delete.rq_objname,
				pjob->ji_qs.ji_jobid);
			break;
		case PBS_BATCH_SignalJob:
			npreq->rq_ind.rq_signal = opreq->rq_ind.rq_signal;
			strcpy(npreq->rq_ind.rq_signal.rq_jid,
				pjob->ji_qs.ji_jobid);
			break;
		case PBS_BATCH_Rerun:
			strcpy(npreq->rq_ind.rq_rerun,
				pjob->ji_qs.ji_jobid);
			break;
		case PBS_BATCH_RunJob:
			npreq->rq_ind.rq_run = opreq->rq_ind.rq_run;
			strcpy(npreq->rq_ind.rq_run.rq_jid,
				pjob->ji_qs.ji_jobid);
			break;
		default:
			delete_link(&npreq->rq_link);
			free(npreq);
			return;
	}

	npreq->rq_parentbr = opreq;
	opreq->rq_refct++;

	func(npreq, pjob);
}
