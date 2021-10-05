# CMakefile: generic automatic Makefile for C and C++

lsdirs = $(foreach dir,$(2),$(foreach file,$(wildcard $(dir)/*.$(1)),$(file)))

CAT_PROJECT := (if [ -f PROJECT ]; then cat PROJECT; else echo ""; fi)

mstrip = $(patsubst [[:space:]]*%[[:space:]]*,%,$(1))
getopt = $(call mstrip,$(shell $(CAT_PROJECT) | sed -n "s/^[[:space:]]*$(1)[[:space:]]*=[[:space:]]*\(.*\)/\1/p"))

CONFIG   := $(call getopt,CONFIG)
ifeq ($(CONFIG),)
PREFIX   :=
getcopt = $(call getopt,$(1))
getropt = $(call getopt,$(1))
else
PREFIX   := $(CONFIG)[[:space:]][[:space:]]*
getcopt = $(call mstrip,$(call getopt,$(1)) $(call getopt,$(PREFIX)$(1)))
getropt = $(if $(call getopt,$(PREFIX)$(1)),$(call getopt,$(PREFIX)$(1)),$(call getopt,$(1)))
endif
# getcopt gets a concatenable option; getropt a replaceable option

CC          := $(call getropt,CC)
ifeq ($(CC),)
CC := gcc
endif
CXX	    := $(call getropt,CXX)
ifeq ($(CXX),)
CXX := g++
endif
YACC        := $(call getropt,YACC)
ifeq ($(YACC),)
YACC := bison
endif
LEX         := $(call getropt,LEX)
ifeq ($(LEX),)
LEX := flex
endif
CFLAGS      := $(call getcopt,CFLAGS)
CXXFLAGS    := $(call getcopt,CXXFLAGS)
CDEPFLAGS   := $(call getcopt,CDEPFLAGS)
ifeq ($(strip $(CDEPFLAGS)),)
CDEPFLAGS   := $(CFLAGS)
endif
CXXDEPFLAGS := $(call getcopt,CXXDEPFLAGS)
ifeq ($(strip $(CXXDEPFLAGS)),)
CXXDEPFLAGS   := $(CXXFLAGS)
endif
CLDFLAGS    := $(call getcopt,CLDFLAGS)
CXXLDFLAGS  := $(call getcopt,CXXLDFLAGS)
YFLAGS      := $(call getcopt,YFLAGS)
LEXFLAGS    := $(call getcopt,LEXFLAGS)
LIBFLAGS    := $(call getcopt,LIBFLAGS)
ifeq ($(strip $(LIBFLAGS)),)
LIBFLAGS := sru
endif

# source directory
SRCDIR    := $(call getropt,SRCDIR)
ifeq ($(SRCDIR),)
SRCDIR     := $(shell if [ -d src ]; then echo "src/"; else echo ""; fi)
else
override SRCDIR     := $(SRCDIR)/
endif
ifeq ($(SRCDIR),./)
BUILDDIR :=
endif
ifeq ($(SRCDIR),.//)
BUILDDIR :=
endif
ifneq ($(SRCDIR),)
INCLUDES := $(call mstrip,-I$(SRCDIR) $(INCLUDES))
endif

# build directory
BUILDDIR    := $(call getropt,BUILDDIR)
ifeq ($(BUILDDIR),)
BUILDDIR     := _build/
else
override BUILDDIR     := $(BUILDDIR)/
endif
ifeq ($(BUILDDIR),./)
BUILDDIR :=
endif
ifeq ($(BUILDDIR),.//)
BUILDDIR :=
endif
ifneq ($(BUILDDIR),)
INCLUDES := $(call mstrip,-I$(BUILDDIR)$(SRCDIR) $(INCLUDES))
ifeq ($(SRCDIR),)
INCLUDES := $(call mstrip,-I. $(INCLUDES))
endif
endif

# update flags
ifneq ($(INCLUDES),)
CFLAGS := $(INCLUDES) $(CFLAGS)
CXXFLAGS := $(INCLUDES) $(CXXFLAGS)
CDEPFLAGS := $(INCLUDES) $(CDEPFLAGS)
CXXDEPFLAGS := $(INCLUDES) $(CXXDEPFLAGS)
endif

# library to create
LIB	    := $(call getropt,LIB)
# programs to create
PROGRAMS     := $(call getcopt,PROGRAMS)
# subdirectories
SUBDIRS     := $(call getcopt,SUBDIRS)
override SUBDIRS := $(patsubst %,$(SRCDIR)%,$(SUBDIRS))
# source files
YSOURCES    := $(strip $(wildcard $(SRCDIR)*.y) $(call lsdirs,y,$(SUBDIRS)))
LEXSOURCES  := $(strip $(wildcard $(SRCDIR)*.lex) $(call lsdirs,lex,$(SUBDIRS)))
CYSOURCES   := $(patsubst %.y,$(BUILDDIR)%.c,$(YSOURCES))
HYSOURCES   := $(patsubst %.y,$(BUILDDIR)%.h,$(YSOURCES))
CLEXSOURCES := $(patsubst %.lex,$(BUILDDIR)%.c,$(LEXSOURCES))
CSOURCES    := $(filter-out $(CYSOURCES) $(CLEXSOURCES),$(strip $(sort $(wildcard $(SRCDIR)*.c) $(call lsdirs,c,$(SUBDIRS)))))
ALLCSOURCES := $(strip $(CSOURCES) $(CYSOURCES) $(CLEXSOURCES))
CPPSOURCES  := $(strip $(wildcard $(SRCDIR)*.cpp) $(call lsdirs,cpp,$(SUBDIRS)))
CXXSOURCES  := $(strip $(wildcard $(SRCDIR)*.cxx) $(call lsdirs,cxx,$(SUBDIRS)))
CCSOURCES  := $(strip $(wildcard $(SRCDIR)*.cc) $(call lsdirs,cc,$(SUBDIRS)))
ALLCPPSOURCES := $(strip $(CPPSOURCES) $(CXXSOURCES) $(CCSOURCES))

# all object files (sort to remove duplicates, which may exist from
# previous builds in a different directory)
COBJECTS    := $(sort $(patsubst %.c,$(BUILDDIR)%.o,$(CSOURCES)) $(patsubst %.c,%.o,$(CYSOURCES) $(CLEXSOURCES)))
CPPOBJECTS  := $(patsubst %.cpp,$(BUILDDIR)%.o,$(CPPSOURCES))
CXXOBJECTS  := $(patsubst %.cxx,$(BUILDDIR)%.o,$(CXXSOURCES))
CCOBJECTS   := $(patsubst %.cc,$(BUILDDIR)%.o,$(CCSOURCES))
ALLCPPOBJECTS := $(strip $(CPPOBJECTS) $(CXXOBJECTS) $(CCOBJECTS))
ALLOBJECTS  := $(strip $(COBJECTS) $(ALLCPPOBJECTS))

# object files which contain the "main" function
ifneq ($(strip $(CSOURCES)),)
   CMAINOBJECTS := $(patsubst %.c,$(BUILDDIR)%.o,$(shell egrep -l '\bint[[:space:]]+main\b' $(CSOURCES)))
else
   CMAINOBJECTS :=
endif
ifneq ($(strip $(CPPSOURCES)),)
   CPPMAINOBJECTS := $(patsubst %.cpp,$(BUILDDIR)%.o,$(shell egrep -l '\bint[[:space:]]+main\b' $(CPPSOURCES)))
else
   CPPMAINOBJECTS :=
endif
ifneq ($(strip $(CXXSOURCES)),)
   CXXMAINOBJECTS := $(patsubst %.cxx,$(BUILDDIR)%.o,$(shell egrep -l 'int[[:space:]]+main\b' $(CXXSOURCES)))
else
   CXXMAINOBJECTS :=
endif
ifneq ($(strip $(CCSOURCES)),)
   CCMAINOBJECTS := $(patsubst %.cxx,$(BUILDDIR)%.o,$(shell egrep -l 'int[[:space:]]+main\b' $(CCSOURCES)))
else
   CCMAINOBJECTS :=
endif
ifneq ($(PROGRAMS),)
MAINOBJECTS  := $(patsubst %,%.o,$(PROGRAMS))
CPROGRAMS    := $(filter $(PROGRAMS),$(patsubst %.o,%,$(COBJECTS)))
CPPPROGRAMS  := $(filter $(PROGRAMS),$(patsubst %.o,%,$(ALLCPPOBJECTS)))
else ifneq ($(LIB),)
MAINOBJECTS  :=
CPROGRAMS    :=
CPPPROGRAMS  :=
else
MAINOBJECTS  := $(CMAINOBJECTS) $(CPPMAINOBJECTS) $(CXXMAINOBJECTS) $(CCMAINOBJECTS)
CPROGRAMS    := $(patsubst %.o,%,$(CMAINOBJECTS))
CPPPROGRAMS  := $(patsubst %.o,%,$(CPPMAINOBJECTS)) $(patsubst %.o,%,$(CXXMAINOBJECTS)) $(patsubst %.o,%,$(CCMAINOBJECTS))
PROGRAMS     := $(patsubst %.o,%,$(MAINOBJECTS))
endif
# dependencies for each source file
CDEPENDS     := $(patsubst %.c,$(BUILDDIR)%.d,$(CSOURCES))
CYLDEPENDS   := $(patsubst %.c,%.d,$(CYSOURCES)) $(patsubst %.c,%.d,$(CLEXSOURCES))
CPPDEPENDS   := $(patsubst %.cpp,$(BUILDDIR)%.d,$(CPPSOURCES))
CXXDEPENDS   := $(patsubst %.cxx,$(BUILDDIR)%.d,$(CXXSOURCES))
CCDEPENDS    := $(patsubst %.cc,$(BUILDDIR)%.d,$(CCSOURCES))
DEPENDS := $(sort $(CDEPENDS) $(CYLDEPENDS) $(CPPDEPENDS) $(CXXDEPENDS) $(CCDEPENDS))
# object files which don't include the "main" function
OBJECTS	    := $(filter-out $(MAINOBJECTS),$(ALLOBJECTS))

# linkers
CCLD	    := $(call getropt,CCLD)
ifeq ($(CCLD),)
ifeq ($(ALLCPPSOURCES),)
CCLD := $(CC)
else
CCLD := $(CXX)
endif
endif

CXXLD	    := $(call getropt,CXXLD)
ifeq ($(CXXLD),)
CXXLD := $(CXX)
endif

LIBTOOL	    := $(call getropt,LIBTOOL)
ifeq ($(LIBTOOL),)
LIBTOOL := ar
endif

ifneq ($(BUILDDIR),)
$(shell mkdir -p $(BUILDDIR))
$(shell mkdir -p $(BUILDDIR)$(SRCDIR))
$(shell for dir in $(patsubst %,$(BUILDDIR)%,$(SUBDIRS)); do mkdir -p $dir; done)
endif

all: $(DEPENDS) $(OBJECTS) $(PROGRAMS) $(LIB)

depend: $(DEPENDS)

$(HYSOURCES) : $(BUILDDIR)%.h : %.y
	$(YACC) $(YFLAGS) -o $(patsubst %.h,%.c,$@) $<

$(CYSOURCES) : $(BUILDDIR)%.c : %.y
	$(YACC) $(YFLAGS) -o $@ $<

$(CLEXSOURCES) : $(BUILDDIR)%.c : %.lex
	$(LEX) $(LEXFLAGS) -o $@ $<

$(CDEPENDS) : $(BUILDDIR)%.d : %.c
	$(CC) $(CDEPFLAGS) -MM -MT $(patsubst %.c,$(BUILDDIR)%.o,$<) $< > $@
	printf "\t$(CC) -c $(CFLAGS) -o $(patsubst %.c,$(BUILDDIR)%.o,$<) $<\n" >> $@

$(CYLDEPENDS) : $(BUILDDIR)%.d : $(BUILDDIR)%.c
	$(CC) $(CDEPFLAGS) -MM -MT $(patsubst %.c,%.o,$<) $< > $@
	printf "\t$(CC) -c $(CFLAGS) -o $(patsubst %.c,%.o,$<) $<\n" >> $@

$(CPPDEPENDS) : $(BUILDDIR)%.d : %.cpp
	$(CXX) $(CXXDEPFLAGS) -MM -MT $(patsubst %.cpp,$(BUILDDIR)%.o,$<) $< > $@
	printf "\t$(CXX) -c $(CXXFLAGS) -o $(patsubst %.cpp,$(BUILDDIR)%.o,$<) $<\n" >> $@

$(CXXDEPENDS) : $(BUILDDIR)%.d : %.cxx
	$(CXX) $(CXXDEPFLAGS) -MM -MT $(patsubst %.cxx,$(BUILDDIR)%.o,$<) $< > $@
	printf "\t$(CXX) -c $(CXXFLAGS) -o $(patsubst %.cxx,$(BUILDDIR)%.o,$<) $<\n" >> $@

$(CCDEPENDS) : $(BUILDDIR)%.d : %.cc
	$(CXX) $(CXXDEPFLAGS) -MM -MT $(patsubst %.cc,$(BUILDDIR)%.o,$<) $< > $@
	printf "\t$(CXX) -c $(CXXFLAGS) -o $(patsubst %.cc,$(BUILDDIR)%.o,$<) $<\n" >> $@

$(CPROGRAMS) : % : $(ALLOBJECTS)
	$(CCLD) -o $@ $@.o $(OBJECTS) $(CLDFLAGS)

$(CPPPROGRAMS) : % : $(ALLOBJECTS)
	$(CXXLD) -o $@  $@.o $(OBJECTS) $(CXXLDFLAGS)

$(LIB) : % : $(OBJECTS)
	$(LIBTOOL) $(LIBFLAGS) $@ $(OBJECTS)

include $(DEPENDS)

clean:
	-rm -f $(PROGRAMS) $(ALLOBJECTS) $(DEPENDS) $(LIB) $(CYSOURCES) $(HYSOURCES) $(CLEXSOURCES)
	-rm $(BUILDDIR).prjconfig
ifneq ($(BUILDDIR),)
	-rm -rf $(BUILDDIR)
endif

Makefile: $(DEPENDS) $(BUILDDIR).prjconfig

$(BUILDDIR).prjconfig: PROJECT
	-rm -f $(PROGRAMS) $(ALLOBJECTS) $(DEPENDS) $(LIB) $(CYSOURCES) $(HYSOURCES) $(CLEXSOURCES)
	touch $(BUILDDIR).prjconfig

ifneq ($(wildcard Makefile-include),)
include Makefile-include
endif
