/* 
   Unix SMB/Netbios implementation.
   Version 3.0
   client transaction calls
   Copyright (C) Andrew Tridgell 1994-1998
   
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

#define NO_SYSLOG

#include "includes.h"


/****************************************************************************
  send a SMB trans or trans2 request
  ****************************************************************************/
BOOL cli_send_trans(struct cli_state *cli, int trans, 
		    const char *pipe_name, 
		    int fid, int flags,
		    uint16 *setup, int lsetup, int msetup,
		    char *param, int lparam, int mparam,
		    char *data, int ldata, int mdata)
{
	int i;
	int this_ldata,this_lparam;
	int tot_data=0,tot_param=0;
	char *outdata,*outparam;
	char *p;
	int pipe_name_len=0;

	this_lparam = MIN(lparam,cli->max_xmit - (500+lsetup*2)); /* hack */
	this_ldata = MIN(ldata,cli->max_xmit - (500+lsetup*2+this_lparam));

	memset(cli->outbuf,'\0',smb_size);
	set_message(cli->outbuf,14+lsetup,0,True);
    SCVAL(cli->outbuf,smb_com,trans);
	SSVAL(cli->outbuf,smb_tid, cli->cnum);
	cli_setup_packet(cli);

	if (pipe_name) {
		pipe_name_len = clistr_push(cli, smb_buf(cli->outbuf), pipe_name, -1, STR_TERMINATE);
	}

	outparam = smb_buf(cli->outbuf)+(trans==SMBtrans ? pipe_name_len : 3);
	outdata = outparam+this_lparam;

	/* primary request */
	SSVAL(cli->outbuf,smb_tpscnt,lparam);	/* tpscnt */
	SSVAL(cli->outbuf,smb_tdscnt,ldata);	/* tdscnt */
	SSVAL(cli->outbuf,smb_mprcnt,mparam);	/* mprcnt */
	SSVAL(cli->outbuf,smb_mdrcnt,mdata);	/* mdrcnt */
	SCVAL(cli->outbuf,smb_msrcnt,msetup);	/* msrcnt */
	SSVAL(cli->outbuf,smb_flags,flags);	/* flags */
	SIVAL(cli->outbuf,smb_timeout,0);		/* timeout */
	SSVAL(cli->outbuf,smb_pscnt,this_lparam);	/* pscnt */
	SSVAL(cli->outbuf,smb_psoff,smb_offset(outparam,cli->outbuf)); /* psoff */
	SSVAL(cli->outbuf,smb_dscnt,this_ldata);	/* dscnt */
	SSVAL(cli->outbuf,smb_dsoff,smb_offset(outdata,cli->outbuf)); /* dsoff */
	SCVAL(cli->outbuf,smb_suwcnt,lsetup);	/* suwcnt */
	for (i=0;i<lsetup;i++)		/* setup[] */
		SSVAL(cli->outbuf,smb_setup+i*2,setup[i]);
	p = smb_buf(cli->outbuf);
	if (trans != SMBtrans) {
		*p++ = 0;  /* put in a null smb_name */
		*p++ = 'D'; *p++ = ' ';	/* observed in OS/2 */
	}
	if (this_lparam)			/* param[] */
		memcpy(outparam,param,this_lparam);
	if (this_ldata)			/* data[] */
		memcpy(outdata,data,this_ldata);
	cli_setup_bcc(cli, outdata+this_ldata);

	show_msg(cli->outbuf);
	cli_send_smb(cli);

	if (this_ldata < ldata || this_lparam < lparam) {
		/* receive interim response */
		if (!cli_receive_smb(cli) || 
		    cli_is_error(cli)) {
			return(False);
		}      

		tot_data = this_ldata;
		tot_param = this_lparam;
		
		while (tot_data < ldata || tot_param < lparam)  {
			this_lparam = MIN(lparam-tot_param,cli->max_xmit - 500); /* hack */
			this_ldata = MIN(ldata-tot_data,cli->max_xmit - (500+this_lparam));

			set_message(cli->outbuf,trans==SMBtrans?8:9,0,True);
			SCVAL(cli->outbuf,smb_com,(trans==SMBtrans ? SMBtranss : SMBtranss2));
			
			outparam = smb_buf(cli->outbuf);
			outdata = outparam+this_lparam;
			
			/* secondary request */
			SSVAL(cli->outbuf,smb_tpscnt,lparam);	/* tpscnt */
			SSVAL(cli->outbuf,smb_tdscnt,ldata);	/* tdscnt */
			SSVAL(cli->outbuf,smb_spscnt,this_lparam);	/* pscnt */
			SSVAL(cli->outbuf,smb_spsoff,smb_offset(outparam,cli->outbuf)); /* psoff */
			SSVAL(cli->outbuf,smb_spsdisp,tot_param);	/* psdisp */
			SSVAL(cli->outbuf,smb_sdscnt,this_ldata);	/* dscnt */
			SSVAL(cli->outbuf,smb_sdsoff,smb_offset(outdata,cli->outbuf)); /* dsoff */
			SSVAL(cli->outbuf,smb_sdsdisp,tot_data);	/* dsdisp */
			if (trans==SMBtrans2)
				SSVALS(cli->outbuf,smb_sfid,fid);		/* fid */
			if (this_lparam)			/* param[] */
				memcpy(outparam,param+tot_param,this_lparam);
			if (this_ldata)			/* data[] */
				memcpy(outdata,data+tot_data,this_ldata);
			cli_setup_bcc(cli, outdata+this_ldata);
			
			show_msg(cli->outbuf);
			cli_send_smb(cli);
			
			tot_data += this_ldata;
			tot_param += this_lparam;
		}
	}

	return(True);
}


/****************************************************************************
  receive a SMB trans or trans2 response allocating the necessary memory
  ****************************************************************************/
BOOL cli_receive_trans(struct cli_state *cli,int trans,
                              char **param, int *param_len,
                              char **data, int *data_len)
{
	int total_data=0;
	int total_param=0;
	int this_data,this_param;
	NTSTATUS status;
	char *tdata;
	char *tparam;

	*data_len = *param_len = 0;

	if (!cli_receive_smb(cli))
		return False;

	show_msg(cli->inbuf);
	
	/* sanity check */
	if (CVAL(cli->inbuf,smb_com) != trans) {
		DEBUG(0,("Expected %s response, got command 0x%02x\n",
			 trans==SMBtrans?"SMBtrans":"SMBtrans2", 
			 CVAL(cli->inbuf,smb_com)));
		return(False);
	}

	/*
	 * An NT RPC pipe call can return ERRDOS, ERRmoredata
	 * to a trans call. This is not an error and should not
	 * be treated as such.
	 */
	status = cli_nt_error(cli);
	
	if (NT_STATUS_IS_ERR(status)) {
		return False;
	}

	/* parse out the lengths */
	total_data = SVAL(cli->inbuf,smb_tdrcnt);
	total_param = SVAL(cli->inbuf,smb_tprcnt);

	/* allocate it */
	if (total_data!=0) {
		tdata = Realloc(*data,total_data);
		if (!tdata) {
			DEBUG(0,("cli_receive_trans: failed to enlarge data buffer\n"));
			return False;
		}
		else
			*data = tdata;
	}

	if (total_param!=0) {
		tparam = Realloc(*param,total_param);
		if (!tparam) {
			DEBUG(0,("cli_receive_trans: failed to enlarge param buffer\n"));
			return False;
		}
		else
			*param = tparam;
	}

	while (1)  {
		this_data = SVAL(cli->inbuf,smb_drcnt);
		this_param = SVAL(cli->inbuf,smb_prcnt);

		if (this_data + *data_len > total_data ||
		    this_param + *param_len > total_param) {
			DEBUG(1,("Data overflow in cli_receive_trans\n"));
			return False;
		}

		if (this_data)
			memcpy(*data + SVAL(cli->inbuf,smb_drdisp),
			       smb_base(cli->inbuf) + SVAL(cli->inbuf,smb_droff),
			       this_data);
		if (this_param)
			memcpy(*param + SVAL(cli->inbuf,smb_prdisp),
			       smb_base(cli->inbuf) + SVAL(cli->inbuf,smb_proff),
			       this_param);
		*data_len += this_data;
		*param_len += this_param;

		/* parse out the total lengths again - they can shrink! */
		total_data = SVAL(cli->inbuf,smb_tdrcnt);
		total_param = SVAL(cli->inbuf,smb_tprcnt);
		
		if (total_data <= *data_len && total_param <= *param_len)
			break;
		
		if (!cli_receive_smb(cli))
			return False;

		show_msg(cli->inbuf);
		
		/* sanity check */
		if (CVAL(cli->inbuf,smb_com) != trans) {
			DEBUG(0,("Expected %s response, got command 0x%02x\n",
				 trans==SMBtrans?"SMBtrans":"SMBtrans2", 
				 CVAL(cli->inbuf,smb_com)));
			return(False);
		}
		if (NT_STATUS_IS_ERR(cli_nt_error(cli))) {
			return(False);
		}
	}
	
	return(True);
}




/****************************************************************************
  send a SMB nttrans request
  ****************************************************************************/
BOOL cli_send_nt_trans(struct cli_state *cli, 
		       int function, 
		       int flags,
		       uint16 *setup, int lsetup, int msetup,
		       char *param, int lparam, int mparam,
		       char *data, int ldata, int mdata)
{
	int i;
	int this_ldata,this_lparam;
	int tot_data=0,tot_param=0;
	char *outdata,*outparam;

	this_lparam = MIN(lparam,cli->max_xmit - (500+lsetup*2)); /* hack */
	this_ldata = MIN(ldata,cli->max_xmit - (500+lsetup*2+this_lparam));

	memset(cli->outbuf,'\0',smb_size);
	set_message(cli->outbuf,19+lsetup,0,True);
	SCVAL(cli->outbuf,smb_com,SMBnttrans);
	SSVAL(cli->outbuf,smb_tid, cli->cnum);
	cli_setup_packet(cli);

	outparam = smb_buf(cli->outbuf)+3;
	outdata = outparam+this_lparam;

	/* primary request */
	SCVAL(cli->outbuf,smb_nt_MaxSetupCount,msetup);
	SCVAL(cli->outbuf,smb_nt_Flags,flags);
	SIVAL(cli->outbuf,smb_nt_TotalParameterCount, lparam);
	SIVAL(cli->outbuf,smb_nt_TotalDataCount, ldata);
	SIVAL(cli->outbuf,smb_nt_MaxParameterCount, mparam);
	SIVAL(cli->outbuf,smb_nt_MaxDataCount, mdata);
	SIVAL(cli->outbuf,smb_nt_ParameterCount, this_lparam);
	SIVAL(cli->outbuf,smb_nt_ParameterOffset, smb_offset(outparam,cli->outbuf));
	SIVAL(cli->outbuf,smb_nt_DataCount, this_ldata);
	SIVAL(cli->outbuf,smb_nt_DataOffset, smb_offset(outdata,cli->outbuf));
	SIVAL(cli->outbuf,smb_nt_SetupCount, lsetup);
	SIVAL(cli->outbuf,smb_nt_Function, function);
	for (i=0;i<lsetup;i++)		/* setup[] */
		SSVAL(cli->outbuf,smb_nt_SetupStart+i*2,setup[i]);
	
	if (this_lparam)			/* param[] */
		memcpy(outparam,param,this_lparam);
	if (this_ldata)			/* data[] */
		memcpy(outdata,data,this_ldata);

	cli_setup_bcc(cli, outdata+this_ldata);

	show_msg(cli->outbuf);
	cli_send_smb(cli);

	if (this_ldata < ldata || this_lparam < lparam) {
		/* receive interim response */
		if (!cli_receive_smb(cli) || 
		    cli_is_error(cli)) {
			return(False);
		}      

		tot_data = this_ldata;
		tot_param = this_lparam;
		
		while (tot_data < ldata || tot_param < lparam)  {
			this_lparam = MIN(lparam-tot_param,cli->max_xmit - 500); /* hack */
			this_ldata = MIN(ldata-tot_data,cli->max_xmit - (500+this_lparam));

			set_message(cli->outbuf,18,0,True);
			SCVAL(cli->outbuf,smb_com,SMBnttranss);

			/* XXX - these should probably be aligned */
			outparam = smb_buf(cli->outbuf);
			outdata = outparam+this_lparam;
			
			/* secondary request */
			SIVAL(cli->outbuf,smb_nts_TotalParameterCount,lparam);
			SIVAL(cli->outbuf,smb_nts_TotalDataCount,ldata);
			SIVAL(cli->outbuf,smb_nts_ParameterCount,this_lparam);
			SIVAL(cli->outbuf,smb_nts_ParameterOffset,smb_offset(outparam,cli->outbuf));
			SIVAL(cli->outbuf,smb_nts_ParameterDisplacement,tot_param);
			SIVAL(cli->outbuf,smb_nts_DataCount,this_ldata);
			SIVAL(cli->outbuf,smb_nts_DataOffset,smb_offset(outdata,cli->outbuf));
			SIVAL(cli->outbuf,smb_nts_DataDisplacement,tot_data);
			if (this_lparam)			/* param[] */
				memcpy(outparam,param+tot_param,this_lparam);
			if (this_ldata)			/* data[] */
				memcpy(outdata,data+tot_data,this_ldata);
			cli_setup_bcc(cli, outdata+this_ldata);
			
			show_msg(cli->outbuf);
			cli_send_smb(cli);
			
			tot_data += this_ldata;
			tot_param += this_lparam;
		}
	}

	return(True);
}



/****************************************************************************
  receive a SMB nttrans response allocating the necessary memory
  ****************************************************************************/
BOOL cli_receive_nt_trans(struct cli_state *cli,
			  char **param, int *param_len,
			  char **data, int *data_len)
{
	int total_data=0;
	int total_param=0;
	int this_data,this_param;
	uint8 eclass;
	uint32 ecode;
	char *tdata;
	char *tparam;

	*data_len = *param_len = 0;

	if (!cli_receive_smb(cli))
		return False;

	show_msg(cli->inbuf);
	
	/* sanity check */
	if (CVAL(cli->inbuf,smb_com) != SMBnttrans) {
		DEBUG(0,("Expected SMBnttrans response, got command 0x%02x\n",
			 CVAL(cli->inbuf,smb_com)));
		return(False);
	}

	/*
	 * An NT RPC pipe call can return ERRDOS, ERRmoredata
	 * to a trans call. This is not an error and should not
	 * be treated as such.
	 */
	if (cli_is_dos_error(cli)) {
                cli_dos_error(cli, &eclass, &ecode);
		if (cli->nt_pipe_fnum == 0 || !(eclass == ERRDOS && ecode == ERRmoredata))
			return(False);
	}

	/* parse out the lengths */
	total_data = SVAL(cli->inbuf,smb_ntr_TotalDataCount);
	total_param = SVAL(cli->inbuf,smb_ntr_TotalParameterCount);

	/* allocate it */
	if (total_data) {
		tdata = Realloc(*data,total_data);
		if (!tdata) {
			DEBUG(0,("cli_receive_nt_trans: failed to enlarge data buffer to %d\n",total_data));
			return False;
		} else {
			*data = tdata;
		}
	}

	if (total_param) {
		tparam = Realloc(*param,total_param);
		if (!tparam) {
			DEBUG(0,("cli_receive_nt_trans: failed to enlarge param buffer to %d\n", total_param));
			return False;
		} else {
			*param = tparam;
		}
	}

	while (1)  {
		this_data = SVAL(cli->inbuf,smb_ntr_DataCount);
		this_param = SVAL(cli->inbuf,smb_ntr_ParameterCount);

		if (this_data + *data_len > total_data ||
		    this_param + *param_len > total_param) {
			DEBUG(1,("Data overflow in cli_receive_trans\n"));
			return False;
		}

		if (this_data)
			memcpy(*data + SVAL(cli->inbuf,smb_ntr_DataDisplacement),
			       smb_base(cli->inbuf) + SVAL(cli->inbuf,smb_ntr_DataOffset),
			       this_data);
		if (this_param)
			memcpy(*param + SVAL(cli->inbuf,smb_ntr_ParameterDisplacement),
			       smb_base(cli->inbuf) + SVAL(cli->inbuf,smb_ntr_ParameterOffset),
			       this_param);
		*data_len += this_data;
		*param_len += this_param;

		/* parse out the total lengths again - they can shrink! */
		total_data = SVAL(cli->inbuf,smb_ntr_TotalDataCount);
		total_param = SVAL(cli->inbuf,smb_ntr_TotalParameterCount);
		
		if (total_data <= *data_len && total_param <= *param_len)
			break;
		
		if (!cli_receive_smb(cli))
			return False;

		show_msg(cli->inbuf);
		
		/* sanity check */
		if (CVAL(cli->inbuf,smb_com) != SMBnttrans) {
			DEBUG(0,("Expected SMBnttrans response, got command 0x%02x\n",
				 CVAL(cli->inbuf,smb_com)));
			return(False);
		}
		if (cli_is_dos_error(cli)) {
                        cli_dos_error(cli, &eclass, &ecode);
			if(cli->nt_pipe_fnum == 0 || 
                           !(eclass == ERRDOS && ecode == ERRmoredata))
				return(False);
		}
	}
	
	return(True);
}
