/*
 * Copyright (C) 1994-2020 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * For a copy of the commercial license terms and conditions,
 * go to: (http://www.pbspro.com/UserArea/agreement.html)
 * or contact the Altair Legal Department.
 *
 * Altair’s dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of PBS Pro and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair’s trademarks, including but not limited to "PBS™",
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
 * trademark licensing policies.
 *
 */

#ifndef WIN32
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <grp.h>
#include "auth.h"
#include "pbs_ifl.h"
#include "log.h"

static pthread_once_t munge_init_once = PTHREAD_ONCE_INIT;

static void *munge_dlhandle = NULL; /* MUNGE dynamic loader handle */
static int (*munge_encode)(char **, void *, const void *, int) = NULL; /* MUNGE munge_encode() function pointer */
static int (*munge_decode)(const char *cred, void *, void **, int *, uid_t *, gid_t *) = NULL; /* MUNGE munge_decode() function pointer */
static char * (*munge_strerror) (int) = NULL; /* MUNGE munge_stderror() function pointer */
static void (*logger)(int type, int objclass, int severity, const char *objname, const char *text);

#define __MUNGE_LOGGER(e, c, s, m) \
	do { \
		if (logger == NULL) { \
			if (s != LOG_DEBUG) \
				fprintf(stderr, "%s: %s\n", __func__, m); \
		} else { \
			logger(e, c, s, __func__, m); \
		} \
	} while(0)
#define MUNGE_LOG_ERR(m) __MUNGE_LOGGER(PBSEVENT_ERROR|PBSEVENT_FORCE, PBS_EVENTCLASS_SERVER, LOG_ERR, m)
#define MUNGE_LOG_DBG(m) __MUNGE_LOGGER(PBSEVENT_DEBUG|PBSEVENT_FORCE, PBS_EVENTCLASS_SERVER, LOG_DEBUG, m)

static void init_munge(void);
static char * munge_get_auth_data();
static int munge_validate_auth_data(void *auth_data);

/**
 * @brief
 *	init_munge Check if libmunge.so shared library is present in the system
 *	and assign specific function pointers to be used at the time
 *	of decode or encode.
 *
 * @note
 *	This function should get invoked only once. Using pthread_once for this purpose.
 *	This function is not expecting any arguments. So storing error messages in a static
 *	variable in case of error.
 *
 * @return void
 *
 */
static void
init_munge(void)
{
	static const char libmunge[] = "libmunge.so";
	char ebuf[LOG_BUF_SIZE];

	ebuf[0] = '\0';
	munge_dlhandle = dlopen(libmunge, RTLD_LAZY);
	if (munge_dlhandle == NULL) {
		snprintf(ebuf, sizeof(ebuf), "%s not found", libmunge);
		MUNGE_LOG_ERR(ebuf);
		goto err;
	}

	munge_encode = dlsym(munge_dlhandle, "munge_encode");
	if (munge_encode == NULL) {
		snprintf(ebuf, sizeof(ebuf), "symbol munge_encode not found in %s", libmunge);
		MUNGE_LOG_ERR(ebuf);
		goto err;
	}

	munge_decode = dlsym(munge_dlhandle, "munge_decode");
	if (munge_decode == NULL) {
		snprintf(ebuf, sizeof(ebuf), "symbol munge_decode not found in %s", libmunge);
		MUNGE_LOG_ERR(ebuf);
		goto err;
	}

	munge_strerror = dlsym(munge_dlhandle, "munge_strerror");
	if (munge_strerror == NULL) {
		snprintf(ebuf, sizeof(ebuf), "symbol munge_strerror not found in %s", libmunge);
		MUNGE_LOG_ERR(ebuf);
		goto err;
	}

	return;

err:
	if (munge_dlhandle)
		dlclose(munge_dlhandle);

	munge_dlhandle = NULL;
	munge_encode = NULL;
	munge_decode = NULL;
	munge_strerror = NULL;
	return;
}

/**
 * @brief
 *	munge_get_auth_data - Call Munge encode API's to get the authentication data for the current user
 *
 * @return char *
 * @retval !NULL - success
 * @retval  NULL - failure
 *
 */
static char *
munge_get_auth_data(void)
{
	char *cred = NULL;
	uid_t myrealuid;
	struct passwd *pwent;
	struct group *grp;
	char payload[PBS_MAXUSER + PBS_MAXGRPN + 1] = { '\0' };
	int munge_err = 0;
	char ebuf[LOG_BUF_SIZE];

	if (munge_dlhandle == NULL) {
		pthread_once(&munge_init_once, init_munge);
		if (munge_encode == NULL) {
			MUNGE_LOG_ERR("Munge lib not loaded");
			goto err;
		}
	}

	myrealuid = getuid();
	pwent = getpwuid(myrealuid);
	if (pwent == NULL) {
		snprintf(ebuf, sizeof(ebuf) - 1, "Failed to obtain user-info for uid = %d", myrealuid);
		MUNGE_LOG_ERR(ebuf);
		goto err;
	}

	grp = getgrgid(pwent->pw_gid);
	if (grp == NULL) {
		snprintf(ebuf, sizeof(ebuf) - 1, "Failed to obtain group-info for gid=%d", pwent->pw_gid);
		MUNGE_LOG_ERR(ebuf);
		goto err;
	}

	snprintf(payload, PBS_MAXUSER + PBS_MAXGRPN, "%s:%s", pwent->pw_name, grp->gr_name);

	munge_err = munge_encode(&cred, NULL, payload, strlen(payload));
	if (munge_err != 0) {
		snprintf(ebuf, sizeof(ebuf) - 1, "MUNGE user-authentication on encode failed with `%s`", munge_strerror(munge_err));
		MUNGE_LOG_ERR(ebuf);
		goto err;
	}
	return cred;

err:
	free(cred);
	return NULL;
}


/**
 * @brief
 *	munge_validate_auth_data - validate given munge authentication data
 *
 * @param[in] auth_data - auth data to be verified
 *
 * @return int
 * @retval 0 - Success
 * @retval -1 - Failure
 *
 */
static int
munge_validate_auth_data(void *auth_data)
{
	uid_t uid;
	gid_t gid;
	int recv_len = 0;
	struct passwd *pwent = NULL;
	struct group *grp = NULL;
	void *recv_payload = NULL;
	int munge_err = 0;
	char *p;
	int rc = -1;
	char ebuf[LOG_BUF_SIZE];

	if (munge_dlhandle == NULL) {
		pthread_once(&munge_init_once, init_munge);
		if (munge_decode == NULL) {
			MUNGE_LOG_ERR("Munge lib not loaded");
			goto err;
		}
	}

	munge_err = munge_decode(auth_data, NULL, &recv_payload, &recv_len, &uid, &gid);
	if (munge_err != 0) {
		snprintf(ebuf, sizeof(ebuf) - 1, "MUNGE user-authentication on decode failed with `%s`", munge_strerror(munge_err));
		MUNGE_LOG_ERR(ebuf);
		goto err;
	}

	if ((pwent = getpwuid(uid)) == NULL) {
		snprintf(ebuf, sizeof(ebuf) - 1, "Failed to obtain user-info for uid = %d", uid);
		MUNGE_LOG_ERR(ebuf);
		goto err;
	}

	if ((grp = getgrgid(pwent->pw_gid)) == NULL) {
		snprintf(ebuf, sizeof(ebuf) - 1, "Failed to obtain group-info for gid=%d", gid);
		MUNGE_LOG_ERR(ebuf);
		goto err;
	}

	p = strtok((char *)recv_payload, ":");

	if (p && (strncmp(pwent->pw_name, p, PBS_MAXUSER) == 0)) /* inline with current pbs_iff we compare with username only */
		rc = 0;
	else
		MUNGE_LOG_ERR("User credentials do not match");

err:
	if (recv_payload)
		free(recv_payload);
	return rc;
}

/********* START OF EXPORTED FUNCS *********/

/** @brief
 *	pbs_auth_set_config - Set config for this lib
 *
 * @param[in] config - auth config structure
 *
 * @return void
 *
 */
void
pbs_auth_set_config(const pbs_auth_config_t *config)
{
	logger = config->logfunc;
}

/** @brief
 *	pbs_auth_create_ctx - allocates external auth context structure for MUNGE authentication
 *
 * @param[in] ctx - pointer to external auth context to be allocated
 * @param[in] mode - AUTH_SERVER or AUTH_CLIENT
 * @param[in] conn_type - AUTH_USER_CONN or AUTH_SERVICE_CONN
 * @param[in] hostname - hostname of other authenticating party
 *
 * @note
 * 	Currently munge doesn't require any context data, so just return 0
 *
 * @return	int
 * @retval	0 - success
 * @retval	1 - error
 */
int
pbs_auth_create_ctx(void **ctx, int mode, int conn_type, const char *hostname)
{
	*ctx = NULL;
	return 0;
}

/** @brief
 *	pbs_auth_destroy_ctx - destroy external auth context structure for MUNGE authentication
 *
 * @param[in] ctx - pointer to external auth context
 *
 * @note
 * 	Currently munge doesn't require any context data, so just return 0
 *
 * @return void
 */
void
pbs_auth_destroy_ctx(void *ctx)
{
	ctx = NULL;
}

/** @brief
 *	pbs_auth_get_userinfo - get user, host and realm from authentication context
 *
 * @param[in] ctx - pointer to external auth context
 * @param[out] user - username assosiate with ctx
 * @param[out] host - hostname/realm assosiate with ctx
 * @param[out] realm - realm assosiate with ctx
 *
 * @return	int
 * @retval	0 on success
 * @retval	1 on error
 *
 * @note
 * 	Currently munge doesn't have context, so just return 0
 */
int
pbs_auth_get_userinfo(void *ctx, char **user, char **host, char **realm)
{
	*user = NULL;
	*host = NULL;
	*realm = NULL;
	return 0;
}

/** @brief
 *	pbs_auth_process_handshake_data - do Munge auth handshake
 *
 * @param[in] ctx - pointer to external auth context
 * @param[in] data_in - received auth token data (if any)
 * @param[in] len_in - length of received auth token data (if any)
 * @param[out] data_out - auth token data to send (if any)
 * @param[out] len_out - lenght of auth token data to send (if any)
 * @param[out] is_handshake_done - indicates whether handshake is done (1) or not (0)
 *
 * @return	int
 * @retval	0 on success
 * @retval	!0 on error
 */
int
pbs_auth_process_handshake_data(void *ctx, void *data_in, size_t len_in, void **data_out, size_t *len_out, int *is_handshake_done)
{
	int rc = -1;

	*len_out = 0;
	*data_out = NULL;
	*is_handshake_done = 0;

	pthread_once(&munge_init_once, init_munge);

	if (munge_dlhandle == NULL) {
		return 1;
	}

	if (len_in > 0) {
		char *data = (char *)data_in;
		/* enforce null char at given length of data */
		data[len_in] = '\0';
		rc = munge_validate_auth_data(data);
		if (rc == 0) {
			*is_handshake_done = 1;
			return 0;
		}
	} else {
		*data_out = (void *)munge_get_auth_data();
		if (*data_out) {
			*len_out = strlen((char *)*data_out);
			*is_handshake_done = 1;
			return 0;
		}
	}

	return 1;
}

/********* END OF EXPORTED FUNCS *********/

#endif /* WIN32 */
