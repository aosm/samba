/* 
 *  Unix SMB/Netbios implementation.
 *  Version 1.9.
 *  RPC Pipe client / server routines
 *  Copyright (C) Andrew Tridgell              1992-1998,
 *  Copyright (C) Jeremy R. Allison            1995-2003.
 *  Copyright (C) Luke Kenneth Casson Leighton 1996-1998,
 *  Copyright (C) Paul Ashton                  1997-1998.
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "includes.h"

#undef DBGC_CLASS
#define DBGC_CLASS DBGC_RPC_PARSE

/*******************************************************************
 Sets up a SEC_ACCESS structure.
********************************************************************/

void init_sec_access(SEC_ACCESS *t, uint32 mask)
{
	t->mask = mask;
}

/*******************************************************************
 Reads or writes a SEC_ACCESS structure.
********************************************************************/

BOOL sec_io_access(const char *desc, SEC_ACCESS *t, prs_struct *ps, int depth)
{
	if (t == NULL)
		return False;

	prs_debug(ps, depth, desc, "sec_io_access");
	depth++;
	
	if(!prs_uint32("mask", ps, depth, &t->mask))
		return False;

	return True;
}

/*******************************************************************
 Check if ACE has OBJECT type.
********************************************************************/

BOOL sec_ace_object(uint8 type)
{
	if (type == SEC_ACE_TYPE_ACCESS_ALLOWED_OBJECT ||
            type == SEC_ACE_TYPE_ACCESS_DENIED_OBJECT ||
            type == SEC_ACE_TYPE_SYSTEM_AUDIT_OBJECT ||
            type == SEC_ACE_TYPE_SYSTEM_ALARM_OBJECT) {
		return True;
	}
	return False;
}

/*******************************************************************
 copy a SEC_ACE structure.
********************************************************************/
void sec_ace_copy(SEC_ACE *ace_dest, SEC_ACE *ace_src)
{
	ace_dest->type  = ace_src->type;
	ace_dest->flags = ace_src->flags;
	ace_dest->size  = ace_src->size;
	ace_dest->info.mask = ace_src->info.mask;
	ace_dest->obj_flags = ace_src->obj_flags;
	memcpy(&ace_dest->obj_guid, &ace_src->obj_guid, GUID_SIZE);
	memcpy(&ace_dest->inh_guid, &ace_src->inh_guid, GUID_SIZE);	
	sid_copy(&ace_dest->trustee, &ace_src->trustee);
}

/*******************************************************************
 Sets up a SEC_ACE structure.
********************************************************************/

void init_sec_ace(SEC_ACE *t, DOM_SID *sid, uint8 type, SEC_ACCESS mask, uint8 flag)
{
	t->type = type;
	t->flags = flag;
	t->size = sid_size(sid) + 8;
	t->info = mask;

	ZERO_STRUCTP(&t->trustee);
	sid_copy(&t->trustee, sid);
}

/*******************************************************************
 Reads or writes a SEC_ACE structure.
********************************************************************/

BOOL sec_io_ace(const char *desc, SEC_ACE *psa, prs_struct *ps, int depth)
{
	uint32 old_offset;
	uint32 offset_ace_size;

	if (psa == NULL)
		return False;

	prs_debug(ps, depth, desc, "sec_io_ace");
	depth++;
	
	old_offset = prs_offset(ps);

	if(!prs_uint8("type ", ps, depth, &psa->type))
		return False;

	if(!prs_uint8("flags", ps, depth, &psa->flags))
		return False;

	if(!prs_uint16_pre("size ", ps, depth, &psa->size, &offset_ace_size))
		return False;

	if(!sec_io_access("info ", &psa->info, ps, depth))
		return False;

	/* check whether object access is present */
	if (!sec_ace_object(psa->type)) {
		if (!smb_io_dom_sid("trustee  ", &psa->trustee , ps, depth))
			return False;
	} else {
		if (!prs_uint32("obj_flags", ps, depth, &psa->obj_flags))
			return False;

		if (psa->obj_flags & SEC_ACE_OBJECT_PRESENT)
			if (!prs_uint8s(False, "obj_guid", ps, depth, psa->obj_guid.info, GUID_SIZE))
				return False;

		if (psa->obj_flags & SEC_ACE_OBJECT_INHERITED_PRESENT)
			if (!prs_uint8s(False, "inh_guid", ps, depth, psa->inh_guid.info, GUID_SIZE))
				return False;

		if(!smb_io_dom_sid("trustee  ", &psa->trustee , ps, depth))
			return False;
	}

	if(!prs_uint16_post("size ", ps, depth, &psa->size, offset_ace_size, old_offset))
		return False;
	return True;
}

/*******************************************************************
 adds new SID with its permissions to ACE list
********************************************************************/

NTSTATUS sec_ace_add_sid(TALLOC_CTX *ctx, SEC_ACE **new, SEC_ACE *old, unsigned *num, DOM_SID *sid, uint32 mask)
{
	unsigned int i = 0;
	
	if (!ctx || !new || !old || !sid || !num)  return NT_STATUS_INVALID_PARAMETER;

	*num += 1;
	
	if((new[0] = (SEC_ACE *) talloc_zero(ctx, (*num) * sizeof(SEC_ACE))) == 0)
		return NT_STATUS_NO_MEMORY;

	for (i = 0; i < *num - 1; i ++)
		sec_ace_copy(&(*new)[i], &old[i]);

	(*new)[i].type  = 0;
	(*new)[i].flags = 0;
	(*new)[i].size  = SEC_ACE_HEADER_SIZE + sid_size(sid);
	(*new)[i].info.mask = mask;
	sid_copy(&(*new)[i].trustee, sid);
	return NT_STATUS_OK;
}

/*******************************************************************
  modify SID's permissions at ACL 
********************************************************************/

NTSTATUS sec_ace_mod_sid(SEC_ACE *ace, size_t num, DOM_SID *sid, uint32 mask)
{
	unsigned int i = 0;

	if (!ace || !sid)  return NT_STATUS_INVALID_PARAMETER;

	for (i = 0; i < num; i ++) {
		if (sid_compare(&ace[i].trustee, sid) == 0) {
			ace[i].info.mask = mask;
			return NT_STATUS_OK;
		}
	}
	return NT_STATUS_NOT_FOUND;
}

/*******************************************************************
 delete SID from ACL
********************************************************************/

static NTSTATUS sec_ace_del_sid(TALLOC_CTX *ctx, SEC_ACE **new, SEC_ACE *old, uint32 *num, DOM_SID *sid)
{
	unsigned int i     = 0;
	unsigned int n_del = 0;

	if (!ctx || !new || !old || !sid || !num)  return NT_STATUS_INVALID_PARAMETER;

	if((new[0] = (SEC_ACE *) talloc_zero(ctx, (*num) * sizeof(SEC_ACE))) == 0)
		return NT_STATUS_NO_MEMORY;

	for (i = 0; i < *num; i ++) {
		if (sid_compare(&old[i].trustee, sid) != 0)
			sec_ace_copy(&(*new)[i], &old[i]);
		else
			n_del ++;
	}
	if (n_del == 0)
		return NT_STATUS_NOT_FOUND;
	else {
		*num -= n_del;
		return NT_STATUS_OK;
	}
}

/*******************************************************************
 Create a SEC_ACL structure.  
********************************************************************/

SEC_ACL *make_sec_acl(TALLOC_CTX *ctx, uint16 revision, int num_aces, SEC_ACE *ace_list)
{
	SEC_ACL *dst;
	int i;

	if((dst = (SEC_ACL *)talloc_zero(ctx,sizeof(SEC_ACL))) == NULL)
		return NULL;

	dst->revision = revision;
	dst->num_aces = num_aces;
	dst->size = SEC_ACL_HEADER_SIZE;

	/* Now we need to return a non-NULL address for the ace list even
	   if the number of aces required is zero.  This is because there
	   is a distinct difference between a NULL ace and an ace with zero
	   entries in it.  This is achieved by checking that num_aces is a
	   positive number. */

	if ((num_aces) && 
            ((dst->ace = (SEC_ACE *)talloc(ctx, sizeof(SEC_ACE) * num_aces)) 
             == NULL)) {
		return NULL;
	}
        
	for (i = 0; i < num_aces; i++) {
		dst->ace[i] = ace_list[i]; /* Structure copy. */
		dst->size += ace_list[i].size;
	}

	return dst;
}

/*******************************************************************
 Duplicate a SEC_ACL structure.  
********************************************************************/

SEC_ACL *dup_sec_acl(TALLOC_CTX *ctx, SEC_ACL *src)
{
	if(src == NULL)
		return NULL;

	return make_sec_acl(ctx, src->revision, src->num_aces, src->ace);
}

/*******************************************************************
 Reads or writes a SEC_ACL structure.  

 First of the xx_io_xx functions that allocates its data structures
 for you as it reads them.
********************************************************************/

BOOL sec_io_acl(const char *desc, SEC_ACL **ppsa, prs_struct *ps, int depth)
{
	unsigned int i;
	uint32 old_offset;
	uint32 offset_acl_size;
	SEC_ACL *psa;

	/*
	 * Note that the size is always a multiple of 4 bytes due to the
	 * nature of the data structure.  Therefore the prs_align() calls
	 * have been removed as they through us off when doing two-layer
	 * marshalling such as in the printing code (NEW_BUFFER).  --jerry
	 */

	if (ppsa == NULL)
		return False;

	psa = *ppsa;

	if(UNMARSHALLING(ps) && psa == NULL) {
		/*
		 * This is a read and we must allocate the stuct to read into.
		 */
		if((psa = (SEC_ACL *)prs_alloc_mem(ps, sizeof(SEC_ACL))) == NULL)
			return False;
		*ppsa = psa;
	}

	prs_debug(ps, depth, desc, "sec_io_acl");
	depth++;
	
	old_offset = prs_offset(ps);

	if(!prs_uint16("revision", ps, depth, &psa->revision))
		return False;

	if(!prs_uint16_pre("size     ", ps, depth, &psa->size, &offset_acl_size))
		return False;

	if(!prs_uint32("num_aces ", ps, depth, &psa->num_aces))
		return False;

	if (UNMARSHALLING(ps)) {
		/*
		 * Even if the num_aces is zero, allocate memory as there's a difference
		 * between a non-present DACL (allow all access) and a DACL with no ACE's
		 * (allow no access).
		 */
		if((psa->ace = (SEC_ACE *)prs_alloc_mem(ps,sizeof(psa->ace[0]) * (psa->num_aces+1))) == NULL)
			return False;
	}

	for (i = 0; i < psa->num_aces; i++) {
		fstring tmp;
		slprintf(tmp, sizeof(tmp)-1, "ace_list[%02d]: ", i);
		if(!sec_io_ace(tmp, &psa->ace[i], ps, depth))
			return False;
	}

	if(!prs_uint16_post("size     ", ps, depth, &psa->size, offset_acl_size, old_offset))
		return False;

	return True;
}

/*******************************************************************
 Works out the linearization size of a SEC_DESC.
********************************************************************/

size_t sec_desc_size(SEC_DESC *psd)
{
	size_t offset;

	if (!psd) return 0;

	offset = SEC_DESC_HEADER_SIZE;

	/* don't align */

	if (psd->owner_sid != NULL)
		offset += sid_size(psd->owner_sid);

	if (psd->grp_sid != NULL)
		offset += sid_size(psd->grp_sid);

	if (psd->sacl != NULL)
		offset += psd->sacl->size;

	if (psd->dacl != NULL)
		offset += psd->dacl->size;

	return offset;
}

/*******************************************************************
 Compares two SEC_ACE structures
********************************************************************/

BOOL sec_ace_equal(SEC_ACE *s1, SEC_ACE *s2)
{
	/* Trivial case */

	if (!s1 && !s2) return True;

	/* Check top level stuff */

	if (s1->type != s2->type || s1->flags != s2->flags ||
	    s1->info.mask != s2->info.mask) {
		return False;
	}

	/* Check SID */

	if (!sid_equal(&s1->trustee, &s2->trustee)) {
		return False;
	}

	return True;
}

/*******************************************************************
 Compares two SEC_ACL structures
********************************************************************/

BOOL sec_acl_equal(SEC_ACL *s1, SEC_ACL *s2)
{
	unsigned int i, j;

	/* Trivial cases */

	if (!s1 && !s2) return True;
	if (!s1 || !s2) return False;

	/* Check top level stuff */

	if (s1->revision != s2->revision) {
		DEBUG(10, ("sec_acl_equal(): revision differs (%d != %d)\n",
			   s1->revision, s2->revision));
		return False;
	}

	if (s1->num_aces != s2->num_aces) {
		DEBUG(10, ("sec_acl_equal(): num_aces differs (%d != %d)\n",
			   s1->revision, s2->revision));
		return False;
	}

	/* The ACEs could be in any order so check each ACE in s1 against 
	   each ACE in s2. */

	for (i = 0; i < s1->num_aces; i++) {
		BOOL found = False;

		for (j = 0; j < s2->num_aces; j++) {
			if (sec_ace_equal(&s1->ace[i], &s2->ace[j])) {
				found = True;
				break;
			}
		}

		if (!found) return False;
	}

	return True;
}

/*******************************************************************
 Compares two SEC_DESC structures
********************************************************************/

BOOL sec_desc_equal(SEC_DESC *s1, SEC_DESC *s2)
{
	/* Trivial case */

	if (!s1 && !s2) {
		goto done;
	}

	/* Check top level stuff */

	if (s1->revision != s2->revision) {
		DEBUG(10, ("sec_desc_equal(): revision differs (%d != %d)\n",
			   s1->revision, s2->revision));
		return False;
	}

	if (s1->type!= s2->type) {
		DEBUG(10, ("sec_desc_equal(): type differs (%d != %d)\n",
			   s1->type, s2->type));
		return False;
	}

	/* Check owner and group */

	if (!sid_equal(s1->owner_sid, s2->owner_sid)) {
		fstring str1, str2;

		sid_to_string(str1, s1->owner_sid);
		sid_to_string(str2, s2->owner_sid);

		DEBUG(10, ("sec_desc_equal(): owner differs (%s != %s)\n",
			   str1, str2));
		return False;
	}

	if (!sid_equal(s1->grp_sid, s2->grp_sid)) {
		fstring str1, str2;

		sid_to_string(str1, s1->grp_sid);
		sid_to_string(str2, s2->grp_sid);

		DEBUG(10, ("sec_desc_equal(): group differs (%s != %s)\n",
			   str1, str2));
		return False;
	}

	/* Check ACLs present in one but not the other */

	if ((s1->dacl && !s2->dacl) || (!s1->dacl && s2->dacl) ||
	    (s1->sacl && !s2->sacl) || (!s1->sacl && s2->sacl)) {
		DEBUG(10, ("sec_desc_equal(): dacl or sacl not present\n"));
		return False;
	}

	/* Sigh - we have to do it the hard way by iterating over all
	   the ACEs in the ACLs */

	if (!sec_acl_equal(s1->dacl, s2->dacl) ||
	    !sec_acl_equal(s1->sacl, s2->sacl)) {
		DEBUG(10, ("sec_desc_equal(): dacl/sacl list not equal\n"));
		return False;
	}

 done:
	DEBUG(10, ("sec_desc_equal(): secdescs are identical\n"));
	return True;
}

/*******************************************************************
 Merge part of security descriptor old_sec in to the empty sections of 
 security descriptor new_sec.
********************************************************************/

SEC_DESC_BUF *sec_desc_merge(TALLOC_CTX *ctx, SEC_DESC_BUF *new_sdb, SEC_DESC_BUF *old_sdb)
{
	DOM_SID *owner_sid, *group_sid;
	SEC_DESC_BUF *return_sdb;
	SEC_ACL *dacl, *sacl;
	SEC_DESC *psd = NULL;
	uint16 secdesc_type;
	size_t secdesc_size;

	/* Copy over owner and group sids.  There seems to be no flag for
	   this so just check the pointer values. */

	owner_sid = new_sdb->sec->owner_sid ? new_sdb->sec->owner_sid :
		old_sdb->sec->owner_sid;

	group_sid = new_sdb->sec->grp_sid ? new_sdb->sec->grp_sid :
		old_sdb->sec->grp_sid;
	
	secdesc_type = new_sdb->sec->type;

	/* Ignore changes to the system ACL.  This has the effect of making
	   changes through the security tab audit button not sticking. 
	   Perhaps in future Samba could implement these settings somehow. */

	sacl = NULL;
	secdesc_type &= ~SEC_DESC_SACL_PRESENT;

	/* Copy across discretionary ACL */

	if (secdesc_type & SEC_DESC_DACL_PRESENT) {
		dacl = new_sdb->sec->dacl;
	} else {
		dacl = old_sdb->sec->dacl;
	}

	/* Create new security descriptor from bits */

	psd = make_sec_desc(ctx, new_sdb->sec->revision, 
			    owner_sid, group_sid, sacl, dacl, &secdesc_size);

	return_sdb = make_sec_desc_buf(ctx, secdesc_size, psd);

	return(return_sdb);
}

/*******************************************************************
 Creates a SEC_DESC structure
********************************************************************/

SEC_DESC *make_sec_desc(TALLOC_CTX *ctx, uint16 revision, 
			DOM_SID *owner_sid, DOM_SID *grp_sid,
			SEC_ACL *sacl, SEC_ACL *dacl, size_t *sd_size)
{
	SEC_DESC *dst;
	uint32 offset     = 0;

	*sd_size = 0;

	if(( dst = (SEC_DESC *)talloc_zero(ctx, sizeof(SEC_DESC))) == NULL)
		return NULL;

	dst->revision = revision;
	dst->type     = SEC_DESC_SELF_RELATIVE;

	if (sacl) dst->type |= SEC_DESC_SACL_PRESENT;
	if (dacl) dst->type |= SEC_DESC_DACL_PRESENT;

	dst->off_owner_sid = 0;
	dst->off_grp_sid   = 0;
	dst->off_sacl      = 0;
	dst->off_dacl      = 0;

	if(owner_sid && ((dst->owner_sid = sid_dup_talloc(ctx,owner_sid)) == NULL))
		goto error_exit;

	if(grp_sid && ((dst->grp_sid = sid_dup_talloc(ctx,grp_sid)) == NULL))
		goto error_exit;

	if(sacl && ((dst->sacl = dup_sec_acl(ctx, sacl)) == NULL))
		goto error_exit;

	if(dacl && ((dst->dacl = dup_sec_acl(ctx, dacl)) == NULL))
		goto error_exit;

	offset = SEC_DESC_HEADER_SIZE;

	/*
	 * Work out the linearization sizes.
	 */

	if (dst->sacl != NULL) {
		dst->off_sacl = offset;
		offset += dst->sacl->size;
	}

	if (dst->dacl != NULL) {
		dst->off_dacl = offset;
		offset += dst->dacl->size;
	}

	if (dst->owner_sid != NULL) {
		dst->off_owner_sid = offset;
		offset += sid_size(dst->owner_sid);
	}

	if (dst->grp_sid != NULL) {
		dst->off_grp_sid = offset;
		offset += sid_size(dst->grp_sid);
	}

	*sd_size = (size_t)offset;
	return dst;

error_exit:

	*sd_size = 0;
	return NULL;
}

/*******************************************************************
 Duplicate a SEC_DESC structure.  
********************************************************************/

SEC_DESC *dup_sec_desc( TALLOC_CTX *ctx, SEC_DESC *src)
{
	size_t dummy;

	if(src == NULL)
		return NULL;

	return make_sec_desc( ctx, src->revision, 
				src->owner_sid, src->grp_sid, src->sacl,
				src->dacl, &dummy);
}

/*******************************************************************
 Creates a SEC_DESC structure with typical defaults.
********************************************************************/

SEC_DESC *make_standard_sec_desc(TALLOC_CTX *ctx, DOM_SID *owner_sid, DOM_SID *grp_sid,
				 SEC_ACL *dacl, size_t *sd_size)
{
	return make_sec_desc(ctx, SEC_DESC_REVISION,
			     owner_sid, grp_sid, NULL, dacl, sd_size);
}

/*******************************************************************
 Reads or writes a SEC_DESC structure.
 If reading and the *ppsd = NULL, allocates the structure.
********************************************************************/

BOOL sec_io_desc(const char *desc, SEC_DESC **ppsd, prs_struct *ps, int depth)
{
	uint32 old_offset;
	uint32 max_offset = 0; /* after we're done, move offset to end */
	uint32 tmp_offset = 0;
	
	SEC_DESC *psd;

	if (ppsd == NULL)
		return False;

	psd = *ppsd;

	if (psd == NULL) {
		if(UNMARSHALLING(ps)) {
			if((psd = (SEC_DESC *)prs_alloc_mem(ps,sizeof(SEC_DESC))) == NULL)
				return False;
			*ppsd = psd;
		} else {
			/* Marshalling - just ignore. */
			return True;
		}
	}

	prs_debug(ps, depth, desc, "sec_io_desc");
	depth++;

#if 0	
	/*
	 * if alignment is needed, should be done by the the 
	 * caller.  Not here.  This caused me problems when marshalling
	 * printer info into a buffer.   --jerry
	 */
	if(!prs_align(ps))
		return False;
#endif
	
	/* start of security descriptor stored for back-calc offset purposes */
	old_offset = prs_offset(ps);

	if(!prs_uint16("revision ", ps, depth, &psd->revision))
		return False;

	if(!prs_uint16("type     ", ps, depth, &psd->type))
		return False;

	if(!prs_uint32("off_owner_sid", ps, depth, &psd->off_owner_sid))
		return False;

	if(!prs_uint32("off_grp_sid  ", ps, depth, &psd->off_grp_sid))
		return False;

	if(!prs_uint32("off_sacl     ", ps, depth, &psd->off_sacl))
		return False;

	if(!prs_uint32("off_dacl     ", ps, depth, &psd->off_dacl))
		return False;

	max_offset = MAX(max_offset, prs_offset(ps));

	if (psd->off_owner_sid != 0) {

		tmp_offset = prs_offset(ps);
		if(!prs_set_offset(ps, old_offset + psd->off_owner_sid))
			return False;

		if (UNMARSHALLING(ps)) {
			/* reading */
			if((psd->owner_sid = (DOM_SID *)prs_alloc_mem(ps,sizeof(*psd->owner_sid))) == NULL)
				return False;
		}

		if(!smb_io_dom_sid("owner_sid ", psd->owner_sid , ps, depth))
			return False;

		max_offset = MAX(max_offset, prs_offset(ps));

		if (!prs_set_offset(ps,tmp_offset))
			return False;
	}

	if (psd->off_grp_sid != 0) {

		tmp_offset = prs_offset(ps);
		if(!prs_set_offset(ps, old_offset + psd->off_grp_sid))
			return False;

		if (UNMARSHALLING(ps)) {
			/* reading */
			if((psd->grp_sid = (DOM_SID *)prs_alloc_mem(ps,sizeof(*psd->grp_sid))) == NULL)
				return False;
		}

		if(!smb_io_dom_sid("grp_sid", psd->grp_sid, ps, depth))
			return False;
			
		max_offset = MAX(max_offset, prs_offset(ps));

		if (!prs_set_offset(ps,tmp_offset))
			return False;
	}

	if ((psd->type & SEC_DESC_SACL_PRESENT) && psd->off_sacl) {
		tmp_offset = prs_offset(ps);
		if(!prs_set_offset(ps, old_offset + psd->off_sacl))
			return False;
		if(!sec_io_acl("sacl", &psd->sacl, ps, depth))
			return False;
		max_offset = MAX(max_offset, prs_offset(ps));
		if (!prs_set_offset(ps,tmp_offset))
			return False;
	}


	if ((psd->type & SEC_DESC_DACL_PRESENT) && psd->off_dacl != 0) {
		tmp_offset = prs_offset(ps);
		if(!prs_set_offset(ps, old_offset + psd->off_dacl))
			return False;
		if(!sec_io_acl("dacl", &psd->dacl, ps, depth))
			return False;
		max_offset = MAX(max_offset, prs_offset(ps));
		if (!prs_set_offset(ps,tmp_offset))
			return False;
	}

	if(!prs_set_offset(ps, max_offset))
		return False;
	return True;
}

/*******************************************************************
 Creates a SEC_DESC_BUF structure.
********************************************************************/

SEC_DESC_BUF *make_sec_desc_buf(TALLOC_CTX *ctx, size_t len, SEC_DESC *sec_desc)
{
	SEC_DESC_BUF *dst;

	if((dst = (SEC_DESC_BUF *)talloc_zero(ctx, sizeof(SEC_DESC_BUF))) == NULL)
		return NULL;

	/* max buffer size (allocated size) */
	dst->max_len = (uint32)len;
	dst->len = (uint32)len;
	
	if(sec_desc && ((dst->sec = dup_sec_desc(ctx, sec_desc)) == NULL)) {
		return NULL;
	}

	dst->ptr = 0x1;

	return dst;
}

/*******************************************************************
 Duplicates a SEC_DESC_BUF structure.
********************************************************************/

SEC_DESC_BUF *dup_sec_desc_buf(TALLOC_CTX *ctx, SEC_DESC_BUF *src)
{
	if(src == NULL)
		return NULL;

	return make_sec_desc_buf( ctx, src->len, src->sec);
}

/*******************************************************************
 Reads or writes a SEC_DESC_BUF structure.
********************************************************************/

BOOL sec_io_desc_buf(const char *desc, SEC_DESC_BUF **ppsdb, prs_struct *ps, int depth)
{
	uint32 off_len;
	uint32 off_max_len;
	uint32 old_offset;
	uint32 size;
	SEC_DESC_BUF *psdb;

	if (ppsdb == NULL)
		return False;

	psdb = *ppsdb;

	if (UNMARSHALLING(ps) && psdb == NULL) {
		if((psdb = (SEC_DESC_BUF *)prs_alloc_mem(ps,sizeof(SEC_DESC_BUF))) == NULL)
			return False;
		*ppsdb = psdb;
	}

	prs_debug(ps, depth, desc, "sec_io_desc_buf");
	depth++;

	if(!prs_align(ps))
		return False;
	
	if(!prs_uint32_pre("max_len", ps, depth, &psdb->max_len, &off_max_len))
		return False;

	if(!prs_uint32    ("ptr  ", ps, depth, &psdb->ptr))
		return False;

	if(!prs_uint32_pre("len    ", ps, depth, &psdb->len, &off_len))
		return False;

	old_offset = prs_offset(ps);

	/* reading, length is non-zero; writing, descriptor is non-NULL */
	if ((UNMARSHALLING(ps) && psdb->len != 0) || (MARSHALLING(ps) && psdb->sec != NULL)) {
		if(!sec_io_desc("sec   ", &psdb->sec, ps, depth))
			return False;
	}

	if(!prs_align(ps))
		return False;
	
	size = prs_offset(ps) - old_offset;
	if(!prs_uint32_post("max_len", ps, depth, &psdb->max_len, off_max_len, size == 0 ? psdb->max_len : size))
		return False;

	if(!prs_uint32_post("len    ", ps, depth, &psdb->len, off_len, size))
		return False;

	return True;
}

/*******************************************************************
 Add a new SID with its permissions to SEC_DESC.
********************************************************************/

NTSTATUS sec_desc_add_sid(TALLOC_CTX *ctx, SEC_DESC **psd, DOM_SID *sid, uint32 mask, size_t *sd_size)
{
	SEC_DESC *sd   = 0;
	SEC_ACL  *dacl = 0;
	SEC_ACE  *ace  = 0;
	NTSTATUS  status;

	*sd_size = 0;

	if (!ctx || !psd || !sid || !sd_size)
		return NT_STATUS_INVALID_PARAMETER;

	status = sec_ace_add_sid(ctx, &ace, psd[0]->dacl->ace, &psd[0]->dacl->num_aces, sid, mask);
	
	if (!NT_STATUS_IS_OK(status))
		return status;

	if (!(dacl = make_sec_acl(ctx, psd[0]->dacl->revision, psd[0]->dacl->num_aces, ace)))
		return NT_STATUS_UNSUCCESSFUL;
	
	if (!(sd = make_sec_desc(ctx, psd[0]->revision, psd[0]->owner_sid, 
		psd[0]->grp_sid, psd[0]->sacl, dacl, sd_size)))
		return NT_STATUS_UNSUCCESSFUL;

	*psd = sd;
	 sd  = 0;
	return NT_STATUS_OK;
}

/*******************************************************************
 Modify a SID's permissions in a SEC_DESC.
********************************************************************/

NTSTATUS sec_desc_mod_sid(SEC_DESC *sd, DOM_SID *sid, uint32 mask)
{
	NTSTATUS status;

	if (!sd || !sid)
		return NT_STATUS_INVALID_PARAMETER;

	status = sec_ace_mod_sid(sd->dacl->ace, sd->dacl->num_aces, sid, mask);

	if (!NT_STATUS_IS_OK(status))
		return status;
	
	return NT_STATUS_OK;
}

/*******************************************************************
 Delete a SID from a SEC_DESC.
********************************************************************/

NTSTATUS sec_desc_del_sid(TALLOC_CTX *ctx, SEC_DESC **psd, DOM_SID *sid, size_t *sd_size)
{
	SEC_DESC *sd   = 0;
	SEC_ACL  *dacl = 0;
	SEC_ACE  *ace  = 0;
	NTSTATUS  status;

	*sd_size = 0;
	
	if (!ctx || !psd[0] || !sid || !sd_size)
		return NT_STATUS_INVALID_PARAMETER;

	status = sec_ace_del_sid(ctx, &ace, psd[0]->dacl->ace, &psd[0]->dacl->num_aces, sid);

	if (!NT_STATUS_IS_OK(status))
		return status;

	if (!(dacl = make_sec_acl(ctx, psd[0]->dacl->revision, psd[0]->dacl->num_aces, ace)))
		return NT_STATUS_UNSUCCESSFUL;
	
	if (!(sd = make_sec_desc(ctx, psd[0]->revision, psd[0]->owner_sid, 
		psd[0]->grp_sid, psd[0]->sacl, dacl, sd_size)))
		return NT_STATUS_UNSUCCESSFUL;

	*psd = sd;
	 sd  = 0;
	return NT_STATUS_OK;
}

/*******************************************************************
  Comparison function to sort non-inherited first.
*******************************************************************/

static int nt_ace_inherit_comp( SEC_ACE *a1, SEC_ACE *a2)
{
	int a1_inh = a1->flags & SEC_ACE_FLAG_INHERITED_ACE;
	int a2_inh = a2->flags & SEC_ACE_FLAG_INHERITED_ACE;

	if (a1_inh == a2_inh)
		return 0;

	if (!a1_inh && a2_inh)
		return -1;
	return 1;
}

/*******************************************************************
  Comparison function to apply the order explained below in a group.
*******************************************************************/

static int nt_ace_canon_comp( SEC_ACE *a1, SEC_ACE *a2)
{
	if ((a1->type == SEC_ACE_TYPE_ACCESS_DENIED) &&
				(a2->type != SEC_ACE_TYPE_ACCESS_DENIED))
		return -1;

	if ((a2->type == SEC_ACE_TYPE_ACCESS_DENIED) &&
				(a1->type != SEC_ACE_TYPE_ACCESS_DENIED))
		return 1;

	/* Both access denied or access allowed. */

	/* 1. ACEs that apply to the object itself */

	if (!(a1->flags & SEC_ACE_FLAG_INHERIT_ONLY) &&
			(a2->flags & SEC_ACE_FLAG_INHERIT_ONLY))
		return -1;
	else if (!(a2->flags & SEC_ACE_FLAG_INHERIT_ONLY) &&
			(a1->flags & SEC_ACE_FLAG_INHERIT_ONLY))
		return 1;

	/* 2. ACEs that apply to a subobject of the object, such as
	 * a property set or property. */

	if (a1->flags & (SEC_ACE_FLAG_CONTAINER_INHERIT|SEC_ACE_FLAG_OBJECT_INHERIT) &&
			!(a2->flags & (SEC_ACE_FLAG_CONTAINER_INHERIT|SEC_ACE_FLAG_OBJECT_INHERIT)))
		return -1;
	else if (a2->flags & (SEC_ACE_FLAG_CONTAINER_INHERIT|SEC_ACE_FLAG_OBJECT_INHERIT) &&
			!(a1->flags & (SEC_ACE_FLAG_CONTAINER_INHERIT|SEC_ACE_FLAG_OBJECT_INHERIT)))
		return 1;

	return 0;
}

/*******************************************************************
 Functions to convert a SEC_DESC ACE DACL list into canonical order.
 JRA.

--- from http://msdn.microsoft.com/library/default.asp?url=/library/en-us/security/security/order_of_aces_in_a_dacl.asp

The following describes the preferred order:

 To ensure that noninherited ACEs have precedence over inherited ACEs,
 place all noninherited ACEs in a group before any inherited ACEs.
 This ordering ensures, for example, that a noninherited access-denied ACE
 is enforced regardless of any inherited ACE that allows access.

 Within the groups of noninherited ACEs and inherited ACEs, order ACEs according to ACE type, as the following shows:
	1. Access-denied ACEs that apply to the object itself
	2. Access-denied ACEs that apply to a subobject of the object, such as a property set or property
	3. Access-allowed ACEs that apply to the object itself
	4. Access-allowed ACEs that apply to a subobject of the object"

********************************************************************/

void dacl_sort_into_canonical_order(SEC_ACE *srclist, unsigned int num_aces)
{
	unsigned int i;

	if (!srclist || num_aces == 0)
		return;

	/* Sort so that non-inherited ACE's come first. */
	qsort( srclist, num_aces, sizeof(srclist[0]), QSORT_CAST nt_ace_inherit_comp);

	/* Find the boundary between non-inherited ACEs. */
	for (i = 0; i < num_aces; i++ ) {
		SEC_ACE *curr_ace = &srclist[i];

		if (curr_ace->flags & SEC_ACE_FLAG_INHERITED_ACE)
			break;
	}

	/* i now points at entry number of the first inherited ACE. */

	/* Sort the non-inherited ACEs. */
	if (i)
		qsort( srclist, i, sizeof(srclist[0]), QSORT_CAST nt_ace_canon_comp);

	/* Now sort the inherited ACEs. */
	if (num_aces - i)
		qsort( &srclist[i], num_aces - i, sizeof(srclist[0]), QSORT_CAST nt_ace_canon_comp);
}
