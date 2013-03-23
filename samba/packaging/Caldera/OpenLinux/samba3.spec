%define Version 	3.0.0rc1
%define date            1
%define Vendor  	Caldera
%define Dist		OpenLinux
%define EtcSamba 	/etc/samba.d

Name        	: samba
Version     	: %{Version}
Release     	: %{date}
Group       	: Server/Network

Summary      : Samba SMB client and server.

Copyright      : Andrew Tridgell, John H Terpstra; GPL Version 2
Packager      : Klaus Singvogel <klaus@caldera.de>
#Icon              : Caldera-daemon.gif
URL              : http://samba.org/samba

Requires    	: libpam >= 0.66, SysVinit-scripts >= 1.04-6


BuildRoot   	: /tmp/%{Name}-%{Version}

Source: ftp://ftp.samba.org/pub/samba/%{Name}-%{Version}.tar.gz


%Package doc
Group       	: Server/Network

Summary      : Documentation on SAMBA.


%Package -n smbfs
Group       	: System/Network

Summary     	: Mount and unmount commands for SMB filesystems (smbfs).


%Package -n swat
Group       	: Administration/Network
Requires       : setup >= 2.0-2, tcp_wrappers

Summary		: Samba Web Adminsitration Tool.

%Package -n libsmbclient
Group           : System/Network

Summary         : Samba Client Library.

%Description
Samba provides an SMB server which can be used to provide network
services to SMB (sometimes called "Lan Manager") clients, including
various versions of MS Windows, OS/2, and other Linux machines.

%Description -l de
Samba stellt einen SMB Server zur Verf�gung, mit dem Netzwerkdienste f�r SMB
(auch "Lan Manager" genannt) Clients bereitgestellt werden k�nnen. Dies
schlie�t verschiedene Versionen von MS Windows, OS/2 und andere Linux
Maschinen ein.

%Description -l es
Samba dispone de un servidor SMB que puede utilizarse para proporcionar
servicios de red a clientes SMB (a veces conocido como "Lan Manager"),
incluyendo varias versiones de MS Windows, OS/2 y otras m�quinas Linux.

%Description -l fr
Samba fournit un serveur SMB qui peut �tre utilis� pour fournir des services
de r�seau aux clients SMB (parfois appel�s "Lan Manager"), comportant
diverses versions de MS Windows, OS/2 et d'autres machines Linux.

%Description -l it
Samba fornisce un server SMB che pu� essere usato per fornire servizi
di rete a client SMB (talvolta chiamato "Lan Manager"), comprese varie
versioni di MS Windows, OS/2 e altre macchine Linux.

%Description -l pt
O Samba fornece um servidor de SMB que pode ser usado para fornecer servi�os de
rede aos clientes de SMB (denominado por vezes como "Lan Manager"), incluindo
v�rias vers�es do Windows, OS/2 e outras m�quinas Linux.

%Description doc
This package contains extensive SAMBA documentation, including a FAQ,
comprehensive usage documentation, and a number of examples.

%Description -l de doc
Dieses Paket enth�lt eine ausf�hrliche SAMBA Dokumentation, inklusive
einer FAQ, umfassender Gebrauchsdokumentation und einer Reihe von
Beispielen.

%Description -l es doc
Este paquete contiene una extensa documentaci�n sobre SAMBA, incluyendo
FAQ (Preguntas de Uso Frecuente), documentaci�n sobre el uso y algunos
ejemplos.

%Description -l fr doc
Ce paquetage contient une documentation compl�te sur Samba, y compris
une FAQ d�taill�e de son utilisation et un certain nombre d'exemples.

%Description -l it doc
Questo pacchetto contiene la documentazione su SAMBA tra cui una FAQ
una esaustiva documentazione d'uso e un certo numero di esempi.

%Description -l pt doc
Este pacote cont�m alguma documenta��o extensa sobre o SAMBA, incluindo a FAQ,
alguma documenta��o compreensiva sobre a utiliza��o e alguns exemplos.

%Description -n smbfs
This package includes the tools necessary to mount filesystems from
SMB servers.

%Description -l de -n smbfs
Dieses Paket enth�lt die n�tigen Tools, um Dateisysteme von SMB-Servern
zu mounten.

%Description -l es -n smbfs
este paqeute incluye las herramientas necesarias para montar sistemas de
ficheros de servidores SMB.

%Description -l fr -n smbfs
Ce paquetage contient les outils n�cessaires pour monter des syst�mes
de fichiers sur des serveurs SMB.

%Description -l it -n smbfs
Questo pacchetto contiene gli strumenti necessari per montare filesystem
da server SMB.

%Description -l pt -n smbfs
Este pacote cont�m as ferramentas necess�rias para montar sistema de
ficheiros de servidores SMB.

%Description -n swat
SWAT allows a Samba administrator to configure the complex smb.conf
file via a Web browser.  It also provides links to all the configurable
options in the smb.conf file allowing an administrator to easily look
up the effects of any change.

%Description -l de -n swat
Mit SWAT kann ein Samba-Administrator die komplexe smb.conf
Datei mit Hilfe eines Web-Browsers konfigurieren.  Es stellt auch Links zu
allen konfigurierbaren Optionen in der smb.conf Datei bereit, wodurch ein
Administrator die Auswirkungen einer �nderung leicht nachvollziehen kann.

%Description -l es -n swat
SWAT permite a un administrador de Samba configurar el complejo fichero
smb.conf mediante una navegador web. Tambi�n proporciona enlaces a todas las
opciones configurables en el fichero smb.conf, permitiendo al administrador
comprobar f�cilmente los efectos de cualquier cambio.

%Description -l fr -n swat
SWAT permet � un administrateur Samba de configurer le fichier smb.conf
complexe via un navigateur Web. Il fournit �galement des liens d'aide pour
toutes les options configurables dans le fichier smb.conf permettant � un
administrateur de consulter ais�ment les effets d'une modification.

%Description -l it -n swat
SWAT permette ad un amministratore Samba di configurare il complesso file
smb.conf attraverso un browser Web. SWAT ha anche dei link di aiuto per
tutte le opzioni di configurazione del file smb.conf.

%Description -l pt -n swat
O SWAT permite a um administrador de Samba configurar o complexo ficheiro
smb.conf atrav�s de uma interface Web. Fornece tamb�m refer�ncias para
todas as op��es configuraveis no smb.conf, permitindo a um admnistrador
verificar rapidamente o efeite de qualquer altera��o.

%Description -n libsmbclient
SMB Client Library allows for POSIX like SMB client calls providing developers
a clean and stable API for SMB client application development.


%Prep
%setup

for i in {cvs.,change-}log; do [ ! -f ../$i ] || mv ../$i source; done

mv swat/help/welcome.html docs
%{fixUP} -vT docs -e '
  s:/usr/local/samba/bin/(smb(client|run)):/usr/bin/$1:g +
  s:/usr/local/samba/bin/((s|n)mbd|swat):/usr/sbin/$1:g +
  s:/usr/local/samba/var/locks:/var/lock/samba.d: +
  s:/usr/local/samba/(var|lib)/log:/var/log/samba.d/smb: +
  s:/usr/local/samba/swat:/usr/share/samba/swat:g +
  s:/usr/local/samba/lib:%{EtcSamba}:g;
'
mv docs/welcome.html swat/help
for i in docs/*/smb.conf.5*; do
  %{fixUP} -vT $i -e '
    s:users\.map:smbusers:g +
    s:SAMBA_INSTALL_DIRECTORY/lib:%{EtcSamba}: +
    s:None \(set in compile\)\.:(see above).: +
    s:/usr/local/:/usr/:g;
  '
done
# End of DirtyHack(TM)


%Build
cd source
rm configure
autoconf

CFLAGS="$RPM_OPT_FLAGS" LDFLAGS="-s" ./configure \
	--prefix=/usr \
	--localstatedir=/var \
	--libdir=/usr/lib/samba \
	--sbindir=/usr/sbin \
	--with-configdir='%{EtcSamba}' \
	--with-privatedir='$(LIBDIR)' \
	--with-lockdir=/var/lock/samba.d \
	--with-swatdir=/usr/share/swat \
	--with-smbmount \
	--with-pam \
	--with-tdbsam \
	--with-ldapsam \
	--with-krb5=/usr/athena \
	--with-winbind \
	--with-utmp \
	--with-quotas \
	--with-vfs \
	--with-msdfs \
	--with-profile \
	--with-syslog \
	--with-netatalk \
	--with-smbwrapper \
	--with-libsmbclient \
	--with-acl-support \
	--with-sambabook=/usr/share/swat/using_samba

make all nsswitch/libnss_wins.so nsswitch/libnss_winbind.so torture nsswitch/pam_winbind.so modules everything pam_smbpass
(cd tdb; make tdbdump tdbtest tdbtorture tdbtool)

%Install
%{mkDESTDIR}
VVS=packaging/%{Vendor}/%{Dist}

mkdir -p $DESTDIR/etc/{{rc.d/init,logrotate,pam}.d,sysconfig/daemons}
mkdir -p $DESTDIR%{EtcSamba}
mkdir -p $DESTDIR/etc/skel/Samba
mkdir -p $DESTDIR/home/samba
mkdir -p $DESTDIR/lib/security
mkdir -p $DESTDIR/%{LSBservedir}/{netlogon,profiles,Public}
mkdir -p $DESTDIR%{NKinetdir}
mkdir -p $DESTDIR/{sbin,bin,usr/{sbin,bin}}
mkdir -p $DESTDIR/%{SVIdir}
mkdir -p $DESTDIR/usr/{include,lib/samba/vfs}
mkdir -p $DESTDIR/usr/share/samba/codepages/src
mkdir -p $DESTDIR/usr/share/swat/using_samba/{gifs,figs}
mkdir -p $DESTDIR/var/{lo{ck,g}/samba.d,spool/samba}

make -C source DESTDIR=$RPM_BUILD_ROOT install-everything installclientlib 

strip $DESTDIR/usr/bin/smb{mount,mnt,umount}
# Add links for mount.smbfs
( cd $DESTDIR/sbin; ln -s /usr/bin/smbmount mount.smbfs; \
	ln -s /usr/bin/smbumount umount.smbfs )

# First install /usr/bin progs
for i in smbfilter debug2html
do
	install -m 755 source/bin/$i $DESTDIR/usr/bin
done
# Next install /usr/sbin progs
for i in talloctort locktest locktest2 masktest msgtest smbtorture
do
	install -m 755 source/bin/$i $DESTDIR/usr/sbin
done
for i in tdbdump tdbtest tdbtorture tdbtool
do
	install -m 755 source/tdb/$i $DESTDIR/usr/sbin
done

# Install the nsswitch library extension file
cp -p source/nsswitch/libnss_wins.so $DESTDIR/lib
cp -p source/nsswitch/libnss_winbind.so $DESTDIR/lib
cp -p source/nsswitch/pam_winbind.so $DESTDIR/lib/security
cp -p source/bin/pam_smbpass.so $DESTDIR/lib/security
# Make link for wins resolver
( cd $DESTDIR/lib; ln -s libnss_wins.so libnss_wins.so.2 )

# Add libsmbclient.a support stuff
install -m 755 source/bin/libsmbclient.a $DESTDIR/usr/lib

# Add smbwrapper support
install -m 755 source/bin/smbsh $DESTDIR/usr/bin
install -m 755 source/bin/smbwrapper.so $DESTDIR/usr/lib

# Ancilliary support files
cp -p $VVS/samba.init $DESTDIR/etc/rc.d/init.d/samba
ln -s /etc/rc.d/init.d/samba $DESTDIR/usr/sbin
cp -p $VVS/smb.conf.sample $DESTDIR%{EtcSamba}/smb.conf.sample
cp -p $VVS/smbusers $DESTDIR%{EtcSamba}
cp -p $VVS/findsmb $DESTDIR/usr/bin
cp -p $VVS/samba.daemon $DESTDIR/etc/sysconfig/daemons/samba
cp -p $VVS/samba.pam $DESTDIR/etc/pam.d/samba
cp -p $VVS/samba.logrotate $DESTDIR/etc/logrotate.d/samba

cat <<-'EoH' > $DESTDIR%{EtcSamba}/lmhosts
	127.0.0.1 localhost
EoH

# lsb has new way of inetd configuration
cat <<EoI >$DESTDIR%{NKinetdir}/swat
swat    stream  tcp     nowait.400 root /usr/sbin/tcpd  swat
EoI

pushd $DESTDIR/usr/sbin
rm -f *.so
popd


DOCD="$DESTDIR/%{_defaultdocdir}/samba-%{Version}"; mkdir -p $DOCD
ln -sf ../Copyrights/GPL-2.0  $DOCD/COPYING
cp -p README Manifest Read-Manifest-Now WHATSNEW.txt Roadmap $DOCD
cp -a docs examples $DOCD

mv $DOCD/docs/htmldocs/wfw_slip.htm $DOCD/docs/wfw_slip.html

rm -rf $DOCD/docs/{htmldocs,manpages,yodldocs}
rm -rf $DOCD/examples/{svr4-startup,printing}
rm -rf $DOCD/CVS $DOCD/*/CVS $DOCD/*/*/CVS $DOCD/*/*/*/CVS

cp -p swat/README $DOCD/README.swat

# This is the O'Reily Samba Book - on-line
for i in docs/htmldocs/using_samba/*.html
do
install -m644 $i $DESTDIR/usr/share/swat/using_samba
done
for i in docs/htmldocs/using_samba/figs/*.gif
do
install -m644 $i $DESTDIR/usr/share/swat/using_samba/figs
done
for i in docs/htmldocs/using_samba/gifs/*.gif
do
install -m644 $i $DESTDIR/usr/share/swat/using_samba/gifs
done

%{fixUP} -vT $DOCD/examples -e 's:/usr/local/bin/:/usr/bin/:g;'
%{fixUP} -T $DESTDIR/%{SVIdir} -e 's:\@SVIdir\@:%{SVIdir}:'
%{fixUP} -vT $DOCD/examples -e 's:/usr/local/bin/:/usr/bin/:g;'
%{fixUP} -vT $DESTDIR/%{EtcSamba} -e 's:\@samba_home\@:%{LSBservedir}:'

%{fixManPages}
( cd $DESTDIR/usr/share/man/lang; \
	cp -a . $DESTDIR/usr/share/man/en; \
	cd ..; \
	rm -rf lang )

%{mkLists} -c samba
cat << 'EOF' | %{mkLists} -d samba
Samba                                   base
/lib/$                                  base
%{LSBservedir}                          config-IGNORED
^/(etc|var|home|tmp)                    config-IGNORED
swat                                    swat
%{_defaultdocdir}/samba-[^/]+/$         base
%{_defaultdocdir}/samba-                doc
tmp                                     IGNORED
man                                     IGNORED
/src/$                                  IGNORED
/usr/private/$                          IGNORED
@default@
EOF
cat << 'EOF' | %{mkLists} -f -a samba
\.old$                                  IGNORED
Samba/README.txt                        base
^/etc                                   config-IGNORED
%{_defaultdocdir}/samba-[^/]+/(COPYING|README$) base
libnss_*                                base
pam_*                                   base
vfs_*                                   base
pdb_*                                   base
smbsh                                   base
smbwrapper.so				base
%{_defaultdocdir}/samba-[^/]+/(COPYING|README$) base
%{_defaultdocdir}/samba-                doc
smb(mount|mnt|umount)                   smbfs
mount.smbfs                             smbfs
swat                                    swat
libsmbclient                            libsmbclient
@default@
EOF


%Clean
%{rmDESTDIR}


%Post
/usr/lib/LSB/init-install %{Name}
ldconfig

%Post -n swat
%{NKinetdReload}
perl -pi -e '$s=1 if /^swat/;
  print "swat:ALL EXCEPT 127.0.0.1\n" if eof && ! $s' /etc/hosts.deny


%PostUn
test "$1" = "0" || exit 0
/usr/lib/LSB/init-remove %{Name}
# We want to remove the browse.dat and wins.dat files so they can not
# interfer with a new version of samba!
rm -f /var/lock/samba/browse.dat
rm -f /var/lock/samba/{brlock,connections,locking,messages}.tdb
if [ -e /var/lock/samba.d/namelist.debug ]; then
        rm -f /var/lock/samba.d/namelist.debug
fi
rm -f /var/lock/samba/unexpected.tdb
rm -f /var/lock/samba/{smbd,nmbd}.pid

# Note: We MUST keep:
#       winbindd_*, sshare_info*, printing*, ntdrivers*


%PostUn -n swat
#$no lsb: lisa --inetd disable swat $1
test "$1" = "0" || exit 0
%{SVIdir}/inet reload
[ -x /usr/sbin/swat ]||perl -ni -e '/^swat\s*\:/||print' /etc/hosts.deny


%Files -f files-samba-base
%defattr(-,root,root)
%config %attr(0755,root,root) %{SVIdir}/samba
%config %attr(644,root,root) /etc/sysconfig/daemons/samba
%config %attr(644,root,root) /etc/pam.d/samba
%config %attr(644,root,root) /etc/logrotate.d/samba
%config %attr(-,root,root) %{EtcSamba}
%dir %attr(755,root,root) /var/lock/samba.d
%dir %attr(755,root,root) /var/log/samba.d
%dir %attr(1777,root,root) /var/spool/samba
%dir %attr(755,root,root) %{LSBservedir}
%dir %attr(755,root,root) %{LSBservedir}/netlogon
%dir %attr(755,root,root) %{LSBservedir}/profiles
%dir %attr(755,root,root) %{LSBservedir}/Public

%Files doc -f files-samba-doc
%defattr(-,root,root)

%Files -n smbfs -f files-samba-smbfs
%defattr(-,root,root)


%Files -n swat  -f files-samba-swat
%defattr(-,root,root)
%config %attr(644,root,root) %{NKinetdir}/swat

%Files -n libsmbclient -f files-samba-libsmbclient
%defattr(-,root,root)

%ChangeLog
* Mon Mar 11 2002 John H Terpstra <jht@samba.org>
- Make this work

