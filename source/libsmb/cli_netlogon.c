/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   NT Domain Authentication SMB / MSRPC client
   Copyright (C) Andrew Tridgell 1994-2000
   Copyright (C) Luke Kenneth Casson Leighton 1996-2000
   Copyright (C) Tim Potter 2001
   
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

#include "includes.h"

/* Opens a SMB connection to the netlogon pipe */

struct cli_state *cli_netlogon_initialise(struct cli_state *cli, 
					  char *system_name,
					  struct ntuser_creds *creds)
{
        return cli_pipe_initialise(cli, system_name, PIPE_NETLOGON, creds);
}

/* LSA Request Challenge. Sends our challenge to server, then gets
   server response. These are used to generate the credentials. */

NTSTATUS new_cli_net_req_chal(struct cli_state *cli, DOM_CHAL *clnt_chal, 
                              DOM_CHAL *srv_chal)
{
        prs_struct qbuf, rbuf;
        NET_Q_REQ_CHAL q;
        NET_R_REQ_CHAL r;
        NTSTATUS result = NT_STATUS_UNSUCCESSFUL;
        extern pstring global_myname;

        prs_init(&qbuf, MAX_PDU_FRAG_LEN, cli->mem_ctx, MARSHALL);
        prs_init(&rbuf, 0, cli->mem_ctx, UNMARSHALL);
        
        /* create and send a MSRPC command with api NET_REQCHAL */

        DEBUG(4,("cli_net_req_chal: LSA Request Challenge from %s to %s: %s\n",
                 cli->desthost, global_myname, credstr(clnt_chal->data)));
        
        /* store the parameters */
        init_q_req_chal(&q, cli->srv_name_slash, global_myname, clnt_chal);
        
        /* Marshall data and send request */

        if (!net_io_q_req_chal("", &q,  &qbuf, 0) ||
            !rpc_api_pipe_req(cli, NET_REQCHAL, &qbuf, &rbuf)) {
                goto done;
        }

        /* Unmarhall response */

        if (!net_io_r_req_chal("", &r, &rbuf, 0)) {
                goto done;
        }

        result = r.status;

        /* Return result */

        if (NT_STATUS_IS_OK(result)) {
                memcpy(srv_chal, r.srv_chal.data, sizeof(srv_chal->data));
        }
        
 done:
        prs_mem_free(&qbuf);
        prs_mem_free(&rbuf);
        
        return result;
}

/****************************************************************************
LSA Authenticate 2

Send the client credential, receive back a server credential.
Ensure that the server credential returned matches the session key 
encrypt of the server challenge originally received. JRA.
****************************************************************************/

NTSTATUS new_cli_net_auth2(struct cli_state *cli, uint16 sec_chan, 
                           uint32 neg_flags, DOM_CHAL *srv_chal)
{
        prs_struct qbuf, rbuf;
        NET_Q_AUTH_2 q;
        NET_R_AUTH_2 r;
        NTSTATUS result = NT_STATUS_UNSUCCESSFUL;
        extern pstring global_myname;

        prs_init(&qbuf, MAX_PDU_FRAG_LEN, cli->mem_ctx, MARSHALL);
        prs_init(&rbuf, 0, cli->mem_ctx, UNMARSHALL);

        /* create and send a MSRPC command with api NET_AUTH2 */

        DEBUG(4,("cli_net_auth2: srv:%s acct:%s sc:%x mc: %s chal %s neg: %x\n",
                 cli->srv_name_slash, cli->mach_acct, sec_chan, global_myname,
                 credstr(cli->clnt_cred.challenge.data), neg_flags));

        /* store the parameters */
        init_q_auth_2(&q, cli->srv_name_slash, cli->mach_acct, 
                      sec_chan, global_myname, &cli->clnt_cred.challenge, 
                      neg_flags);

        /* turn parameters into data stream */

        if (!net_io_q_auth_2("", &q,  &qbuf, 0) ||
            !rpc_api_pipe_req(cli, NET_AUTH2, &qbuf, &rbuf)) {
                goto done;
        }
        
        /* Unmarshall response */
        
        if (!net_io_r_auth_2("", &r, &rbuf, 0)) {
                goto done;
        }

        result = r.status;

        if (NT_STATUS_IS_OK(result)) {
                UTIME zerotime;
                
                /*
                 * Check the returned value using the initial
                 * server received challenge.
                 */

                zerotime.time = 0;
                if (cred_assert( &r.srv_chal, cli->sess_key, srv_chal, 
                                 zerotime) == 0) {

                        /*
                         * Server replied with bad credential. Fail.
                         */
                        DEBUG(0,("cli_net_auth2: server %s replied with bad credential (bad machine \
password ?).\n", cli->desthost ));
                        result = NT_STATUS_ACCESS_DENIED;
                        goto done;
                }
        }

 done:
        prs_mem_free(&qbuf);
        prs_mem_free(&rbuf);
        
        return result;
}

/* Initialize domain session credentials */

NTSTATUS new_cli_nt_setup_creds(struct cli_state *cli, 
                                unsigned char mach_pwd[16])
{
        DOM_CHAL clnt_chal;
        DOM_CHAL srv_chal;
        UTIME zerotime;
        NTSTATUS result;

        /******************* Request Challenge ********************/

        generate_random_buffer(clnt_chal.data, 8, False);
	
        /* send a client challenge; receive a server challenge */
        result = new_cli_net_req_chal(cli, &clnt_chal, &srv_chal);

        if (!NT_STATUS_IS_OK(result)) {
                DEBUG(0,("cli_nt_setup_creds: request challenge failed\n"));
                return result;
        }
        
        /**************** Long-term Session key **************/

        /* calculate the session key */
        cred_session_key(&clnt_chal, &srv_chal, (char *)mach_pwd, 
                         cli->sess_key);
        memset((char *)cli->sess_key+8, '\0', 8);

        /******************* Authenticate 2 ********************/

        /* calculate auth-2 credentials */
        zerotime.time = 0;
        cred_create(cli->sess_key, &clnt_chal, zerotime, 
                    &cli->clnt_cred.challenge);

        /*  
         * Send client auth-2 challenge.
         * Receive an auth-2 challenge response and check it.
         */
        
	result = new_cli_net_auth2(cli, (lp_server_role() == ROLE_DOMAIN_MEMBER) ?
				   SEC_CHAN_WKSTA : SEC_CHAN_BDC, 0x000001ff, 
				   &srv_chal);
	if (!NT_STATUS_IS_OK(result)) {
                DEBUG(0,("cli_nt_setup_creds: auth2 challenge failed %s\n",
			 get_nt_error_msg(result)));
        }

        return result;
}

/* Logon Control 2 */

NTSTATUS cli_netlogon_logon_ctrl2(struct cli_state *cli, TALLOC_CTX *mem_ctx,
                                  uint32 query_level)
{
	prs_struct qbuf, rbuf;
	NET_Q_LOGON_CTRL2 q;
	NET_R_LOGON_CTRL2 r;
	NTSTATUS result = NT_STATUS_UNSUCCESSFUL;

	ZERO_STRUCT(q);
	ZERO_STRUCT(r);

	/* Initialise parse structures */

	prs_init(&qbuf, MAX_PDU_FRAG_LEN, mem_ctx, MARSHALL);
	prs_init(&rbuf, 0, mem_ctx, UNMARSHALL);

	/* Initialise input parameters */

	init_net_q_logon_ctrl2(&q, cli->srv_name_slash, query_level);

	/* Marshall data and send request */

	if (!net_io_q_logon_ctrl2("", &q, &qbuf, 0) ||
	    !rpc_api_pipe_req(cli, NET_LOGON_CTRL2, &qbuf, &rbuf)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	/* Unmarshall response */

	if (!net_io_r_logon_ctrl2("", &r, &rbuf, 0)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	result = r.status;

 done:
	prs_mem_free(&qbuf);
	prs_mem_free(&rbuf);

	return result;
}

/****************************************************************************
Generate the next creds to use.  Yuck - this is a cut&paste from another
file.  They should be combined at some stage.  )-:
****************************************************************************/

static void gen_next_creds( struct cli_state *cli, DOM_CRED *new_clnt_cred)
{
  /*
   * Create the new client credentials.
   */

  cli->clnt_cred.timestamp.time = time(NULL);

  memcpy(new_clnt_cred, &cli->clnt_cred, sizeof(*new_clnt_cred));

  /* Calculate the new credentials. */
  cred_create(cli->sess_key, &(cli->clnt_cred.challenge),
              new_clnt_cred->timestamp, &(new_clnt_cred->challenge));

}

/* Sam synchronisation */

NTSTATUS cli_netlogon_sam_sync(struct cli_state *cli, TALLOC_CTX *mem_ctx,
                               uint32 database_id, uint32 *num_deltas,
                               SAM_DELTA_HDR **hdr_deltas, 
                               SAM_DELTA_CTR **deltas)
{
	prs_struct qbuf, rbuf;
	NET_Q_SAM_SYNC q;
	NET_R_SAM_SYNC r;
	NTSTATUS result = NT_STATUS_UNSUCCESSFUL;
        DOM_CRED clnt_creds;

	ZERO_STRUCT(q);
	ZERO_STRUCT(r);

	/* Initialise parse structures */

	prs_init(&qbuf, MAX_PDU_FRAG_LEN, mem_ctx, MARSHALL);
	prs_init(&rbuf, 0, mem_ctx, UNMARSHALL);

	/* Initialise input parameters */

        gen_next_creds(cli, &clnt_creds);

	init_net_q_sam_sync(&q, cli->srv_name_slash, cli->clnt_name_slash + 2,
                            &clnt_creds, database_id);

	/* Marshall data and send request */

	if (!net_io_q_sam_sync("", &q, &qbuf, 0) ||
	    !rpc_api_pipe_req(cli, NET_SAM_SYNC, &qbuf, &rbuf)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	/* Unmarshall response */

	if (!net_io_r_sam_sync("", cli->sess_key, &r, &rbuf, 0)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

        /* Return results */

	result = r.status;
        *num_deltas = r.num_deltas2;
        *hdr_deltas = r.hdr_deltas;
        *deltas = r.deltas;

 done:
	prs_mem_free(&qbuf);
	prs_mem_free(&rbuf);

	return result;
}

/* Sam synchronisation */

NTSTATUS cli_netlogon_sam_deltas(struct cli_state *cli, TALLOC_CTX *mem_ctx,
                                 uint32 database_id, UINT64_S seqnum,
                                 uint32 *num_deltas, 
                                 SAM_DELTA_HDR **hdr_deltas, 
                                 SAM_DELTA_CTR **deltas)
{
	prs_struct qbuf, rbuf;
	NET_Q_SAM_DELTAS q;
	NET_R_SAM_DELTAS r;
	NTSTATUS result = NT_STATUS_UNSUCCESSFUL;
        DOM_CRED clnt_creds;

	ZERO_STRUCT(q);
	ZERO_STRUCT(r);

	/* Initialise parse structures */

	prs_init(&qbuf, MAX_PDU_FRAG_LEN, mem_ctx, MARSHALL);
	prs_init(&rbuf, 0, mem_ctx, UNMARSHALL);

	/* Initialise input parameters */

        gen_next_creds(cli, &clnt_creds);

	init_net_q_sam_deltas(&q, cli->srv_name_slash, 
                              cli->clnt_name_slash + 2, &clnt_creds, 
                              database_id, seqnum);

	/* Marshall data and send request */

	if (!net_io_q_sam_deltas("", &q, &qbuf, 0) ||
	    !rpc_api_pipe_req(cli, NET_SAM_DELTAS, &qbuf, &rbuf)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	/* Unmarshall response */

	if (!net_io_r_sam_deltas("", cli->sess_key, &r, &rbuf, 0)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

        /* Return results */

	result = r.status;
        *num_deltas = r.num_deltas2;
        *hdr_deltas = r.hdr_deltas;
        *deltas = r.deltas;

 done:
	prs_mem_free(&qbuf);
	prs_mem_free(&rbuf);

	return result;
}

/* Logon domain user */

NTSTATUS cli_netlogon_sam_logon(struct cli_state *cli, TALLOC_CTX *mem_ctx,
                                char *username, char *password,
                                int logon_type)
{
	prs_struct qbuf, rbuf;
	NET_Q_SAM_LOGON q;
	NET_R_SAM_LOGON r;
	NTSTATUS result = NT_STATUS_UNSUCCESSFUL;
        DOM_CRED clnt_creds, dummy_rtn_creds;
        extern pstring global_myname;
        NET_ID_INFO_CTR ctr;
        NET_USER_INFO_3 user;
        int validation_level = 3;

	ZERO_STRUCT(q);
	ZERO_STRUCT(r);

	/* Initialise parse structures */

	prs_init(&qbuf, MAX_PDU_FRAG_LEN, mem_ctx, MARSHALL);
	prs_init(&rbuf, 0, mem_ctx, UNMARSHALL);

        /* Initialise input parameters */

        gen_next_creds(cli, &clnt_creds);

        q.validation_level = validation_level;

	memset(&dummy_rtn_creds, '\0', sizeof(dummy_rtn_creds));
	dummy_rtn_creds.timestamp.time = time(NULL);

        ctr.switch_value = logon_type;

        switch (logon_type) {
        case INTERACTIVE_LOGON_TYPE: {
                unsigned char lm_owf_user_pwd[16], nt_owf_user_pwd[16];

                nt_lm_owf_gen(password, nt_owf_user_pwd, lm_owf_user_pwd);

                init_id_info1(&ctr.auth.id1, lp_workgroup(), 
                              0, /* param_ctrl */
                              0xdead, 0xbeef, /* LUID? */
                              username, cli->clnt_name_slash,
                              (char *)cli->sess_key, lm_owf_user_pwd,
                              nt_owf_user_pwd);

                break;
        }
        case NET_LOGON_TYPE: {
                uint8 chal[8];
                unsigned char local_lm_response[24];
                unsigned char local_nt_response[24];

                generate_random_buffer(chal, 8, False);

                SMBencrypt((unsigned char *)password, chal, local_lm_response);
                SMBNTencrypt((unsigned char *)password, chal, local_nt_response);

                init_id_info2(&ctr.auth.id2, lp_workgroup(), 
                              0, /* param_ctrl */
                              0xdead, 0xbeef, /* LUID? */
                              username, cli->clnt_name_slash, chal,
                              local_lm_response, 24, local_nt_response, 24);
                break;
        }
        default:
                DEBUG(0, ("switch value %d not supported\n", 
                          ctr.switch_value));
                goto done;
        }

        init_sam_info(&q.sam_id, cli->srv_name_slash, global_myname,
                      &clnt_creds, &dummy_rtn_creds, logon_type,
                      &ctr);

        /* Marshall data and send request */

	if (!net_io_q_sam_logon("", &q, &qbuf, 0) ||
	    !rpc_api_pipe_req(cli, NET_SAMLOGON, &qbuf, &rbuf)) {
		goto done;
	}

	/* Unmarshall response */

        r.user = &user;

	if (!net_io_r_sam_logon("", &r, &rbuf, 0)) {
		goto done;
	}

        /* Return results */

	result = r.status;

 done:
        return result;
}
