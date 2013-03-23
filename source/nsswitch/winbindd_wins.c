/* 
   Unix SMB/Netbios implementation.
   Version 2.0

   Winbind daemon - WINS related functions

   Copyright (C) Andrew Tridgell 1999
   Copyright (C) Herb Lewis 2002
   
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

/* Use our own create socket code so we don't recurse.... */

static int wins_lookup_open_socket_in(void)
{
	struct sockaddr_in sock;
	int val=1;
	int res;

	memset((char *)&sock,'\0',sizeof(sock));

#ifdef HAVE_SOCK_SIN_LEN
	sock.sin_len = sizeof(sock);
#endif
	sock.sin_port = 0;
	sock.sin_family = AF_INET;
	sock.sin_addr.s_addr = interpret_addr("0.0.0.0");
	res = socket(AF_INET, SOCK_DGRAM, 0);
	if (res == -1)
		return -1;

	setsockopt(res,SOL_SOCKET,SO_REUSEADDR,(char *)&val,sizeof(val));
#ifdef SO_REUSEPORT
	setsockopt(res,SOL_SOCKET,SO_REUSEPORT,(char *)&val,sizeof(val));
#endif /* SO_REUSEPORT */

	/* now we've got a socket - we need to bind it */

	if (bind(res, (struct sockaddr * ) &sock,sizeof(sock)) < 0) {
		close(res);
		return(-1);
	}

	set_socket_options(res,"SO_BROADCAST");

	return res;
}


static struct node_status *lookup_byaddr_backend(char *addr, int *count)
{
	int fd;
	struct in_addr  ip;
	struct nmb_name nname;
	struct node_status *status;

	fd = wins_lookup_open_socket_in();
	if (fd == -1)
		return NULL;

	make_nmb_name(&nname, "*", 0);
	ip = *interpret_addr2(addr);
	status = node_status_query(fd,&nname,ip, count);

	close(fd);
	return status;
}

static struct in_addr *lookup_byname_backend(const char *name, int *count)
{
	int fd;
	struct in_addr *ret = NULL;
	struct in_addr  p;
	int j;

	*count = 0;

	fd = wins_lookup_open_socket_in();
	if (fd == -1)
		return NULL;

	p = wins_srv_ip();
	if( !zero_ip(p) ) {
		ret = name_query(fd,name,0x20,False,True, p, count);
		goto out;
	}

	if (lp_wins_support()) {
		/* we are our own WINS server */
		ret = name_query(fd,name,0x20,False,True, *interpret_addr2("127.0.0.1"), count);
		goto out;
	}

	/* uggh, we have to broadcast to each interface in turn */
	for (j=iface_count() - 1;
	     j >= 0;
	     j--) {
		struct in_addr *bcast = iface_n_bcast(j);
		ret = name_query(fd,name,0x20,True,True,*bcast,count);
		if (ret) break;
	}

 out:

	close(fd);
	return ret;
}

/* Get hostname from IP  */

enum winbindd_result winbindd_wins_byip(struct winbindd_cli_state *state)
{
	char response[1024];
	int i, count, len, size, maxsize;
	struct node_status *status;

	DEBUG(3, ("[%5d]: wins_byip %s\n", state->pid,
		state->request.data.name));

	*response = '\0';
	maxsize = len = sizeof(response) - 1;

	if ((status = lookup_byaddr_backend(state->request.data.name, &count))){
	    size = strlen(state->request.data.name) + 1;
	    if (size > len) {
		SAFE_FREE(status);
		return WINBINDD_ERROR;
	    }
	    len -= size;
	    safe_strcat(response,state->request.data.name,maxsize);
	    safe_strcat(response,"\t",maxsize);
	    for (i = 0; i < count; i++) {
		/* ignore group names */
		if (status[i].flags & 0x80) continue;
		if (status[i].type == 0x20) {
			size = sizeof(status[i].name) + 1;
			if (size > len) {
			    SAFE_FREE(status);
			    return WINBINDD_ERROR;
			}
			len -= size;
			safe_strcat(response, status[i].name, maxsize);
			safe_strcat(response, " ", maxsize);
		}
	    }
	    SAFE_FREE(status);
	}
	fstrcpy(state->response.data.name.name,response);
	return WINBINDD_OK;
}

/* Get IP from hostname */

enum winbindd_result winbindd_wins_byname(struct winbindd_cli_state *state)
{
	struct in_addr *ip_list;
	int i, count, len, size, maxsize;
	char response[1024];
	char * addr;

	DEBUG(3, ("[%5d]: wins_byname %s\n", state->pid,
		state->request.data.name));

	*response = '\0';
	maxsize = len = sizeof(response) - 1;

	if ((ip_list = lookup_byname_backend(state->request.data.name,&count))){
		for (i = count; i ; i--) {
		    addr = inet_ntoa(ip_list[i-1]);
		    size = strlen(addr) + 1;
		    if (size > len) {
			SAFE_FREE(ip_list);
			return WINBINDD_ERROR;
		    }
		    len -= size;
		    safe_strcat(response,addr,maxsize);
		    safe_strcat(response," ",maxsize);
		}
		size = strlen(state->request.data.name) + 1;
		if (size > len) {
		    SAFE_FREE(ip_list);
		    return WINBINDD_ERROR;
		}   
		response[strlen(response)-1] = '\t';
		safe_strcat(response,state->request.data.name,maxsize);
		SAFE_FREE(ip_list);
	} else
		return WINBINDD_ERROR;

	fstrcpy(state->response.data.name.name,response);

	return WINBINDD_OK;
}
