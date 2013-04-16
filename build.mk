PLATFORM ?= $(shell sh -c 'uname -s | tr "[A-Z]" "[a-z]"')


ifeq (darwin,$(PLATFORM))
SOEXT = dylib
else
SOEXT = so
endif

ifneq (,$(findstring linux,$(PLATFORM)))
include $(SRCDIR)/config-unix.mk
else
include $(SRCDIR)/config-mingw.mk
endif
