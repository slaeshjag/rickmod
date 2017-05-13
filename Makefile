TOPDIR	=	.
include config.mk

SRCDIR		= src
SUBDIRS		= $(SRCDIR)

OUTFILE		= out.a
LIBS            = $(addsuffix /$(OUTFILE),$(SUBDIRS))


.PHONY: all clean
.PHONY: $(SUBDIRS)
.SUFFIXES:

ifeq ($(STANDALONE),1)
all: $(ELFFILE)
	@echo "Build complete."
	@echo 
else
all: $(AFILE)
	@echo "Build complete."
	@echo 
endif

clean: $(SUBDIRS)
	@echo " [ RM ] $(AFILE)"
	@$(RM) $(AFILE)

$(ELFFILE): $(SUBDIRS)
	@echo " [ LD ] $@"
	@$(CC) -o $@ $(CFLAGS) -Wl,--whole-archive $(addsuffix /out.a,$(SRCDIR)) $(MODULESLIBS) -Wl,--no-whole-archive $(LDFLAGS)

$(AFILE): $(SUBDIRS)
	@echo " [ AR ] $(CURRENTPATH)$(AFILE)"
	@$(RM) $(AFILE)
	@$(AR) -cm $(AFILE) $(shell $(AR) t $(LIBS))

$(SUBDIRS):
	@echo " [ CD ] $(CURRENTPATH)$@/"
	@+make -C "$@" "CURRENTPATH=$(CURRENTPATH)$@/" $(MAKECMDGOALS)
