/*
   Unix SMB/Netbios implementation.
   Version 3.0
   change notify handling - linux kernel based implementation
   Copyright (C) Andrew Tridgell 2000

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

#if HAVE_KERNEL_CHANGE_NOTIFY

static VOLATILE sig_atomic_t fd_pending;
static VOLATILE sig_atomic_t signals_received;
static VOLATILE sig_atomic_t signals_processed;

#ifndef DN_ACCESS
#define DN_ACCESS       0x00000001      /* File accessed in directory */
#define DN_MODIFY       0x00000002      /* File modified in directory */
#define DN_CREATE       0x00000004      /* File created in directory */
#define DN_DELETE       0x00000008      /* File removed from directory */
#define DN_RENAME       0x00000010      /* File renamed in directory */
#define DN_ATTRIB       0x00000020      /* File changed attribute */
#define DN_MULTISHOT    0x80000000      /* Don't remove notifier */
#endif


#ifndef RT_SIGNAL_NOTIFY
#define RT_SIGNAL_NOTIFY 34
#endif

#ifndef F_SETSIG
#define F_SETSIG 10
#endif

#ifndef F_NOTIFY
#define F_NOTIFY 1026
#endif

/****************************************************************************
 This is the structure to keep the information needed to
 determine if a directory has changed.
*****************************************************************************/
struct change_data {
	int directory_handle;
};

/****************************************************************************
the signal handler for change notify
*****************************************************************************/
static void signal_handler(int sig, siginfo_t *info, void *unused)
{
	BlockSignals(True, sig);
	fd_pending = (sig_atomic_t)info->si_fd;
	signals_received++;
	sys_select_signal();
}



/****************************************************************************
 Check if a change notify should be issued.
 time non-zero means timeout check (used for hash). Ignore this (async method
 where time is zero will be used instead).
*****************************************************************************/
static BOOL kernel_check_notify(connection_struct *conn, uint16 vuid, char *path, uint32 flags, void *datap, time_t t)
{
	struct change_data *data = (struct change_data *)datap;

	if (t)
		return False;

	if (data->directory_handle != (int)fd_pending) return False;

	DEBUG(3,("kernel change notify on %s fd=%d\n", path, (int)fd_pending));

	close((int)fd_pending);
	fd_pending = (sig_atomic_t)-1;
	data->directory_handle = -1;
	signals_processed++;
	BlockSignals(False, RT_SIGNAL_NOTIFY);
	return True;
}

/****************************************************************************
remove a change notify data structure
*****************************************************************************/
static void kernel_remove_notify(void *datap)
{
	struct change_data *data = (struct change_data *)datap;
	int fd = data->directory_handle;
	if (fd != -1) {
		if (fd == (int)fd_pending) {
			fd_pending = (sig_atomic_t)-1;
			signals_processed++;
			BlockSignals(False, RT_SIGNAL_NOTIFY);
		}
		close(fd);
	}
	SAFE_FREE(data);
	DEBUG(3,("removed kernel change notify fd=%d\n", fd));
}


/****************************************************************************
register a change notify request
*****************************************************************************/
static void *kernel_register_notify(connection_struct *conn, char *path, uint32 flags)
{
	struct change_data data;
	int fd;
	unsigned long kernel_flags;
	
	fd = sys_open(dos_to_unix(path,False),O_RDONLY, 0);

	if (fd == -1) {
		DEBUG(3,("Failed to open directory %s for change notify\n", path));
		return NULL;
	}

	if (fcntl(fd, F_SETSIG, RT_SIGNAL_NOTIFY) == -1) {
		DEBUG(3,("Failed to set signal handler for change notify\n"));
		return NULL;
	}

	kernel_flags = DN_CREATE|DN_DELETE|DN_RENAME; /* creation/deletion changes everything! */
	if (flags & FILE_NOTIFY_CHANGE_FILE)        kernel_flags |= DN_MODIFY;
	if (flags & FILE_NOTIFY_CHANGE_DIR_NAME)    kernel_flags |= DN_RENAME|DN_DELETE;
	if (flags & FILE_NOTIFY_CHANGE_ATTRIBUTES)  kernel_flags |= DN_ATTRIB;
	if (flags & FILE_NOTIFY_CHANGE_SIZE)        kernel_flags |= DN_MODIFY;
	if (flags & FILE_NOTIFY_CHANGE_LAST_WRITE)  kernel_flags |= DN_MODIFY;
	if (flags & FILE_NOTIFY_CHANGE_LAST_ACCESS) kernel_flags |= DN_ACCESS;
	if (flags & FILE_NOTIFY_CHANGE_CREATION)    kernel_flags |= DN_CREATE;
	if (flags & FILE_NOTIFY_CHANGE_SECURITY)    kernel_flags |= DN_ATTRIB;
	if (flags & FILE_NOTIFY_CHANGE_EA)          kernel_flags |= DN_ATTRIB;
	if (flags & FILE_NOTIFY_CHANGE_FILE_NAME)   kernel_flags |= DN_RENAME|DN_DELETE;

	if (fcntl(fd, F_NOTIFY, kernel_flags) == -1) {
		DEBUG(3,("Failed to set async flag for change notify\n"));
		return NULL;
	}

	data.directory_handle = fd;

	DEBUG(3,("kernel change notify on %s (ntflags=0x%x flags=0x%x) fd=%d\n", 
		 path, (int)flags, (int)kernel_flags, fd));

	return (void *)memdup(&data, sizeof(data));
}

/****************************************************************************
see if the kernel supports change notify
****************************************************************************/
static BOOL kernel_notify_available(void) 
{
	int fd, ret;
	fd = open("/tmp", O_RDONLY);
	if (fd == -1) return False; /* uggh! */
	ret = fcntl(fd, F_NOTIFY, 0);
	close(fd);
	return ret == 0;
}


/****************************************************************************
setup kernel based change notify
****************************************************************************/
struct cnotify_fns *kernel_notify_init(void) 
{
	static struct cnotify_fns cnotify;
        struct sigaction act;

        act.sa_handler = NULL;
        act.sa_sigaction = signal_handler;
        act.sa_flags = SA_SIGINFO;
        if (sigaction(RT_SIGNAL_NOTIFY, &act, NULL) != 0) {
		DEBUG(0,("Failed to setup RT_SIGNAL_NOTIFY handler\n"));
		return NULL;
        }

	if (!kernel_notify_available()) return NULL;

	cnotify.register_notify = kernel_register_notify;
	cnotify.check_notify = kernel_check_notify;
	cnotify.remove_notify = kernel_remove_notify;
	cnotify.select_time = -1;

	return &cnotify;
}


#else
 void notify_kernel_dummy(void) {}
#endif /* HAVE_KERNEL_CHANGE_NOTIFY */
