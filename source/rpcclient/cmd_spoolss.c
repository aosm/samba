/*
   Unix SMB/Netbios implementation.
   Version 2.2
   RPC pipe client

   Copyright (C) Gerald Carter                     2001
   Copyright (C) Tim Potter                        2000
   Copyright (C) Andrew Tridgell              1992-1999
   Copyright (C) Luke Kenneth Casson Leighton 1996-1999
 
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
#include "rpcclient.h"

struct table_node {
	char 	*long_archi;
	char 	*short_archi;
	int	version;
};
 
struct table_node archi_table[]= {

	{"Windows 4.0",          "WIN40",	0 },
	{"Windows NT x86",       "W32X86",	2 },
	{"Windows NT R4000",     "W32MIPS",	2 },
	{"Windows NT Alpha_AXP", "W32ALPHA",	2 },
	{"Windows NT PowerPC",   "W32PPC",	2 },
	{NULL,                   "",		-1 }
};

/****************************************************************************
function to do the mapping between the long architecture name and
the short one.
****************************************************************************/
BOOL get_short_archi(char *short_archi, char *long_archi)
{
        int i=-1;

        DEBUG(107,("Getting architecture dependant directory\n"));
        do {
                i++;
        } while ( (archi_table[i].long_archi!=NULL ) &&
                  StrCaseCmp(long_archi, archi_table[i].long_archi) );

        if (archi_table[i].long_archi==NULL) {
                DEBUGADD(10,("Unknown architecture [%s] !\n", long_archi));
                return False;
        }

        StrnCpy (short_archi, archi_table[i].short_archi, strlen(archi_table[i].short_archi));

        DEBUGADD(108,("index: [%d]\n", i));
        DEBUGADD(108,("long architecture: [%s]\n", long_archi));
        DEBUGADD(108,("short architecture: [%s]\n", short_archi));

        return True;
}


/**********************************************************************
 * dummy function  -- placeholder
  */
static NTSTATUS cmd_spoolss_not_implemented(struct cli_state *cli, 
                                            TALLOC_CTX *mem_ctx,
                                            int argc, char **argv)
{
	printf ("(*) This command is not currently implemented.\n");
	return NT_STATUS_OK;
}

/****************************************************************************
 display sec_ace structure
 ****************************************************************************/
static void display_sec_ace(SEC_ACE *ace)
{
	fstring sid_str;

	sid_to_string(sid_str, &ace->trustee);
	printf("\t\tSID: %s\n", sid_str);

	printf("\t\ttype:[%d], flags:[0x%02x], mask:[0x%08x]\n", 
	       ace->type, ace->flags, ace->info.mask);
}

/****************************************************************************
 display sec_acl structure
 ****************************************************************************/
static void display_sec_acl(SEC_ACL *acl)
{
	if (acl->size != 0 && acl->num_aces != 0) {
		int i;

		printf("\t\tRevision:[%d]\n", acl->revision);
		for (i = 0; i < acl->num_aces; i++) {
			display_sec_ace(&acl->ace[i]);
		}
	}
}

/****************************************************************************
 display sec_desc structure
 ****************************************************************************/
static void display_sec_desc(SEC_DESC *sec)
{
	fstring sid_str;

	printf("\tRevision:[%d]\n", sec->revision);

	if (sec->off_owner_sid) {
		sid_to_string(sid_str, sec->owner_sid);
		printf("\tOwner SID: %s\n", sid_str);
	}

	if (sec->off_grp_sid) {
		sid_to_string(sid_str, sec->grp_sid);
		printf("\tGroup SID: %s\n", sid_str);
	}

	if (sec->off_sacl) display_sec_acl(sec->sacl);
	if (sec->off_dacl) display_sec_acl(sec->dacl);
}

/***********************************************************************
 * Get printer information
 */
static NTSTATUS cmd_spoolss_open_printer_ex(struct cli_state *cli, 
                                            TALLOC_CTX *mem_ctx,
                                            int argc, char **argv)
{
	NTSTATUS 	result = NT_STATUS_UNSUCCESSFUL; 
	pstring		printername;
	fstring		servername, user;
	POLICY_HND	hnd;
	
	if (argc != 2) {
		printf("Usage: %s <printername>\n", argv[0]);
		return NT_STATUS_OK;
	}
	
	if (!cli)
		return NT_STATUS_UNSUCCESSFUL;

	slprintf (servername, sizeof(fstring)-1, "\\\\%s", cli->desthost);
	strupper (servername);
	fstrcpy  (user, cli->user_name);
	fstrcpy  (printername, argv[1]);

	/* Open the printer handle */
	result = cli_spoolss_open_printer_ex (cli, mem_ctx, printername, "", 
				MAXIMUM_ALLOWED_ACCESS, servername, user, &hnd);

	if (NT_STATUS_IS_OK(result)) {
		printf ("Printer %s opened successfully\n", printername);
		result = cli_spoolss_close_printer (cli, mem_ctx, &hnd);
		if (!NT_STATUS_IS_OK(result)) {
			printf ("Error closing printer handle! (%s)\n", get_nt_error_msg(result));
		}
	}

	return result;
}


/****************************************************************************
printer info level 0 display function
****************************************************************************/
static void display_print_info_0(PRINTER_INFO_0 *i1)
{
	fstring 	name;
	fstring 	servername;

	rpcstr_pull(name, i1->printername.buffer, sizeof(name), 0, STR_TERMINATE);
	rpcstr_pull(servername, i1->servername.buffer, sizeof(servername), 0,STR_TERMINATE);
  
	printf("\tprintername:[%s]\n", name);
	printf("\tservername:[%s]\n", servername);
	printf("\tcjobs:[0x%x]\n", i1->cjobs);
	printf("\ttotal_jobs:[0x%x]\n", i1->total_jobs);
	
	printf("\t:date: [%d]-[%d]-[%d] (%d)\n", i1->year, i1->month, 
	       i1->day, i1->dayofweek);
	printf("\t:time: [%d]-[%d]-[%d]-[%d]\n", i1->hour, i1->minute, 
	       i1->second, i1->milliseconds);
	
	printf("\tglobal_counter:[0x%x]\n", i1->global_counter);
	printf("\ttotal_pages:[0x%x]\n", i1->total_pages);
	
	printf("\tmajorversion:[0x%x]\n", i1->major_version);
	printf("\tbuildversion:[0x%x]\n", i1->build_version);
	
	printf("\tunknown7:[0x%x]\n", i1->unknown7);
	printf("\tunknown8:[0x%x]\n", i1->unknown8);
	printf("\tunknown9:[0x%x]\n", i1->unknown9);
	printf("\tsession_counter:[0x%x]\n", i1->session_counter);
	printf("\tunknown11:[0x%x]\n", i1->unknown11);
	printf("\tprinter_errors:[0x%x]\n", i1->printer_errors);
	printf("\tunknown13:[0x%x]\n", i1->unknown13);
	printf("\tunknown14:[0x%x]\n", i1->unknown14);
	printf("\tunknown15:[0x%x]\n", i1->unknown15);
	printf("\tunknown16:[0x%x]\n", i1->unknown16);
	printf("\tchange_id:[0x%x]\n", i1->change_id);
	printf("\tunknown18:[0x%x]\n", i1->unknown18);
	printf("\tstatus:[0x%x]\n", i1->status);
	printf("\tunknown20:[0x%x]\n", i1->unknown20);
	printf("\tc_setprinter:[0x%x]\n", i1->c_setprinter);
	printf("\tunknown22:[0x%x]\n", i1->unknown22);
	printf("\tunknown23:[0x%x]\n", i1->unknown23);
	printf("\tunknown24:[0x%x]\n", i1->unknown24);
	printf("\tunknown25:[0x%x]\n", i1->unknown25);
	printf("\tunknown26:[0x%x]\n", i1->unknown26);
	printf("\tunknown27:[0x%x]\n", i1->unknown27);
	printf("\tunknown28:[0x%x]\n", i1->unknown28);
	printf("\tunknown29:[0x%x]\n", i1->unknown29);
}

/****************************************************************************
printer info level 1 display function
****************************************************************************/
static void display_print_info_1(PRINTER_INFO_1 *i1)
{
	fstring desc;
	fstring name;
	fstring comm;

	rpcstr_pull(desc, i1->description.buffer, sizeof(desc), 0, STR_TERMINATE);
	rpcstr_pull(name, i1->name.buffer, sizeof(name), 0, STR_TERMINATE);
	rpcstr_pull(comm, i1->comment.buffer, sizeof(comm), 0, STR_TERMINATE);

	printf("\tflags:[0x%x]\n", i1->flags);
	printf("\tname:[%s]\n", name);
	printf("\tdescription:[%s]\n", desc);
	printf("\tcomment:[%s]\n\n", comm);
}

/****************************************************************************
printer info level 2 display function
****************************************************************************/
static void display_print_info_2(PRINTER_INFO_2 *i2)
{
	fstring servername;
	fstring printername;
	fstring sharename;
	fstring portname;
	fstring drivername;
	fstring comment;
	fstring location;
	fstring sepfile;
	fstring printprocessor;
	fstring datatype;
	fstring parameters;
	
	rpcstr_pull(servername, i2->servername.buffer,sizeof(servername), 0, STR_TERMINATE);
	rpcstr_pull(printername, i2->printername.buffer,sizeof(printername), 0, STR_TERMINATE);
	rpcstr_pull(sharename, i2->sharename.buffer,sizeof(sharename), 0, STR_TERMINATE);
	rpcstr_pull(portname, i2->portname.buffer,sizeof(portname), 0, STR_TERMINATE);
	rpcstr_pull(drivername, i2->drivername.buffer,sizeof(drivername), 0, STR_TERMINATE);
	rpcstr_pull(comment, i2->comment.buffer,sizeof(comment), 0, STR_TERMINATE);
	rpcstr_pull(location, i2->location.buffer,sizeof(location), 0, STR_TERMINATE);
	rpcstr_pull(sepfile, i2->sepfile.buffer,sizeof(sepfile), 0, STR_TERMINATE);
	rpcstr_pull(printprocessor, i2->printprocessor.buffer,sizeof(printprocessor), 0, STR_TERMINATE);
	rpcstr_pull(datatype, i2->datatype.buffer,sizeof(datatype), 0, STR_TERMINATE);
	rpcstr_pull(parameters, i2->parameters.buffer,sizeof(parameters), 0, STR_TERMINATE);

	printf("\tservername:[%s]\n", servername);
	printf("\tprintername:[%s]\n", printername);
	printf("\tsharename:[%s]\n", sharename);
	printf("\tportname:[%s]\n", portname);
	printf("\tdrivername:[%s]\n", drivername);
	printf("\tcomment:[%s]\n", comment);
	printf("\tlocation:[%s]\n", location);
	printf("\tsepfile:[%s]\n", sepfile);
	printf("\tprintprocessor:[%s]\n", printprocessor);
	printf("\tdatatype:[%s]\n", datatype);
	printf("\tparameters:[%s]\n", parameters);
	printf("\tattributes:[0x%x]\n", i2->attributes);
	printf("\tpriority:[0x%x]\n", i2->priority);
	printf("\tdefaultpriority:[0x%x]\n", i2->defaultpriority);
	printf("\tstarttime:[0x%x]\n", i2->starttime);
	printf("\tuntiltime:[0x%x]\n", i2->untiltime);
	printf("\tstatus:[0x%x]\n", i2->status);
	printf("\tcjobs:[0x%x]\n", i2->cjobs);
	printf("\taverageppm:[0x%x]\n", i2->averageppm);

	if (i2->secdesc) display_sec_desc(i2->secdesc);
}

/****************************************************************************
printer info level 3 display function
****************************************************************************/
static void display_print_info_3(PRINTER_INFO_3 *i3)
{
	printf("\tflags:[0x%x]\n", i3->flags);

	display_sec_desc(i3->secdesc);
}

/* Enumerate printers */

static NTSTATUS cmd_spoolss_enum_printers(struct cli_state *cli, 
                                          TALLOC_CTX *mem_ctx,
                                          int argc, char **argv)
{
	NTSTATUS		result = NT_STATUS_UNSUCCESSFUL;
	uint32			info_level = 1;
	PRINTER_INFO_CTR	ctr;
	int 			returned;
	uint32			i = 0;

	if (argc > 2) 
	{
		printf("Usage: %s [level]\n", argv[0]);
		return NT_STATUS_OK;
	}

	if (argc == 2) {
		info_level = atoi(argv[1]);
	}

	/* Enumerate printers  -- Should we enumerate types other 
	   than PRINTER_ENUM_LOCAL?  Maybe accept as a parameter?  --jerry */
	ZERO_STRUCT(ctr);
	result = cli_spoolss_enum_printers(cli, mem_ctx, PRINTER_ENUM_LOCAL, 
					   info_level, &returned, &ctr);

	if (NT_STATUS_IS_OK(result)) 
	{
		if (!returned)
			printf ("No Printers returned.\n");
	
		switch(info_level) {
		case 0:
			for (i=0; i<returned; i++) {
				display_print_info_0(&(ctr.printers_0[i]));
			}
			break;
		case 1:
			for (i=0; i<returned; i++) {
				display_print_info_1(&(ctr.printers_1[i]));
			}
			break;
		case 2:
			for (i=0; i<returned; i++) {
				display_print_info_2(&(ctr.printers_2[i]));
			}
			break;
		case 3:
			for (i=0; i<returned; i++) {
				display_print_info_3(&(ctr.printers_3[i]));
			}
			break;
		default:
			printf("unknown info level %d\n", info_level);
			break;
		}
	}

	return result;
}

/****************************************************************************
port info level 1 display function
****************************************************************************/
static void display_port_info_1(PORT_INFO_1 *i1)
{
	fstring buffer;
	
	rpcstr_pull(buffer, i1->port_name.buffer, sizeof(buffer), 0, STR_TERMINATE);
	printf("\tPort Name:\t[%s]\n", buffer);
}

/****************************************************************************
port info level 2 display function
****************************************************************************/
static void display_port_info_2(PORT_INFO_2 *i2)
{
	fstring buffer;
	
	rpcstr_pull(buffer, i2->port_name.buffer, sizeof(buffer), 0, STR_TERMINATE);
	printf("\tPort Name:\t[%s]\n", buffer);
	rpcstr_pull(buffer, i2->monitor_name.buffer, sizeof(buffer), 0, STR_TERMINATE);

	printf("\tMonitor Name:\t[%s]\n", buffer);
	rpcstr_pull(buffer, i2->description.buffer, sizeof(buffer), 0, STR_TERMINATE);

	printf("\tDescription:\t[%s]\n", buffer);
	printf("\tPort Type:\t[%d]\n", i2->port_type);
	printf("\tReserved:\t[%d]\n", i2->reserved);
	printf("\n");
}

/* Enumerate ports */

static NTSTATUS cmd_spoolss_enum_ports(struct cli_state *cli, 
                                       TALLOC_CTX *mem_ctx,
                                       int argc, char **argv)
{
	NTSTATUS		result = NT_STATUS_UNSUCCESSFUL;
	uint32                  info_level = 1;
	PORT_INFO_CTR 		ctr;
	int 			returned;
	
	if (argc > 2) {
		printf("Usage: %s [level]\n", argv[0]);
		return NT_STATUS_OK;
	}
	
	if (argc == 2) {
		info_level = atoi(argv[1]);
	}

	/* Enumerate ports */
	ZERO_STRUCT(ctr);

	result = cli_spoolss_enum_ports(cli, mem_ctx, info_level, &returned, &ctr);

	if (NT_STATUS_IS_OK(result)) {
		int i;

		for (i = 0; i < returned; i++) {
			switch (info_level) {
			case 1:
				display_port_info_1(&ctr.port.info_1[i]);
			break;
			case 2:
				display_port_info_2(&ctr.port.info_2[i]);
				break;
			default:
				printf("unknown info level %d\n", info_level);
				break;
			}
		}
	}

	return result;
}

/***********************************************************************
 * Set printer comment - use a level2 set.
 */
static NTSTATUS cmd_spoolss_setprinter(struct cli_state *cli, 
                                       TALLOC_CTX *mem_ctx,
                                       int argc, char **argv)
{
	POLICY_HND 	pol;
	NTSTATUS	result;
	uint32 		info_level = 2;
	BOOL 		opened_hnd = False;
	PRINTER_INFO_CTR ctr;
	fstring 	printername, 
			servername,
			user,
			comment;

	if (argc == 1 || argc > 3) {
		printf("Usage: %s printername comment\n", argv[0]);
		
		return NT_STATUS_OK;
	}

	/* Open a printer handle */
	if (argc == 3) {
		fstrcpy(comment, argv[2]);
	}

	slprintf (servername, sizeof(fstring)-1, "\\\\%s", cli->desthost);
	strupper (servername);
	fstrcpy (printername, argv[1]);
	fstrcpy  (user, cli->user_name);
	
	/* get a printer handle */
	result = cli_spoolss_open_printer_ex(
		cli, mem_ctx, printername, "", MAXIMUM_ALLOWED_ACCESS, servername,
		user, &pol);
	if (!NT_STATUS_IS_OK(result)) {
		goto done;
	}
 
	opened_hnd = True;

	/* Get printer info */
	result = cli_spoolss_getprinter(cli, mem_ctx, &pol, info_level, &ctr);
	if (!NT_STATUS_IS_OK(result)) {
		goto done;
	}

	/* Modify the comment. */
	init_unistr(&ctr.printers_2->comment, comment);
	ctr.printers_2->devmode = NULL;
	ctr.printers_2->secdesc = NULL;

	result =  cli_spoolss_setprinter(cli, mem_ctx, &pol, info_level, &ctr, 0);
	if (NT_STATUS_IS_OK(result))
		printf("Success in setting comment.\n");

 done: 
	if (opened_hnd) 
		cli_spoolss_close_printer(cli, mem_ctx, &pol);

	return result;
}

/***********************************************************************
 * Get printer information
 */
static NTSTATUS cmd_spoolss_getprinter(struct cli_state *cli, 
                                       TALLOC_CTX *mem_ctx,
                                       int argc, char **argv)
{
	POLICY_HND 	pol;
	NTSTATUS	result;
	uint32 		info_level = 1;
	BOOL 		opened_hnd = False;
	PRINTER_INFO_CTR ctr;
	fstring 	printername, 
			servername,
			user;

	if (argc == 1 || argc > 3) {
		printf("Usage: %s <printername> [level]\n", argv[0]);
		
		return NT_STATUS_OK;
	}

	/* Open a printer handle */
	if (argc == 3) {
		info_level = atoi(argv[2]);
	}

	slprintf (servername, sizeof(fstring)-1, "\\\\%s", cli->desthost);
	strupper (servername);
	fstrcpy (printername, argv[1]);
	fstrcpy  (user, cli->user_name);
	
	/* get a printer handle */
	result = cli_spoolss_open_printer_ex(
		cli, mem_ctx, printername, "", MAXIMUM_ALLOWED_ACCESS, servername,
		user, &pol);
	if (!NT_STATUS_IS_OK(result)) {
		goto done;
	}
 
	opened_hnd = True;

	/* Get printer info */
	result = cli_spoolss_getprinter(cli, mem_ctx, &pol, info_level, &ctr);
	if (!NT_STATUS_IS_OK(result)) {
		goto done;
	}

	/* Display printer info */

	switch (info_level) {
	case 0: 
		display_print_info_0(ctr.printers_0);
		break;
	case 1:
		display_print_info_1(ctr.printers_1);
		break;
	case 2:
		display_print_info_2(ctr.printers_2);
		break;
	case 3:
		display_print_info_3(ctr.printers_3);
		break;
	default:
		printf("unknown info level %d\n", info_level);
		break;
	}

 done: 
	if (opened_hnd) 
		cli_spoolss_close_printer(cli, mem_ctx, &pol);

	return result;
}

/****************************************************************************
printer info level 0 display function
****************************************************************************/
static void display_print_driver_1(DRIVER_INFO_1 *i1)
{
	fstring name;
	if (i1 == NULL)
		return;

	rpcstr_pull(name, i1->name.buffer, sizeof(name), 0, STR_TERMINATE);

	printf ("Printer Driver Info 1:\n");
	printf ("\tDriver Name: [%s]\n\n", name);
	
	return;
}

/****************************************************************************
printer info level 1 display function
****************************************************************************/
static void display_print_driver_2(DRIVER_INFO_2 *i1)
{
	fstring name;
	fstring architecture;
	fstring driverpath;
	fstring datafile;
	fstring configfile;
	if (i1 == NULL)
		return;

	rpcstr_pull(name, i1->name.buffer, sizeof(name), 0, STR_TERMINATE);
	rpcstr_pull(architecture, i1->architecture.buffer, sizeof(architecture), 0, STR_TERMINATE);
	rpcstr_pull(driverpath, i1->driverpath.buffer, sizeof(driverpath), 0, STR_TERMINATE);
	rpcstr_pull(datafile, i1->datafile.buffer, sizeof(datafile), 0, STR_TERMINATE);
	rpcstr_pull(configfile, i1->configfile.buffer, sizeof(configfile), 0, STR_TERMINATE);

	printf ("Printer Driver Info 2:\n");
	printf ("\tVersion: [%x]\n", i1->version);
	printf ("\tDriver Name: [%s]\n", name);
	printf ("\tArchitecture: [%s]\n", architecture);
	printf ("\tDriver Path: [%s]\n", driverpath);
	printf ("\tDatafile: [%s]\n", datafile);
	printf ("\tConfigfile: [%s]\n\n", configfile);

	return;
}

/****************************************************************************
printer info level 2 display function
****************************************************************************/
static void display_print_driver_3(DRIVER_INFO_3 *i1)
{
	fstring name;
	fstring architecture;
	fstring driverpath;
	fstring datafile;
	fstring configfile;
	fstring helpfile;
	fstring dependentfiles;
	fstring monitorname;
	fstring defaultdatatype;
	
	int length=0;
	BOOL valid = True;
	
	if (i1 == NULL)
		return;

	rpcstr_pull(name, i1->name.buffer, sizeof(name), 0, STR_TERMINATE);
	rpcstr_pull(architecture, i1->architecture.buffer, sizeof(architecture), 0, STR_TERMINATE);
	rpcstr_pull(driverpath, i1->driverpath.buffer, sizeof(driverpath), 0, STR_TERMINATE);
	rpcstr_pull(datafile, i1->datafile.buffer, sizeof(datafile), 0, STR_TERMINATE);
	rpcstr_pull(configfile, i1->configfile.buffer, sizeof(configfile), 0, STR_TERMINATE);
	rpcstr_pull(helpfile, i1->helpfile.buffer, sizeof(helpfile), 0, STR_TERMINATE);
	rpcstr_pull(monitorname, i1->monitorname.buffer, sizeof(monitorname), 0, STR_TERMINATE);
	rpcstr_pull(defaultdatatype, i1->defaultdatatype.buffer, sizeof(defaultdatatype), 0, STR_TERMINATE);

	printf ("Printer Driver Info 3:\n");
	printf ("\tVersion: [%x]\n", i1->version);
	printf ("\tDriver Name: [%s]\n",name);
	printf ("\tArchitecture: [%s]\n", architecture);
	printf ("\tDriver Path: [%s]\n", driverpath);
	printf ("\tDatafile: [%s]\n", datafile);
	printf ("\tConfigfile: [%s]\n", configfile);
	printf ("\tHelpfile: [%s]\n\n", helpfile);

	while (valid)
	{
		rpcstr_pull(dependentfiles, i1->dependentfiles+length, sizeof(dependentfiles), 0, STR_TERMINATE);
		
		length+=strlen(dependentfiles)+1;
		
		if (strlen(dependentfiles) > 0)
		{
			printf ("\tDependentfiles: [%s]\n", dependentfiles);
		}
		else
		{
			valid = False;
		}
	}
	
	printf ("\n");

	printf ("\tMonitorname: [%s]\n", monitorname);
	printf ("\tDefaultdatatype: [%s]\n\n", defaultdatatype);

	return;	
}

/***********************************************************************
 * Get printer information
 */
static NTSTATUS cmd_spoolss_getdriver(struct cli_state *cli, 
                                      TALLOC_CTX *mem_ctx,
                                      int argc, char **argv)
{
	POLICY_HND 	pol;
	NTSTATUS	result;
	uint32		info_level = 3;
	BOOL 		opened_hnd = False;
	PRINTER_DRIVER_CTR 	ctr;
	fstring 	printername, 
			servername, 
			user;
	uint32		i;

	if ((argc == 1) || (argc > 3)) 
	{
		printf("Usage: %s <printername> [level]\n", argv[0]);
		return NT_STATUS_OK;
	}

	/* get the arguments need to open the printer handle */
	slprintf (servername, sizeof(fstring)-1, "\\\\%s", cli->desthost);
	strupper (servername);
	fstrcpy  (user, cli->user_name);
	fstrcpy  (printername, argv[1]);
	if (argc == 3)
		info_level = atoi(argv[2]);

	/* Open a printer handle */
	result=cli_spoolss_open_printer_ex (cli, mem_ctx, printername, "", 
					    MAXIMUM_ALLOWED_ACCESS, servername, user, &pol);
	if (!NT_STATUS_IS_OK(result)) {
		printf ("Error opening printer handle for %s!\n", printername);
		return result;
	}

	opened_hnd = True;

	/* loop through and print driver info level for each architecture */
	for (i=0; archi_table[i].long_archi!=NULL; i++) 
	{
		result = cli_spoolss_getprinterdriver(cli, mem_ctx, &pol, info_level, 
						       archi_table[i].long_archi, &ctr);
		if (!NT_STATUS_IS_OK(result)) {
			continue;
		}

			
		printf ("\n[%s]\n", archi_table[i].long_archi);
		switch (info_level) 
		{
			
		case 1:
			display_print_driver_1 (ctr.info1);
			break;
		case 2:
			display_print_driver_2 (ctr.info2);
			break;
		case 3:
			display_print_driver_3 (ctr.info3);
			break;
		default:
			printf("unknown info level %d\n", info_level);
			break;
		}
	}
	
	/* cleanup */
	if (opened_hnd)
		cli_spoolss_close_printer (cli, mem_ctx, &pol);
	
	return result;
		
}

/***********************************************************************
 * Get printer information
 */
static NTSTATUS cmd_spoolss_enum_drivers(struct cli_state *cli, 
                                         TALLOC_CTX *mem_ctx,
                                         int argc, char **argv)
{
	NTSTATUS	result = NT_STATUS_OK;
	uint32          info_level = 1;
	PRINTER_DRIVER_CTR 	ctr;
	fstring 	servername;
	uint32		i, j,
			returned;

	if (argc > 2) 
	{
		printf("Usage: enumdrivers [level]\n");
		return NT_STATUS_OK;
	}

	/* get the arguments need to open the printer handle */
	slprintf (servername, sizeof(fstring)-1, "\\\\%s", cli->desthost);
	strupper (servername);
	if (argc == 2)
		info_level = atoi(argv[1]);


	/* loop through and print driver info level for each architecture */
	for (i=0; archi_table[i].long_archi!=NULL; i++) 
	{
		returned = 0;	
		result = cli_spoolss_enumprinterdrivers (cli, mem_ctx, info_level, 
				archi_table[i].long_archi, &returned, &ctr);

		if (returned == 0)
			continue;
			

		if (!NT_STATUS_IS_OK(result))
		{
			printf ("Error getting driver for environment [%s] - %s\n",
				archi_table[i].long_archi, get_nt_error_msg(result));
			continue;
		}
		
		printf ("\n[%s]\n", archi_table[i].long_archi);
		switch (info_level) 
		{
			
		case 1:
			for (j=0; j < returned; j++) {
				display_print_driver_1 (&(ctr.info1[j]));
			}
			break;
		case 2:
			for (j=0; j < returned; j++) {
				display_print_driver_2 (&(ctr.info2[j]));
			}
			break;
		case 3:
			for (j=0; j < returned; j++) {
				display_print_driver_3 (&(ctr.info3[j]));
			}
			break;
		default:
			printf("unknown info level %d\n", info_level);
			break;
		}
	}
	
	return result;
}

/****************************************************************************
printer info level 1 display function
****************************************************************************/
static void display_printdriverdir_1(DRIVER_DIRECTORY_1 *i1)
{
        fstring name;
        if (i1 == NULL)
                return;
 
	rpcstr_pull(name, i1->name.buffer, sizeof(name), 0, STR_TERMINATE);
 
	printf ("\tDirectory Name:[%s]\n", name);
}

/***********************************************************************
 * Get printer driver directory information
 */
static NTSTATUS cmd_spoolss_getdriverdir(struct cli_state *cli, 
                                         TALLOC_CTX *mem_ctx,
                                         int argc, char **argv)
{
	NTSTATUS		result;
	fstring			env;
	DRIVER_DIRECTORY_CTR	ctr;

	if (argc > 2) 
	{
		printf("Usage: %s [environment]\n", argv[0]);
		return NT_STATUS_OK;
	}

	/* get the arguments need to open the printer handle */
	if (argc == 2)
		fstrcpy (env, argv[1]);
	else
		fstrcpy (env, "Windows NT x86");

	/* Get the directory.  Only use Info level 1 */
	result = cli_spoolss_getprinterdriverdir (cli, mem_ctx, 1, env, &ctr);
	if (!NT_STATUS_IS_OK(result)) {
		return result;
	}

	
	display_printdriverdir_1 (ctr.info1);

	return result;
}

/*******************************************************************************
 set the version and environment fields of a DRIVER_INFO_3 struct
 ******************************************************************************/
void set_drv_info_3_env (DRIVER_INFO_3 *info, const char *arch)
{

	int i;
	
	for (i=0; archi_table[i].long_archi != NULL; i++) 
	{
		if (strcmp(arch, archi_table[i].short_archi) == 0)
		{
			info->version = archi_table[i].version;
			init_unistr (&info->architecture, archi_table[i].long_archi);
			break;
		}
	}
	
	if (archi_table[i].long_archi == NULL)
	{
		DEBUG(0, ("set_drv_info_3_env: Unknown arch [%s]\n", arch));
	}
	
	return;
}


/**************************************************************************
 wrapper for strtok to get the next parameter from a delimited list.
 Needed to handle the empty parameter string denoted by "NULL"
 *************************************************************************/
static char* get_driver_3_param (char* str, char* delim, UNISTR* dest)
{
	char	*ptr;

	/* get the next token */
	ptr = strtok(str, delim);

	/* a string of 'NULL' is used to represent an empty
	   parameter because two consecutive delimiters
	   will not return an empty string.  See man strtok(3)
	   for details */
	if (StrCaseCmp(ptr, "NULL") == 0)
		ptr = NULL;

	if (dest != NULL)
		init_unistr(dest, ptr);	

	return ptr;
}

/********************************************************************************
 fill in the members of a DRIVER_INFO_3 struct using a character 
 string in the form of
 	 <Long Printer Name>:<Driver File Name>:<Data File Name>:\
	     <Config File Name>:<Help File Name>:<Language Monitor Name>:\
	     <Default Data Type>:<Comma Separated list of Files> 
 *******************************************************************************/
static BOOL init_drv_info_3_members (
	TALLOC_CTX *mem_ctx, 
	DRIVER_INFO_3 *info, 
	char *args
)
{
	char	*str, *str2;
	uint32	len, i;
	
	/* fill in the UNISTR fields */
	str = get_driver_3_param (args, ":", &info->name);
	str = get_driver_3_param (NULL, ":", &info->driverpath);
	str = get_driver_3_param (NULL, ":", &info->datafile);
	str = get_driver_3_param (NULL, ":", &info->configfile);
	str = get_driver_3_param (NULL, ":", &info->helpfile);
	str = get_driver_3_param (NULL, ":", &info->monitorname);
	str = get_driver_3_param (NULL, ":", &info->defaultdatatype);

	/* <Comma Separated List of Dependent Files> */
	str2 = get_driver_3_param (NULL, ":", NULL); /* save the beginning of the string */
	str = str2;			

	/* begin to strip out each filename */
	str = strtok(str, ",");		
	len = 0;
	while (str != NULL)
	{
		/* keep a cumlative count of the str lengths */
		len += strlen(str)+1;
		str = strtok(NULL, ",");
	}

	/* allocate the space; add one extra slot for a terminating NULL.
	   Each filename is NULL terminated and the end contains a double
	   NULL */
	if ((info->dependentfiles=(uint16*)talloc(mem_ctx, (len+1)*sizeof(uint16))) == NULL)
	{
		DEBUG(0,("init_drv_info_3_members: Unable to malloc memory for dependenfiles\n"));
		return False;
	}
	for (i=0; i<len; i++)
	{
		info->dependentfiles[i] = SSVAL(&info->dependentfiles[i], 0, str2[i]);
	}
	info->dependentfiles[len] = '\0';

	return True;
}


static NTSTATUS cmd_spoolss_addprinterdriver(struct cli_state *cli, 
                                             TALLOC_CTX *mem_ctx,
                                             int argc, char **argv)
{
	NTSTATUS		result;
	uint32                  level = 3;
	PRINTER_DRIVER_CTR	ctr;
	DRIVER_INFO_3		info3;
	fstring			arch;
	fstring			driver_name;

	/* parse the command arguements */
	if (argc != 3)
	{
		printf ("Usage: %s <Environment>\\\n", argv[0]);
		printf ("\t<Long Printer Name>:<Driver File Name>:<Data File Name>:\\\n");
    		printf ("\t<Config File Name>:<Help File Name>:<Language Monitor Name>:\\\n");
	    	printf ("\t<Default Data Type>:<Comma Separated list of Files>\n");

		return NT_STATUS_OK;
        }
		
	/* Fill in the DRIVER_INFO_3 struct */
	ZERO_STRUCT(info3);
	if (!get_short_archi(arch, argv[1]))
	{
		printf ("Error Unknown architechture [%s]\n", argv[1]);
		return NT_STATUS_INVALID_PARAMETER;
	}
	else
		set_drv_info_3_env(&info3, arch);

	if (!init_drv_info_3_members(mem_ctx, &info3, argv[2]))
	{
		printf ("Error Invalid parameter list - %s.\n", argv[2]);
		return NT_STATUS_INVALID_PARAMETER;
	}


	ctr.info3 = &info3;
	result = cli_spoolss_addprinterdriver (cli, mem_ctx, level, &ctr);
	if (!NT_STATUS_IS_OK(result)) {
		return result;
	}

	rpcstr_pull(driver_name, info3.name.buffer, sizeof(driver_name), 0, STR_TERMINATE);
	printf ("Printer Driver %s successfully installed.\n", driver_name);

	return result;
}


static NTSTATUS cmd_spoolss_addprinterex(struct cli_state *cli, 
                                         TALLOC_CTX *mem_ctx, 
                                         int argc, char **argv)
{
	NTSTATUS		result;
	uint32			level = 2;
	PRINTER_INFO_CTR	ctr;
	PRINTER_INFO_2		info2;
	fstring			servername;
	
	/* parse the command arguements */
	if (argc != 5)
	{
		printf ("Usage: %s <name> <shared name> <driver> <port>\n", argv[0]);
		return NT_STATUS_OK;
        }
	
        slprintf (servername, sizeof(fstring)-1, "\\\\%s", cli->desthost);
        strupper (servername);

	/* Fill in the DRIVER_INFO_3 struct */
	ZERO_STRUCT(info2);
#if 0	/* JERRY */
	init_unistr( &info2.servername, 	servername);
#endif
	init_unistr( &info2.printername,	argv[1]);
	init_unistr( &info2.sharename, 		argv[2]);
	init_unistr( &info2.drivername,		argv[3]);
	init_unistr( &info2.portname,		argv[4]);
	init_unistr( &info2.comment,		"Created by rpcclient");
	init_unistr( &info2.printprocessor, 	"winprint");
	init_unistr( &info2.datatype,		"RAW");
	info2.devmode = 	NULL;
	info2.secdesc = 	NULL;
	info2.attributes 	= PRINTER_ATTRIBUTE_SHARED;
	info2.priority 		= 0;
	info2.defaultpriority	= 0;
	info2.starttime		= 0;
	info2.untiltime		= 0;
	
	/* These three fields must not be used by AddPrinter() 
	   as defined in the MS Platform SDK documentation..  
	   --jerry
	info2.status		= 0;
	info2.cjobs		= 0;
	info2.averageppm	= 0;
	*/

	ctr.printers_2 = &info2;
	result = cli_spoolss_addprinterex (cli, mem_ctx, level, &ctr);
	if (!NT_STATUS_IS_OK(result)) {
		return result;
	}

	printf ("Printer %s successfully installed.\n", argv[1]);

	return result;
}

static NTSTATUS cmd_spoolss_setdriver(struct cli_state *cli, 
                                      TALLOC_CTX *mem_ctx,
                                      int argc, char **argv)
{
	POLICY_HND		pol;
	NTSTATUS		result;
	uint32			level = 2;
	BOOL			opened_hnd = False;
	PRINTER_INFO_CTR	ctr;
	PRINTER_INFO_2		info2;
	fstring			servername,
				printername,
				user;
	
	/* parse the command arguements */
	if (argc != 3)
	{
		printf ("Usage: %s <printer> <driver>\n", argv[0]);
		return NT_STATUS_OK;
        }

	slprintf (servername, sizeof(fstring)-1, "\\\\%s", cli->desthost);
	strupper (servername);
	slprintf (printername, sizeof(fstring)-1, "%s\\%s", servername, argv[1]);
	fstrcpy  (user, cli->user_name);

	/* get a printer handle */
	result = cli_spoolss_open_printer_ex(cli, mem_ctx, printername, "",
					     MAXIMUM_ALLOWED_ACCESS, servername, user, &pol);
	if (!NT_STATUS_IS_OK(result)) {
		goto done;
	}

	opened_hnd = True;

	/* Get printer info */
	ZERO_STRUCT (info2);
	ctr.printers_2 = &info2;
	result = cli_spoolss_getprinter(cli, mem_ctx, &pol, level, &ctr);
	if (!NT_STATUS_IS_OK(result)) {
		printf ("Unable to retrieve printer information!\n");
		goto done;
	}

	/* set the printer driver */
	init_unistr(&ctr.printers_2->drivername, argv[2]);
	result = cli_spoolss_setprinter(cli, mem_ctx, &pol, level, &ctr, 0);
	if (!NT_STATUS_IS_OK(result)) {
		printf ("SetPrinter call failed!\n");
		goto done;;
	}
	printf ("Succesfully set %s to driver %s.\n", argv[1], argv[2]);


done:
	/* cleanup */
	if (opened_hnd)
		cli_spoolss_close_printer(cli, mem_ctx, &pol);

	return result;
}


static NTSTATUS cmd_spoolss_deletedriver(struct cli_state *cli,
                                         TALLOC_CTX *mem_ctx,
                                         int argc, char **argv)
{
	NTSTATUS		result = NT_STATUS_UNSUCCESSFUL;
	fstring			servername;
	int			i;
	
	/* parse the command arguements */
	if (argc != 2)
	{
		printf ("Usage: %s <driver>\n", argv[0]);
		return NT_STATUS_OK;
        }

	slprintf (servername, sizeof(fstring)-1, "\\\\%s", cli->desthost);
	strupper (servername);

	/* delete the driver for all architectures */
	for (i=0; archi_table[i].long_archi; i++)
	{
		/* make the call to remove the driver */
		result = cli_spoolss_deleteprinterdriver(cli, mem_ctx, 
							 archi_table[i].long_archi, argv[1]);
		if (!NT_STATUS_IS_OK(result)) {
			printf ("Failed to remove driver %s for arch [%s] - error %s!\n", 
				argv[1], archi_table[i].long_archi, get_nt_error_msg(result));
		}
		else
			printf ("Driver %s removed for arch [%s].\n", argv[1], archi_table[i].long_archi);
	}
		
	return NT_STATUS_OK;		
}

static NTSTATUS cmd_spoolss_getprintprocdir(struct cli_state *cli, 
					    TALLOC_CTX *mem_ctx,
					    int argc, char **argv)
{
	NTSTATUS result = NT_STATUS_UNSUCCESSFUL;
	char *servername = NULL, *environment = NULL;
	fstring procdir;
	
	/* parse the command arguements */
	if (argc < 2 || argc > 3) {
		printf ("Usage: %s <server> [environment]\n", argv[0]);
		return NT_STATUS_OK;
        }

	if (asprintf(&servername, "\\\\%s", cli->desthost) < 0)
		return NT_STATUS_NO_MEMORY;
	strupper(servername);

	if (asprintf(&environment, "%s", (argc == 3) ? argv[2] :
		 			PRINTER_DRIVER_ARCHITECTURE) < 0) {
		SAFE_FREE(servername);
		return NT_STATUS_NO_MEMORY;
	}

	result = cli_spoolss_getprintprocessordirectory(
		cli, mem_ctx, servername, environment, procdir);

	if (NT_STATUS_IS_OK(result))
		printf("%s", procdir);

	SAFE_FREE(servername);
	SAFE_FREE(environment);

	return result;
}

static NTSTATUS cmd_spoolss_setprinterdata(struct cli_state *cli,
					    TALLOC_CTX *mem_ctx,
					    int argc, char **argv)
{
	NTSTATUS result = NT_STATUS_UNSUCCESSFUL;
	fstring servername, printername, user;
	POLICY_HND pol;
	BOOL opened_hnd = False;
	PRINTER_INFO_CTR ctr;
	PRINTER_INFO_0 *info = NULL;

	/* parse the command arguements */
	if (argc != 4) {
		printf ("Usage: %s <printer> <value> <data>\n", argv[0]);
		return NT_STATUS_OK;
        }

	slprintf (servername, sizeof(fstring)-1, "\\\\%s", cli->desthost);
	strupper (servername);
	slprintf (printername, sizeof(fstring)-1, "%s\\%s", servername, argv[1]);
	fstrcpy  (user, cli->user_name);

	/* get a printer handle */
	result = cli_spoolss_open_printer_ex(cli, mem_ctx, printername, "",
					     MAXIMUM_ALLOWED_ACCESS, servername, user, &pol);
	if (!NT_STATUS_IS_OK(result)) {
		goto done;
	}

	opened_hnd = True;
	
	printf("%s\n", timestring(True));
	
	result = cli_spoolss_getprinter(cli, mem_ctx, &pol, 0, &ctr);
	info = ctr.printers_0;
	if (!NT_STATUS_IS_OK(result)) {
		goto done;
	}
	printf("\tchange_id (before set)\t:[0x%x]\n", info->change_id);
	

	/* Get printer info */
	result = cli_spoolss_setprinterdata(cli, mem_ctx, &pol, argv[2], argv[3]);
	if (!NT_STATUS_IS_OK(result)) {
		printf ("Unable to set [%s=%s]!\n", argv[2], argv[3]);
		goto done;
	}
	printf("\tSetPrinterData succeeded [%s: %s]\n", argv[2], argv[3]);

	result = cli_spoolss_getprinter(cli, mem_ctx, &pol, 0, &ctr);
	info = ctr.printers_0;
	if (!NT_STATUS_IS_OK(result)) {
		goto done;
	}
	printf("\tchange_id (after set)\t:[0x%x]\n", info->change_id);
	printf("%s\n", timestring(True));


done:
	/* cleanup */
	if (opened_hnd)
		cli_spoolss_close_printer(cli, mem_ctx, &pol);

	return result;
}

/* List of commands exported by this module */
struct cmd_set spoolss_commands[] = {

	{ "SPOOLSS"  },

	{ "adddriver",		cmd_spoolss_addprinterdriver,	PIPE_SPOOLSS, "Add a print driver",                  "" },
	{ "addprinter",		cmd_spoolss_addprinterex,	PIPE_SPOOLSS, "Add a printer",                       "" },
	{ "deldriver",		cmd_spoolss_deletedriver,	PIPE_SPOOLSS, "Delete a printer driver",             "" },
	{ "enumdata",		cmd_spoolss_not_implemented,	PIPE_SPOOLSS, "Enumerate printer data (*)",          "" },
	{ "enumjobs",		cmd_spoolss_not_implemented,	PIPE_SPOOLSS, "Enumerate print jobs (*)",            "" },
	{ "enumports", 		cmd_spoolss_enum_ports, 	PIPE_SPOOLSS, "Enumerate printer ports",             "" },
	{ "enumdrivers", 	cmd_spoolss_enum_drivers, 	PIPE_SPOOLSS, "Enumerate installed printer drivers", "" },
	{ "enumprinters", 	cmd_spoolss_enum_printers, 	PIPE_SPOOLSS, "Enumerate printers",                  "" },
	{ "getdata",		cmd_spoolss_not_implemented,	PIPE_SPOOLSS, "Get print driver data (*)",           "" },
	{ "getdriver",		cmd_spoolss_getdriver,		PIPE_SPOOLSS, "Get print driver information",        "" },
	{ "getdriverdir",	cmd_spoolss_getdriverdir,	PIPE_SPOOLSS, "Get print driver upload directory",   "" },
	{ "getprinter", 	cmd_spoolss_getprinter, 	PIPE_SPOOLSS, "Get printer info",                    "" },
	{ "getprintprocdir",	cmd_spoolss_getprintprocdir,    PIPE_SPOOLSS, "Get print processor directory",       "" },
	{ "openprinter",	cmd_spoolss_open_printer_ex,	PIPE_SPOOLSS, "Open printer handle",                 "" },
	{ "setdriver",		cmd_spoolss_setdriver,		PIPE_SPOOLSS, "Set printer driver",                  "" },
	{ "setprinter",	        cmd_spoolss_setprinter,         PIPE_SPOOLSS, "Set printer comment",                 "" },
	{ "setprinterdata",	cmd_spoolss_setprinterdata,     PIPE_SPOOLSS, "Set REG_SZ printer data",             "" },

	{ NULL }
};
