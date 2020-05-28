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


#include <pbs_config.h>   /* the master config generated by configure */

#if defined(PBS_SECURITY) && (PBS_SECURITY == KRB5)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <gssapi.h>
#include <gssapi.h>
#include <krb5.h>
#include "pbs_ifl.h"
#include "libauth.h"

#if defined(KRB5_HEIMDAL)
#define PBS_GSS_MECH_OID GSS_KRB5_MECHANISM
#else
#include <gssapi/gssapi_krb5.h>
#define PBS_GSS_MECH_OID (gss_OID)gss_mech_krb5
#endif

static pthread_mutex_t gss_lock;
static pthread_once_t gss_init_lock_once = PTHREAD_ONCE_INIT;
static char gss_log_buffer[LOG_BUF_SIZE];
static void (*logger)(int type, int objclass, int severity, const char *objname, const char *text);
#define DEFAULT_CREDENTIAL_LIFETIME 7200

#define __GSS_LOGGER(e, c, s, m) \
	do { \
		if (logger == NULL) { \
			if (s != LOG_DEBUG) \
				fprintf(stderr, "%s: %s\n", __func__, m); \
		} else { \
			logger(e, c, s, "", m); \
		} \
	} while(0)
#define GSS_LOG_ERR(m) __GSS_LOGGER(PBSEVENT_ERROR|PBSEVENT_FORCE, PBS_EVENTCLASS_SERVER, LOG_ERR, m)
#define GSS_LOG_DBG(m) __GSS_LOGGER(PBSEVENT_DEBUG|PBSEVENT_FORCE, PBS_EVENTCLASS_SERVER, LOG_DEBUG, m)
#define __GSS_LOGGER_STS(m, s, c) \
	do { \
		OM_uint32 _mstat; \
		gss_buffer_desc _msg; \
		OM_uint32 _msg_ctx; \
		_msg_ctx = 0; \
		char buf[LOG_BUF_SIZE]; \
		do { \
			buf[0] = '\0'; \
			gss_display_status(&_mstat, s, c, GSS_C_NULL_OID, &_msg_ctx, &_msg); \
			snprintf(buf, LOG_BUF_SIZE, "GSS - %s : %.*s", m, (int)_msg.length, (char *)_msg.value); \
			GSS_LOG_ERR(buf); \
			(void) gss_release_buffer(&_mstat, &_msg); \
		} while (_msg_ctx != 0); \
	} while (0)
#define GSS_LOG_STS(m, mjs, mis) __GSS_LOGGER_STS(m, mjs, GSS_C_GSS_CODE); __GSS_LOGGER_STS(m, mis, GSS_C_MECH_CODE)

#define PBS_KRB5_SERVICE_NAME "host"
#define PBS_KRB5_CLIENT_CCNAME "FILE:/tmp/krb5cc_pbs_client"

#define GSS_NT_SERVICE_NAME GSS_C_NT_HOSTBASED_SERVICE

typedef struct {
	gss_ctx_id_t gssctx;	/* gss security context */
	int gssctx_established;	/* true if gss context has been established */
	int is_secure;		/* wrapping includes encryption */
	enum AUTH_ROLE role;	/* value is client or server */
	int conn_type;		/* type of connection one of user-oriented or service-oriented */
	char *hostname;		/* server name */
	char *clientname;	/* client name in string */
} pbs_gss_extra_t;

enum PBS_GSS_ERRORS {
	PBS_GSS_OK = 0,
	PBS_GSS_CONTINUE_NEEDED,
	PBS_GSS_ERR_INTERNAL,
	PBS_GSS_ERR_IMPORT_NAME,
	PBS_GSS_ERR_ACQUIRE_CREDS,
	PBS_GSS_ERR_CONTEXT_INIT,
	PBS_GSS_ERR_CONTEXT_ACCEPT,
	PBS_GSS_ERR_CONTEXT_DELETE,
	PBS_GSS_ERR_CONTEXT_ESTABLISH,
	PBS_GSS_ERR_NAME_CONVERT,
	PBS_GSS_ERR_WRAP,
	PBS_GSS_ERR_UNWRAP,
	PBS_GSS_ERR_OID,
	PBS_GSS_ERR_LAST
};

static int pbs_gss_can_get_creds(const gss_OID_set oidset);
static int init_pbs_client_ccache_from_keytab(char *err_buf, int err_buf_size);
static void init_gss_lock(void);

static void
init_gss_lock(void)
{
	pthread_mutexattr_t attr;

	if (pthread_mutexattr_init(&attr) != 0) {
		GSS_LOG_ERR("Failed to initialize mutex attr");
		return;
	}

	if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP)) {
		GSS_LOG_ERR("Failed to set mutex type");
		return;
	}

	if (pthread_mutex_init(&gss_lock, &attr) != 0) {
		GSS_LOG_ERR("Failed to initialize mutex");
		return;
	}

	return;
}

/** @brief
 *	If oid set is null then create oid set. Once we have the oid set,
 *	the appropriate gss mechanism is added (e.g. kerberos).
 *
 * @param[in/out] oidset - oid set for change
 *
 * @return	int
 * @retval	PBS_GSS_OK on success
 * @retval	!= PBS_GSS_OK on error
 */
static int
pbs_gss_oidset_mech(gss_OID_set *oidset)
{
	OM_uint32 maj_stat;
	OM_uint32 min_stat;
	if (*oidset == GSS_C_NULL_OID_SET) {
		maj_stat = gss_create_empty_oid_set(&min_stat, oidset);
		if (maj_stat != GSS_S_COMPLETE) {
			GSS_LOG_STS("gss_create_empty_oid_set", maj_stat, min_stat);
			return PBS_GSS_ERR_OID;
		}
	}

	maj_stat = gss_add_oid_set_member(&min_stat, PBS_GSS_MECH_OID, oidset);
	if (maj_stat != GSS_S_COMPLETE) {
		GSS_LOG_STS("gss_add_oid_set_member", maj_stat, min_stat);
		return PBS_GSS_ERR_OID;
	}

	return PBS_GSS_OK;
}

/** @brief
 *	Release oid set
 *
 * @param[in] oidset - oid set for releasing
 *
 * @return void
 *
 */
static void
pbs_gss_release_oidset(gss_OID_set *oidset)
{
	OM_uint32 maj_stat;
	OM_uint32 min_stat;

	maj_stat = gss_release_oid_set(&min_stat, oidset);
	if (maj_stat != GSS_S_COMPLETE) {
		GSS_LOG_STS("gss_release_oid_set", maj_stat, min_stat);
	}
}

/** @brief
 *	Copy data from gss buffer into string and provides the length of the data.
 *
 * @param[in] tok - token with source data
 * @param[out] data - data to be filled
 * @param[out] len - length of data
 *
 * @return	int
 * @retval	PBS_GSS_OK on success
 * @retval	!= PBS_GSS_OK on error
 */
static int
pbs_gss_fill_data(gss_buffer_t tok, void **data, size_t *len)
{
	*data = malloc(tok->length);
	if (*data == NULL) {
		GSS_LOG_ERR("malloc failure");
		return PBS_GSS_ERR_INTERNAL;
	}

	memcpy(*data, tok->value, tok->length);
	*len = tok->length;
	return PBS_GSS_OK;
}

/** @brief
 *	Imports a service name and acquires credentials for it. The service name
 *	is imported with gss_import_name, and service credentials are acquired
 *	with gss_acquire_cred.
 *
 * @param[in] service_name - the service name
 * @param[out] server_creds - the GSS-API service credentials
 *
 * @return	int
 * @retval	PBS_GSS_OK on success
 * @retval	!= PBS_GSS_OK on error
 */
static int
pbs_gss_server_acquire_creds(char *service_name, gss_cred_id_t* server_creds)
{
	gss_name_t server_name;
	OM_uint32 maj_stat;
	OM_uint32 min_stat = 0;
	gss_OID_set oidset = GSS_C_NO_OID_SET;
	gss_buffer_desc name_buf;

	name_buf.value = service_name;
	name_buf.length = strlen(service_name) + 1;

	maj_stat = gss_import_name(&min_stat, &name_buf, GSS_NT_SERVICE_NAME, &server_name);

	if (maj_stat != GSS_S_COMPLETE) {
		GSS_LOG_STS("gss_import_name", maj_stat, min_stat);
		return PBS_GSS_ERR_IMPORT_NAME;
	}

	if (pbs_gss_oidset_mech(&oidset) != PBS_GSS_OK)
		return PBS_GSS_ERR_OID;

	maj_stat = gss_acquire_cred(&min_stat, server_name, 0, oidset, GSS_C_ACCEPT, server_creds, NULL, NULL);

	pbs_gss_release_oidset(&oidset);

	if (maj_stat != GSS_S_COMPLETE) {
		GSS_LOG_STS("gss_acquire_cred", maj_stat, min_stat);

		if (gss_release_name(&min_stat, &server_name) != GSS_S_COMPLETE) {
			GSS_LOG_STS("gss_release_name", maj_stat, min_stat);
			return PBS_GSS_ERR_INTERNAL;
		}

		return PBS_GSS_ERR_ACQUIRE_CREDS;
	}

	maj_stat = gss_release_name(&min_stat, &server_name);
	if (maj_stat != GSS_S_COMPLETE) {
		GSS_LOG_STS("gss_release_name", maj_stat, min_stat);
		return PBS_GSS_ERR_INTERNAL;
	}

	return PBS_GSS_OK;
}

/* @brief
 *	Client part of GSS hadshake
 *
 * @param[in] service_name - GSS service name
 * @param[in] creds - client credentials
 * @param[in] oid - The security mechanism to use. GSS_C_NULL_OID for default
 * @param[in] gss_flags - Flags indicating additional services or parameters requested for the context.
 * @param[in/out] gss_context - this context is being established here
 * @param[out] ret_flags - Flags indicating additional services or parameters requested for the context.
 * @param[in] data_in - received GSS token data
 * @param[in] len_in - length of data_in
 * @param[out] data_out - GSS token data for transmitting
 * @param[out] len_out - length of data_out
 *
 * @return	int
 * @retval	PBS_GSS_OK on success
 * @retval	!= PBS_GSS_OK on error
 */
static int
pbs_gss_client_establish_context(char *service_name, gss_cred_id_t creds, gss_OID oid, OM_uint32 gss_flags, gss_ctx_id_t * gss_context, OM_uint32 *ret_flags, void* data_in, size_t len_in, void **data_out, size_t *len_out)
{
	gss_buffer_desc send_tok;
	gss_buffer_desc recv_tok;
	gss_buffer_desc *token_ptr;
	gss_name_t target_name;
	OM_uint32 maj_stat;
	OM_uint32 min_stat = 0;
	OM_uint32 init_sec_maj_stat;
	OM_uint32 init_sec_min_stat = 0;

	send_tok.value = service_name;
	send_tok.length = strlen(service_name) ;
	maj_stat = gss_import_name(&min_stat, &send_tok, GSS_NT_SERVICE_NAME, &target_name);
	if (maj_stat != GSS_S_COMPLETE) {
		GSS_LOG_STS("gss_import_name", maj_stat, min_stat);
		return PBS_GSS_ERR_IMPORT_NAME;
	}

	send_tok.value = NULL;
	send_tok.length = 0;

	recv_tok.value = (void *)data_in;
	recv_tok.length = len_in;

	if (recv_tok.length > 0)
		token_ptr = &recv_tok;
	else
		token_ptr = GSS_C_NO_BUFFER;

	init_sec_maj_stat = gss_init_sec_context(&init_sec_min_stat, creds ? creds : GSS_C_NO_CREDENTIAL, gss_context, target_name, oid, gss_flags, 0, NULL, token_ptr, NULL, &send_tok, ret_flags, NULL);

	if (send_tok.length != 0) {
		pbs_gss_fill_data(&send_tok, data_out, len_out);

		maj_stat = gss_release_buffer(&min_stat, &send_tok);
		if (maj_stat != GSS_S_COMPLETE) {
			GSS_LOG_STS("gss_release_buffer", maj_stat, min_stat);
			return PBS_GSS_ERR_INTERNAL;
		}
	}

	maj_stat = gss_release_name(&min_stat, &target_name);
	if (maj_stat != GSS_S_COMPLETE) {
		GSS_LOG_STS("gss_release_name", maj_stat, min_stat);
		return PBS_GSS_ERR_INTERNAL;
	}

	if (init_sec_maj_stat != GSS_S_COMPLETE && init_sec_maj_stat != GSS_S_CONTINUE_NEEDED) {
		GSS_LOG_STS("gss_init_sec_context", init_sec_maj_stat, init_sec_min_stat);

		if (*gss_context != GSS_C_NO_CONTEXT) {
			maj_stat = gss_delete_sec_context(&min_stat, gss_context, GSS_C_NO_BUFFER);
			if (maj_stat != GSS_S_COMPLETE) {
				GSS_LOG_STS("gss_delete_sec_context", maj_stat, min_stat);
				return PBS_GSS_ERR_CONTEXT_DELETE;
			}
		}

		return PBS_GSS_ERR_CONTEXT_INIT;
	}

	if (init_sec_maj_stat == GSS_S_CONTINUE_NEEDED)
		return PBS_GSS_CONTINUE_NEEDED;

	return PBS_GSS_OK;
}

/* @brief
 *	Server part of GSS hadshake
 *
 * @param[in] server_creds - server credentials
 * @param[in] client_creds - optional credentials, can be NULL
 * @param[in/out] gss_context - this context is being established here
 * @param[out] client_name - GSS client name
 * @param[out] ret_flags - Flags indicating additional services or parameters requested for the context.
 * @param[in] data_in - received GSS token data
 * @param[in] len_in - length of data_in
 * @param[out] data_out - GSS token data for transmitting
 * @param[out] len_out - length of data_out
 *
 * @return	int
 * @retval	PBS_GSS_OK on success
 * @retval	!= PBS_GSS_OK on error
 */
static int
pbs_gss_server_establish_context(gss_cred_id_t server_creds, gss_cred_id_t* client_creds, gss_ctx_id_t* gss_context, gss_buffer_t client_name, OM_uint32* ret_flags, void* data_in, size_t len_in, void **data_out, size_t *len_out)
{
	gss_buffer_desc send_tok;
	gss_buffer_desc recv_tok;
	gss_name_t client;
	gss_OID doid;
	OM_uint32 maj_stat;
	OM_uint32 min_stat = 0;
	OM_uint32 acc_sec_maj_stat;
	OM_uint32 acc_sec_min_stat = 0;

	recv_tok.value = data_in;
	recv_tok.length = len_in;

	if (recv_tok.length == 0) {
		GSS_LOG_ERR("Invalid input data");
		return PBS_GSS_ERR_INTERNAL;
	}

	acc_sec_maj_stat = gss_accept_sec_context(&acc_sec_min_stat, gss_context, server_creds, &recv_tok, GSS_C_NO_CHANNEL_BINDINGS, &client, &doid, &send_tok, ret_flags, NULL, client_creds);

	if (send_tok.length != 0) {
		pbs_gss_fill_data(&send_tok, data_out, len_out);

		maj_stat = gss_release_buffer(&min_stat, &send_tok);
		if (maj_stat != GSS_S_COMPLETE) {
			GSS_LOG_STS("gss_release_buffer", maj_stat, min_stat);
			return PBS_GSS_ERR_INTERNAL;
		}
	}

	if (acc_sec_maj_stat != GSS_S_COMPLETE && acc_sec_maj_stat != GSS_S_CONTINUE_NEEDED) {
		GSS_LOG_STS("gss_accept_sec_context", acc_sec_maj_stat, acc_sec_min_stat);

		if (*gss_context != GSS_C_NO_CONTEXT) {
			if ((maj_stat = gss_delete_sec_context(&min_stat, gss_context, GSS_C_NO_BUFFER)) != GSS_S_COMPLETE) {
				GSS_LOG_STS("gss_delete_sec_context", maj_stat, min_stat);
				return PBS_GSS_ERR_CONTEXT_DELETE;
			}
		}

		return PBS_GSS_ERR_CONTEXT_ACCEPT;
	}

	maj_stat = gss_display_name(&min_stat, client, client_name, &doid);
	if (maj_stat != GSS_S_COMPLETE) {
		GSS_LOG_STS("gss_display_name", maj_stat, min_stat);
		return PBS_GSS_ERR_NAME_CONVERT;
	}

	maj_stat = gss_release_name(&min_stat, &client);
	if (maj_stat != GSS_S_COMPLETE) {
		GSS_LOG_STS("gss_release_name", maj_stat, min_stat);
		return PBS_GSS_ERR_INTERNAL;
	}

	if (acc_sec_maj_stat == GSS_S_CONTINUE_NEEDED)
		return PBS_GSS_CONTINUE_NEEDED;

	return PBS_GSS_OK;
}

/**
 * @brief
 *	Determines whether GSS credentials can be acquired
 *
 * @return	int
 * @retval	!= 0 if creds can be acquired
 * @retval	0 if creds can not be acquired
 */
static int
pbs_gss_can_get_creds(const gss_OID_set oidset)
{
	OM_uint32 maj_stat;
	OM_uint32 min_stat;
	OM_uint32 valid_sec = 0;
	gss_cred_id_t creds = GSS_C_NO_CREDENTIAL;

	maj_stat = gss_acquire_cred(&min_stat, GSS_C_NO_NAME, GSS_C_INDEFINITE, oidset, GSS_C_INITIATE, &creds, NULL, &valid_sec);
	if (maj_stat == GSS_S_COMPLETE && creds != GSS_C_NO_CREDENTIAL)
		gss_release_cred(&min_stat, &creds);

	/*
	 * There is a bug in old MIT implementation.
	 * It causes valid_sec is always 0.
	 * The problem is fixed in version >= 1.14
	 */
	return (maj_stat == GSS_S_COMPLETE && valid_sec > 10);
}

/**
 * @brief
 * 	create or renew ccache from keytab for the gss client side.
 *
 * @param[in] err_buf - buffer to put error log
 * @param[in] err_buf_size - err_buf size
 *
 * @return 	int
 * @retval	0 on success
 * @retval	!= 0 otherwise
 */
static int
init_pbs_client_ccache_from_keytab(char *err_buf, int err_buf_size)
{
	krb5_error_code ret = KRB5KRB_ERR_GENERIC;
	krb5_context context = NULL;
	krb5_principal pbs_service = NULL;
	krb5_keytab keytab = NULL;
	krb5_creds *creds = NULL;
	krb5_get_init_creds_opt *opt = NULL;
	krb5_ccache ccache = NULL;
	krb5_creds *mcreds = NULL;
	char *realm;
	char **realms = NULL;
	char hostname[PBS_MAXHOSTNAME + 1];
	int endtime = 0;

	creds = malloc(sizeof(krb5_creds));
	if (creds == NULL) {
		snprintf(err_buf, err_buf_size, "malloc failure");
		goto out;
	}
	memset(creds, 0, sizeof(krb5_creds));

	mcreds = malloc(sizeof(krb5_creds));
	if (mcreds == NULL) {
		snprintf(err_buf, err_buf_size, "malloc failure");
		goto out;
	}
	memset(mcreds, 0, sizeof(krb5_creds));

	setenv("KRB5CCNAME", PBS_KRB5_CLIENT_CCNAME, 1);

	ret = krb5_init_context(&context);
	if (ret) {
		snprintf(err_buf, err_buf_size, "Cannot initialize Kerberos context.");
		goto out;
	}

	ret = krb5_sname_to_principal(context, NULL, PBS_KRB5_SERVICE_NAME, KRB5_NT_SRV_HST, &pbs_service);
	if (ret) {
		snprintf(err_buf, err_buf_size, "Preparing principal failed (%s)", krb5_get_error_message(context, ret));
		goto out;
	}

	ret = krb5_cc_resolve(context, PBS_KRB5_CLIENT_CCNAME, &ccache);
	if (ret) /* for ret = true it is not a real error, we will just create new ccache */
		snprintf(err_buf, err_buf_size, "Couldn't resolve ccache name (%s) New ccache will be created.", krb5_get_error_message(context, ret));

	ret = gethostname(hostname, PBS_MAXHOSTNAME + 1);
	if (ret) {
		snprintf(err_buf, err_buf_size, "Failed to get host name");
		goto out;
	}

	ret = krb5_get_host_realm(context, hostname, &realms);
	if (ret) {
		snprintf(err_buf, err_buf_size, "Failed to get host realms (%s)", krb5_get_error_message(context, ret));
		goto out;
	}

	realm = realms[0];
	ret = krb5_build_principal(context, &mcreds->server, strlen(realm), realm, KRB5_TGS_NAME, realm, NULL);
	if (ret) {
		snprintf(err_buf, err_buf_size, "Couldn't build server principal (%s)", krb5_get_error_message(context, ret));
		goto out;
	}

	ret = krb5_copy_principal(context, pbs_service, &mcreds->client);
	if (ret) {
		snprintf(err_buf, err_buf_size, "Couldn't copy client principal (%s)", krb5_get_error_message(context, ret));
		goto out;
	}

	ret = krb5_cc_retrieve_cred(context, ccache, 0, mcreds, creds);
	if (ret) /* for ret = true it is not a real error, we will just create new ccache */
		snprintf(err_buf, err_buf_size, "Couldn't retrieve credentials from cache (%s) New ccache will be created.", krb5_get_error_message(context, ret));
	else
		endtime = creds->times.endtime;

	/* if we have valid credentials in ccache goto out
	 * if the credentials are about to expire soon (60 * 30 = 30 minutes)
	 * then try to renew from keytab.
	 */
	if (endtime - (60 * 30) >= time(NULL)) {
		ret = 0;
		goto out;
	}

	ret = krb5_cc_new_unique(context, "FILE", NULL, &ccache);
	if (ret) {
		snprintf(err_buf, err_buf_size, "Failed to create ccache (%s)", krb5_get_error_message(context, ret));
		goto out;
	}

	ret = krb5_cc_resolve(context, PBS_KRB5_CLIENT_CCNAME, &ccache);
	if (ret) {
		snprintf(err_buf, err_buf_size, "Couldn't resolve cache name (%s)", krb5_get_error_message(context, ret));
		goto out;
	}

	ret = krb5_kt_default(context, &keytab);
	if (ret) {
		snprintf(err_buf, err_buf_size, "Couldn't open keytab (%s)", krb5_get_error_message(context, ret));
		goto out;
	}
	ret = krb5_get_init_creds_opt_alloc(context, &opt);
	if (ret) {
		snprintf(err_buf, err_buf_size, "Couldn't allocate a new initial credential options structure (%s)", krb5_get_error_message(context, ret));
		goto out;
	}

	krb5_get_init_creds_opt_set_forwardable(opt, 1);

	ret = krb5_get_init_creds_keytab(context, creds, pbs_service, keytab, 0, NULL, opt);
	if (ret) {
		snprintf(err_buf, err_buf_size, "Couldn't get initial credentials using a key table (%s)", krb5_get_error_message(context, ret));
		goto out;
	}

	ret = krb5_cc_initialize(context, ccache, creds->client);
	if (ret) {
		snprintf(err_buf, err_buf_size, "Credentials cache initializing failed (%s)", krb5_get_error_message(context, ret));
		goto out;
	}

	ret = krb5_cc_store_cred(context, ccache, creds);
	if (ret) {
		snprintf(err_buf, err_buf_size, "Couldn't store ccache (%s)", krb5_get_error_message(context, ret));
		goto out;
	}

out:
	if (creds)
		krb5_free_creds(context, creds);
	if (mcreds)
		krb5_free_creds(context, mcreds);
	if (opt)
		krb5_get_init_creds_opt_free(context, opt);
	if (pbs_service)
		krb5_free_principal(context, pbs_service);
	if (ccache)
		krb5_cc_close(context, ccache);
	if (realms)
		krb5_free_host_realm(context, realms);
	if (keytab)
		krb5_kt_close(context, keytab);
	if (context)
		krb5_free_context(context);
	return (ret);
}

/** @brief
 *	This is the main gss handshake function for asynchronous handshake.
 *	It has two branches: client and server. Once the handshake is finished
 *	the GSS structure is set to ready for un/wrapping.
 *
 *
 * @param[in] gss_extra - gss structure
 * @param[in] data_in - received GSS token data
 * @param[in] len_in - length of data_in
 * @param[out] data_out - GSS token data for transmitting
 * @param[out] len_out - length of data_out
 *
 * @return int
 * @retval PBS_GSS_OK - success
 * @retval !PBS_GSS_OK - failure
 *
 */
int
pbs_gss_establish_context(pbs_gss_extra_t *gss_extra, void *data_in, size_t len_in, void **data_out, size_t *len_out)
{
	OM_uint32 maj_stat;
	OM_uint32 min_stat = 0;
	gss_ctx_id_t gss_context = GSS_C_NO_CONTEXT;
	static gss_cred_id_t server_creds = GSS_C_NO_CREDENTIAL;
	gss_cred_id_t creds = GSS_C_NO_CREDENTIAL;
	char *service_name = NULL;
	time_t now = time((time_t *)NULL);
	static time_t lastcredstime = 0;
	static time_t credlifetime = 0;
	OM_uint32 lifetime;
	OM_uint32 gss_flags;
	OM_uint32 ret_flags;
	gss_OID oid;
	gss_OID_set oidset = GSS_C_NO_OID_SET;
	int ret;
	gss_buffer_desc client_name = {0};
	int ccache_from_keytab = 0;

	if (gss_extra == NULL)
		return PBS_GSS_ERR_INTERNAL;

	if (gss_extra->role == AUTH_ROLE_UNKNOWN)
		return PBS_GSS_ERR_INTERNAL;

	if (gss_extra->hostname == NULL)
		return PBS_GSS_ERR_INTERNAL;

	gss_context = gss_extra->gssctx;

	if (service_name == NULL) {
		service_name = (char *) malloc(strlen(PBS_KRB5_SERVICE_NAME) + 1 + strlen(gss_extra->hostname) + 1);
		if (service_name == NULL) {
			GSS_LOG_ERR("malloc failure");
			return PBS_GSS_ERR_INTERNAL;
		}
		sprintf(service_name, "%s@%s", PBS_KRB5_SERVICE_NAME, gss_extra->hostname);
	}

	switch(gss_extra->role) {

		case AUTH_CLIENT:
			if (pbs_gss_oidset_mech(&oidset) != PBS_GSS_OK)
				return PBS_GSS_ERR_OID;

			if (gss_extra->conn_type == AUTH_USER_CONN) {
				if (!pbs_gss_can_get_creds(oidset)) {
					ccache_from_keytab = 1;

					if (init_pbs_client_ccache_from_keytab(gss_log_buffer, LOG_BUF_SIZE)) {
						GSS_LOG_DBG(gss_log_buffer);
						unsetenv("KRB5CCNAME");
					}
				}
			} else {
				if (init_pbs_client_ccache_from_keytab(gss_log_buffer, LOG_BUF_SIZE)) {
					GSS_LOG_DBG(gss_log_buffer);
					unsetenv("KRB5CCNAME");
				}
			}

			maj_stat = gss_acquire_cred(&min_stat, GSS_C_NO_NAME, GSS_C_INDEFINITE, oidset, GSS_C_INITIATE, &creds, NULL, NULL);

			pbs_gss_release_oidset(&oidset);

			if (maj_stat != GSS_S_COMPLETE) {
				GSS_LOG_STS("gss_acquire_cred", maj_stat, min_stat);
				if (ccache_from_keytab || gss_extra->conn_type == AUTH_SERVICE_CONN)
					unsetenv("KRB5CCNAME");
				return PBS_GSS_ERR_ACQUIRE_CREDS;
			}

			gss_flags = GSS_C_MUTUAL_FLAG | GSS_C_DELEG_FLAG | GSS_C_INTEG_FLAG | GSS_C_CONF_FLAG;
			oid = PBS_GSS_MECH_OID;

			ret = pbs_gss_client_establish_context(service_name, creds, oid, gss_flags, &gss_context, &ret_flags, data_in, len_in, data_out, len_out);

			if (ccache_from_keytab || gss_extra->conn_type == AUTH_SERVICE_CONN)
				unsetenv("KRB5CCNAME");

			if (creds != GSS_C_NO_CREDENTIAL) {
				maj_stat = gss_release_cred(&min_stat, &creds);
				if (maj_stat != GSS_S_COMPLETE) {
					GSS_LOG_STS("gss_release_cred", maj_stat, min_stat);
					return PBS_GSS_ERR_INTERNAL;
				}
			}

			break;

		case AUTH_SERVER:
			/*
			 * if credentials are old, try to get new ones. If we can't, keep the old
			 * ones since they're probably still valid and hope that
			 * we can get new credentials next time
			 */
			if (now - lastcredstime > credlifetime) {
				gss_cred_id_t new_server_creds = GSS_C_NO_CREDENTIAL;

				if (pbs_gss_server_acquire_creds(service_name, &new_server_creds) != PBS_GSS_OK) {
					snprintf(gss_log_buffer, LOG_BUF_SIZE, "Failed to acquire server credentials for %s", service_name);
					GSS_LOG_ERR(gss_log_buffer);

					/* try again in 2 minutes */
					lastcredstime = now + 120;
				} else {
					lastcredstime = now;
					snprintf(gss_log_buffer, LOG_BUF_SIZE, "Refreshing server credentials at %ld", (long)now);
					GSS_LOG_DBG(gss_log_buffer);

					if (server_creds != GSS_C_NO_CREDENTIAL) {
						maj_stat = gss_release_cred(&min_stat, &server_creds);
						if (maj_stat != GSS_S_COMPLETE) {
							GSS_LOG_STS("gss_release_cred", maj_stat, min_stat);
							return PBS_GSS_ERR_INTERNAL;
						}
					}

					server_creds = new_server_creds;

					/* fetch information about the fresh credentials */
					if (gss_inquire_cred(&ret_flags, server_creds, NULL, &lifetime, NULL, NULL) == GSS_S_COMPLETE) {
						if (lifetime == GSS_C_INDEFINITE) {
							credlifetime = DEFAULT_CREDENTIAL_LIFETIME;
							snprintf(gss_log_buffer, LOG_BUF_SIZE, "Server credentials renewed with indefinite lifetime, using %d.", DEFAULT_CREDENTIAL_LIFETIME);
							GSS_LOG_DBG(gss_log_buffer);
						} else {
							snprintf(gss_log_buffer, LOG_BUF_SIZE, "Server credentials renewed with lifetime as %u.", lifetime);
							GSS_LOG_DBG(gss_log_buffer);
							credlifetime = lifetime;
						}
					} else {
						/* could not read information from credential */
						credlifetime = 0;
					}
				}
			}

			ret = pbs_gss_server_establish_context(server_creds, NULL, &gss_context, &(client_name), &ret_flags, data_in, len_in, data_out, len_out);

			break;

		default:
			return -1;
	}

	if (service_name != NULL)
		free(service_name);

	if (gss_context == GSS_C_NO_CONTEXT) {
		GSS_LOG_ERR("Failed to establish gss context");
		return PBS_GSS_ERR_CONTEXT_ESTABLISH;
	}

	gss_extra->gssctx = gss_context;

	if (ret == PBS_GSS_CONTINUE_NEEDED) {
		return PBS_GSS_OK;
	}

	if (client_name.length) {
		gss_extra->clientname = malloc(client_name.length + 1);
		if (gss_extra->clientname == NULL) {
			GSS_LOG_ERR("malloc failure");
			return PBS_GSS_ERR_INTERNAL;
		}

		memcpy(gss_extra->clientname, client_name.value, client_name.length);
		gss_extra->clientname[client_name.length] = '\0';
	}

	if (ret == PBS_GSS_OK) {
		gss_extra->gssctx_established = 1;
		gss_extra->is_secure = (ret_flags & GSS_C_CONF_FLAG);
		if (gss_extra->role == AUTH_SERVER) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, "GSS context established with client %s", gss_extra->clientname);
		} else {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, "GSS context established with server %s", gss_extra->hostname);
		}
		GSS_LOG_DBG(gss_log_buffer);
	} else {
		if (gss_extra->role == AUTH_SERVER) {
			if (gss_extra->clientname)
				snprintf(gss_log_buffer, LOG_BUF_SIZE, "Failed to establish GSS context with client %s", gss_extra->clientname);
			else
				snprintf(gss_log_buffer, LOG_BUF_SIZE, "Failed to establish GSS context with client");
		} else {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, "Failed to establish GSS context with server %s", gss_extra->hostname);
		}
		GSS_LOG_ERR(gss_log_buffer);
		return PBS_GSS_ERR_CONTEXT_ESTABLISH;
	}

	return PBS_GSS_OK;
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
 *	pbs_auth_create_ctx - allocates external auth context structure for GSS authentication
 *
 * @param[in] ctx - pointer to external auth context to be allocated
 * @param[in] mode - AUTH_SERVER or AUTH_CLIENT
 * @param[in] conn_type - AUTH_USER_CONN or AUTH_SERVICE_CONN
 * @param[in] hostname - hostname of other authenticating party in case of AUTH_CLIENT else not used
 *
 * @return	int
 * @retval	0 - success
 * @retval	1 - error
 */
int
pbs_auth_create_ctx(void **ctx, int mode, int conn_type, const char *hostname)
{
	pbs_gss_extra_t *gss_extra = NULL;

	*ctx = NULL;

	gss_extra = (pbs_gss_extra_t *)calloc(1, sizeof(pbs_gss_extra_t));
	if (gss_extra == NULL) {
		return 1;
	}

	gss_extra->gssctx = GSS_C_NO_CONTEXT;
	gss_extra->role = mode;
	gss_extra->conn_type = conn_type;
	if (gss_extra->role == AUTH_SERVER) {
		char *hn = NULL;
		if ((hn = malloc(PBS_MAXHOSTNAME + 1)) == NULL) {
			return PBS_GSS_ERR_INTERNAL;
		}
		gethostname(hn, PBS_MAXHOSTNAME + 1);
		gss_extra->hostname = hn;
	} else {
		gss_extra->hostname = strdup(hostname);
		if (gss_extra->hostname == NULL) {
			return PBS_GSS_ERR_INTERNAL;
		}
	}

	*ctx = gss_extra;
	return 0;
}

/** @brief
 *	pbs_auth_destroy_ctx - destroy external auth context structure for GSS authentication
 *
 * @param[in] ctx - pointer to external auth context
 *
 * @return void
 */
void
pbs_auth_destroy_ctx(void *ctx)
{
	pbs_gss_extra_t *gss_extra = (pbs_gss_extra_t *)ctx;
	OM_uint32 min_stat = 0;

	if (gss_extra == NULL)
		return;

	free(gss_extra->hostname);
	free(gss_extra->clientname);

	if (gss_extra->gssctx != GSS_C_NO_CONTEXT)
		(void)gss_delete_sec_context(&min_stat, &gss_extra->gssctx, GSS_C_NO_BUFFER);

	memset(gss_extra, 0, sizeof(pbs_gss_extra_t));
	free(gss_extra);
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
 */
int
pbs_auth_get_userinfo(void *ctx, char **user, char **host, char **realm)
{
	pbs_gss_extra_t *gss_extra = (pbs_gss_extra_t *)ctx;

	*user = NULL;
	*host = NULL;
	*realm = NULL;

	if (gss_extra != NULL && gss_extra->clientname != NULL) {
		char *cn = NULL;
		char *p = NULL;

		cn = strdup(gss_extra->clientname);
		if (cn == NULL) {
			GSS_LOG_ERR("malloc failure");
			return 1;
		}
		p = strchr(cn, '@');
		if (p == NULL) {
			free(cn);
			GSS_LOG_ERR("Invalid clientname in auth context");
			return 1;
		}
		*p = '\0';
		if (strlen(cn) > PBS_MAXUSER || strlen(p + 1) > PBS_MAXHOSTNAME) {
			free(cn);
			GSS_LOG_ERR("Invalid clientname in auth context");
			return 1;
		}
		*user = strdup(cn);
		if (*user == NULL) {
			GSS_LOG_ERR("malloc failure");
			free(cn);
			return 1;
		}
		*realm = strdup(p + 1);
		if (*realm == NULL) {
			GSS_LOG_ERR("malloc failure");
			free(*user);
			*user = NULL;
			free(cn);
			return 1;
		}
		*host = strdup(*realm);
		if (*host == NULL) {
			GSS_LOG_ERR("malloc failure");
			free(*user);
			*user = NULL;
			free(*realm);
			*realm = NULL;
			free(cn);
			return 1;
		}
	}

	return 0;
}

/** @brief
 *	pbs_auth_process_handshake_data - do GSS auth handshake
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
	pbs_gss_extra_t *gss_extra = (pbs_gss_extra_t *) ctx;
	int rc = 0;

	if (gss_extra == NULL) {
		GSS_LOG_ERR("No auth context available");
		return 1;
	}

	if (gss_extra->gssctx_established) {
		GSS_LOG_ERR("GSS context already established");
		return 1;
	}

	*is_handshake_done = 0;

	pthread_once(&gss_init_lock_once, init_gss_lock);

	if (pthread_mutex_lock(&gss_lock) != 0) {
		GSS_LOG_ERR("Failed to lock gss mutex");
		return 1;
	}

	rc = pbs_gss_establish_context(gss_extra, data_in, len_in, data_out, len_out);

	if (pthread_mutex_unlock(&gss_lock) != 0) {
		GSS_LOG_ERR("Failed to unlock gss mutex");
		return 1;
	}

	if (gss_extra->gssctx_established) {
		*is_handshake_done = 1;

		if (gss_extra->role == AUTH_SERVER) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, "Entered encrypted communication with client %s", gss_extra->clientname);
			GSS_LOG_DBG(gss_log_buffer);
		} else {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, "Entered encrypted communication with server %s", gss_extra->hostname);
			GSS_LOG_DBG(gss_log_buffer);
		}
	}

	return rc;
}

/** @brief
 *	pbs_auth_encrypt_data - encrypt data based on given GSS context.
 *
 * @param[in] ctx - pointer to external auth context
 * @param[in] data_in - clear text data
 * @param[in] len_in - length of clear text data
 * @param[out] data_out - encrypted data
 * @param[out] len_out - length of encrypted data
 *
 * @return	int
 * @retval	0 on success
 * @retval	1 on error
 */
int
pbs_auth_encrypt_data(void *ctx, void *data_in, size_t len_in, void **data_out, size_t *len_out)
{
	pbs_gss_extra_t *gss_extra = (pbs_gss_extra_t *)ctx;
	OM_uint32 maj_stat;
	OM_uint32 min_stat = 0;
	gss_buffer_desc unwrapped;
	gss_buffer_desc wrapped;
	int conf_state = 0;

	if (gss_extra == NULL) {
		GSS_LOG_ERR("No auth context available");
		return PBS_GSS_ERR_INTERNAL;
	}

	if (len_in == 0) {
		GSS_LOG_ERR("No data available to encrypt");
		return PBS_GSS_ERR_INTERNAL;
	}

	wrapped.length = 0;
	wrapped.value = NULL;

	unwrapped.length = len_in;
	unwrapped.value  = data_in;

	maj_stat = gss_wrap(&min_stat, gss_extra->gssctx, gss_extra->is_secure, GSS_C_QOP_DEFAULT, &unwrapped, &conf_state, &wrapped);

	if (maj_stat != GSS_S_COMPLETE) {
		GSS_LOG_STS("gss_wrap", maj_stat, min_stat);

		maj_stat = gss_release_buffer(&min_stat, &wrapped);
		if (maj_stat != GSS_S_COMPLETE) {
			GSS_LOG_STS("gss_release_buffer", maj_stat, min_stat);
			return PBS_GSS_ERR_INTERNAL;
		}

		return PBS_GSS_ERR_WRAP;
	}

	*len_out = wrapped.length;
	*data_out = malloc(wrapped.length);
	if (*data_out == NULL) {
		GSS_LOG_ERR("malloc failure");
		return PBS_GSS_ERR_INTERNAL;
	}
	memcpy(*data_out, wrapped.value, wrapped.length);

	maj_stat = gss_release_buffer(&min_stat, &wrapped);
	if (maj_stat != GSS_S_COMPLETE) {
		GSS_LOG_STS("gss_release_buffer", maj_stat, min_stat);
		return PBS_GSS_ERR_INTERNAL;
	}

	return PBS_GSS_OK;
}

/** @brief
 *	pbs_auth_decrypt_data - decrypt data based on given GSS context.
 *
 * @param[in] ctx - pointer to external auth context
 * @param[in] data_in - encrypted data
 * @param[in] len_in - length of encrypted data
 * @param[out] data_out - clear text data
 * @param[out] len_out - length of clear text data
 *
 * @return	int
 * @retval	0 on success
 * @retval	1 on error
 */
int
pbs_auth_decrypt_data(void *ctx, void *data_in, size_t len_in, void **data_out, size_t *len_out)
{
	pbs_gss_extra_t *gss_extra = (pbs_gss_extra_t *)ctx;
	OM_uint32 maj_stat;
	OM_uint32 min_stat = 0;
	gss_buffer_desc wrapped;
	gss_buffer_desc unwrapped;

	if (gss_extra == NULL) {
		GSS_LOG_ERR("No auth context available");
		return PBS_GSS_ERR_INTERNAL;
	}

	if (len_in == 0) {
		GSS_LOG_ERR("No data available to decrypt");
		return PBS_GSS_ERR_INTERNAL;
	}

	if (gss_extra->is_secure == 0) {
		GSS_LOG_ERR("wrapped data ready but auth context is not secure");
		return PBS_GSS_ERR_INTERNAL;
	}

	unwrapped.length = 0;
	unwrapped.value = NULL;

	wrapped.length = len_in;
	wrapped.value = data_in;

	maj_stat = gss_unwrap(&min_stat, gss_extra->gssctx, &wrapped, &unwrapped, NULL, NULL);

	if (maj_stat != GSS_S_COMPLETE) {
		GSS_LOG_STS("gss_unwrap", maj_stat, min_stat);

		maj_stat = gss_release_buffer(&min_stat, &unwrapped);
		if (maj_stat != GSS_S_COMPLETE) {
			GSS_LOG_STS("gss_release_buffer", maj_stat, min_stat);
			return PBS_GSS_ERR_INTERNAL;
		}

		return PBS_GSS_ERR_UNWRAP;
	}

	if (unwrapped.length == 0)
		return PBS_GSS_ERR_UNWRAP;

	*len_out = unwrapped.length;
	*data_out = malloc(unwrapped.length);
	if (*data_out == NULL) {
		GSS_LOG_ERR("malloc failure");
		return PBS_GSS_ERR_INTERNAL;
	}
	memcpy(*data_out, unwrapped.value, unwrapped.length);

	maj_stat = gss_release_buffer(&min_stat, &unwrapped);
	if (maj_stat != GSS_S_COMPLETE) {
		GSS_LOG_STS("gss_release_buffer", maj_stat, min_stat);
		return PBS_GSS_ERR_INTERNAL;
	}

	return PBS_GSS_OK;
}

/********* END OF EXPORTED FUNCS *********/

#endif /* PBS_SECURITY */
