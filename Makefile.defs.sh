echo CFLAGS+=`pkg-config --cflags glib-2.0`
echo LDFLAGS+=`pkg-config --libs glib-2.0`

libeventlibs=`pkg-config --libs libevent 2>/dev/null`
libeventcflags=`pkg-config --cflags libevent 2>/dev/null`

if [ -z "${libeventcflags}" ]; then
	libeventcflags=
fi

if [ -z "${libeventlibs}" ]; then
	libeventlibs=-levent
fi

echo CFLAGS+=${libeventcflags}
echo LDFLAGS+=${libeventlibs}
