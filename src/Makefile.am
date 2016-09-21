# Note: plugindir is set in configure

plugin_LTLIBRARIES = libbinningplugin.la

# Path to installation of the output SDK 
#BINNING_CFLAGS = 
#BINNING_LIBS = 

# sources used to compile this plug-in
libbinningplugin_la_SOURCES = gstbinningfilter.c binning-rgb.c binning-resize-rgb.c binning-chroma.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libbinningplugin_la_CFLAGS = $(GST_CFLAGS)
libbinningplugin_la_LIBADD = $(GST_LIBS) -lgstvideo-1.0 
libbinningplugin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -rpath /usr/local/lib
libbinningplugin_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstbinningfilter.h
