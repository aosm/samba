/* 
   Unix SMB/Netbios implementation.
   Version 2.0

   Winbind daemon - sid related functions

   Copyright (C) Tim Potter 2000
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "winbindd.h"
#include "sids.h"

/* Convert a string  */

enum winbindd_result winbindd_lookupsid(struct winbindd_cli_state *state)
{
	extern DOM_SID global_sid_Builtin;
	enum SID_NAME_USE type;
	DOM_SID sid, tmp_sid;
	uint32 rid;
	fstring name;

	DEBUG(3, ("[%5d]: lookupsid %s\n", state->pid, 
		  state->request.data.sid));

	/* Lookup sid from PDC using lsa_lookup_sids() */

	string_to_sid(&sid, state->request.data.sid);

	/* Don't look up BUILTIN sids */

	sid_copy(&tmp_sid, &sid);
	sid_split_rid(&tmp_sid, &rid);

	if (sid_equal(&tmp_sid, &global_sid_Builtin)) {
		return WINBINDD_ERROR;
	}

	/* Lookup the sid */

	if (!winbindd_lookup_name_by_sid(&sid, name, &type)) {
		return WINBINDD_ERROR;
	}

	string_sub(name, "\\", lp_winbind_separator(), sizeof(fstring));
	fstrcpy(state->response.data.name.name, name);
	state->response.data.name.type = type;

	return WINBINDD_OK;
}

/* Convert a sid to a string */

enum winbindd_result winbindd_lookupname(struct winbindd_cli_state *state)
{
	enum SID_NAME_USE type;
	fstring sid_str, name_domain, name_user, name;
	DOM_SID sid;
	
	DEBUG(3, ("[%5d]: lookupname %s\n", state->pid,
		  state->request.data.name));

	if (!parse_domain_user(state->request.data.name, name_domain, name_user))
		return WINBINDD_ERROR;

	snprintf(name, sizeof(name), "%s\\%s", name_domain, name_user);

	/* Lookup name from PDC using lsa_lookup_names() */

	if (!winbindd_lookup_sid_by_name(name, &sid, &type)) {
		return WINBINDD_ERROR;
	}

	sid_to_string(sid_str, &sid);
	fstrcpy(state->response.data.sid.sid, sid_str);
	state->response.data.sid.type = type;

	return WINBINDD_OK;
}

/* Convert a sid to a uid.  We assume we only have one rid attached to the
   sid. */

enum winbindd_result winbindd_sid_to_uid(struct winbindd_cli_state *state)
{
	DOM_SID sid;
	uint32 user_rid;
	struct winbindd_domain *domain;

	DEBUG(3, ("[%5d]: sid to uid %s\n", state->pid,
		  state->request.data.sid));

	/* Split sid into domain sid and user rid */

	string_to_sid(&sid, state->request.data.sid);
	sid_split_rid(&sid, &user_rid);

	/* Find domain this sid belongs to */

	if ((domain = find_domain_from_sid(&sid)) == NULL) {
		fstring sid_str;

		sid_to_string(sid_str, &sid);
		DEBUG(1, ("Could not find domain for sid %s\n", sid_str));
		return WINBINDD_ERROR;
	}

	/* Find uid for this sid and return it */

	if (!winbindd_idmap_get_uid_from_rid(domain->name, user_rid,
					     &state->response.data.uid)) {
		DEBUG(1, ("Could not get uid for sid %s\n",
			  state->request.data.sid));
		return WINBINDD_ERROR;
	}

	return WINBINDD_OK;
}

/* Convert a sid to a gid.  We assume we only have one rid attached to the
   sid.*/

enum winbindd_result winbindd_sid_to_gid(struct winbindd_cli_state *state)
{
	DOM_SID sid;
	uint32 group_rid;
	struct winbindd_domain *domain;

	DEBUG(3, ("[%5d]: sid to gid %s\n", state->pid, 
		  state->request.data.sid));

	/* Split sid into domain sid and user rid */

	string_to_sid(&sid, state->request.data.sid);
	sid_split_rid(&sid, &group_rid);

	/* Find domain this sid belongs to */

	if ((domain = find_domain_from_sid(&sid)) == NULL) {
		fstring sid_str;

		sid_to_string(sid_str, &sid);
		DEBUG(1, ("Could not find domain for sid %s\n", sid_str));
		return WINBINDD_ERROR;
	}

	/* Find uid for this sid and return it */

	if (!winbindd_idmap_get_gid_from_rid(domain->name, group_rid,
					     &state->response.data.gid)) {
		DEBUG(1, ("Could not get gid for sid %s\n",
			  state->request.data.sid));
		return WINBINDD_ERROR;
	}

	return WINBINDD_OK;
}

/* Convert a uid to a sid */

enum winbindd_result winbindd_uid_to_sid(struct winbindd_cli_state *state)
{
	struct winbindd_domain *domain;
	uint32 user_rid;
	DOM_SID sid;

	/* Bug out if the uid isn't in the winbind range */

	if ((state->request.data.uid < server_state.uid_low ) ||
	    (state->request.data.uid > server_state.uid_high)) {
		return WINBINDD_ERROR;
	}

	DEBUG(3, ("[%5d]: uid to sid %d\n", state->pid, 
		  state->request.data.uid));

	/* Lookup rid for this uid */

	if (!winbindd_idmap_get_rid_from_uid(state->request.data.uid,
					     &user_rid, &domain)) {
		DEBUG(1, ("Could not convert uid %d to rid\n",
			  state->request.data.uid));
		return WINBINDD_ERROR;
	}

	/* Construct sid and return it */

	sid_copy(&sid, &domain->sid);
	sid_append_rid(&sid, user_rid);
	sid_to_string(state->response.data.sid.sid, &sid);
	state->response.data.sid.type = SID_NAME_USER;

	return WINBINDD_OK;
}

/* Convert a gid to a sid */

enum winbindd_result winbindd_gid_to_sid(struct winbindd_cli_state *state)
{
	struct winbindd_domain *domain;
	uint32 group_rid;
	DOM_SID sid;

	/* Bug out if the gid isn't in the winbind range */

	if ((state->request.data.gid < server_state.gid_low) ||
	    (state->request.data.gid > server_state.gid_high)) {
		return WINBINDD_ERROR;
	}

	DEBUG(3, ("[%5d]: gid to sid %d\n", state->pid,
		  state->request.data.gid));

	/* Lookup rid for this uid */

	if (!winbindd_idmap_get_rid_from_gid(state->request.data.gid,
					     &group_rid, &domain)) {
		DEBUG(1, ("Could not convert gid %d to rid\n",
			  state->request.data.gid));
		return WINBINDD_ERROR;
	}

	/* Construct sid and return it */

	sid_copy(&sid, &domain->sid);
	sid_append_rid(&sid, group_rid);
	sid_to_string(state->response.data.sid.sid, &sid);
	state->response.data.sid.type = SID_NAME_DOM_GRP;

	return WINBINDD_OK;
}
