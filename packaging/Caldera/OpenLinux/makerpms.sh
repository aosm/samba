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
        ( cd ../../../.. ; chown -R ${USERID}.${GRPID} samba; mv samba samba-2.2.3a )
        ( cd ../../../.. ; tar czvf ${SRCDIR}/samba-2.2.3a.tar.gz samba-2.2.3a; mv samba-2.2.3a samba )
else
        ( cd ../../../.. ; chown -R ${USERID}.${GRPID} samba-2.2.3a )
        ( cd ../../../.. ; tar czvf ${SRCDIR}/samba-2.2.3a.tar.gz samba-2.2.3a )
fi

cp -af *.spec *.spec-lsb $SPECDIR
if [ $team -ne 0 ]; then
	cp *.spec-team $SPECDIR
fi
for i in `ls *.patch`
do
	cp $i $SRCDIR/
done
# Start building the package
cd $SPECDIR
if [ $old -eq 0 ]; then
mv -f samba2.spec samba2.spec-nonlsb
ln -f samba2.spec-lsb samba2.spec
fi
if [ $team -ne 0 ]; then
	mv -f samba2.spec samba2.spec-lsb
	ln -f samba2.spec-team samba2.spec
	rpm -ba -v samba2.spec
else
	rpm -ba -v --rmsource --clean samba2.spec
fi
