#!/bin/sh
# Copyright (C) 1998 John H Terpstra, 2000 Klaus Singvogel
#
SPECDIR=${SPECDIR:-/usr/src/OpenLinux/SPECS}
SRCDIR=${SRCDIR:-/usr/src/OpenLinux/SOURCES}
USERID=`id -u`
GRPID=`id -g`
devel=0;
old=0;

# Do some argument parsing...
if [ z$1 = z"devel" ]; then
	devel=1;
	shift
fi
if [ z$1 = z"old" ]; then
	old=1;
	shift
fi
if [ z$1 = z"team" ]; then
	team=1;
	shift
fi

# Start preparing the packages...
if [ $devel -ne 0 ]; then
        ( cd ../../../.. ; chown -R ${USERID}.${GRPID} samba3; mv samba3 samba-3.0.0rc1 )
        ( cd ../../../.. ; tar czvf ${SRCDIR}/samba-3.0.0rc1.tar.gz samba-3.0.0rc1; mv samba-3.0.0rc1 samba3 )
else
        ( cd ../../../.. ; chown -R ${USERID}.${GRPID} samba-3.0.0rc1 )
        ( cd ../../../.. ; tar czvf ${SRCDIR}/samba-3.0.0rc1.tar.gz samba-3.0.0rc1 )
fi

cp -af *.spec *.spec-lsb $SPECDIR
#if [ $team -ne 0 ]; then
#	cp *.spec-team $SPECDIR
#fi
for i in `ls *.patch`
do
	cp $i $SRCDIR/
done
# Start building the package
cd $SPECDIR
#if [ $old -eq 0 ]; then
#mv -f samba2.spec samba2.spec-nonlsb
#ln -f samba2.spec-lsb samba3.spec
#fi
if [ $team -ne 0 ]; then
#	mv -f samba3.spec samba3.spec-lsb
#	ln -f samba3.spec-team samba3.spec
	rpm -ba -v samba3.spec
else
	rpm -ba -v --rmsource --clean samba3.spec
fi
