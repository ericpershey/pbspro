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

/**
 * @file	pbs_msgjob.c
 * @brief
 *	send the MessageJob request and get the reply.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "libpbs.h"
#include "dis.h"
#include "pbs_ecl.h"


/**
 * @brief
 *	-send the MessageJob request and get the reply.
 *
 * @param[in] c - socket descriptor
 * @param[in] jobid - job id
 * @param[in] fileopt - which file
 * @param[in] msg - msg to be encoded
 * @param[in] extend - extend string for encoding req
 *
 * @return	int
 * @retval	0	success
 * @retval	!0	error
 *
 */

int
__pbs_msgjob(int c, char *jobid, int fileopt, char *msg, char *extend)
{
	struct batch_reply *reply;
	int	rc;

	if ((jobid == NULL) || (*jobid == '\0') ||
		(msg == NULL) || (*msg == '\0'))
		return (pbs_errno = PBSE_IVALREQ);

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return pbs_errno;

	/* lock pthread mutex here for this connection */
	/* blocking call, waits for mutex release */
	if (pbs_client_thread_lock_connection(c) != 0)
		return pbs_errno;

	/* setup DIS support routines for following DIS calls */
	DIS_tcp_funcs();

	if ((rc = PBSD_msg_put(c, jobid, fileopt, msg, extend, PROT_TCP, NULL)) != 0) {
		if (set_conn_errtxt(c, dis_emsg[rc]) != 0) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_PROTOCOL;
		}
		(void)pbs_client_thread_unlock_connection(c);
		return pbs_errno;
	}

	/* read reply */
	reply = PBSD_rdrpy(c);
	rc = get_conn_errno(c);

	PBSD_FreeReply(reply);

	/* unlock the thread lock and update the thread context data */
	if (pbs_client_thread_unlock_connection(c) != 0)
		return pbs_errno;

	return rc;
}

/**
 * @brief
 *	-Send a request to spawn a python script to the MS
 *	of a job.  It will run as a task.
 *
 * @param[in] c - communication handle
 * @param[in] jobid - job id
 * @param[in] argv - pointer to argument list
 * @param[in] envp - pointer to environment variable
 *
 * @return	int
 * @retval	exit value of the task	success
 * @retval	-1			error
 *
 */
int
pbs_py_spawn(int c, char *jobid, char **argv, char **envp)
{
	struct batch_reply *reply;
	int	rc;

	/*
	 ** Must have jobid and argv[0] as a minimum.
	 */
	if ((jobid == NULL) || (*jobid == '\0') ||
		(argv == NULL) || (argv[0] == NULL)) {
		pbs_errno = PBSE_IVALREQ;
		return -1;
	}
	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return -1;

	/* lock pthread mutex here for this connection */
	/* blocking call, waits for mutex release */
	if (pbs_client_thread_lock_connection(c) != 0)
		return -1;

	/* setup DIS support routines for following DIS calls */

	DIS_tcp_funcs();

	if ((rc = PBSD_py_spawn_put(c, jobid, argv, envp, 0, NULL)) != 0) {
		if (set_conn_errtxt(c, dis_emsg[rc]) != 0) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_PROTOCOL;
		}
		(void)pbs_client_thread_unlock_connection(c);
		return -1;
	}

	/* read reply */

	reply = PBSD_rdrpy(c);
	if ((reply == NULL) || (get_conn_errno(c) != 0))
		rc = -1;
	else
		rc = reply->brp_auxcode;

	PBSD_FreeReply(reply);

	/* unlock the thread lock and update the thread context data */
	if (pbs_client_thread_unlock_connection(c) != 0)
		return -1;

	return rc;
}

/**
 * @brief
 * 	-pbs_relnodesjob - release a set of sister nodes or vnodes,
 * 	or all sister nodes or vnodes assigned to the specified PBS
 * 	batch job.
 *
 * @param[in] c 	communication handle
 * @param[in] jobid  job identifier
 * @param[in] node_list 	list of hosts or vnodes to be released
 * @param[in] extend 	additional params, currently passes -k arguments
 *
 * @return	int
 * @retval	0	Success
 * @retval	!0	error
 *
 */

int
pbs_relnodesjob(c, jobid, node_list, extend)
int c;
char *jobid;
char *node_list;
char *extend;
{
	struct batch_reply *reply;
	int	rc;

	if ((jobid == NULL) || (*jobid == '\0') ||
					(node_list == NULL))
		return (pbs_errno = PBSE_IVALREQ);

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return pbs_errno;

	/* first verify the resource list in keep_select option */
	if (extend) {
		struct attrl *attrib = NULL;
		char emsg_illegal_k_value[] = "illegal -k value";
		char ebuff[PBS_PARSE_ERR_MSG_LEN_MAX + sizeof(emsg_illegal_k_value) + 4], *erp, *emsg = NULL;
		int i;
		struct pbs_client_thread_connect_context *con;
		char nd_ct_selstr[20];
		char *endptr = NULL;
		long int rc_long;

		errno = 0;
		rc_long = strtol(extend, &endptr, 10);

		if ((errno == 0) && (rc_long > 0) && (*endptr == '\0')) {
			snprintf(nd_ct_selstr, sizeof(nd_ct_selstr), "select=%s", extend);
			extend = nd_ct_selstr;
		} else if ((i = set_resources(&attrib, extend, 1, &erp))) {
			if (i > 1) {
				snprintf(ebuff, sizeof(ebuff), "%s: %s\n", emsg_illegal_k_value, pbs_parse_err_msg(i));
				emsg = strdup(ebuff);
			} else
				emsg = strdup("illegal -k value\n");
			pbs_errno = PBSE_INVALSELECTRESC;
		} else {
			if (!attrib || strcmp(attrib->resource, "select")) {
				emsg = strdup("only a \"select=\" string is valid in -k option\n");
				pbs_errno = PBSE_IVALREQ;
			} else
				pbs_errno = PBSE_NONE;
		}
		if (pbs_errno) {
			if ((con = pbs_client_thread_find_connect_context(c))) {
				free(con->th_ch_errtxt);
				con->th_ch_errtxt = emsg;
				con->th_ch_errno = pbs_errno;
			} else {
				(void)set_conn_errtxt(c, emsg);
				(void)set_conn_errno(c, pbs_errno);
				free(emsg);
			}
			return pbs_errno;
		}
		rc = pbs_verify_attributes(c, PBS_BATCH_RelnodesJob,
			MGR_OBJ_JOB, MGR_CMD_NONE, (struct attropl *) attrib);
		if (rc)
			return rc;
	}

	/* lock pthread mutex here for this connection */
	/* blocking call, waits for mutex release */
	if (pbs_client_thread_lock_connection(c) != 0)
		return pbs_errno;

	/* setup DIS support routines for following DIS calls */

	DIS_tcp_funcs();

	if ((rc = PBSD_relnodes_put(c, jobid, node_list, extend, 0, NULL)) != 0) {
		if (set_conn_errtxt(c, dis_emsg[rc]) != 0) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_PROTOCOL;
		}
		(void)pbs_client_thread_unlock_connection(c);
		return pbs_errno;
	}

	/* read reply */

	reply = PBSD_rdrpy(c);
	rc = get_conn_errno(c);

	PBSD_FreeReply(reply);

	/* unlock the thread lock and update the thread context data */
	if (pbs_client_thread_unlock_connection(c) != 0)
		return pbs_errno;

	return rc;
}
