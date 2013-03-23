#!/bin/sh
# Copyright (C) John H Terpstra 1998-2002
# Updated for RPM 3 by Jochen Wiedmann, joe@ispsoft.de
# Changed for a generic tar file rebuild by abartlet@pcug.org.au
# Taken from Red Hat build area by JHT
# Changed by John H Terpstra to build on RH8.1 - should also work for earlier versions jht@samba.org

# The following allows environment variables to override the target directories
#   the alternative is to have a file in your home directory calles .rpmmacros
#   containing the following:
#   %_topdir  /home/mylogin/RPM
#
# Note: Under this directory rpm expects to find the same directories that are under the
#   /usr/src/redhat directory
#
if [ -x ~/.rpmmacros ]; then
	TOPDIR=`awk '/topdir/ {print $2}' < ~/.rpmmacros`
	if [ z$TOPDIR <> "z" ]; then
		SPECDIR=${TOPDIR}/SPECS
		SRCDIR=${TOPDIR}/SOURCES
	fi
fi

SPECDIR=${SPECDIR:-/usr/src/RPM/SPECS}
SRCDIR=${SRCDIR:-/usr/src/RPM/SOURCES}

# At this point the SPECDIR and SRCDIR vaiables must have a value!

USERID=`id -u`
GRPID=`id -g`
VERSION='2.2.3a'

RPMVER=`rpm --version | awk '{print $3}'`
echo The RPM Version on this machine is: $RPMVER

case $RPMVER in
    2*)
       echo Building for RPM v2.x
       sed -e "s/MANDIR_MACRO/\%\{prefix\}\/man/g" < samba2.spec > samba.spec
       ;;
    3*)
       echo Building for RPM v3.x
       sed -e "s/MANDIR_MACRO/\%\{prefix\}\/man/g" < samba2.spec > samba.spec
       ;;
    4*)
       echo Building for RPM v4.x
       sed -e "s/MANDIR_MACRO/\%\{_mandir\}/g" < samba2.spec > samba.spec
       ;;
    *)
       echo "Unknown RPM version: `rpm --version`"
       exit 1
       ;;
esac

( cd ../../source; if [ -f Makefile ]; then make distclean; fi )
( cd ../../.. ; chown -R ${USERID}.${GRPID} samba-${VERSION} )
( cd ../../.. ; tar --exclude=CVS -cvf ${SRCDIR}/samba-${VERSION}.tar.gz samba-${VERSION} )
( cd ${SRCDIR}; bzip2 samba-$VERSION.tar )

cp -av samba.spec ${SPECDIR}
cp -a *.patch smb.* samba.log $SRCDIR

echo Getting Ready to build release package
cd ${SPECDIR}
rpm -ba -v --clean --rmsource samba.spec

echo Done.
