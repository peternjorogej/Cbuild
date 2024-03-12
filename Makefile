
ifndef ($(CONFIG))
  CONFIG=Debug
endif

ifeq ($(CONFIG),Debug)
  CONFIG=Debug
  DEFINES=-DCBUILD_DEBUG
  FLAGS=-ggdb -Og
endif

ifeq ($(CONFIG),Release)
  CONFIG=Release
  DEFINES=-DCBUILD_RELEASE
  FLAGS=-O2
endif

ALL_FLAGS=-m64 -std=c++20 $(FLAGS)
ALL_DEFINES=-DCBUILD_WIN32 $(DEFINES)

INCLUDES=-I. -I./3rdparty
EXTRASRCS=3rdparty/pugixml/pugixml.cpp

.PHONY:
	g++ cbuild.cpp $(EXTRASRCS) $(ALL_DEFINES) $(INCLUDES) $(ALL_FLAGS) -o bin/$(CONFIG)/cbuild.exe

all: .PHONY

