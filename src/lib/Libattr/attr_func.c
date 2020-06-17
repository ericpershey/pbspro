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

#include <assert.h>
#include <ctype.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "pbs_ifl.h"
#include "list_link.h"
#include "attribute.h"
#include "pbs_error.h"
#include "libpbs.h"

/**
 * @file	attr_func.c
 * @brief
 * 	This file contains general functions for manipulating attributes and attribute lists.
 *
 * @par Included are:
 *	clear_attr()
 *	find_attr()
 *	free_null()
 *	attrlist_alloc()
 *	attrlist_create()
 *	free_attrlist()
 *	parse_equal_string()
 *	parse_comma_string()
 *	count_substrings()
 *	add_to_svrattrl_list()
 *	add_to_svrattrl_list_sorted()
 *	copy_svrattrl_list()
 *	find_svrattrl_list_entry()
 *	get_svrattrl_flag()
 *	compare_svrattrl_list()
 *	svrattrl_to_str_array()
 *	str_array_to_svrattrl()
 *	str_array_to_str()
 *	env_array_to_str()
 *	str_to_str_array()
 *	free_str_array()
 *	strtok_quoted()
 *
 * The prototypes are declared in "attribute.h"
 */

/**
 * @brief
 * 	clear_attr - clear an attribute value structure and clear ATR_VFLAG_SET
 *
 * @param[in] pattr - pointer to attribute structure
 * @param[in] pdef - pointer to attribute_def structure
 *
 * @return	Void
 *
 */

void
clear_attr(attribute *pattr, struct attribute_def *pdef)
{
#ifndef NDEBUG
	if (pdef == 0) {
		(void)fprintf(stderr, "Assertion failed, bad pdef in clear_attr\n");
		abort();
	}
#endif
	(void)memset((char *)pattr, 0, sizeof(struct attribute));
	pattr->at_type  = pdef->at_type;
	if ((pattr->at_type == ATR_TYPE_RESC) ||
		(pattr->at_type == ATR_TYPE_LIST))
		CLEAR_HEAD(pattr->at_val.at_list);
}

/**
 * @brief
 * 	find_attr - find attribute definition by name
 *
 *	Searches array of attribute definition strutures to find one
 *	whose name matches the requested name.
 *
 * @param[in] attr_def - ptr to attribute definitions
 * @param[in] name - attribute name to find
 * @param[in] limit - limit on size of def array
 *
 * @return	int
 * @retval	>=0	index into definition struture array
 * @retval	-1	if didn't find matching name
 *
 */

int
find_attr(struct attribute_def *attr_def, char *name, int limit)
{
	int index;

	if (attr_def) {
		for (index = 0; index < limit; index++) {
			if (!strcasecmp(attr_def->at_name, name))
				return (index);
			attr_def++;
		}
	}
	return (-1);
}

/**
 * @brief
 * 	free_svrcache - free the cached svrattrl entries associated with an attribute
 *
 * @param[in] attr - pointer to attribute structure
 *
 * @return	Void
 *
 */

void
free_svrcache(struct attribute *attr)
{
	struct svrattrl *working;
	struct svrattrl *sister;

	working = attr->at_user_encoded;
	if ((working != NULL) && (--working->al_refct <= 0)) {
		while (working) {
			sister = working->al_sister;
			delete_link(&working->al_link);
			(void)free(working);
			working = sister;
		}
	}
	attr->at_user_encoded = NULL;

	working = attr->at_priv_encoded;
	if ((working != NULL) && (--working->al_refct <= 0)) {
		while (working) {
			sister = working->al_sister;
			delete_link(&working->al_link);
			(void)free(working);
			working = sister;
		}
	}
	attr->at_priv_encoded = NULL;
}

/**
 * @brief
 *	free_null - A free routine for attributes which do not
 *	have malloc-ed space ( boolean, char, long ).
 *
 * @param[in] attr - pointer to attribute structure
 *
 * @return	Void
 *
 */
/*ARGSUSED*/
void
free_null(struct attribute *attr)
{
	memset(&attr->at_val, 0, sizeof(attr->at_val));
	if (attr->at_type == ATR_TYPE_SIZE)
		attr->at_val.at_size.atsv_shift = 10;
	attr->at_flags &= ~(ATR_VFLAG_SET|ATR_VFLAG_INDIRECT|ATR_VFLAG_TARGET);
	if (attr->at_user_encoded != NULL || attr->at_priv_encoded != NULL)
		free_svrcache(attr);
}

/**
 * @brief
 * 		decode_null - Null attribute decode routine for Read Only (server
 *		and queue ) attributes.  It just returns 0.
 *
 * @param[in]	patr	-	not used
 * @param[in]	name	-	not used
 * @param[in]	rn	-	not used
 * @param[in]	val	-	not used
 *
 * @return	zero
 */

int
decode_null(attribute *patr, char *name, char *rn, char *val)
{
	return 0;
}

/**
 * @brief
 * 		set_null - Null set routine for Read Only attributes.
 *
 * @param[in]	pattr	-	not used
 * @param[in]	new	-	not used
 * @param[in]	op	-	not used
 *
 * @return	zero
 */

int
set_null(attribute *pattr, attribute *new, enum batch_op op)
{
	return 0;
}

/**
 * @brief
 * 	comp_null - A do nothing, except return 0, attribute comparison
 *	function.
 *
 * @param[in] attr - pointer to attribute structure
 * @param[in] with - pointer to attribute structure
 *
 * @return	int
 * @retval	0
 *
 */

int
comp_null(struct attribute *attr, struct attribute *with)
{
	return 0;
}


/**
 * @brief
 * 	attrlist_alloc - allocate space for an svrattrl structure entry
 *
 *	The space required for the entry is calculated and allocated.
 *	The total size and three string lengths are set in the entry,
 *	but no values are placed in it.
 *
 * @param[in] szname - string size for name
 * @param[in] szresc - string size for resource
 * @param[in] szval - string size for value
 *
 * @return 	svrattrl *
 * @retval	ptr to entry 	on success
 * @retval	NULL 		if error
 *
 */

svrattrl *
attrlist_alloc(int szname, int szresc, int szval)
{
	register size_t tsize;
	svrattrl *pal;

	if (szname < 0 || szresc < 0 || szval < 0)
		return NULL;
	tsize = sizeof(svrattrl) + szname + szresc + szval;
	pal = (svrattrl *)malloc(tsize);
	if (pal == NULL)
		return NULL;
#ifdef DEBUG
	memset(pal, 0, sizeof(svrattrl));
#endif

	CLEAR_LINK(pal->al_link);	/* clear link */
	pal->al_sister	   = NULL;
	pal->al_atopl.next = 0;
	pal->al_tsize = tsize;		/* set various string sizes */
	pal->al_nameln = szname;
	pal->al_rescln = szresc;
	pal->al_valln  = szval;
	pal->al_flags  = 0;
	pal->al_op     = SET;
	pal->al_name = (char *)pal + sizeof(svrattrl);
	if (szresc)
		pal->al_resc = pal->al_name + szname;
	else
		pal->al_resc = NULL;
	pal->al_value = pal->al_name + szname + szresc;
	pal->al_refct = 0;
	return (pal);
}

/**
 * @brief
 * 	attrlist_create - create an svrattrl structure entry
 *
 *	The space required for the entry is calculated and allocated.
 * 	The attribute and resource name is copied into the entry.
 * 	Note, the value string should be inserted by the caller after this returns.
 *
 * @param[in] aname - attribute name
 * @param[in] rname - resource name if needed or null
 * @param[in] vsize - size of resource value
 *
 * @return      svrattrl *
 * @retval      ptr to entry    on success
 * @retval      NULL            if error
 *
 */

svrattrl *
attrlist_create(char  *aname, char  *rname, int vsize)
{
	svrattrl *pal;
	size_t	     asz;
	size_t	     rsz;

	asz = strlen(aname) + 1;     /* attribute name,allow for null term */

	if (rname == NULL)      /* resource name only if type resource */
		rsz = 0;
	else
		rsz = strlen(rname) + 1;

	pal = attrlist_alloc(asz, rsz, vsize + 1);
	if (pal != NULL) {
		strcpy(pal->al_name, aname);    /* copy name right after struct */
		if (rsz)
			strcpy(pal->al_resc, rname);
		pal->al_refct++;
	}
	return (pal);
}

/**
 * @brief
 *	free_attrlist - free the space allocated to a list of svrattrl
 *	structures
 *
 * @param[in] pattrlisthead - Pointer to the head of the linked list to free
 *
 * @return	Void
 *
 */
void
free_attrlist(pbs_list_head *pattrlisthead)
{
	free_svrattrl((svrattrl *)GET_NEXT(*pattrlisthead));
}

/**
 * @brief
 *	free an attribute list
 *
 * @param[in] pal - Pointer to the attribute list
 *
 * @return void
 *
 */
void
free_svrattrl(svrattrl *pal)
{
	svrattrl *nxpal;
	svrattrl *sister;

	while (pal != NULL) {
		if (--pal->al_refct <= 0) {
			/* if we have any sisters, need to delete them now */
			/* just in case we end up deleting them later and  */
			/* still pointing to them			   */
			sister = pal->al_sister;
			while (sister) {
				nxpal = sister->al_sister;
				delete_link(&sister->al_link);
				(void)free(sister);
				sister = nxpal;
			}
		}
		nxpal = (struct svrattrl *)GET_NEXT(pal->al_link);
		delete_link(&pal->al_link);
		if (pal->al_refct <= 0)
			(void)free(pal);
		pal = nxpal;
	}
}

/**
 * @brief
 * 	parse_comma_string() - parse a string of the form:
 *		value1 [, value2 ...]
 *
 *	On the first call, start is non null, a pointer to the first value
 *	element upto a comma, new-line, or end of string is returned.
 *
 *	On any following calls with start set to a null pointer NULL,
 *	the next value element is returned...
 *
 *	A null pointer is returned when there are no (more) value elements.
 */

char *
parse_comma_string(char *start)
{
	static char *pc;	/* if start is null, restart from here */

	char	    *back;
	char	    *rv;

	if (start != NULL)
		pc = start;

	if (*pc == '\0')
		return NULL;	/* already at end, no strings */

	/* skip over leading white space */

	while ((*pc != '\n') && isspace((int)*pc) && *pc)
		pc++;

	rv = pc;		/* the start point which will be returned */

	/* go find comma or end of line */

	while (*pc) {
		if (((*pc == ',') && ((rv == pc) || (*(pc - 1) != ESC_CHAR))) || (*pc == '\n'))
			break;
		++pc;
	}
	back = pc;
	while (isspace((int)*--back))	/* strip trailing spaces */
		*back = '\0';

	if (*pc)
		*pc++ = '\0';	/* if not end, terminate this and adv past */

	return (rv);
}




/**
 * @brief
 * 	count_substrings - counts number of substrings in a comma separated string
 *
 * @see parse_comma_string
 *
 * @param[in] val - comma separated string of substrings
 * @param[in] pcnt - where to return the value
 *
 * @return	int
 * @retval	0			success
 * @retval	PBSE error code		error
 */
int
count_substrings(char *val, int *pcnt)
{
	int	rc = 0;
	int	ns;
	char   *pc;

	if (val == NULL)
		return  (PBSE_INTERNAL);
	/*
	 * determine number of substrings, each sub string is terminated
	 * by a non-escaped comma or a new-line, the whole string is terminated
	 * by a null
	 */

	ns = 1;
	for (pc = val; *pc; pc++) {
		if (*pc == ESC_CHAR) {
			if (*(pc + 1))
				pc++;
		} else {
			if (*pc == ',' || *pc == '\n')
				++ns;
		}
	}
	if (pc > val)
		pc--;
	if ((*pc == '\n') || (*pc == ',')) {
		if ((pc > val) && (*(pc - 1) != ESC_CHAR)) {
			/* strip trailing empty string */
			ns--;
			*pc = '\0';
		}
	}

	*pcnt = ns;
	return rc;
}



/**
 * @brief
 * 	attrl_fixlink - fix up the next pointer within the attropl substructure
 *	within a svrattrl list.
 *
 * @param[in] phead - pointer to head of svrattrl list
 *
 * @return	Void
 *
 */

void
attrl_fixlink(pbs_list_head *phead)
{
	svrattrl *pal;
	svrattrl *pnxt;

	pal = (svrattrl *)GET_NEXT(*phead);
	while (pal) {
		pnxt = (svrattrl *)GET_NEXT(pal->al_link);
		if (pal->al_flags & ATR_VFLAG_DEFLT) {
			pal->al_atopl.op = DFLT;
		} else {
			pal->al_atopl.op = SET;
		}
		if (pnxt)
			pal->al_atopl.next = &pnxt->al_atopl;
		else
			pal->al_atopl.next = NULL;
		pal = pnxt;
	}
}

/**
 * @brief
 * 	free_none - when scheduler modifies accrue_type, we don't
 *            want to delete previous value.
 *
 * @param[in] attr - pointer to attribute structure
 *
 * @return	Void
 *
 */

void
free_none(struct attribute *attr)
{
	/* do nothing */
	/* to be used for accrue_type attribute of job */
	if (attr->at_user_encoded != NULL || attr->at_priv_encoded != NULL) {
		free_svrcache(attr);
	}
}

/**
 * @brief
 * 	Adds a new entry (name_str, resc_str, val_str, flag) to the 'phead'
 *	svrattrl list.
 *	If 'name_prefix' is not NULL, then instead of adding 'name_str',
 *	add 'name_prefix.name_str'.
 *
 * @param[in/out]	phead - head of the svrattrl list to be populated.
 * @param[in]		name_str - the name field
 * @param[in]		resc_str - the resource name field
 * @param[in]		val_str - the value field.
 * @param[in]		flag - the flag entry
 * @param[in]		name_prefix - string to prefix the 'name_str'
 *
 * @return int
 * @retval 0 for sucess
 * @retval -1 for error
 *
 */
int
add_to_svrattrl_list(pbs_list_head *phead, char *name_str, char *resc_str,
	char *val_str, unsigned int flag, char *name_prefix)
{
	svrattrl 	 *psvrat = NULL;
	int		 valln = 0;
	char 	*tmp_str = NULL;
	size_t	sz;
	char	*the_str;

	if (name_str == NULL)
		return -1;

	the_str = name_str;

	if (name_prefix != NULL) {

		/* for <name_prefix>.<name_str>\0 */
		sz = strlen(name_prefix)+strlen(name_str)+2;
		tmp_str = (char *)malloc(sz);
		if (tmp_str == NULL) {
			return -1;
		} else {
			snprintf(tmp_str, sz, "%s.%s", name_prefix, name_str);
			the_str = tmp_str;
		}
	}

	if (val_str) {
		valln = (int)strlen(val_str) + 1;
	}
	psvrat = attrlist_create(the_str, resc_str, valln);

	free(tmp_str);

	if (!psvrat) {
		return -1;
	}
	if (val_str) {
		strcpy(psvrat->al_value, val_str);
	}
	psvrat->al_flags = flag;
	append_link(phead, &psvrat->al_link, psvrat);

	return 0;
}

/**
 * @brief
 * 	Adds a new entry (name_str, resc_str, val_str, flag) to the 'phead'
 *	svrattrl list in a sorted (by [name_prefix.]name_str) way.
 *
 * @param[in]	phead	- pointer to the targeted list.
 * @param[in]	name_str - fills in the svrattrl al_name field
 * @param[in]	resc_str - fills in the svrattrl al_resc field
 * @param[in]	val_str - fills in the svrattrl al_value field
 * @param[in]	flag - fills in the svrattrl al_flags field
 * @param[in]	name_prefix - string to prefix the 'name_str'
 *
 * @return int
 * @retval 0	success
 * @retval -1	error
 */
int
add_to_svrattrl_list_sorted(pbs_list_head *phead, char *name_str, char *resc_str,
	char *val_str, unsigned int flag, char *name_prefix)
{
	svrattrl 	*psvrat = NULL;
	int		valln = 0;
	pbs_list_link	*plink_cur;
	svrattrl 	*psvr_cur;
	char 	*tmp_str = NULL;
	size_t	sz;
	char	*the_str;

	the_str = name_str;

	if (name_prefix != NULL) {

		/* for <name_prefix>.<name_str>\0 */
		sz = strlen(name_prefix)+strlen(name_str)+2;
		tmp_str = (char *)malloc(sz);
		if (tmp_str == NULL) {
			return -1;
		} else {
			snprintf(tmp_str, sz, "%s.%s", name_prefix, name_str);
			the_str = tmp_str;
		}
	}


	if (val_str) {
		valln = (int)strlen(val_str) + 1;
	}
	psvrat = attrlist_create(the_str, resc_str, valln);

	if (tmp_str != NULL)
		free(tmp_str);

	if (!psvrat) {
		return -1;
	}
	if (val_str) {
		strcpy(psvrat->al_value, val_str);
	}
	psvrat->al_flags = flag;

	plink_cur = phead;
	psvr_cur = (svrattrl *)GET_NEXT(*phead);

	while (psvr_cur) {
		plink_cur = &psvr_cur->al_link;

		if (strcmp(psvr_cur->al_name, psvrat->al_name) > 0) {
			break;
		}
		psvr_cur = (svrattrl *)GET_NEXT(*plink_cur);
	}

	if (psvr_cur) {
		/* link before 'current' svrattrl in list */
		insert_link(plink_cur, &psvrat->al_link, psvrat, LINK_INSET_BEFORE);
	} else {
		/* attach either at the beginning or the last of the list */
		insert_link(plink_cur, &psvrat->al_link, psvrat, LINK_INSET_AFTER);
	}
	return 0;
}

/**
 * @brief
 * 	Copies contents of list headed by 'from_head' into 'to_head'
 *
 * @param[in]		from_head	- source list
 * @param[in,out]	to_head		- destination list
 *
 * @return int
 * @retval 0	- success
 * @retval -1	- failure
 *
 */
int
copy_svrattrl_list(pbs_list_head *from_head, pbs_list_head *to_head)
{
	svrattrl *plist = NULL;

	if ((from_head == NULL) || (to_head == NULL))
		return -1;

	CLEAR_HEAD((*to_head));
	plist = (svrattrl *)GET_NEXT((*from_head));
	while (plist) {

		if (add_to_svrattrl_list(to_head, plist->al_name, plist->al_resc,
			plist->al_value, plist->al_op, NULL) == -1) {
			free_attrlist(to_head);
			return -1;
		}

		plist = (svrattrl *)GET_NEXT(plist->al_link);
	}
	return 0;
}

/**
 * @brief
 * 	returns the svrattrl list matching 'name' and 'resc' (if resc is non-NULL)
 *  @param[in]	phead	- list being searched
 *  *param[in]	name	- search name
 *  @param[in]	resc	- search resource
 *
 *  @retval	*svrattrl
 *
 *  @retval	<pointer to the matching svrattrl entry>
 *  @retval	NULL - none found
 */
svrattrl *
find_svrattrl_list_entry(pbs_list_head *phead, char *name, char *resc)
{
	svrattrl *plist = NULL;

	if (!name)
		return NULL;

	plist = (svrattrl *)GET_NEXT(*phead);
	while (plist) {


		if ((strcmp(plist->al_name, name) == 0) &&
			(!resc || (strcmp(plist->al_resc, resc) == 0))) {
			return plist;
		}

		plist = (svrattrl *)GET_NEXT(plist->al_link);
	}
	return NULL;
}

/**
 * @brief
 * 	 Checks svrattrl_list to see if 'name' and 'resc' (if set) appear
 * as al_name and al_resc values. if so, return that entry's al_flags value.
 *
 * @param[in]	name - al_name value to match
 * @param[in]	resc - al_resc value to match
 * @param[in]	hook_set_flag -  if set to 1, then add the ATR_VFLAG_HOOK flag
 * 			to the return value of al_flags
 * @return	int
 * @retval	ATR_VFLAG_HOOK	if there's no matching entry found for 'name' and
 *				'resc', but 'hook_set_flag' is set to 1.
 * @retval	0		if there's no matching entry found for 'name' and
 * 				'resc', and 'hook_set_flag' is 0.
 * @retval	<value>		al_flags matching entry for 'name' and 'resc',
 * 				appended with ATR_VFLAG_HOOK if 'hook_set_flag' is
 * 				set to 1.
 *
 */
unsigned int
get_svrattrl_flag(char *name, char *resc, char *val,
	pbs_list_head *svrattrl_list, int hook_set_flag)
{
	svrattrl *svrattrl_e;
	unsigned int flag = 0;

	/* get the flag to set */
	if ((svrattrl_e=find_svrattrl_list_entry(svrattrl_list, name, resc)) != NULL)
		flag = svrattrl_e->al_flags;

	if (hook_set_flag == 1)
		flag |= ATR_VFLAG_HOOK;

	return (flag);
}

/**
 * @brief
 *	Compares 2 svrattrl linked lists.
 *
 * @param[in]	l1 - svrattrl list #1
 * @param[in]	l2 - svrattrl list #2
 *
 * @return int
 * @retval 1	if the 2 lists are the same
 * @retval 0	otherwise
 */
int
compare_svrattrl_list(pbs_list_head *l1, pbs_list_head *l2)
{
	pbs_list_head	list1;
	pbs_list_head	list2;
	svrattrl	*pal1 = NULL;
	svrattrl	*pal2 = NULL;
	svrattrl	*nxpal1 = NULL;
	svrattrl	*nxpal2 = NULL;
	int		rc;
	int		found_match = 0;

	if (copy_svrattrl_list(l1, &list1) == -1) {
		rc  = 0;
		goto compare_svrattrl_list_exit;
	}
	if (copy_svrattrl_list(l2, &list2) == -1) {
		rc  = 0;
		goto compare_svrattrl_list_exit;
	}

	/* now compare the 2 lists */
	pal1 = (svrattrl *)GET_NEXT(list1);
	while (pal1 != NULL) {

		nxpal1 = (svrattrl *)GET_NEXT(pal1->al_link);

		pal2 = (svrattrl *)GET_NEXT(list2);
		found_match = 0;
		while (pal2 != NULL) {
			nxpal2 = (struct svrattrl *)GET_NEXT(pal2->al_link);
			if ((strcmp(pal1->al_name, pal2->al_name) == 0) &&
				(strcmp(pal1->al_value, pal2->al_value) == 0)) {
				found_match = 1;
				delete_link(&pal2->al_link);
				free(pal2);

				delete_link(&pal1->al_link);
				free(pal1);
				break;
			}

			pal2 = nxpal2;
		}
		if (!found_match) {
			rc = 0;
			goto compare_svrattrl_list_exit;
		}
		pal1 = nxpal1;
	}
	pal1 = (svrattrl *)GET_NEXT(list1);
	pal2 = (svrattrl *)GET_NEXT(list2);

	if ((pal1 == NULL) && (pal2 == NULL)) {
		rc = 1;
	} else {
		rc = 0;
	}

compare_svrattrl_list_exit:
	free_attrlist(&list1);
	free_attrlist(&list2);

	return (rc);
}

/**
 * @brief
 *	Free up malloc-ed entries of a 'str_array' and the array itself.
 *
 * @param[in]	str_array	- array of strings terminated by a NULL entry
 *
 * @return void
 *
 */
void
free_str_array(char **str_array)
{
	int	i;

	if (str_array == NULL)
		return;

	i=0;
	while (str_array[i]) {
		free(str_array[i]);
		i++;
	}
	free(str_array);
}

/**
 * @brief
 * 	Given a 'pbs_list', store the al_value field values into
 * 	a string array, and return that array.
 *
 * @param[in]	pbs_list	- the source list
 *
 * @return	char **
 * @retval	pointer to the string array
 * @retval	NULL	- could not allocate memory or input invalid.
 */
char **
svrattrl_to_str_array(pbs_list_head *pbs_list)
{
	int	i;
	int	len;
	char **str_array = NULL;
	svrattrl *plist = NULL;

	if (pbs_list == NULL)
		return NULL;

	/* calculate the list size */
	len = 0;
	plist = (svrattrl *)GET_NEXT(*pbs_list);
	while (plist) {
		if (plist->al_value == NULL) {
			return NULL;
		}

		len++;
		plist = (svrattrl *)GET_NEXT(plist->al_link);
	}

	/* add one more entry to calloc for the terminating NULL entry */
	str_array = (char **)calloc(len+1, sizeof(char *));
	if (str_array == NULL) {
		return NULL;
	}

	plist = (svrattrl *)GET_NEXT(*pbs_list);
	i=0;
	while (plist) {
		if (plist->al_value != NULL) {
			str_array[i] = strdup(plist->al_value);
			if (str_array[i] == NULL) {
				free_str_array(str_array);
				return NULL;
			}
		}
		plist = (svrattrl *)GET_NEXT(plist->al_link);
		i++;
	}
	return (str_array);
}
/**
 * @brief
 * 	Given a string array 'str_array', dumps its contents
 * 	into the 'to_head' list, in the same order as indexed
 * 	in the array.
 *
 * @param[in]		str_array - the array of strings to dump
 * @param[in,out]	to_head - the destination list
 * @param[in]		name_str - name to associate the values with
 *
 * @return	int
 * @retval	0	- success
 * @retval	-1	- error
 *
 */
int
str_array_to_svrattrl(char **str_array, pbs_list_head *to_head, char *name_str)
{
	int	i;

	if ((str_array == NULL) || (to_head == NULL))
		return -1;

	CLEAR_HEAD((*to_head));
	i=0;
	while (str_array[i]) {
		if (add_to_svrattrl_list(to_head, name_str, NULL, str_array[i], 0, NULL) == -1) {
			/* clear what we've accumulated so far*/
			free_attrlist(to_head);
			CLEAR_HEAD((*to_head));
			return -1;
		}
		i++;
	}
	return (0);

}

/**
 * @brief
 * 	Given a string array 'str_array', return a malloc-ed
 * 	string, containing the entries of 'str_array' separated
 *	by 'delimiter'.
 * @note
 *	Need to free() returned value.
 *
 * @param[in]	str_array - the array of strings to dump
 * @param[in]	delimiter  - the separator character used in the resultant
 *				string.
 *
 * @return	char *
 * @retval	<string>	- pointer to a malloced area holding
 *				  the contents of 'str_array'.
 * @retval	NULL		- error or the input <string> passed
 * 				  is empty.
 *
 */
char	*
str_array_to_str(char **str_array, char delimiter)
{
	int	i, j, len;
	char	*ret_string = NULL;

	if (str_array == NULL)
		return NULL;

	len=0;
	i=0;

	while (str_array[i]) {

		len += strlen(str_array[i]);
		len++;	/* for 'delimiter' */
		i++;
	}
	len++;	/* for trailing '\0' */

	if (len > 1) { /* not just an empty string */

		ret_string = (char *)malloc(len);

		if (ret_string == NULL)
			return NULL;
		i=0;
		while (str_array[i]) {

			if (i == 0) {
				strcpy(ret_string, str_array[i]);
			} else {
				j = strlen(ret_string);
				ret_string[j] = delimiter;
				ret_string[j+1] = '\0';
				strcat(ret_string, str_array[i]);
			}
			i++;
		}
	}
	return (ret_string);

}

/**
 * @brief
 * 	Given a 'delimiter'-separated string 'str', store the
 * 	the string entities into
 * 	a string array, and return that array.
 * @note
 *	Need to free() returned value.
 *
 * @param[in]	str_array - the array of strings to dump
 * @param[in]	delimiter  - the delimiter to match.
 *
 * @return	char *
 * @retval	<string>	- pointer to a malloced area holding
 *				  the contents of 'str_array'.
 * @retval	NULL		- error
 *
 */
char	**
str_to_str_array(char *str, char delimiter)
{
	int	i;
	int	len;
	char 	**str_array = NULL;
	char	*str1;
	char	*p;

	if (str == NULL)
		return NULL;

	/* calculate the list size */
	len = 0;

	str1 = strdup(str);
	if (str1 == NULL)
		return NULL;

	len=0;
	p = strtok_quoted(str1, delimiter);
	while (p) {
		len++;
		p = strtok_quoted(NULL, delimiter);
	}
	(void)free(str1);

	/* add one more entry to calloc for the terminating NULL entry */
	str_array = (char **)calloc(len+1, sizeof(char *));
	if (str_array == NULL) {
		return NULL;
	}
	str1 = strdup(str);
	if (str1 == NULL) {
		free_str_array(str_array);
		return NULL;
	}
	p = strtok_quoted(str1, delimiter);
	i = 0;
	while (p) {
		str_array[i] = strdup(p);
		if (str_array[i] == NULL) {
			free_str_array(str_array);
			free(str1);
			return NULL;
		}
		i++;
		p = strtok_quoted(NULL, delimiter);
	}
	free(str1);

	return (str_array);
}



/**
 * @brief
 * 	Given a environment string array 'env_array' where there are
 *	<var>=<value> entries, return a malloc-ed
 * 	string, containing the entries of 'env_array' separated
 *	by 'delimiter'.
 *
 * @note
 *	Need to free() returned value.
 *	If 'env_array' has a <value> entry containing the 'delimiter' character,
 *	then it is escaped (using ESC_CHAR). Similarly, if <value> contains the escape
 *	character, then that is also escaped.
 *	Ex:  env_array_to_str(envstr, ',')
 *		where   envstr[0]='HOME=/home/somebody'
 *			envstr[1]='G_FILENAME_ENCODING=@locale,UTF-8,ISO-8859-15,CP1252'
 *	then string returned will be:
 *		'HOME=/home/somebody,G_FILENAME_ENCODING="@locale\,UTF-8\,ISO-8859-15\,CP1252"'
 *
 * @param[in]	env_array - the environment array of strings to dump
 * @param[in]	delimiter  - the separator character
 *
 * @return	char *
 * @retval	<string>	- pointer to a malloced area holding
 *				  the contents of 'env_array'.
 * @retval	NULL		- error or the input <string> passed
 * 				  is empty.
 *
 */
char	*
env_array_to_str(char **env_array, char delimiter)
{
	int	i, j, len;
	char	*ret_string = NULL;
	int	escape = 0;
	char	*var = NULL;
	char	*val = NULL;
	char	*pc = NULL;
	char	*pc2 = NULL;

	if (env_array == NULL)
		return NULL;

	len=0;
	i=0;

	while (env_array[i]) {
		val  = strchr(env_array[i], '=');
		if (val != NULL) {
			val++;
			escape = 0;
			for (pc2 = val; *pc2 != 0; pc2++) {
				if ((*pc2 == delimiter) || (*pc2 == ESC_CHAR)) {
					escape++;
				}
			}

		}

		len += strlen(env_array[i]);
		if (escape > 0) {
			len += escape;	/* the ESC_CHAR */
		}
		len++;	/* for delimiter */
		i++;
	}
	len++;	/* for trailing '\0' */

	if (len > 1) { /* not just an empty string */

		ret_string = (char *)malloc(len);

		if (ret_string == NULL)
			return NULL;
		i=0;
		while (env_array[i]) {
			var = env_array[i];
			pc = strchr(env_array[i], '=');
			val = NULL;
			if (pc != NULL) {
				*pc = '\0';
				val = pc+1;
			}

			if (i == 0) {
				sprintf(ret_string, "%s=", var);
			} else {
				j = strlen(ret_string);
				ret_string[j] = delimiter;
				ret_string[j+1] = '\0';
				strcat(ret_string, var);
				strcat(ret_string, "=");
			}
			if (val != NULL) {
				pc2 = ret_string + strlen(ret_string);
				while (*val != '\0') {
					if (( *val == delimiter) ||
					    		(*val == ESC_CHAR)) {
						*pc2 = ESC_CHAR;
						pc2++;
					}
					*pc2++ = *val++;
				}
				*pc2 = '\0';
			}

			if (pc != NULL)
				*pc = '=';	/* restore */
			i++;
		}
	}
	return (ret_string);

}

/* @brief
 * 	Function that takes a string 'str', and modifies it "in-place",
 *	removing each escape backslash preceding the character being
 *	escaped.
 *
 * @param[in,out]	str	- input string.
 *
 * @return void
 *
 */
static void
prune_esc_backslash(char *str)
{

	int s, d, skip_idx;

	if (str == NULL)
		return;

	s = 0;	/* source */
	d = 0;  /* dest */

	/* initialize to an index that cannot be matched at the start */
	skip_idx = -2;

	do {
		while ((str[s] == ESC_CHAR) && (skip_idx != (s - 1))) {
			skip_idx = s;
			s++;
		}
		str[d++] = str[s++];
	} while (str[s-1] != '\0');
}

/**
 * @brief
 * 	Like strtok, except this understands quoted (unescaped) substrings
 * 	(single quotes, or double quotes) and include the value as is.
 *	 For instance, given_str: 'foo_float=1.5,foo_stra="glad,elated"some,squote=',foo_size=10mb,dquote="'
 *	string, this would return tokens:
 *		strtok_quoted(given_str, ',')=foo_float=1.5
 *		strtok_quoted(NULL,',')=foo_stra="glad,elated"some
 *		strtok_quoted(NULL,',')=squote='
 * 		strtok_quoted(NULL,',')=foo_size=10mb
 *		strtok_quoted(NULL,',')=dquote="
 *
 * @param[in]	source - input string
 * @param[in]	delimiter  - each element in this string represents the delimeter to match.
 *
 * @return	char *
 * @retval	<string token>
 * @retval	NULL if end of processing, or problem found with quoted string.
 */
char *
strtok_quoted(char *source, char delimiter)
{
	static char *pc = NULL;	/* save pointer position */
	char *stok = NULL;  	/* token to return */
	char *quoted = NULL;

	if (source != NULL) {
		pc = source;
	}

	if ((pc == NULL) || (*pc == '\0'))
		return NULL;

	for (stok = pc; *pc != 0; pc++) {

		/* must not match <ESC_CHAR><delim> or <ESC_CHAR><ESC_CHAR><delim>
		 * the latter means <ESC_CHAR> is the one escaped not <delim>
		 */
		if ((*pc == delimiter) &&
			(((pc - 1) < stok) || (*(pc - 1) != ESC_CHAR) ||
			 ((pc - 2) < stok) || (*(pc - 2) == ESC_CHAR))) {
			*pc = '\0';
			pc++;
			prune_esc_backslash(stok);
			return (stok);
		}

		/* check for a quoted value and advance
		 * pointer up to the closing quote. If a non-escaped
		 * delimiter appears first, ex. "apple,bee", this will
		 * return the token string just before the delimiter
		 * (ex. "apple).
		 */
		if ((*pc == '\'') || (*pc == '"')) {

			/* if immediately following the quote, try to match
			 * one of:
			 * 	'<null>
			 *	'<delimiter>
			 * 	"<null>
			 *	"<delimiter>
			 */
			if ((*(pc+1) == '\0') || (*(pc+1) == delimiter)) {
				pc++;
				if (*pc != '\0') {
					*pc = '\0';
					pc++;
				}
				prune_esc_backslash(stok);
				return (stok);
			}
			/* Otherwise, look for the matching endquote<delimiter>
			 * or <endquote<null>
			 * if not, just use the value as is, up to but not
			 * including the non-escaped <delimiter>.
			 */
			quoted = pc;
			while (*++pc) {
				if (*pc == *quoted) {
					if ((*(pc+1) == '\0') ||
						(*(pc+1) == delimiter)) {
						quoted = NULL;
						break;
					}
				} else if ((*pc == delimiter) &&
					(((pc-1) < stok) || (*(pc-1) != ESC_CHAR) ||
			 		((pc-2) < stok) || (*(pc-2) == ESC_CHAR))) {
					*pc = '\0';
					pc++;
					prune_esc_backslash(stok);
					return (stok);
				}
			}

			if (quoted != NULL) { /* didn't find a close quote */
				pc = NULL;	/* use quote value as is */
				prune_esc_backslash(stok);
				return (stok);
			}
		}
	}

	prune_esc_backslash(stok);
	return (stok);
}

/**
 * @brief	Convert an attropl struct to an attrl struct
 *
 * @param[in]	from - the attropl struct to convert
 *
 * @return struct attrl*
 * @retval a newly converted attrl struct
 * @retval NULL on error
 */
struct attrl *
attropl2attrl(struct attropl *from)
{
	struct attrl *ap = NULL, *rattrl = NULL;

	while (from != NULL) {
		if (ap == NULL) {
			if ((ap = new_attrl()) == NULL) {
				perror("Out of memory");
				return NULL;
			}
			rattrl = ap;
		}
		else  {
			if ((ap->next = new_attrl()) == NULL) {
				perror("Out of memory");
				return NULL;
			}
			ap = ap->next;
		}

		if (from->name != NULL) {
			if ((ap->name = (char*) malloc(strlen(from->name) + 1)) == NULL) {
				perror("Out of memory");
				free_attrl_list(rattrl);
				return NULL;
			}
			strcpy(ap->name, from->name);
		}
		if (from->resource != NULL) {
			if ((ap->resource = (char*) malloc(strlen(from->resource) + 1)) == NULL) {
				perror("Out of memory");
				free_attrl_list(rattrl);
				return NULL;
			}
			strcpy(ap->resource, from->resource);
		}
		if (from->value != NULL) {
			if ((ap->value = (char*) malloc(strlen(from->value) + 1)) == NULL) {
				perror("Out of memory");
				free_attrl_list(rattrl);
				return NULL;
			}
			strcpy(ap->value, from->value);
		}
		from = from->next;
	}

	return rattrl;
}

/**
 *  @brief attrl copy constructor
 *
 *  @param[in] oattr - attrl to dup
 *
 *  @return dup'd attrl
 */

struct attrl *
dup_attrl(struct attrl *oattr)
{
	struct attrl *nattr;

	if (oattr == NULL)
		return NULL;

	nattr = new_attrl();
	if (nattr == NULL)
		return NULL;
	if (oattr->name != NULL)
		nattr->name = strdup(oattr->name);
	if (oattr->resource != NULL)
		nattr->resource = strdup(oattr->resource);
	if (oattr->value != NULL)
		nattr->value = strdup(oattr->value);

	nattr->op = oattr->op;
	return nattr;
}

/**
 * @brief copy constructor for attrl list
 * @param oattr_list - list to dup
 * @return dup'd attrl list
 */

struct attrl *
dup_attrl_list(struct attrl *oattr_list)
{
	struct attrl *nattr_head = NULL;
	struct attrl *nattr;
	struct attrl *nattr_prev = NULL;
	struct attrl *oattr;

	if (oattr_list == NULL)
		return NULL;

	for (oattr = oattr_list; oattr != NULL; oattr = oattr->next) {
		nattr = dup_attrl(oattr);
		if (nattr_prev == NULL) {
			nattr_head = nattr;
			nattr_prev = nattr_head;
		} else {
			nattr_prev->next = nattr;
			nattr_prev = nattr;
		}
	}
	return nattr_head;
}

/**
 *	@brief create a new attrl structure and initialize it
 */
struct attrl *
new_attrl()
{
	struct attrl *at;

	if ((at = malloc(sizeof(struct attrl))) == NULL)
		return NULL;

	at->next = NULL;
	at->name = NULL;
	at->resource = NULL;
	at->value = NULL;
	at->op = SET;

	return at;
}

/**
 * @brief frees attrl structure
 *
 * @param [in] at - attrl to free
 * @return nothing
 */
void
free_attrl(struct attrl *at)
 {
	if (at == NULL)
		return;

	free(at->name);
	free(at->resource);
	free(at->value);

	free(at);
}

/**
 * @brief frees attrl list
 *
 * @param[in] at_list - attrl list to free
 * @return nothing
 */
void free_attrl_list(struct attrl *at_list)
{
	struct attrl *cur, *tmp;
	if(at_list == NULL )
		return;

	for(cur = at_list; cur != NULL; cur = tmp) {
		tmp = cur->next;
		free_attrl(cur);
	}

}

/**
 * @brief	Helper function to remove flag(s) from an array of attributes
 *
 * @param[in]	pattr - pointer to the attribute list
 * @param[in]	flags - the flags to unset
 * @param[in]	numattrs - number of attributes
 *
 * @return void
 */
void
unset_attr_array_flags(attribute *pattr, int flags, int numattrs)
{
	int i;

	if (pattr == NULL || numattrs < 1)
		return;

	for (i = 0; i < numattrs; i++) {
		(pattr + i)->at_flags &= ~flags;
	}
}
