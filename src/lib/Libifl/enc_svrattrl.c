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


/**
 * @file	enc_svrattrl.c
 * @brief
 * encode_DIS_svrattrl() - encode a list of server "svrattrl" structures
 *
 *	The first item encoded is a unsigned integer, a count of the
 *	number of svrattrl entries in the linked list.  This is encoded
 *	even when there are no svrattrl entries in the list.
 *
 *	Each individual entry is then encoded as:
 *		u int	size of the three strings (name, resource, value)
 *			including the terminating nulls
 *		string	attribute name
 *		u int	1 or 0 if resource name does or does not follow
 *		string	resource name (if one)
 *		string  value of attribute/resource
 *		u int	"op" of attrlop
 *
 * @note
 *	the encoding of a svrattrl is the same as the encoding of
 *	the pbs_ifl.h structures "attrl" and "attrlop".  Any one of the
 *	three forms can be decoded into any of the three with the possible
 *	loss of the "flags" field (which is the "op" of the attrlop).
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include "libpbs.h"
#include "list_link.h"
#include "attribute.h"
#include "dis.h"

/**
 * @brief
 *	-encode a list of server "svrattrl" structures
 *
 * @par	Functionality:
 *		The first item encoded is a unsigned integer, a count of the
 *      	number of svrattrl entries in the linked list.  This is encoded
 *      	even when there are no svrattrl entries in the list.
 *
 * @par	Each individual entry is then encoded as:\n
 *			u int   size of the three strings (name, resource, value)
 *                      	including the terminating nulls\n
 *			string  attribute name\n
 *			u int   1 or 0 if resource name does or does not follow\n
 *			string  resource name (if one)\n
 *			string  value of attribute/resource\n
 *			u int   "op" of attrlop
 *
 * @param[in] sock - socket descriptor
 * @param[in] psattl - pointer to svr attr list
 *
 * @return      int
 * @retval      DIS_SUCCESS(0)  success
 * @retval      error code      error
 *
 */

int
encode_DIS_svrattrl(int sock, svrattrl *psattl)
{
	unsigned int ct = 0;
	unsigned int name_len;
	svrattrl *ps;
	int rc;

	/* count how many */

	for (ps = psattl; ps; ps = (svrattrl *)GET_NEXT(ps->al_link)) {
		++ct;
	}

	if ((rc = diswui(sock, ct)) != 0)
		return rc;

	for (ps = psattl; ps; ps = (svrattrl *)GET_NEXT(ps->al_link)) {
		/* length of three strings */
		name_len = (int)strlen(ps->al_atopl.name) +
			(int)strlen(ps->al_atopl.value) + 2;
		if (ps->al_atopl.resource)
			name_len += strlen(ps->al_atopl.resource) + 1;

		if ((rc = diswui(sock, name_len)) != 0)
			break;
		if ((rc = diswst(sock, ps->al_atopl.name)) != 0)
			break;
		if (ps->al_rescln) {	/* has a resource name */
			if ((rc = diswui(sock, 1)) != 0)
				break;
			if ((rc = diswst(sock, ps->al_atopl.resource)) != 0)
				break;
		} else {
			if ((rc = diswui(sock, 0)) != 0) /* no resource name */
				break;
		}
		if ((rc = diswst(sock, ps->al_atopl.value))	||
			(rc = diswui(sock, (unsigned int)ps->al_op)))
				break;
	}
	return rc;
}
