#!/bin/sh

case $( uname -s ) in
 Darwin)  alias vwlibtool=glibtoolize;;
 *)	alias vwlibtool=libtoolize;;
esac

vwlibtool -f -c && aclocal -I ./acinclude.d -I /usr/share/aclocal && autoheader && automake -ac -Woverride && autoconf && ./configure "$@"
