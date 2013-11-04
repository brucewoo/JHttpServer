
all:

# Set libraries external to this component.
EXTERNAL_LIBS = -lpthread

# Section 2 ############################################################################
# Set common flags recognized by automake.
# DO NOT USE CPPFLAGS, CXXFLAGS, CFLAGS, LDFLAGS here! Set in configure.ac and|or override on command line.
# USE flags AM_CXXFLAGS, AM_CFLAGS, AM_CPPFLAGS, AM_LDFLAGS, LDADD in this section.
AM_CPPFLAGS = -I..

AUTO_OPTIONS=foreign
bin_PROGRAMS=JHttpServer
JHttpServer_SOURCES=jhttpserver.c http_connect.c log.c

