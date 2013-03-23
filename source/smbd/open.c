/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   file opening and share modes
   Copyright (C) Andrew Tridgell 1992-1998
   Copyright (C) Jeremy Allison 2001
   
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

extern userdom_struct current_user_info;
extern uint16 global_oplock_port;
extern BOOL global_client_failed_oplock_break;

/****************************************************************************
 fd support routines - attempt to do a dos_open.
****************************************************************************/

static int fd_open(struct connection_struct *conn, char *fname, 
		   int flags, mode_t mode)
{
	int fd;

#ifdef O_NOFOLLOW
	if (!lp_symlinks(SNUM(conn)))
		flags |= O_NOFOLLOW;
#endif

	fd = conn->vfs_ops.open(conn,dos_to_unix(fname,False),flags,mode);

	/* Fix for files ending in '.' */
	if((fd == -1) && (errno == ENOENT) &&
	   (strchr(fname,'.')==NULL)) {
		pstrcat(fname,".");
		fd = conn->vfs_ops.open(conn,dos_to_unix(fname,False),flags,mode);
	}

	DEBUG(10,("fd_open: name %s, flags = 0%o mode = 0%o, fd = %d. %s\n", fname,
		flags, (int)mode, fd, (fd == -1) ? strerror(errno) : "" ));

	return fd;
}

/****************************************************************************
 Close the file associated with a fsp.
****************************************************************************/

int fd_close(struct connection_struct *conn, files_struct *fsp)
{
	if (fsp->fd == -1)
		return -1;
	return fd_close_posix(conn, fsp);
}


/****************************************************************************
 Check a filename for the pipe string.
****************************************************************************/

static void check_for_pipe(char *fname)
{
	/* special case of pipe opens */
	char s[10];
	StrnCpy(s,fname,sizeof(s)-1);
	strlower(s);
	if (strstr(s,"pipe/")) {
		DEBUG(3,("Rejecting named pipe open for %s\n",fname));
		unix_ERR_class = ERRSRV;
		unix_ERR_code = ERRaccess;
	}
}

/****************************************************************************
 Open a file.
****************************************************************************/

static BOOL open_file(files_struct *fsp,connection_struct *conn,
		      char *fname1,SMB_STRUCT_STAT *psbuf,int flags,mode_t mode)
{
	extern struct current_user current_user;
	pstring fname;
	int accmode = (flags & O_ACCMODE);
	int local_flags = flags;

	fsp->fd = -1;
	fsp->oplock_type = NO_OPLOCK;
	errno = EPERM;

	pstrcpy(fname,fname1);

	/* Check permissions */

	/*
	 * This code was changed after seeing a client open request 
	 * containing the open mode of (DENY_WRITE/read-only) with
	 * the 'create if not exist' bit set. The previous code
	 * would fail to open the file read only on a read-only share
	 * as it was checking the flags parameter  directly against O_RDONLY,
	 * this was failing as the flags parameter was set to O_RDONLY|O_CREAT.
	 * JRA.
	 */

	if (!CAN_WRITE(conn)) {
		/* It's a read-only share - fail if we wanted to write. */
		if(accmode != O_RDONLY) {
			DEBUG(3,("Permission denied opening %s\n",fname));
			check_for_pipe(fname);
			return False;
		} else if(flags & O_CREAT) {
			/* We don't want to write - but we must make sure that O_CREAT
			   doesn't create the file if we have write access into the
			   directory.
			*/
			flags &= ~O_CREAT;
		}
	}

	/*
	 * This little piece of insanity is inspired by the
	 * fact that an NT client can open a file for O_RDONLY,
	 * but set the create disposition to FILE_EXISTS_TRUNCATE.
	 * If the client *can* write to the file, then it expects to
	 * truncate the file, even though it is opening for readonly.
	 * Quicken uses this stupid trick in backup file creation...
	 * Thanks *greatly* to "David W. Chapman Jr." <dwcjr@inethouston.net>
	 * for helping track this one down. It didn't bite us in 2.0.x
	 * as we always opened files read-write in that release. JRA.
	 */

	if ((accmode == O_RDONLY) && ((flags & O_TRUNC) == O_TRUNC))
		local_flags = (flags & ~O_ACCMODE)|O_RDWR;

	/*
	 * We can't actually truncate here as the file may be locked.
	 * open_file_shared will take care of the truncate later. JRA.
	 */

	local_flags &= ~O_TRUNC;

	/* actually do the open */
	fsp->fd = fd_open(conn, fname, local_flags, mode);

	if (fsp->fd == -1)  {
		DEBUG(3,("Error opening file %s (%s) (local_flags=%d) (flags=%d)\n",
			 fname,strerror(errno),local_flags,flags));
		check_for_pipe(fname);
		return False;
	}

	if (!VALID_STAT(*psbuf)) {
		if (vfs_fstat(fsp,fsp->fd,psbuf) == -1) {
			DEBUG(0,("Error doing fstat on open file %s (%s)\n", fname,strerror(errno) ));
			fd_close(conn, fsp);
			return False;
		}
	}

	/*
	 * POSIX allows read-only opens of directories. We don't
	 * want to do this (we use a different code path for this)
	 * so catch a directory open and return an EISDIR. JRA.
	 */

	if(S_ISDIR(psbuf->st_mode)) {
		fd_close(conn, fsp);
		errno = EISDIR;
		return False;
	}

	fsp->mode = psbuf->st_mode;
	fsp->inode = psbuf->st_ino;
	fsp->dev = psbuf->st_dev;
	fsp->vuid = current_user.vuid;
	fsp->size = psbuf->st_size;
	fsp->pos = -1;
	fsp->can_lock = True;
	fsp->can_read = ((flags & O_WRONLY)==0);
	fsp->can_write = ((flags & (O_WRONLY|O_RDWR))!=0);
	fsp->share_mode = 0;
	fsp->print_file = False;
	fsp->modified = False;
	fsp->oplock_type = NO_OPLOCK;
	fsp->sent_oplock_break = NO_BREAK_SENT;
	fsp->is_directory = False;
	fsp->stat_open = False;
	fsp->directory_delete_on_close = False;
	fsp->conn = conn;
	/*
	 * Note that the file name here is the *untranslated* name
	 * ie. it is still in the DOS codepage sent from the client.
	 * All use of this filename will pass though the sys_xxxx
	 * functions which will do the dos_to_unix translation before
	 * mapping into a UNIX filename. JRA.
	 */
	string_set(&fsp->fsp_name,fname);
	fsp->wbmpx_ptr = NULL;      
	fsp->wcp = NULL; /* Write cache pointer. */

	DEBUG(2,("%s opened file %s read=%s write=%s (numopen=%d)\n",
		 *current_user_info.smb_name ? current_user_info.smb_name : conn->user,fsp->fsp_name,
		 BOOLSTR(fsp->can_read), BOOLSTR(fsp->can_write),
		 conn->num_files_open + 1));

	return True;
}

/****************************************************************************
  C. Hoch 11/22/95
  Helper for open_file_shared. 
  Truncate a file after checking locking; close file if locked.
  **************************************************************************/

static int truncate_unless_locked(struct connection_struct *conn, files_struct *fsp)
{
	SMB_BIG_UINT mask = (SMB_BIG_UINT)-1;

	if (is_locked(fsp,fsp->conn,mask,0,WRITE_LOCK,True)){
		errno = EACCES;
		unix_ERR_class = ERRDOS;
		unix_ERR_code = ERRlock;
		return -1;
	} else {
		return conn->vfs_ops.ftruncate(fsp,fsp->fd,0); 
	}
}

/*******************************************************************
return True if the filename is one of the special executable types
********************************************************************/
static BOOL is_executable(const char *fname)
{
	if ((fname = strrchr(fname,'.'))) {
		if (strequal(fname,".com") ||
		    strequal(fname,".dll") ||
		    strequal(fname,".exe") ||
		    strequal(fname,".sym")) {
			return True;
		}
	}
	return False;
}

enum {AFAIL,AREAD,AWRITE,AALL};

/*******************************************************************
reproduce the share mode access table
this is horrendoously complex, and really can't be justified on any
rational grounds except that this is _exactly_ what NT does. See
the DENY1 and DENY2 tests in smbtorture for a comprehensive set of
test routines.
********************************************************************/
static int access_table(int new_deny,int old_deny,int old_mode,
			BOOL same_pid, BOOL isexe)
{
	  if (new_deny == DENY_ALL || old_deny == DENY_ALL) return(AFAIL);

	  if (same_pid) {
		  if (isexe && old_mode == DOS_OPEN_RDONLY && 
		      old_deny == DENY_DOS && new_deny == DENY_READ) {
			  return AFAIL;
		  }
		  if (!isexe && old_mode == DOS_OPEN_RDONLY && 
		      old_deny == DENY_DOS && new_deny == DENY_DOS) {
			  return AREAD;
		  }
		  if (new_deny == DENY_FCB && old_deny == DENY_DOS) {
			  if (isexe) return AFAIL;
			  if (old_mode == DOS_OPEN_RDONLY) return AFAIL;
			  return AALL;
		  }
		  if (old_mode == DOS_OPEN_RDONLY && old_deny == DENY_DOS) {
			  if (new_deny == DENY_FCB || new_deny == DENY_READ) {
				  if (isexe) return AREAD;
				  return AFAIL;
			  }
		  }
		  if (old_deny == DENY_FCB) {
			  if (new_deny == DENY_DOS || new_deny == DENY_FCB) return AALL;
			  return AFAIL;
		  }
	  }

	  if (old_deny == DENY_DOS || new_deny == DENY_DOS || 
	      old_deny == DENY_FCB || new_deny == DENY_FCB) {
		  if (isexe) {
			  if (old_deny == DENY_FCB || new_deny == DENY_FCB) {
				  return AFAIL;
			  }
			  if (old_deny == DENY_DOS) {
				  if (new_deny == DENY_READ && 
				      (old_mode == DOS_OPEN_RDONLY || 
				       old_mode == DOS_OPEN_RDWR)) {
					  return AFAIL;
				  }
				  if (new_deny == DENY_WRITE && 
				      (old_mode == DOS_OPEN_WRONLY || 
				       old_mode == DOS_OPEN_RDWR)) {
					  return AFAIL;
				  }
				  return AALL;
			  }
			  if (old_deny == DENY_NONE) return AALL;
			  if (old_deny == DENY_READ) return AWRITE;
			  if (old_deny == DENY_WRITE) return AREAD;
		  }
		  /* it isn't a exe, dll, sym or com file */
		  if (old_deny == new_deny && same_pid)
			  return(AALL);    

		  if (old_deny == DENY_READ || new_deny == DENY_READ) return AFAIL;
		  if (old_mode == DOS_OPEN_RDONLY) return(AREAD);
		  
		  return(AFAIL);
	  }
	  
	  switch (new_deny) 
		  {
		  case DENY_WRITE:
			  if (old_deny==DENY_WRITE && old_mode==DOS_OPEN_RDONLY) return(AREAD);
			  if (old_deny==DENY_READ && old_mode==DOS_OPEN_RDONLY) return(AWRITE);
			  if (old_deny==DENY_NONE && old_mode==DOS_OPEN_RDONLY) return(AALL);
			  return(AFAIL);
		  case DENY_READ:
			  if (old_deny==DENY_WRITE && old_mode==DOS_OPEN_WRONLY) return(AREAD);
			  if (old_deny==DENY_READ && old_mode==DOS_OPEN_WRONLY) return(AWRITE);
			  if (old_deny==DENY_NONE && old_mode==DOS_OPEN_WRONLY) return(AALL);
			  return(AFAIL);
		  case DENY_NONE:
			  if (old_deny==DENY_WRITE) return(AREAD);
			  if (old_deny==DENY_READ) return(AWRITE);
			  if (old_deny==DENY_NONE) return(AALL);
			  return(AFAIL);      
		  }
	  return(AFAIL);      
}


/****************************************************************************
check if we can open a file with a share mode
****************************************************************************/

static BOOL check_share_mode(connection_struct *conn, share_mode_entry *share, int share_mode, 
			     const char *fname, BOOL fcbopen, int *flags)
{
	int deny_mode = GET_DENY_MODE(share_mode);
	int old_open_mode = GET_OPEN_MODE(share->share_mode);
	int old_deny_mode = GET_DENY_MODE(share->share_mode);

	/*
	 * share modes = false means don't bother to check for
	 * DENY mode conflict. This is a *really* bad idea :-). JRA.
	 */

	if(!lp_share_modes(SNUM(conn)))
		return True;

	/*
	 * Don't allow any opens once the delete on close flag has been
	 * set.
	 */

	if (GET_DELETE_ON_CLOSE_FLAG(share->share_mode)) {
		DEBUG(5,("check_share_mode: Failing open on file %s as delete on close flag is set.\n",
			fname ));
		unix_ERR_class = ERRDOS;
		unix_ERR_code = ERRnoaccess;
		return False;
	}

	/*
	 * If delete access was requested and the existing share mode doesn't have
	 * ALLOW_SHARE_DELETE then deny.
	 */

	if (GET_DELETE_ACCESS_REQUESTED(share_mode) && !GET_ALLOW_SHARE_DELETE(share->share_mode)) {
		DEBUG(5,("check_share_mode: Failing open on file %s as delete access requested and allow share delete not set.\n",
			fname ));
		unix_ERR_class = ERRDOS;
		unix_ERR_code = ERRbadshare;

		return False;
	}

	/*
	 * The inverse of the above.
	 * If delete access was granted and the new share mode doesn't have
	 * ALLOW_SHARE_DELETE then deny.
	 */

	if (GET_DELETE_ACCESS_REQUESTED(share->share_mode) && !GET_ALLOW_SHARE_DELETE(share_mode)) {
		DEBUG(5,("check_share_mode: Failing open on file %s as delete access granted and allow share delete not requested.\n",
			fname ));
		unix_ERR_class = ERRDOS;
		unix_ERR_code = ERRbadshare;

		return False;
	}

	{
		int access_allowed = access_table(deny_mode,old_deny_mode,old_open_mode,
										(share->pid == sys_getpid()),is_executable(fname));

		if ((access_allowed == AFAIL) ||
			(!fcbopen && (access_allowed == AREAD && *flags == O_RDWR)) ||
			(access_allowed == AREAD && *flags != O_RDONLY) ||
			(access_allowed == AWRITE && *flags != O_WRONLY)) {

			DEBUG(2,("Share violation on file (%d,%d,%d,%d,%s,fcbopen = %d, flags = %d) = %d\n",
				deny_mode,old_deny_mode,old_open_mode,
				(int)share->pid,fname, fcbopen, *flags, access_allowed));

			unix_ERR_class = ERRDOS;
			unix_ERR_code = ERRbadshare;

			return False;
		}

		if (access_allowed == AREAD)
			*flags = O_RDONLY;

		if (access_allowed == AWRITE)
			*flags = O_WRONLY;

	}

	return True;
}

/****************************************************************************
 Deal with open deny mode and oplock break processing.
 Invarient: Share mode must be locked on entry and exit.
 Returns -1 on error, or number of share modes on success (may be zero).
****************************************************************************/

static int open_mode_check(connection_struct *conn, const char *fname, SMB_DEV_T dev,
							SMB_INO_T inode, int share_mode, int *p_flags, int *p_oplock_request,
							BOOL *p_all_current_opens_are_level_II)
{
	int i;
	int num_share_modes;
	int oplock_contention_count = 0;
	share_mode_entry *old_shares = 0;
	BOOL fcbopen = False;
	BOOL broke_oplock;	

	if(GET_OPEN_MODE(share_mode) == DOS_OPEN_FCB)
		fcbopen = True;

	num_share_modes = get_share_modes(conn, dev, inode, &old_shares);

	if(num_share_modes == 0)
		return 0;

	/*
	 * Check if the share modes will give us access.
	 */

	do {
		share_mode_entry broken_entry;

		broke_oplock = False;
		*p_all_current_opens_are_level_II = True;

		for(i = 0; i < num_share_modes; i++) {
			share_mode_entry *share_entry = &old_shares[i];

			/* 
			 * By observation of NetBench, oplocks are broken *before* share
			 * modes are checked. This allows a file to be closed by the client
			 * if the share mode would deny access and the client has an oplock. 
			 * Check if someone has an oplock on this file. If so we must break 
			 * it before continuing. 
			 */

			if((*p_oplock_request && EXCLUSIVE_OPLOCK_TYPE(share_entry->op_type)) ||
						(!*p_oplock_request && (share_entry->op_type != NO_OPLOCK))) {

				BOOL opb_ret;

				DEBUG(5,("open_mode_check: oplock_request = %d, breaking oplock (%x) on file %s, \
dev = %x, inode = %.0f\n", *p_oplock_request, share_entry->op_type, fname, (unsigned int)dev, (double)inode));

				/* Oplock break - unlock to request it. */
				unlock_share_entry(conn, dev, inode);

				opb_ret = request_oplock_break(share_entry);

				/* Now relock. */
				lock_share_entry(conn, dev, inode);

				if(opb_ret == False) {
					DEBUG(0,("open_mode_check: FAILED when breaking oplock (%x) on file %s, \
dev = %x, inode = %.0f\n", old_shares[i].op_type, fname, (unsigned int)dev, (double)inode));
					SAFE_FREE(old_shares);
					errno = EACCES;
					unix_ERR_class = ERRDOS;
					unix_ERR_code = ERRbadshare;
					return -1;
				}

				broke_oplock = True;
				broken_entry = *share_entry;
				break;

			} else if (!LEVEL_II_OPLOCK_TYPE(share_entry->op_type)) {
				*p_all_current_opens_are_level_II = False;
			}

			/* someone else has a share lock on it, check to see 
				if we can too */

			if(check_share_mode(conn, share_entry, share_mode, fname, fcbopen, p_flags) == False) {
				SAFE_FREE(old_shares);
				errno = EACCES;
				return -1;
			}

		} /* end for */

		if(broke_oplock) {
			SAFE_FREE(old_shares);
			num_share_modes = get_share_modes(conn, dev, inode, &old_shares);
			oplock_contention_count++;

			/* Paranoia check that this is no longer an exlusive entry. */
			for(i = 0; i < num_share_modes; i++) {
				share_mode_entry *share_entry = &old_shares[i];

				if (share_modes_identical(&broken_entry, share_entry) && 
								EXCLUSIVE_OPLOCK_TYPE(share_entry->op_type) ) {

					/*
					 * This should not happen. The target left this oplock
					 * as exlusive.... The process *must* be dead.... 
					 */

					DEBUG(0,("open_mode_check: exlusive oplock left by process %d after break ! For file %s, \
dev = %x, inode = %.0f. Deleting it to continue...\n", (int)broken_entry.pid, fname, (unsigned int)dev, (double)inode));

					if (process_exists(broken_entry.pid)) {
						DEBUG(0,("open_mode_check: Existent process %d left active oplock.\n",
								broken_entry.pid ));
					}

					if (del_share_entry(dev, inode, &broken_entry, NULL) == -1) {
						errno = EACCES;
						unix_ERR_class = ERRDOS;
						unix_ERR_code = ERRbadshare;
						return -1;
					}

					/*
					 * We must reload the share modes after deleting the 
					 * other process's entry.
					 */

					SAFE_FREE(old_shares);
					num_share_modes = get_share_modes(conn, dev, inode, &old_shares);
					break;
				}
			} /* end for paranoia... */
		} /* end if broke_oplock */

	} while(broke_oplock);

	SAFE_FREE(old_shares);

	/*
	 * Refuse to grant an oplock in case the contention limit is
	 * reached when going through the lock list multiple times.
	 */

	if(oplock_contention_count >= lp_oplock_contention_limit(SNUM(conn))) {
		*p_oplock_request = 0;
		DEBUG(4,("open_mode_check: oplock contention = %d. Not granting oplock.\n",
				oplock_contention_count ));
	}

	return num_share_modes;
}

/****************************************************************************
set a kernel flock on a file for NFS interoperability
this requires a patch to Linux
****************************************************************************/
static void kernel_flock(files_struct *fsp, int deny_mode)
{
#if HAVE_KERNEL_SHARE_MODES
	int kernel_mode = 0;
	if (deny_mode == DENY_READ) kernel_mode = LOCK_MAND|LOCK_WRITE;
	else if (deny_mode == DENY_WRITE) kernel_mode = LOCK_MAND|LOCK_READ;
	else if (deny_mode == DENY_ALL) kernel_mode = LOCK_MAND;
	if (kernel_mode) flock(fsp->fd, kernel_mode);
#endif
	;;
}


/****************************************************************************
 Open a file with a share mode. On output from this open we are guarenteeing
 that 
****************************************************************************/
files_struct *open_file_shared(connection_struct *conn,char *fname, SMB_STRUCT_STAT *psbuf, 
				int share_mode,int ofun, mode_t mode,int oplock_request, int *Access,int *action)
{
	int flags=0;
	int flags2=0;
	int deny_mode = GET_DENY_MODE(share_mode);
	BOOL allow_share_delete = GET_ALLOW_SHARE_DELETE(share_mode);
	BOOL delete_access_requested = GET_DELETE_ACCESS_REQUESTED(share_mode);
	BOOL delete_on_close = GET_DELETE_ON_CLOSE_FLAG(share_mode);
	BOOL file_existed = VALID_STAT(*psbuf);
	BOOL fcbopen = False;
	SMB_DEV_T dev = 0;
	SMB_INO_T inode = 0;
	int num_share_modes = 0;
	BOOL all_current_opens_are_level_II = False;
	BOOL fsp_open = False;
	files_struct *fsp = NULL;
	int open_mode=0;
	uint16 port = 0;

	if (conn->printer) {
		/* printers are handled completely differently. Most
			of the passed parameters are ignored */
		*Access = DOS_OPEN_WRONLY;
		*action = FILE_WAS_CREATED;
		return print_fsp_open(conn, fname);
	}

	fsp = file_new(conn);
	if(!fsp)
		return NULL;

	DEBUG(10,("open_file_shared: fname = %s, share_mode = %x, ofun = %x, mode = %o, oplock request = %d\n",
		fname, share_mode, ofun, (int)mode,  oplock_request ));

	if (!check_name(fname,conn)) {
		file_free(fsp);
		return NULL;
	} 

	/* ignore any oplock requests if oplocks are disabled */
	if (!lp_oplocks(SNUM(conn)) || global_client_failed_oplock_break) {
		oplock_request = 0;
	}

	/* this is for OS/2 EAs - try and say we don't support them */
	if (strstr(fname,".+,;=[].")) {
		unix_ERR_class = ERRDOS;
		/* OS/2 Workplace shell fix may be main code stream in a later release. */ 
#if 1 /* OS2_WPS_FIX - Recent versions of OS/2 need this. */
		unix_ERR_code = ERRcannotopen;
#else /* OS2_WPS_FIX */
		unix_ERR_code = ERROR_EAS_NOT_SUPPORTED;
#endif /* OS2_WPS_FIX */

		DEBUG(5,("open_file_shared: OS/2 EA's are not supported.\n"));
		file_free(fsp);
		return NULL;
	}

	if ((GET_FILE_OPEN_DISPOSITION(ofun) == FILE_EXISTS_FAIL) && file_existed)  {
		DEBUG(5,("open_file_shared: create new requested for file %s and file already exists.\n",
			fname ));
		file_free(fsp);
		errno = EEXIST;
		return NULL;
	}
      
	if (CAN_WRITE(conn) && (GET_FILE_CREATE_DISPOSITION(ofun) == FILE_CREATE_IF_NOT_EXIST))
		flags2 |= O_CREAT;

	if (CAN_WRITE(conn) && (GET_FILE_OPEN_DISPOSITION(ofun) == FILE_EXISTS_TRUNCATE))
		flags2 |= O_TRUNC;

	if (GET_FILE_OPEN_DISPOSITION(ofun) == FILE_EXISTS_FAIL)
		flags2 |= O_EXCL;

	/* note that we ignore the append flag as 
		append does not mean the same thing under dos and unix */

	switch (GET_OPEN_MODE(share_mode)) {
		case DOS_OPEN_WRONLY: 
			flags = O_WRONLY; 
			break;
		case DOS_OPEN_FCB: 
			fcbopen = True;
			flags = O_RDWR; 
			break;
		case DOS_OPEN_RDWR: 
			flags = O_RDWR; 
			break;
		default:
			flags = O_RDONLY;
			break;
	}

#if defined(O_SYNC)
	if (GET_FILE_SYNC_OPENMODE(share_mode)) {
		flags2 |= O_SYNC;
	}
#endif /* O_SYNC */
  
	if (flags != O_RDONLY && file_existed && 
			(!CAN_WRITE(conn) || IS_DOS_READONLY(dos_mode(conn,fname,psbuf)))) {
		if (!fcbopen) {
			DEBUG(5,("open_file_shared: read/write access requested for file %s on read only %s\n",
				fname, !CAN_WRITE(conn) ? "share" : "file" ));
			file_free(fsp);
			errno = EACCES;
			return NULL;
		}
		flags = O_RDONLY;
	}

	if (deny_mode > DENY_NONE && deny_mode!=DENY_FCB) {
		DEBUG(2,("Invalid deny mode %d on file %s\n",deny_mode,fname));
		file_free(fsp);
		errno = EINVAL;
		return NULL;
	}

	if (file_existed) {

		dev = psbuf->st_dev;
		inode = psbuf->st_ino;

		lock_share_entry(conn, dev, inode);

		num_share_modes = open_mode_check(conn, fname, dev, inode, share_mode,
								&flags, &oplock_request, &all_current_opens_are_level_II);
		if(num_share_modes == -1) {

			/*
			 * This next line is a subtlety we need for MS-Access. If a file open will
			 * fail due to share permissions and also for security (access)
			 * reasons, we need to return the access failed error, not the
			 * share error. This means we must attempt to open the file anyway
			 * in order to get the UNIX access error - even if we're going to
			 * fail the open for share reasons. This is bad, as we're burning
			 * another fd if there are existing locks but there's nothing else
			 * we can do. We also ensure we're not going to create or tuncate
			 * the file as we only want an access decision at this stage. JRA.
			 */
			fsp_open = open_file(fsp,conn,fname,psbuf,flags|(flags2&~(O_TRUNC|O_CREAT)),mode);

			DEBUG(4,("open_file_shared : share_mode deny - calling open_file with \
flags=0x%X flags2=0x%X mode=0%o returned %d\n",
				flags,(flags2&~(O_TRUNC|O_CREAT)),(int)mode,(int)fsp_open ));

			unlock_share_entry(conn, dev, inode);
			if (fsp_open)
				fd_close(conn, fsp);
			file_free(fsp);
			return NULL;
		}

		/*
		 * We exit this block with the share entry *locked*.....
		 */
	}

	DEBUG(4,("calling open_file with flags=0x%X flags2=0x%X mode=0%o\n",
			flags,flags2,(int)mode));

	/*
	 * open_file strips any O_TRUNC flags itself.
	 */

	fsp_open = open_file(fsp,conn,fname,psbuf,flags|flags2,mode);

	if (!fsp_open && (flags == O_RDWR) && (errno != ENOENT) && fcbopen) {
		if((fsp_open = open_file(fsp,conn,fname,psbuf,O_RDONLY,mode)) == True)
			flags = O_RDONLY;
	}

	if (!fsp_open) {
		if(file_existed)
			unlock_share_entry(conn, dev, inode);
		file_free(fsp);
		return NULL;
	}

	/*
	 * Deal with the race condition where two smbd's detect the file doesn't
	 * exist and do the create at the same time. One of them will win and
	 * set a share mode, the other (ie. this one) should check if the
	 * requested share mode for this create is allowed.
	 */

	if (!file_existed) { 

		lock_share_entry_fsp(fsp);

		num_share_modes = open_mode_check(conn, fname, dev, inode, share_mode,
								&flags, &oplock_request, &all_current_opens_are_level_II);

		if(num_share_modes == -1) {
			unlock_share_entry_fsp(fsp);
			fd_close(conn,fsp);
			file_free(fsp);
			return NULL;
		}

		/*
		 * If there are any share modes set then the file *did*
		 * exist. Ensure we return the correct value for action.
		 */

		if (num_share_modes > 0)
			file_existed = True;

		/*
		 * We exit this block with the share entry *locked*.....
		 */
	}

	/* note that we ignore failure for the following. It is
           basically a hack for NFS, and NFS will never set one of
           these only read them. Nobody but Samba can ever set a deny
           mode and we have already checked our more authoritative
           locking database for permission to set this deny mode. If
           the kernel refuses the operations then the kernel is wrong */
	kernel_flock(fsp, deny_mode);

	/*
	 * At this point onwards, we can guarentee that the share entry
	 * is locked, whether we created the file or not, and that the
	 * deny mode is compatible with all current opens.
	 */

	/*
	 * If requested, truncate the file.
	 */

	if (flags2&O_TRUNC) {
		/*
		 * We are modifing the file after open - update the stat struct..
		 */
		if ((truncate_unless_locked(conn,fsp) == -1) || (vfs_fstat(fsp,fsp->fd,psbuf)==-1)) {
			unlock_share_entry_fsp(fsp);
			fd_close(conn,fsp);
			file_free(fsp);
			return NULL;
		}
	}

	switch (flags) {
		case O_RDONLY:
			open_mode = DOS_OPEN_RDONLY;
			break;
		case O_RDWR:
			open_mode = DOS_OPEN_RDWR;
			break;
		case O_WRONLY:
			open_mode = DOS_OPEN_WRONLY;
			break;
	}

	fsp->share_mode = SET_DENY_MODE(deny_mode) | 
						SET_OPEN_MODE(open_mode) | 
						SET_ALLOW_SHARE_DELETE(allow_share_delete) |
						SET_DELETE_ACCESS_REQUESTED(delete_access_requested);

	DEBUG(10,("open_file_shared : share_mode = %x\n", fsp->share_mode ));

	if (Access)
		(*Access) = open_mode;

	if (action) {
		if (file_existed && !(flags2 & O_TRUNC))
			*action = FILE_WAS_OPENED;
		if (!file_existed)
			*action = FILE_WAS_CREATED;
		if (file_existed && (flags2 & O_TRUNC))
			*action = FILE_WAS_OVERWRITTEN;
	}

	/* 
	 * Setup the oplock info in both the shared memory and
	 * file structs.
	 */

	if(oplock_request && (num_share_modes == 0) && 
			!IS_VETO_OPLOCK_PATH(conn,fname) && set_file_oplock(fsp, oplock_request) ) {
		port = global_oplock_port;
	} else if (oplock_request && all_current_opens_are_level_II) {
		port = global_oplock_port;
		oplock_request = LEVEL_II_OPLOCK;
		set_file_oplock(fsp, oplock_request);
	} else {
		port = 0;
		oplock_request = 0;
	}

	set_share_mode(fsp, port, oplock_request);

	if (delete_on_close) {
		NTSTATUS result = set_delete_on_close_internal(fsp, delete_on_close);

		if (NT_STATUS_V(result) !=  NT_STATUS_V(NT_STATUS_OK)) {
			unlock_share_entry_fsp(fsp);
			fd_close(conn,fsp);
			file_free(fsp);
			return NULL;
		}
	}
	
	/*
	 * Take care of inherited ACLs on created files. JRA.
	 */

	if (!file_existed && (conn->vfs_ops.fchmod_acl != NULL)) {
		int saved_errno = errno; /* We might get ENOSYS in the next call.. */
		if (conn->vfs_ops.fchmod_acl(fsp, fsp->fd, mode) == -1 && errno == ENOSYS)
			errno = saved_errno; /* Ignore ENOSYS */
	}
		
	unlock_share_entry_fsp(fsp);

	conn->num_files_open++;

	return fsp;
}

/****************************************************************************
 Open a file for permissions read only. Return a pseudo file entry
 with the 'stat_open' flag set 
****************************************************************************/

files_struct *open_file_stat(connection_struct *conn, char *fname,
							SMB_STRUCT_STAT *psbuf, int smb_ofun, int *action)
{
	extern struct current_user current_user;
	files_struct *fsp = NULL;

	if (!VALID_STAT(*psbuf)) {
		DEBUG(0,("open_file_stat: unable to stat name = %s. Error was %s\n", fname, strerror(errno) ));
		return NULL;
	}

	if(S_ISDIR(psbuf->st_mode)) {
		DEBUG(0,("open_file_stat: %s is a directory !\n", fname ));
		return NULL;
	}

	fsp = file_new(conn);
	if(!fsp)
		return NULL;

	*action = FILE_WAS_OPENED;
	
	DEBUG(5,("open_file_stat: opening file %s as a stat entry\n", fname));

	/*
	 * Setup the files_struct for it.
	 */
	
	fsp->mode = psbuf->st_mode;
	fsp->inode = psbuf->st_ino;
	fsp->dev = psbuf->st_dev;
	fsp->size = psbuf->st_size;
	fsp->vuid = current_user.vuid;
	fsp->pos = -1;
	fsp->can_lock = False;
	fsp->can_read = False;
	fsp->can_write = False;
	fsp->share_mode = 0;
	fsp->print_file = False;
	fsp->modified = False;
	fsp->oplock_type = NO_OPLOCK;
	fsp->sent_oplock_break = NO_BREAK_SENT;
	fsp->is_directory = False;
	fsp->stat_open = True;
	fsp->directory_delete_on_close = False;
	fsp->conn = conn;
	/*
	 * Note that the file name here is the *untranslated* name
	 * ie. it is still in the DOS codepage sent from the client.
	 * All use of this filename will pass though the sys_xxxx
	 * functions which will do the dos_to_unix translation before
	 * mapping into a UNIX filename. JRA.
	 */
	string_set(&fsp->fsp_name,fname);
	fsp->wbmpx_ptr = NULL;
	fsp->wcp = NULL; /* Write cache pointer. */

	conn->num_files_open++;

	return fsp;
}

/****************************************************************************
 Open a file for for write to ensure that we can fchmod it.
****************************************************************************/

files_struct *open_file_fchmod(connection_struct *conn, char *fname, SMB_STRUCT_STAT *psbuf)
{
	files_struct *fsp = NULL;
	BOOL fsp_open;

	if (!VALID_STAT(*psbuf))
		return NULL;

	fsp = file_new(conn);
	if(!fsp)
		return NULL;

	fsp_open = open_file(fsp,conn,fname,psbuf,O_WRONLY,0);

	/* 
	 * This is not a user visible file open.
	 * Don't set a share mode and don't increment
	 * the conn->num_files_open.
	 */

	if (!fsp_open) {
		file_free(fsp);
		return NULL;
	}

	return fsp;
}

/****************************************************************************
 Close the fchmod file fd - ensure no locks are lost.
****************************************************************************/

int close_file_fchmod(files_struct *fsp)
{
	int ret = fd_close(fsp->conn, fsp);
	file_free(fsp);
	return ret;
}

/****************************************************************************
 Open a directory from an NT SMB call.
****************************************************************************/

files_struct *open_directory(connection_struct *conn, char *fname,
		SMB_STRUCT_STAT *psbuf, int share_mode, int smb_ofun, mode_t unixmode, int *action)
{
	extern struct current_user current_user;
	BOOL got_stat = False;
	files_struct *fsp = file_new(conn);
	BOOL delete_on_close = GET_DELETE_ON_CLOSE_FLAG(share_mode);

	if(!fsp)
		return NULL;

	fsp->conn = conn; /* The vfs_fXXX() macros need this. */

	if (VALID_STAT(*psbuf))
		got_stat = True;

	if (got_stat && (GET_FILE_OPEN_DISPOSITION(smb_ofun) == FILE_EXISTS_FAIL)) {
		file_free(fsp);
		errno = EEXIST; /* Setup so correct error is returned to client. */
		return NULL;
	}

	if (GET_FILE_CREATE_DISPOSITION(smb_ofun) == FILE_CREATE_IF_NOT_EXIST) {

		if (got_stat) {

			if(!S_ISDIR(psbuf->st_mode)) {
				DEBUG(0,("open_directory: %s is not a directory !\n", fname ));
				file_free(fsp);
				errno = EACCES;
				return NULL;
			}
			*action = FILE_WAS_OPENED;

		} else {

			/*
			 * Try and create the directory.
			 */

			if(!CAN_WRITE(conn)) {
				DEBUG(2,("open_directory: failing create on read-only share\n"));
				file_free(fsp);
				errno = EACCES;
				return NULL;
			}

			if(vfs_mkdir(conn,fname, unix_mode(conn,aDIR, fname)) < 0) {
				DEBUG(2,("open_directory: unable to create %s. Error was %s\n",
					 fname, strerror(errno) ));
				file_free(fsp);
				return NULL;
			}

			if(vfs_stat(conn,fname, psbuf) != 0) {
				file_free(fsp);
				return NULL;
			}

			*action = FILE_WAS_CREATED;

		}
	} else {

		/*
		 * Don't create - just check that it *was* a directory.
		 */

		if(!got_stat) {
			DEBUG(0,("open_directory: unable to stat name = %s. Error was %s\n",
				 fname, strerror(errno) ));
			file_free(fsp);
			return NULL;
		}

		if(!S_ISDIR(psbuf->st_mode)) {
			DEBUG(0,("open_directory: %s is not a directory !\n", fname ));
			file_free(fsp);
			return NULL;
		}

		*action = FILE_WAS_OPENED;
	}
	
	DEBUG(5,("open_directory: opening directory %s\n", fname));

	/*
	 * Setup the files_struct for it.
	 */
	
	fsp->mode = psbuf->st_mode;
	fsp->inode = psbuf->st_ino;
	fsp->dev = psbuf->st_dev;
	fsp->size = psbuf->st_size;
	fsp->vuid = current_user.vuid;
	fsp->pos = -1;
	fsp->can_lock = True;
	fsp->can_read = False;
	fsp->can_write = False;
	fsp->share_mode = share_mode;
	fsp->print_file = False;
	fsp->modified = False;
	fsp->oplock_type = NO_OPLOCK;
	fsp->sent_oplock_break = NO_BREAK_SENT;
	fsp->is_directory = True;
	fsp->directory_delete_on_close = False;
	fsp->conn = conn;

	if (delete_on_close) {
		NTSTATUS result = set_delete_on_close_internal(fsp, delete_on_close);

		if (NT_STATUS_V(result) !=  NT_STATUS_V(NT_STATUS_OK)) {
			file_free(fsp);
			return NULL;
		}
	}
	
	/*
	 * Note that the file name here is the *untranslated* name
	 * ie. it is still in the DOS codepage sent from the client.
	 * All use of this filename will pass though the sys_xxxx
	 * functions which will do the dos_to_unix translation before
	 * mapping into a UNIX filename. JRA.
	 */
	string_set(&fsp->fsp_name,fname);
	fsp->wbmpx_ptr = NULL;

	conn->num_files_open++;

	return fsp;
}

/*******************************************************************
 Check if the share mode on a file allows it to be deleted or unlinked.
 Return True if sharing doesn't prevent the operation.
********************************************************************/

BOOL check_file_sharing(connection_struct *conn,char *fname, BOOL rename_op)
{
  int i;
  int ret = False;
  share_mode_entry *old_shares = 0;
  int num_share_modes;
  SMB_STRUCT_STAT sbuf;
  pid_t pid = sys_getpid();
  SMB_DEV_T dev;
  SMB_INO_T inode;

  if (vfs_stat(conn,fname,&sbuf) == -1)
    return(True);

  dev = sbuf.st_dev;
  inode = sbuf.st_ino;

  lock_share_entry(conn, dev, inode);
  num_share_modes = get_share_modes(conn, dev, inode, &old_shares);

  /*
   * Check if the share modes will give us access.
   */

  if(num_share_modes != 0)
  {
    BOOL broke_oplock;

    do
    {

      broke_oplock = False;
      for(i = 0; i < num_share_modes; i++)
      {
        share_mode_entry *share_entry = &old_shares[i];

        /* 
         * Break oplocks before checking share modes. See comment in
         * open_file_shared for details. 
         * Check if someone has an oplock on this file. If so we must 
         * break it before continuing. 
         */
        if(BATCH_OPLOCK_TYPE(share_entry->op_type))
        {

#if 0

/* JRA. Try removing this code to see if the new oplock changes
   fix the problem. I'm dubious, but Andrew is recommending we
   try this....
*/

          /*
           * It appears that the NT redirector may have a bug, in that
           * it tries to do an SMBmv on a file that it has open with a
           * batch oplock, and then fails to respond to the oplock break
           * request. This only seems to occur when the client is doing an
           * SMBmv to the smbd it is using - thus we try and detect this
           * condition by checking if the file being moved is open and oplocked by
           * this smbd process, and then not sending the oplock break in this
           * special case. If the file was open with a deny mode that 
           * prevents the move the SMBmv will fail anyway with a share
           * violation error. JRA.
           */
          if(rename_op && (share_entry->pid == pid))
          {

            DEBUG(0,("check_file_sharing: NT redirector workaround - rename attempted on \
batch oplocked file %s, dev = %x, inode = %.0f\n", fname, (unsigned int)dev, (double)inode));

            /* 
             * This next line is a test that allows the deny-mode
             * processing to be skipped. This seems to be needed as
             * NT insists on the rename succeeding (in Office 9x no less !).
             * This should be removed as soon as (a) MS fix the redirector
             * bug or (b) NT SMB support in Samba makes NT not issue the
             * call (as is my fervent hope). JRA.
             */ 
            continue;
          }
          else
#endif /* 0 */
          {

            DEBUG(5,("check_file_sharing: breaking oplock (%x) on file %s, \
dev = %x, inode = %.0f\n", share_entry->op_type, fname, (unsigned int)dev, (double)inode));

            /* Oplock break.... */
            unlock_share_entry(conn, dev, inode);
            if(request_oplock_break(share_entry) == False)
            {
              DEBUG(0,("check_file_sharing: FAILED when breaking oplock (%x) on file %s, \
dev = %x, inode = %.0f\n", old_shares[i].op_type, fname, (unsigned int)dev, (double)inode));

              SAFE_FREE(old_shares);
              return False;
            }
            lock_share_entry(conn, dev, inode);
            broke_oplock = True;
            break;
          }
        }

        /* 
         * If this is a delete request and ALLOW_SHARE_DELETE is set then allow 
         * this to proceed. This takes precedence over share modes.
         */

        if(!rename_op && GET_ALLOW_SHARE_DELETE(share_entry->share_mode))
          continue;

        /* 
         * Someone else has a share lock on it, check to see 
         * if we can too.
         */

        if ((GET_DENY_MODE(share_entry->share_mode) != DENY_DOS) || 
	    (share_entry->pid != pid))
          goto free_and_exit;

      } /* end for */

      if(broke_oplock)
      {
        SAFE_FREE(old_shares);
        num_share_modes = get_share_modes(conn, dev, inode, &old_shares);
      }
    } while(broke_oplock);
  }

  /* XXXX exactly what share mode combinations should be allowed for
     deleting/renaming? */
  /* 
   * If we got here then either there were no share modes or
   * all share modes were DENY_DOS and the pid == getpid() or
   * delete access was requested and all share modes had the
   * ALLOW_SHARE_DELETE bit set (takes precedence over other
   * share modes).
   */

  ret = True;

free_and_exit:

  unlock_share_entry(conn, dev, inode);
  SAFE_FREE(old_shares);
  return(ret);
}