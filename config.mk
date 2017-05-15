# Name of the output file
NAME		= rickmod

# Change this to build a standalone modplayer binary
STANDALONE	= 0

# Filenames
AFILE		= $(NAME).a
ELFFILE		= $(NAME).elf

# Tools
#TARGET		= m68k-elf-
#CC		= $(TARGET)gcc
#AS		= $(TARGET)as
#LD		= $(TARGET)ld
#AR		= $(TARGET)ar

# Paths
MODULESDIR      = $(TOPDIR)/libs
MODULES		= $(patsubst %/,%,$(foreach dir,$(wildcard $(MODULESDIR)/*/Makefile),$(dir $(dir))))
MODULESINCLUDE	= $(addsuffix /include,$(MODULES))

INCLUDEDIRS	= $(TOPDIR)/include $(MODULESINCLUDE)

# Compiler and linker flags
INCLUDES	= $(addprefix -I,$(INCLUDEDIRS))

CFLAGS		+= $(INCLUDES)
ifeq ($(STANDALONE),1)
CFLAGS		+= -DSTANDALONE
endif

# Makefile configurations
MAKEFLAGS	+=	--no-print-directory
