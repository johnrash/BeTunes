## Haiku Generic Makefile v2.6 ##
## Build natively on Haiku (needs haiku_devel): make

NAME = BeTunes
TYPE = APP
APP_MIME_SIG = application/x-vnd.BeTunes

SRCS = \
	src/App.cpp \
	src/AudioOutput.cpp \
	src/BrowserList.cpp \
	src/Controls.cpp \
	src/Equalizer.cpp \
	src/EqualizerWindow.cpp \
	src/Icons.cpp \
	src/Id3Tag.cpp \
	src/Importer.cpp \
	src/InfoWindow.cpp \
	src/Library.cpp \
	src/MainWindow.cpp \
	src/Player.cpp \
	src/PlayQueue.cpp \
	src/SearchField.cpp \
	src/Settings.cpp \
	src/SongList.cpp \
	src/SourceList.cpp \
	src/StatusDisplay.cpp \
	src/TagReader.cpp \
	src/TagWriter.cpp \
	src/TextUtils.cpp

RDEFS = BeTunes.rdef

# tracker: BFilePanel. columnlistview: BColumnListView, a static library
# with its headers in the private tree, like BAboutWindow (in libbe).
LIBS = be media tracker columnlistview $(STDCPPLIBS)

LOCAL_INCLUDE_PATHS = src
SYSTEM_INCLUDE_PATHS = /boot/system/develop/headers/private/interface
OPTIMIZE := FULL
WARNINGS = ALL
DEBUGGER :=

DEVEL_DIRECTORY := /boot/system/develop
include $(DEVEL_DIRECTORY)/etc/makefile-engine
