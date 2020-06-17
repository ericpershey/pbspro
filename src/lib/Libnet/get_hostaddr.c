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

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <memory.h>
#include <arpa/inet.h>
#include "portability.h"
#include "server_limits.h"
#include "pbs_ifl.h"
#include "net_connect.h"
#include "pbs_error.h"
#include "pbs_internal.h"

#if !defined(H_ERRNO_DECLARED)
extern int h_errno;
#endif


/**
 * @file	get_hostaddr.c
 * @brief
 * get_hostaddr.c - contains functions to provide the internal
 *	internet address for a host and to provide the port
 *	number for a service.
 *
 * get_hostaddr - get internal internet address of a host
 *
 *	Returns a pbs_net_t (unsigned long) containing the network
 *	address in host byte order.  A Null value is returned on
 *	an error.
 */

/**
 * @brief
 *	get internal internet address of a host
 *
 * @param[in] hostname - hostname whose internet addr to be returned
 *
 * @return	pbs_net_t
 * @retval	internat address	success
 * @retval	o			error
 *
 */
pbs_net_t
get_hostaddr(char *hostname)
{
	struct addrinfo *aip, *pai;
	struct addrinfo hints;
	struct sockaddr_in *inp;
	int		err;
	pbs_net_t	res;

	if ((hostname == 0) || (*hostname == '\0')) {
		pbs_errno = PBS_NET_RC_FATAL;
		return ((pbs_net_t)0);
	}

	memset(&hints, 0, sizeof(struct addrinfo));
	/*
	 *      Why do we use AF_UNSPEC rather than AF_INET?  Some
	 *      implementations of getaddrinfo() will take an IPv6
	 *      address and map it to an IPv4 one if we ask for AF_INET
	 *      only.  We don't want that - we want only the addresses
	 *      that are genuinely, natively, IPv4 so we start with
	 *      AF_UNSPEC and filter ai_family below.
	 */
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	if ((err = getaddrinfo(hostname, NULL, &hints, &pai)) != 0) {
		if (err == EAI_AGAIN)
			pbs_errno = PBS_NET_RC_RETRY;
		else
			pbs_errno = PBS_NET_RC_FATAL;
		return ((pbs_net_t)0);
	}
	for (aip = pai; aip != NULL; aip = aip->ai_next) {
		/* skip non-IPv4 addresses */
		if (aip->ai_family == AF_INET) {
			inp = (struct sockaddr_in *) aip->ai_addr;
			break;
		}
	}
	if (aip == NULL) {
		/* treat no IPv4 addresses as fatal getaddrinfo() failure */
		pbs_errno = PBS_NET_RC_FATAL;
		freeaddrinfo(pai);
		return ((pbs_net_t)0);
	}
	res = ntohl(inp->sin_addr.s_addr);
	freeaddrinfo(pai);
	return (res);
}

/**
 * @brief
 * 		compare a short hostname with a FQ host match if same up to dot
 *
 * @param[in]	shost	- short hostname
 * @param[in]	lhost	- FQ host
 *
 * @return	int
 * @retval	0	- match
 * @retval	1	- no match
 */
int
compare_short_hostname(char *shost, char *lhost)
{
	size_t   len;
	char    *pdot;
	int	is_shost_ip;
	int	is_lhost_ip;
	struct	sockaddr_in check_ip;

	if ((shost == NULL) || (lhost == NULL))
		return 1;

	/* check if hostnames given are in IPV4 dotted-decimal form: ddd.ddd.ddd.ddd */
	is_shost_ip = inet_pton(AF_INET, shost, &(check_ip.sin_addr));
	is_lhost_ip = inet_pton(AF_INET, lhost, &(check_ip.sin_addr));
	if ((is_shost_ip > 0) || (is_lhost_ip > 0)) {
		/* ((3 * 4) + 3) = 15 characters, max length dotted decimal addr */
		if (strncmp(shost, lhost, 15) == 0)
			return 0;
		return 1;
	}


	if ((pdot = strchr(shost, '.')) != NULL)
		len = (size_t)(pdot - shost);
	else
		len = strlen(shost);
	if ((strncasecmp(shost, lhost, len) == 0) &&
		((*(lhost+len) == '.') || (*(lhost+len) == '\0')))
		return 0;	/* match */
	else
		return 1;	/* no match */
}

/**
 * @brief
 *
 * comp_svraddr - get internal internet address of the given host
 *		  check to see if any of the addresses match the given server
 *		  net address.
 *
 *
 * @param[in] svr_addr - net address of the server
 * @param[in] hostname - hostname whose internet addr needs to be compared
 *
 * @return	int
 * @retval	0 address found
 * @retval	1 address not found
 * @retval	2 failed to find address
 *
 */
int
comp_svraddr(pbs_net_t svr_addr, char *hostname)
{
	struct addrinfo *aip, *pai;
	struct addrinfo hints;
	struct sockaddr_in *inp;
	pbs_net_t	res;

	if ((hostname == NULL) || (*hostname == '\0')) {
		return (2);
	}

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	if (getaddrinfo(hostname, NULL, &hints, &pai) != 0) {
		pbs_errno = PBSE_BADHOST;
		return (2);
	}
	for (aip = pai; aip != NULL; aip = aip->ai_next) {
		if (aip->ai_family == AF_INET) {
			inp = (struct sockaddr_in *) aip->ai_addr;
			res = ntohl(inp->sin_addr.s_addr);
			if (res == svr_addr) {
				freeaddrinfo(pai);
				return 0;
			}
		}
	}
	/* no match found */
	freeaddrinfo(pai);
	return (1);
}
