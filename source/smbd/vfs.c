/*
   Unix SMB/Netbios implementation.
   Version 1.9.
   VFS initialisation and support functions
   Copyright (C) Tim Potter 1999

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

/* Some structures to help us initialise the vfs operations table */

struct vfs_syminfo {
	char *name;
	void *fptr;
};

/* Default vfs hooks.  WARNING: The order of these initialisers is
   very important.  They must be in the same order as defined in
   vfs.h.  Change at your own peril. */

struct vfs_ops default_vfs_ops = {

	/* Disk operations */

	vfswrap_dummy_connect,
	vfswrap_dummy_disconnect,
	vfswrap_disk_free,

	/* Directory operations */

	vfswrap_opendir,
	vfswrap_readdir,
	vfswrap_mkdir,
	vfswrap_rmdir,
	vfswrap_closedir,

	/* File operations */

	vfswrap_open,
	vfswrap_close,
	vfswrap_read,
	vfswrap_write,
	vfswrap_lseek,
	vfswrap_rename,
	vfswrap_fsync,
	vfswrap_stat,
	vfswrap_fstat,
	vfswrap_lstat,
	vfswrap_unlink,
	vfswrap_chmod,
	vfswrap_fchmod,
	vfswrap_chown,
	vfswrap_fchown,
	vfswrap_chdir,
	vfswrap_getwd,
	vfswrap_utime,
	vfswrap_ftruncate,
	vfswrap_lock,
	vfswrap_symlink,
	vfswrap_readlink,
	vfswrap_link,
	vfswrap_mknod,
	vfswrap_realpath,

	vfswrap_fget_nt_acl,
	vfswrap_get_nt_acl,
	vfswrap_fset_nt_acl,
	vfswrap_set_nt_acl,

#if defined(HAVE_NO_ACLS)
	NULL,
	NULL
#else
	vfswrap_chmod_acl,
	vfswrap_fchmod_acl
#endif
};

/****************************************************************************
  initialise default vfs hooks
****************************************************************************/

static BOOL vfs_init_default(connection_struct *conn)
{
    DEBUG(3, ("Initialising default vfs hooks\n"));

    memcpy(&conn->vfs_ops, &default_vfs_ops, sizeof(struct vfs_ops));
    return True;
}

/****************************************************************************
  initialise custom vfs hooks
****************************************************************************/

#ifdef HAVE_LIBDL
static BOOL vfs_init_custom(connection_struct *conn)
{
	int vfs_version = -1;
    struct vfs_ops *ops, *(*init_fptr)(int *);

    DEBUG(3, ("Initialising custom vfs hooks from %s\n",
	      lp_vfsobj(SNUM(conn))));

    /* Open object file */

    if ((conn->dl_handle = sys_dlopen(lp_vfsobj(SNUM(conn)), RTLD_NOW | RTLD_GLOBAL)) == NULL) {
		DEBUG(0, ("Error opening %s: %s\n", lp_vfsobj(SNUM(conn)), dlerror()));
		return False;
    }

    /* Get handle on vfs_init() symbol */

    init_fptr = (struct vfs_ops *(*)(int *))sys_dlsym(conn->dl_handle, "vfs_init");

    if (init_fptr == NULL) {
		DEBUG(0, ("No vfs_init() symbol found in %s\n",
		  lp_vfsobj(SNUM(conn))));
		return False;
    }

    /* Initialise vfs_ops structure */

    if ((ops = init_fptr(&vfs_version)) == NULL) {
        DEBUG(0, ("vfs_init function from %s failed\n", lp_vfsobj(SNUM(conn))));
		return False;
    }

	if (vfs_version != SMB_VFS_INTERFACE_VERSION) {
		DEBUG(0, ("vfs_init returned wrong interface version info (was %d, should be %d)\n",
			vfs_version, SMB_VFS_INTERFACE_VERSION ));
		return False;
	}

    /* Fill in unused operations with default (disk based) ones.
       There's probably a neater way to do this then a whole bunch of
       if statements. */

    memcpy(&conn->vfs_ops, ops, sizeof(struct vfs_ops));

    if (conn->vfs_ops.connect == NULL)
		conn->vfs_ops.connect = default_vfs_ops.connect;

    if (conn->vfs_ops.disconnect == NULL)
		conn->vfs_ops.disconnect = default_vfs_ops.disconnect;

    if (conn->vfs_ops.disk_free == NULL)
		conn->vfs_ops.disk_free = default_vfs_ops.disk_free;

    if (conn->vfs_ops.opendir == NULL)
		conn->vfs_ops.opendir = default_vfs_ops.opendir;

    if (conn->vfs_ops.readdir == NULL)
		conn->vfs_ops.readdir = default_vfs_ops.readdir;

    if (conn->vfs_ops.mkdir == NULL)
		conn->vfs_ops.mkdir = default_vfs_ops.mkdir;

    if (conn->vfs_ops.rmdir == NULL)
		conn->vfs_ops.rmdir = default_vfs_ops.rmdir;

    if (conn->vfs_ops.closedir == NULL)
		conn->vfs_ops.closedir = default_vfs_ops.closedir;

    if (conn->vfs_ops.open == NULL)
		conn->vfs_ops.open = default_vfs_ops.open;

    if (conn->vfs_ops.close == NULL)
		conn->vfs_ops.close = default_vfs_ops.close;

    if (conn->vfs_ops.read == NULL)
		conn->vfs_ops.read = default_vfs_ops.read;

    if (conn->vfs_ops.write == NULL)
		conn->vfs_ops.write = default_vfs_ops.write;

    if (conn->vfs_ops.lseek == NULL)
		conn->vfs_ops.lseek = default_vfs_ops.lseek;

    if (conn->vfs_ops.rename == NULL)
		conn->vfs_ops.rename = default_vfs_ops.rename;

    if (conn->vfs_ops.fsync == NULL)
		conn->vfs_ops.fsync = default_vfs_ops.fsync;

    if (conn->vfs_ops.stat == NULL)
		conn->vfs_ops.stat = default_vfs_ops.stat;

    if (conn->vfs_ops.fstat == NULL)
		conn->vfs_ops.fstat = default_vfs_ops.fstat;

    if (conn->vfs_ops.lstat == NULL)
		conn->vfs_ops.lstat = default_vfs_ops.lstat;

    if (conn->vfs_ops.unlink == NULL)
		conn->vfs_ops.unlink = default_vfs_ops.unlink;

    if (conn->vfs_ops.chmod == NULL)
		conn->vfs_ops.chmod = default_vfs_ops.chmod;

    if (conn->vfs_ops.fchmod == NULL)
		conn->vfs_ops.fchmod = default_vfs_ops.fchmod;

    if (conn->vfs_ops.chown == NULL)
		conn->vfs_ops.chown = default_vfs_ops.chown;

    if (conn->vfs_ops.fchown == NULL)
		conn->vfs_ops.fchown = default_vfs_ops.fchown;

    if (conn->vfs_ops.chdir == NULL)
		conn->vfs_ops.chdir = default_vfs_ops.chdir;

    if (conn->vfs_ops.getwd == NULL)
		conn->vfs_ops.getwd = default_vfs_ops.getwd;

    if (conn->vfs_ops.utime == NULL)
		conn->vfs_ops.utime = default_vfs_ops.utime;

    if (conn->vfs_ops.ftruncate == NULL)
		conn->vfs_ops.ftruncate = default_vfs_ops.ftruncate;

    if (conn->vfs_ops.lock == NULL)
		conn->vfs_ops.lock = default_vfs_ops.lock;

    if (conn->vfs_ops.symlink == NULL)
		conn->vfs_ops.symlink = default_vfs_ops.symlink;

    if (conn->vfs_ops.readlink == NULL)
		conn->vfs_ops.readlink = default_vfs_ops.readlink;

    if (conn->vfs_ops.link == NULL)
		conn->vfs_ops.link = default_vfs_ops.link;

    if (conn->vfs_ops.mknod == NULL)
		conn->vfs_ops.mknod = default_vfs_ops.mknod;

    if (conn->vfs_ops.fget_nt_acl == NULL)
		conn->vfs_ops.fget_nt_acl = default_vfs_ops.fget_nt_acl;

    if (conn->vfs_ops.get_nt_acl == NULL)
		conn->vfs_ops.get_nt_acl = default_vfs_ops.get_nt_acl;

    if (conn->vfs_ops.fset_nt_acl == NULL)
		conn->vfs_ops.fset_nt_acl = default_vfs_ops.fset_nt_acl;

    if (conn->vfs_ops.set_nt_acl == NULL)
		conn->vfs_ops.set_nt_acl = default_vfs_ops.set_nt_acl;

    if (conn->vfs_ops.chmod_acl == NULL)
		conn->vfs_ops.chmod_acl = default_vfs_ops.chmod_acl;

    if (conn->vfs_ops.fchmod_acl == NULL)
		conn->vfs_ops.fchmod_acl = default_vfs_ops.fchmod_acl;

    return True;
}
#endif

/*****************************************************************
 Generic VFS init.
******************************************************************/

BOOL vfs_init(connection_struct *conn)
{
	if (*lp_vfsobj(SNUM(conn))) {
#ifdef HAVE_LIBDL
 
		/* Loadable object file */
 
		if (!vfs_init_custom(conn)) {
			DEBUG(0, ("vfs_init: vfs_init_custom failed\n"));
			return False;
		}

		return True;
#else
		DEBUG(0, ("vfs_init: No libdl present - cannot use VFS objects\n"));
		return False;
#endif
	}
 
	/* Normal share - initialise with disk access functions */
 
	return vfs_init_default(conn);
}

/*******************************************************************
 Check if directory exists.
********************************************************************/

BOOL vfs_directory_exist(connection_struct *conn, char *dname, SMB_STRUCT_STAT *st)
{
	SMB_STRUCT_STAT st2;
	BOOL ret;

	if (!st)
		st = &st2;

	if (vfs_stat(conn,dname,st) != 0)
		return(False);

	ret = S_ISDIR(st->st_mode);
	if(!ret)
		errno = ENOTDIR;

  return ret;
}

/*******************************************************************
 vfs mkdir wrapper that calls dos_to_unix.
********************************************************************/

int vfs_mkdir(connection_struct *conn, char *fname, mode_t mode)
{
	int ret;
	pstring name;
	SMB_STRUCT_STAT sbuf;

	pstrcpy(name,dos_to_unix(fname,False)); /* paranoia copy */
	if(!(ret=conn->vfs_ops.mkdir(conn,name,mode))) {
		/*
		 * Check if high bits should have been set,
		 * then (if bits are missing): add them.
		 * Consider bits automagically set by UNIX, i.e. SGID bit from parent dir.
		 */
		if(mode & ~(S_IRWXU|S_IRWXG|S_IRWXO) &&
				!vfs_stat(conn,name,&sbuf) && (mode & ~sbuf.st_mode))
			vfs_chmod(conn,name,sbuf.st_mode | (mode & ~sbuf.st_mode));
	}
	return ret;
}

/*******************************************************************
 vfs getwd wrapper that calls dos_to_unix.
********************************************************************/

char *vfs_getwd(connection_struct *conn, char *unix_path)
{
    char *wd;
    wd = conn->vfs_ops.getwd(conn,unix_path);
    if (wd)
        unix_to_dos(wd, True);
    return wd;
}

/*******************************************************************
 Check if an object exists in the vfs.
********************************************************************/

BOOL vfs_object_exist(connection_struct *conn,char *fname,SMB_STRUCT_STAT *sbuf)
{
	SMB_STRUCT_STAT st;

	if (!sbuf)
		sbuf = &st;

	ZERO_STRUCTP(sbuf);

	if (vfs_stat(conn,fname,sbuf) == -1)
		return(False);
	return True;
}

/*******************************************************************
 Check if a file exists in the vfs.
********************************************************************/

BOOL vfs_file_exist(connection_struct *conn,char *fname,SMB_STRUCT_STAT *sbuf)
{
	SMB_STRUCT_STAT st;

	if (!sbuf)
		sbuf = &st;

	ZERO_STRUCTP(sbuf);

	if (vfs_stat(conn,fname,sbuf) == -1)
		return False;
	return(S_ISREG(sbuf->st_mode));
}

/****************************************************************************
 Read data from fsp on the vfs. (note: EINTR re-read differs from vfs_write_data)
****************************************************************************/

ssize_t vfs_read_data(files_struct *fsp, char *buf, size_t byte_count)
{
	size_t total=0;

	while (total < byte_count)
	{
		ssize_t ret = fsp->conn->vfs_ops.read(fsp, fsp->fd, buf + total,
											  byte_count - total);

		if (ret == 0) return total;
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			else
				return -1;
		}
		total += ret;
	}
	return (ssize_t)total;
}

/****************************************************************************
 Write data to a fd on the vfs.
****************************************************************************/

ssize_t vfs_write_data(files_struct *fsp,char *buffer,size_t N)
{
	size_t total=0;
	ssize_t ret;

	while (total < N) {
		ret = fsp->conn->vfs_ops.write(fsp,fsp->fd,buffer + total,N - total);

		if (ret == -1)
			return -1;
		if (ret == 0)
			return total;

		total += ret;
	}

	return (ssize_t)total;
}

/****************************************************************************
 An allocate file space call using the vfs interface.
 Allocates space for a file from a filedescriptor.
 Returns 0 on success, -1 on failure.
****************************************************************************/

int vfs_allocate_file_space(files_struct *fsp, SMB_OFF_T len)
{
	int ret;
	SMB_STRUCT_STAT st;
	connection_struct *conn = fsp->conn;
	struct vfs_ops *vfs_ops = &conn->vfs_ops;
	SMB_OFF_T space_avail;
	SMB_BIG_UINT bsize,dfree,dsize;

	release_level_2_oplocks_on_change(fsp);

	/*
	 * Actually try and commit the space on disk....
	 */

	DEBUG(10,("vfs_allocate_file_space: file %s, len %.0f\n", fsp->fsp_name, (double)len ));

	ret = vfs_fstat(fsp,fsp->fd,&st);
	if (ret == -1)
		return ret;

	if (len == st.st_size)
		return 0;

	if (len < st.st_size) {
		/* Shrink - use ftruncate. */

		DEBUG(10,("vfs_allocate_file_space: file %s, shrink. Current size %.0f\n",
				fsp->fsp_name, (double)st.st_size ));

		flush_write_cache(fsp, SIZECHANGE_FLUSH);
		if ((ret = vfs_ops->ftruncate(fsp, fsp->fd, len)) != -1) {
			set_filelen_write_cache(fsp, len);
		}
		return ret;
	}

	/* Grow - we need to test if we have enough space. */

	if (!lp_strict_allocate(SNUM(fsp->conn)))
		return 0;

	len -= st.st_size;
	len /= 1024; /* Len is now number of 1k blocks needed. */
	space_avail = (SMB_OFF_T)conn->vfs_ops.disk_free(conn,fsp->fsp_name,False,&bsize,&dfree,&dsize);

	DEBUG(10,("vfs_allocate_file_space: file %s, grow. Current size %.0f, needed blocks = %lu, space avail = %lu\n",
			fsp->fsp_name, (double)st.st_size, (unsigned long)len, (unsigned long)space_avail ));

	if (len > space_avail) {
		errno = ENOSPC;
		return -1;
	}

	return 0;
}

/****************************************************************************
 A vfs set_filelen call.
 set the length of a file from a filedescriptor.
 Returns 0 on success, -1 on failure.
****************************************************************************/

int vfs_set_filelen(files_struct *fsp, SMB_OFF_T len)
{
	int ret;

	release_level_2_oplocks_on_change(fsp);
	DEBUG(10,("vfs_set_filelen: ftruncate %s to len %.0f\n", fsp->fsp_name, (double)len));
	flush_write_cache(fsp, SIZECHANGE_FLUSH);
	if ((ret = fsp->conn->vfs_ops.ftruncate(fsp, fsp->fd, len)) != -1)
		set_filelen_write_cache(fsp, len);

	return ret;
}

/****************************************************************************
 Transfer some data (n bytes) between two file_struct's.
****************************************************************************/

static files_struct *in_fsp;
static files_struct *out_fsp;

static ssize_t read_fn(int fd, void *buf, size_t len)
{
	return in_fsp->conn->vfs_ops.read(in_fsp, fd, buf, len);
}

static ssize_t write_fn(int fd, const void *buf, size_t len)
{
	return out_fsp->conn->vfs_ops.write(out_fsp, fd, buf, len);
}

SMB_OFF_T vfs_transfer_file(files_struct *in, files_struct *out, SMB_OFF_T n)
{
	in_fsp = in;
	out_fsp = out;

	return transfer_file_internal(in_fsp->fd, out_fsp->fd, n, read_fn, write_fn);
}

/*******************************************************************
 A vfs_readdir wrapper which just returns the file name.
********************************************************************/

char *vfs_readdirname(connection_struct *conn, void *p)
{
	struct dirent *ptr;
	char *dname;

	if (!p)
		return(NULL);

	ptr = (struct dirent *)conn->vfs_ops.readdir(conn,p);
	if (!ptr)
		return(NULL);

	dname = ptr->d_name;

#ifdef NEXT2
	if (telldir(p) < 0)
		return(NULL);
#endif

#ifdef HAVE_BROKEN_READDIR
	/* using /usr/ucb/cc is BAD */
	dname = dname - 2;
#endif

	{
		static pstring buf;
		memcpy(buf, dname, NAMLEN(ptr)+1);
		unix_to_dos(buf, True);
		dname = buf;
	}

	return(dname);
}

/* VFS options not quite working yet */

#if 0

/***************************************************************************
  handle the interpretation of the vfs option parameter
 *************************************************************************/
static BOOL handle_vfs_option(char *pszParmValue, char **ptr)
{
    struct vfs_options *new_option, **options = (struct vfs_options **)ptr;
    int i;

    /* Create new vfs option */

    new_option = (struct vfs_options *)malloc(sizeof(*new_option));
    if (new_option == NULL) {
	return False;
    }

    ZERO_STRUCTP(new_option);

    /* Get name and value */

    new_option->name = strtok(pszParmValue, "=");

    if (new_option->name == NULL) {
	return False;
    }

    while(isspace(*new_option->name)) {
	new_option->name++;
    }

    for (i = strlen(new_option->name); i > 0; i--) {
	if (!isspace(new_option->name[i - 1])) break;
    }

    new_option->name[i] = '\0';
    new_option->name = strdup(new_option->name);

    new_option->value = strtok(NULL, "=");

    if (new_option->value != NULL) {

	while(isspace(*new_option->value)) {
	    new_option->value++;
	}
	
	for (i = strlen(new_option->value); i > 0; i--) {
	    if (!isspace(new_option->value[i - 1])) break;
	}
	
	new_option->value[i] = '\0';
	new_option->value = strdup(new_option->value);
    }

    /* Add to list */

    DLIST_ADD(*options, new_option);

    return True;
}

#endif


/*******************************************************************
 A wrapper for vfs_chdir().
********************************************************************/

int vfs_ChDir(connection_struct *conn, char *path)
{
	int res;
	static pstring LastDir="";

	if (strcsequal(path,"."))
		return(0);

	if (*path == '/' && strcsequal(LastDir,path))
		return(0);

	DEBUG(3,("vfs_ChDir to %s\n",path));

	res = vfs_chdir(conn,path);
	if (!res)
		pstrcpy(LastDir,path);
	return(res);
}

/* number of list structures for a caching GetWd function. */
#define MAX_GETWDCACHE (50)

struct
{
  SMB_DEV_T dev; /* These *must* be compatible with the types returned in a stat() call. */
  SMB_INO_T inode; /* These *must* be compatible with the types returned in a stat() call. */
  char *dos_path; /* The pathname in DOS format. */
  BOOL valid;
} ino_list[MAX_GETWDCACHE];

extern BOOL use_getwd_cache;

/****************************************************************************
 Prompte a ptr (to make it recently used)
****************************************************************************/

static void array_promote(char *array,int elsize,int element)
{
	char *p;
	if (element == 0)
		return;

	p = (char *)malloc(elsize);

	if (!p) {
		DEBUG(5,("array_promote: malloc fail\n"));
		return;
	}

	memcpy(p,array + element * elsize, elsize);
	memmove(array + elsize,array,elsize*element);
	memcpy(array,p,elsize);
	SAFE_FREE(p);
}

/*******************************************************************
 Return the absolute current directory path - given a UNIX pathname.
 Note that this path is returned in DOS format, not UNIX
 format. Note this can be called with conn == NULL.
********************************************************************/

char *vfs_GetWd(connection_struct *conn, char *path)
{
  pstring s;
  static BOOL getwd_cache_init = False;
  SMB_STRUCT_STAT st, st2;
  int i;

  *s = 0;

  if (!use_getwd_cache)
    return(vfs_getwd(conn,path));

  /* init the cache */
  if (!getwd_cache_init)
  {
    getwd_cache_init = True;
    for (i=0;i<MAX_GETWDCACHE;i++)
    {
      string_set(&ino_list[i].dos_path,"");
      ino_list[i].valid = False;
    }
  }

  /*  Get the inode of the current directory, if this doesn't work we're
      in trouble :-) */

  if (vfs_stat(conn, ".",&st) == -1)
  {
    DEBUG(0,("Very strange, couldn't stat \".\" path=%s\n", path));
    return(vfs_getwd(conn,path));
  }


  for (i=0; i<MAX_GETWDCACHE; i++)
    if (ino_list[i].valid)
    {

      /*  If we have found an entry with a matching inode and dev number
          then find the inode number for the directory in the cached string.
          If this agrees with that returned by the stat for the current
          directory then all is o.k. (but make sure it is a directory all
          the same...) */

      if (st.st_ino == ino_list[i].inode &&
          st.st_dev == ino_list[i].dev)
      {
        if (vfs_stat(conn,ino_list[i].dos_path,&st2) == 0)
        {
          if (st.st_ino == st2.st_ino &&
              st.st_dev == st2.st_dev &&
              (st2.st_mode & S_IFMT) == S_IFDIR)
          {
            pstrcpy (path, ino_list[i].dos_path);

            /* promote it for future use */
            array_promote((char *)&ino_list[0],sizeof(ino_list[0]),i);
            return (path);
          }
          else
          {
            /*  If the inode is different then something's changed,
                scrub the entry and start from scratch. */
            ino_list[i].valid = False;
          }
        }
      }
    }


  /*  We don't have the information to hand so rely on traditional methods.
      The very slow getcwd, which spawns a process on some systems, or the
      not quite so bad getwd. */

  if (!vfs_getwd(conn,s))
  {
    DEBUG(0,("vfs_GetWd: vfs_getwd call failed, errno %s\n",strerror(errno)));
    return (NULL);
  }

  pstrcpy(path,s);

  DEBUG(5,("vfs_GetWd %s, inode %.0f, dev %.0f\n",s,(double)st.st_ino,(double)st.st_dev));

  /* add it to the cache */
  i = MAX_GETWDCACHE - 1;
  string_set(&ino_list[i].dos_path,s);
  ino_list[i].dev = st.st_dev;
  ino_list[i].inode = st.st_ino;
  ino_list[i].valid = True;

  /* put it at the top of the list */
  array_promote((char *)&ino_list[0],sizeof(ino_list[0]),i);

  return (path);
}

/*******************************************************************
 Reduce a file name, removing .. elements and checking that
 it is below dir in the heirachy. This uses vfs_GetWd() and so must be run
 on the system that has the referenced file system.
 Widelinks are allowed if widelinks is true.
********************************************************************/

BOOL reduce_name(connection_struct *conn, char *s,char *dir,BOOL widelinks)
{
#ifndef REDUCE_PATHS
  return True;
#else
  pstring dir2;
  pstring wd;
  pstring base_name;
  pstring newname;
  char *p=NULL;
  BOOL relative = (*s != '/');

  *dir2 = *wd = *base_name = *newname = 0;

  if (widelinks)
  {
    unix_clean_name(s);
    /* can't have a leading .. */
    if (strncmp(s,"..",2) == 0 && (s[2]==0 || s[2]=='/'))
    {
      DEBUG(3,("Illegal file name? (%s)\n",s));
      return(False);
    }

    if (strlen(s) == 0)
      pstrcpy(s,"./");

    return(True);
  }

  DEBUG(3,("reduce_name [%s] [%s]\n",s,dir));

  /* remove any double slashes */
  all_string_sub(s,"//","/",0);

  pstrcpy(base_name,s);
  p = strrchr(base_name,'/');

  if (!p)
    return(True);

  if (!vfs_GetWd(conn,wd))
  {
    DEBUG(0,("couldn't vfs_GetWd for %s %s\n",s,dir));
    return(False);
  }

  if (vfs_ChDir(conn,dir) != 0)
  {
    DEBUG(0,("couldn't vfs_ChDir to %s\n",dir));
    return(False);
  }

  if (!vfs_GetWd(conn,dir2))
  {
    DEBUG(0,("couldn't vfs_GetWd for %s\n",dir));
    vfs_ChDir(conn,wd);
    return(False);
  }

  if (p && (p != base_name))
  {
    *p = 0;
    if (strcmp(p+1,".")==0)
      p[1]=0;
    if (strcmp(p+1,"..")==0)
      *p = '/';
  }

  if (vfs_ChDir(conn,base_name) != 0)
  {
    vfs_ChDir(conn,wd);
    DEBUG(3,("couldn't vfs_ChDir for %s %s basename=%s\n",s,dir,base_name));
    return(False);
  }

  if (!vfs_GetWd(conn,newname))
  {
    vfs_ChDir(conn,wd);
    DEBUG(2,("couldn't get vfs_GetWd for %s %s\n",s,dir2));
    return(False);
  }

  if (p && (p != base_name))
  {
    pstrcat(newname,"/");
    pstrcat(newname,p+1);
  }

  {
    size_t l = strlen(dir2);
    if (dir2[l-1] == '/')
      l--;

    if (strncmp(newname,dir2,l) != 0)
    {
      vfs_ChDir(conn,wd);
      DEBUG(2,("Bad access attempt? s=%s dir=%s newname=%s l=%d\n",s,dir2,newname,(int)l));
      return(False);
    }

    if (relative)
    {
      if (newname[l] == '/')
        pstrcpy(s,newname + l + 1);
      else
        pstrcpy(s,newname+l);
    }
    else
      pstrcpy(s,newname);
  }

  vfs_ChDir(conn,wd);

  if (strlen(s) == 0)
    pstrcpy(s,"./");

  DEBUG(3,("reduced to %s\n",s));
  return(True);
#endif
}
