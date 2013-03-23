/* 
   Unix SMB/Netbios implementation.
   Version 2.0

   Winbind daemon - miscellaneous other functions

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

extern pstring global_myname;

/************************************************************************
 Routine to get the trust account password for a domain
************************************************************************/
static BOOL _get_trust_account_password(char *domain, unsigned char *ret_pwd, 
					time_t *pass_last_set_time)
{
	struct machine_acct_pass *pass;
	size_t size;

	if (!(pass = secrets_fetch(trust_keystr(domain), &size)) ||
	    size != sizeof(*pass)) 
                return False;
        
	if (pass_last_set_time) 
                *pass_last_set_time = pass->mod_time;

	memcpy(ret_pwd, pass->hash, 16);
	SAFE_FREE(pass);

	return True;
}

/* Check the machine account password is valid */

enum winbindd_result winbindd_check_machine_acct(struct winbindd_cli_state *state)
{
	NTSTATUS result = NT_STATUS_UNSUCCESSFUL;
	uchar trust_passwd[16];
        int num_retries = 0;
        struct cli_state *cli;
	DEBUG(3, ("[%5d]: check machine account\n", state->pid));

	/* Get trust account password */

 again:
	if (!_get_trust_account_password(lp_workgroup(), trust_passwd, 
                                         NULL)) {
		result = NT_STATUS_INTERNAL_ERROR;
		goto done;
	}

        /* This call does a cli_nt_setup_creds() which implicitly checks
           the trust account password. */

        result = cm_get_netlogon_cli(lp_workgroup(), trust_passwd, &cli);

        if (!NT_STATUS_IS_OK(result)) {
                DEBUG(3, ("could not open handle to NETLOGON pipe\n"));
                goto done;
        }

        cli_shutdown(cli);

        /* There is a race condition between fetching the trust account
           password and joining the domain so it's possible that the trust
           account password has been changed on us.  We are returned
           NT_STATUS_ACCESS_DENIED if this happens. */

#define MAX_RETRIES 8

        if ((num_retries < MAX_RETRIES) && 
            NT_STATUS_V(result) == NT_STATUS_V(NT_STATUS_ACCESS_DENIED)) {
                num_retries++;
                goto again;
        }

	/* Pass back result code - zero for success, other values for
	   specific failures. */

	DEBUG(3, ("secret is %s\n", NT_STATUS_IS_OK(result) ?  
                  "good" : "bad"));

 done:
	state->response.data.num_entries = NT_STATUS_V(result);

	return WINBINDD_OK;
}

enum winbindd_result winbindd_list_trusted_domains(struct winbindd_cli_state
						   *state)
{
	struct winbindd_domain *domain;
	int total_entries = 0, extra_data_len = 0;
	char *ted, *extra_data = NULL;

	DEBUG(3, ("[%5d]: list trusted domains\n", state->pid));

        if (domain_list == NULL)
                get_domain_info();

	for(domain = domain_list; domain; domain = domain->next) {

		/* Skip own domain */

		if (strequal(domain->name, lp_workgroup())) continue;

		/* Add domain to list */

		total_entries++;
		ted = Realloc(extra_data, sizeof(fstring) * 
                              total_entries);

		if (!ted) {
			DEBUG(0,("winbindd_list_trusted_domains: failed to enlarge buffer!\n"));
			SAFE_FREE(extra_data);
			return WINBINDD_ERROR;
		} else 
                        extra_data = ted;

		memcpy(&extra_data[extra_data_len], domain->name,
		       strlen(domain->name));

		extra_data_len  += strlen(domain->name);
		extra_data[extra_data_len++] = ',';
	}

	if (extra_data) {
		if (extra_data_len > 1) 
                        extra_data[extra_data_len - 1] = '\0';
		state->response.extra_data = extra_data;
		state->response.length += extra_data_len;
	}

	return WINBINDD_OK;
}
