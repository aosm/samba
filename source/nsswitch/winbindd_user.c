/* 
   Unix SMB/Netbios implementation.
   Version 2.2

   Winbind daemon - user related functions

   Copyright (C) Tim Potter 2000
   Copyright (C) Jeremy Allison 2001.
   
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

/* Fill a pwent structure with information we have obtained */

static BOOL winbindd_fill_pwent(char *domain_name, char *name, 
				uint32 user_rid, uint32 group_rid, 
				char *full_name, struct winbindd_pw *pw)
{
	extern userdom_struct current_user_info;
	fstring name_domain, name_user;
	pstring homedir;
	
	if (!pw || !name)
		return False;
	
	/* Resolve the uid number */
	
	if (!winbindd_idmap_get_uid_from_rid(domain_name, user_rid, 
					     &pw->pw_uid)) {
		DEBUG(1, ("error getting user id for rid %d\n", user_rid));
		return False;
	}
	
	/* Resolve the gid number */   
	
	if (!winbindd_idmap_get_gid_from_rid(domain_name, group_rid, 
					     &pw->pw_gid)) {
		DEBUG(1, ("error getting group id for rid %d\n", group_rid));
		return False;
	}

	/* Username */
	
	safe_strcpy(pw->pw_name, name, sizeof(pw->pw_name) - 1);
	
	/* Full name (gecos) */
	
	safe_strcpy(pw->pw_gecos, full_name, sizeof(pw->pw_gecos) - 1);

	/* Home directory and shell - use template config parameters.  The
	   defaults are /tmp for the home directory and /bin/false for
	   shell. */
	
	if (!parse_domain_user(name, name_domain, name_user)) {
		DEBUG(1, ("error parsing domain user for %s\n", name_user ));
		return False;
	}
	
	/* The substitution of %U and %D in the 'template homedir' is done
	   by lp_string() calling standard_sub_basic(). */

	fstrcpy(current_user_info.smb_name, name_user);
	fstrcpy(current_user_info.domain, name_domain);

	pstrcpy(homedir, lp_template_homedir());
	
	safe_strcpy(pw->pw_dir, homedir, sizeof(pw->pw_dir) - 1);
	
	safe_strcpy(pw->pw_shell, lp_template_shell(), 
		    sizeof(pw->pw_shell) - 1);
	
	/* Password - set to "x" as we can't generate anything useful here.
	   Authentication can be done using the pam_ntdom module. */

	safe_strcpy(pw->pw_passwd, "x", sizeof(pw->pw_passwd) - 1);
	
	return True;
}

/************************************************************************
 Empty static struct for negative caching.
*************************************************************************/

static struct winbindd_pw negative_pw_cache_entry;

/* Return a password structure from a username.  Specify whether cached data 
   can be returned. */

enum winbindd_result winbindd_getpwnam_from_user(struct winbindd_cli_state *state) 
{
	uint32 user_rid, group_rid;
	SAM_USERINFO_CTR *user_info;
	DOM_SID user_sid;
	fstring name_domain, name_user, name, gecos_name;
	enum SID_NAME_USE name_type;
	struct winbindd_domain *domain;
	TALLOC_CTX *mem_ctx;
	
	DEBUG(3, ("[%5d]: getpwnam %s\n", state->pid,
		  state->request.data.username));
	
	/* Parse domain and username */

	if (!parse_domain_user(state->request.data.username, name_domain, 
			  name_user))
		return WINBINDD_ERROR;
	
	if ((domain = find_domain_from_name(name_domain)) == NULL) {
		DEBUG(5, ("No such domain: %s\n", name_domain));
		return WINBINDD_ERROR;
	}

	/* Check for cached user entry */

	if (winbindd_fetch_user_cache_entry(domain, name_user, &state->response.data.pw)) {
		/* Check if this is a negative cache entry. */
		if (memcmp(&negative_pw_cache_entry, &state->response.data.pw,
						sizeof(state->response.data.pw)) == 0)
			return WINBINDD_ERROR;
		return WINBINDD_OK;
	}

	slprintf(name, sizeof(name) - 1, "%s\\%s", name_domain, name_user);
	
	/* Get rid and name type from name */

	if (!winbindd_lookup_sid_by_name(name, &user_sid, &name_type)) {
		DEBUG(1, ("user '%s' does not exist\n", name_user));
		winbindd_store_user_cache_entry(domain, name_user, &negative_pw_cache_entry);
		return WINBINDD_ERROR;
	}

	if (name_type != SID_NAME_USER) {
		DEBUG(1, ("name '%s' is not a user name: %d\n", name_user, 
			  name_type));
		winbindd_store_user_cache_entry(domain, name_user, &negative_pw_cache_entry);
		return WINBINDD_ERROR;
	}
	
	/* Get some user info.  Split the user rid from the sid obtained
	   from the winbind_lookup_by_name() call and use it in a
	   winbind_lookup_userinfo() */
    
	if (!(mem_ctx = talloc_init())) {
		DEBUG(1, ("out of memory\n"));
		return WINBINDD_ERROR;
	}

	sid_split_rid(&user_sid, &user_rid);
	
	if (!winbindd_lookup_userinfo(domain, mem_ctx, user_rid, &user_info)) {
		DEBUG(1, ("pwnam_from_user(): error getting user info for "
			  "user '%s'\n", name_user));
		winbindd_store_user_cache_entry(domain, name_user, &negative_pw_cache_entry);
		talloc_destroy(mem_ctx);
		return WINBINDD_ERROR;
	}
    
	group_rid = user_info->info.id21->group_rid;

	unistr2_to_ascii(gecos_name, &user_info->info.id21->uni_full_name,
			 sizeof(gecos_name) - 1);

	talloc_destroy(mem_ctx);
	user_info = NULL;

	/* Now take all this information and fill in a passwd structure */
	
	if (!winbindd_fill_pwent(name_domain, state->request.data.username, 
				 user_rid, group_rid, gecos_name,
				 &state->response.data.pw)) {
		winbindd_store_user_cache_entry(domain, name_user, &negative_pw_cache_entry);
		/* talloc_destroy(mem_ctx); Surely this is wrong */
		return WINBINDD_ERROR;
	}
	
	winbindd_store_user_cache_entry(domain, name_user, &state->response.data.pw);
	
	return WINBINDD_OK;
}       

/* Return a password structure given a uid number */

enum winbindd_result winbindd_getpwnam_from_uid(struct winbindd_cli_state *state)
{
	DOM_SID user_sid;
	struct winbindd_domain *domain;
	uint32 user_rid, group_rid;
	fstring user_name, gecos_name;
	enum SID_NAME_USE name_type;
	SAM_USERINFO_CTR *user_info;
	gid_t gid;
	TALLOC_CTX *mem_ctx;
	
	/* Bug out if the uid isn't in the winbind range */

	if ((state->request.data.uid < server_state.uid_low ) ||
	    (state->request.data.uid > server_state.uid_high))
		return WINBINDD_ERROR;

	DEBUG(3, ("[%5d]: getpwuid %d\n", state->pid, 
		  state->request.data.uid));
	
	/* Get rid from uid */

	if (!winbindd_idmap_get_rid_from_uid(state->request.data.uid, 
					     &user_rid, &domain)) {
		DEBUG(1, ("Could not convert uid %d to rid\n", 
			  state->request.data.uid));
		return WINBINDD_ERROR;
	}
	
	/* Check for cached uid entry */

	if (winbindd_fetch_uid_cache_entry(domain, 
					   state->request.data.uid,
					   &state->response.data.pw)) {
		/* Check if this is a negative cache entry. */
		if (memcmp(&negative_pw_cache_entry, &state->response.data.pw,
						sizeof(state->response.data.pw)) == 0)
			return WINBINDD_ERROR;
		return WINBINDD_OK;
	}

	/* Get name and name type from rid */

	sid_copy(&user_sid, &domain->sid);
	sid_append_rid(&user_sid, user_rid);
	
	if (!winbindd_lookup_name_by_sid(&user_sid, user_name, &name_type)) {
		fstring temp;
		
		sid_to_string(temp, &user_sid);
		DEBUG(1, ("Could not lookup sid %s\n", temp));

		winbindd_store_uid_cache_entry(domain, state->request.data.uid, &negative_pw_cache_entry);
		return WINBINDD_ERROR;
	}
	
	if (strcmp("\\", lp_winbind_separator()))
		string_sub(user_name, "\\", lp_winbind_separator(), 
			   sizeof(fstring));

	/* Get some user info */
	
	if (!(mem_ctx = talloc_init())) {
		DEBUG(1, ("out of memory\n"));
		return WINBINDD_ERROR;
	}

	if (!winbindd_lookup_userinfo(domain, mem_ctx, user_rid, &user_info)) {
		DEBUG(1, ("pwnam_from_uid(): error getting user info for "
			  "user '%s'\n", user_name));
		winbindd_store_uid_cache_entry(domain, state->request.data.uid, &negative_pw_cache_entry);
		talloc_destroy(mem_ctx);
		return WINBINDD_ERROR;
	}
	
	group_rid = user_info->info.id21->group_rid;
	unistr2_to_ascii(gecos_name, &user_info->info.id21->uni_full_name,
			 sizeof(gecos_name) - 1);

	talloc_destroy(mem_ctx);
	user_info = NULL;

	/* Resolve gid number */

	if (!winbindd_idmap_get_gid_from_rid(domain->name, group_rid, &gid)) {
		DEBUG(1, ("error getting group id for user %s\n", user_name));
		return WINBINDD_ERROR;
	}

	/* Fill in password structure */

	if (!winbindd_fill_pwent(domain->name, user_name, user_rid, group_rid,
				 gecos_name, &state->response.data.pw)) {
		winbindd_store_uid_cache_entry(domain, state->request.data.uid, &negative_pw_cache_entry);
		return WINBINDD_ERROR;
	}
	
	winbindd_store_uid_cache_entry(domain, state->request.data.uid, &state->response.data.pw);
	
	return WINBINDD_OK;
}

/*
 * set/get/endpwent functions
 */

/* Rewind file pointer for ntdom passwd database */

enum winbindd_result winbindd_setpwent(struct winbindd_cli_state *state)
{
	struct winbindd_domain *tmp;
        
	DEBUG(3, ("[%5d]: setpwent\n", state->pid));
        
	/* Check user has enabled this */
        
	if (!lp_winbind_enum_users())
		return WINBINDD_ERROR;

	/* Free old static data if it exists */
        
	if (state->getpwent_state != NULL) {
		free_getent_state(state->getpwent_state);
		state->getpwent_state = NULL;
	}
        
	/* Create sam pipes for each domain we know about */
        
	if (domain_list == NULL)
		get_domain_info();

	for(tmp = domain_list; tmp != NULL; tmp = tmp->next) {
		struct getent_state *domain_state;
                
		/*
		 * Skip domains other than WINBINDD_DOMAIN environment
		 * variable.
		 */
                
		if ((strcmp(state->request.domain, "") != 0) &&
		    !check_domain_env(state->request.domain, tmp->name)) {
			DEBUG(5, ("skipping domain %s because of env var\n",
				  tmp->name));
			continue;
		}

		/* Create a state record for this domain */
                
		if ((domain_state = create_getent_state(tmp)) == NULL) {
			DEBUG(5, ("error connecting to dc for domain %s\n",
				  tmp->name));
			continue;
		}
                
		/* Add to list of open domains */
                
		DLIST_ADD(state->getpwent_state, domain_state);
	}
        
	return WINBINDD_OK;
}

/* Close file pointer to ntdom passwd database */

enum winbindd_result winbindd_endpwent(struct winbindd_cli_state *state)
{
	DEBUG(3, ("[%5d]: endpwent\n", state->pid));

	free_getent_state(state->getpwent_state);    
	state->getpwent_state = NULL;
        
	return WINBINDD_OK;
}

/* Get partial list of domain users for a domain.  We fill in the sam_entries,
   and num_sam_entries fields with domain user information.  The dispinfo_ndx
   field is incremented to the index of the next user to fetch.  Return True if
   some users were returned, False otherwise. */

#define MAX_FETCH_SAM_ENTRIES 100

static BOOL get_sam_user_entries(struct getent_state *ent)
{
	NTSTATUS status;
	uint32 num_entries;
	SAM_DISPINFO_1 info1;
	SAM_DISPINFO_CTR ctr;
	struct getpwent_user *name_list = NULL;
	uint32 group_rid;
	BOOL result = False;
	TALLOC_CTX *mem_ctx;

	if (ent->got_all_sam_entries)
		return False;

	if (!(mem_ctx = talloc_init()))
		return False;

	ZERO_STRUCT(info1);
	ZERO_STRUCT(ctr);

	ctr.sam.info1 = &info1;

#if 0
	/* Look in cache for entries, else get them direct */
		    
	if (winbindd_fetch_user_cache(ent->domain,
				      (struct getpwent_user **)
				      &ent->sam_entries, 
				      &ent->num_sam_entries)) {
		return True;
	}
#endif

	/* For the moment we set the primary group for every user to be the
	   Domain Users group.  There are serious problems with determining
	   the actual primary group for large domains.  This should really
	   be made into a 'winbind force group' smb.conf parameter or
	   something like that. */ 

	group_rid = DOMAIN_GROUP_RID_USERS;

	/* Free any existing user info */

	SAFE_FREE(ent->sam_entries);
	ent->num_sam_entries = 0;
	
	/* Call query_dispinfo to get a list of usernames and user rids */

	do {
		int i;
					
		num_entries = 0;

		status = winbindd_query_dispinfo(ent->domain, mem_ctx, &ent->dom_pol,
						 &ent->dispinfo_ndx, 1,
						 &num_entries, &ctr);
		
		if (num_entries) {
			struct getpwent_user *tnl;

			tnl = (struct getpwent_user *)Realloc(name_list, 
					    sizeof(struct getpwent_user) *
					    (ent->num_sam_entries + 
					     num_entries));

			if (!tnl) {
				DEBUG(0,("get_sam_user_entries: Realloc failed.\n"));
				SAFE_FREE(name_list);
                                goto done;
			} else
				name_list = tnl;
		}

		for (i = 0; i < num_entries; i++) {

			/* Store account name and gecos */

			unistr2_to_ascii(
				name_list[ent->num_sam_entries + i].name, 
				&info1.str[i].uni_acct_name, 
				sizeof(fstring));

			unistr2_to_ascii(
				name_list[ent->num_sam_entries + i].gecos, 
				&info1.str[i].uni_full_name, 
				sizeof(fstring));

			/* User and group ids */

			name_list[ent->num_sam_entries + i].user_rid =
				info1.sam[i].rid_user;

			name_list[ent->num_sam_entries + i].
				group_rid = group_rid;
		}
		
		ent->num_sam_entries += num_entries;

		if (NT_STATUS_V(status) != NT_STATUS_V(STATUS_MORE_ENTRIES))
			break;

	} while (ent->num_sam_entries < MAX_FETCH_SAM_ENTRIES);
	
#if 0
	/* Fill cache with received entries */
	
	winbindd_store_user_cache(ent->domain, ent->sam_entries, 
				  ent->num_sam_entries);
#endif

	/* Fill in remaining fields */
	
	ent->sam_entries = name_list;
	ent->sam_entry_index = 0;
	ent->got_all_sam_entries = (NT_STATUS_V(status) != NT_STATUS_V(STATUS_MORE_ENTRIES));

	result = ent->num_sam_entries > 0;

 done:

	talloc_destroy(mem_ctx);

	return result;
}

/* Fetch next passwd entry from ntdom database */

#define MAX_GETPWENT_USERS 500

enum winbindd_result winbindd_getpwent(struct winbindd_cli_state *state)
{
	struct getent_state *ent;
	struct winbindd_pw *user_list;
	int num_users, user_list_ndx = 0, i;
	char *sep;

	DEBUG(3, ("[%5d]: getpwent\n", state->pid));

	/* Check user has enabled this */

	if (!lp_winbind_enum_users())
		return WINBINDD_ERROR;

	/* Allocate space for returning a chunk of users */

	num_users = MIN(MAX_GETPWENT_USERS, state->request.data.num_entries);
	
	if ((state->response.extra_data = 
	     malloc(num_users * sizeof(struct winbindd_pw))) == NULL)
		return WINBINDD_ERROR;

	memset(state->response.extra_data, 0, num_users * 
	       sizeof(struct winbindd_pw));

	user_list = (struct winbindd_pw *)state->response.extra_data;
	sep = lp_winbind_separator();
	
	if (!(ent = state->getpwent_state))
		return WINBINDD_ERROR;

	/* Start sending back users */

	for (i = 0; i < num_users; i++) {
		struct getpwent_user *name_list = NULL;
		fstring domain_user_name;
		uint32 result;

		/* Do we need to fetch another chunk of users? */

		if (ent->num_sam_entries == ent->sam_entry_index) {

			while(ent && !get_sam_user_entries(ent)) {
				struct getent_state *next_ent;

				/* Free state information for this domain */

				SAFE_FREE(ent->sam_entries);

				next_ent = ent->next;
				DLIST_REMOVE(state->getpwent_state, ent);

				SAFE_FREE(ent);
				ent = next_ent;
			}
 
			/* No more domains */

			if (!ent) 
				break;
		}

		name_list = ent->sam_entries;

		/* Skip machine accounts */

		if (name_list[ent->sam_entry_index].
		    name[strlen(name_list[ent->sam_entry_index].name) - 1] 
		    == '$') {
			ent->sam_entry_index++;
			continue;
		}

		/* Lookup user info */
		
		slprintf(domain_user_name, sizeof(domain_user_name) - 1,
			 "%s%s%s", ent->domain->name, sep,
			 name_list[ent->sam_entry_index].name);
		
		result = winbindd_fill_pwent(
			ent->domain->name, 
			domain_user_name,
			name_list[ent->sam_entry_index].user_rid,
			name_list[ent->sam_entry_index].group_rid,
			name_list[ent->sam_entry_index].gecos,
			&user_list[user_list_ndx]);
		
		ent->sam_entry_index++;
		
		/* Add user to return list */
		
		if (result) {
				
			user_list_ndx++;
			state->response.data.num_entries++;
			state->response.length += 
				sizeof(struct winbindd_pw);

		} else
			DEBUG(1, ("could not lookup domain user %s\n",
				  domain_user_name));
	}

	/* Out of domains */

	return (user_list_ndx > 0) ? WINBINDD_OK : WINBINDD_ERROR;
}

/* List domain users without mapping to unix ids */

enum winbindd_result winbindd_list_users(struct winbindd_cli_state *state)
{
	struct winbindd_domain *domain;
	SAM_DISPINFO_CTR ctr;
	SAM_DISPINFO_1 info1;
	uint32 num_entries = 0, total_entries = 0;
	char *ted, *extra_data = NULL;
	int extra_data_len = 0;
	TALLOC_CTX *mem_ctx;
	enum winbindd_result rv = WINBINDD_ERROR;

	DEBUG(3, ("[%5d]: list users\n", state->pid));

	if (!(mem_ctx = talloc_init()))
		return WINBINDD_ERROR;

	/* Enumerate over trusted domains */

	ctr.sam.info1 = &info1;

	if (domain_list == NULL)
		get_domain_info();

	for (domain = domain_list; domain; domain = domain->next) {
		NTSTATUS status;
		uint32 start_ndx = 0;
		POLICY_HND dom_pol;

		/* Skip domains other than WINBINDD_DOMAIN environment
		   variable */ 

		if ((strcmp(state->request.domain, "") != 0) &&
		    !check_domain_env(state->request.domain, domain->name))
			continue;

		if (!create_samr_domain_handle(domain, &dom_pol))
			continue;

		/* Query display info */

		do {
			int i;

			status = winbindd_query_dispinfo(
                                domain, mem_ctx, &dom_pol, &start_ndx, 
                                1, &num_entries, &ctr);

			if (num_entries == 0)
				continue;

			/* Allocate some memory for extra data */

			total_entries += num_entries;
			
			ted = Realloc(extra_data, sizeof(fstring) * 
					     total_entries);
			
			if (!ted) {
				DEBUG(0,("winbindd_list_users: failed to enlarge buffer!\n"));
				SAFE_FREE(extra_data);
				goto done;
			} else 
				extra_data = ted;
			
			/* Pack user list into extra data fields */
			
			for (i = 0; i < num_entries; i++) {
				UNISTR2 *uni_acct_name;
				fstring acct_name, name;

				/* Convert unistring to ascii */
				
				uni_acct_name = &ctr.sam.info1->str[i]. 
					uni_acct_name;
				unistr2_to_ascii(acct_name, uni_acct_name,
						 sizeof(acct_name) - 1);
                                                 
				slprintf(name, sizeof(name) - 1, "%s%s%s",
					 domain->name, lp_winbind_separator(),
					 acct_name);

				/* Append to extra data */
			
				memcpy(&extra_data[extra_data_len], name, 
				       strlen(name));
				extra_data_len += strlen(name);
				
				extra_data[extra_data_len++] = ',';
			}   
		} while (NT_STATUS_V(status) == NT_STATUS_V(STATUS_MORE_ENTRIES));

		close_samr_domain_handle(domain, &dom_pol);
        }

	/* Assign extra_data fields in response structure */

	if (extra_data) {
		extra_data[extra_data_len - 1] = '\0';
		state->response.extra_data = extra_data;
		state->response.length += extra_data_len;
	}

	/* No domains responded but that's still OK so don't return an
	   error. */

	rv = WINBINDD_OK;

 done:

	talloc_destroy(mem_ctx);

	return rv;
}
