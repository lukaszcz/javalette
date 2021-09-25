# -*- coding: utf-8 -*-
# Generyczny, automatyczny Makefile, dla C i C++.

CONFIG   := $(strip $(shell cat PROJECT | sed -n 's/^[ ]*CONFIG[ ]*=[ ]*\(.*\)/\1/p'))

CC          := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*CC[ ]*=[ ]*\(.*\)/\1/p")
ifeq ($(CC),)
CC          := $(shell cat PROJECT | sed -n "s/^[ ]*CC[ ]*=[ ]*\(.*\)/\1/p")
ifeq ($(CC),)
CC := gcc
endif
endif
CXX	    := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*CXX[ ]*=[ ]*\(.*\)/\1/p")
ifeq ($(CXX),)
CXX	    := $(shell cat PROJECT | sed -n "s/^[ ]*CXX[ ]*=[ ]*\(.*\)/\1/p")
ifeq ($(CXX),)
CXX := g++
endif
endif
YACC        := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*YACC[ ]*=[ ]*\(.*\)/\1/p")
ifeq ($(YACC),)
YACC        := $(shell cat PROJECT | sed -n "s/^[ ]*YACC[ ]*=[ ]*\(.*\)/\1/p")
ifeq ($(YACC),)
YACC := bison
endif
endif
LEX         := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*FLEX[ ]*=[ ]*\(.*\)/\1/p")
ifeq ($(LEX),)
LEX         := $(shell cat PROJECT | sed -n "s/^[ ]*FLEX[ ]*=[ ]*\(.*\)/\1/p")
ifeq ($(LEX),)
LEX := flex
endif
endif
CFLAGS      := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*CFLAGS[ ]*=[ ]*\(.*\)/\1/p")
ifneq ($(CONFIG),)
CFLAGS      := $(strip $(shell cat PROJECT | sed -n "s/^[ ]*CFLAGS[ ]*=[ ]*\(.*\)/\1/p") $(CFLAGS))
endif
CXXFLAGS    := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*CXXFLAGS[ ]*=[ ]*\(.*\)/\1/p")
ifneq ($(CONFIG),)
CXXFLAGS    := $(strip $(shell cat PROJECT | sed -n "s/^[ ]*CXXFLAGS[ ]*=[ ]*\(.*\)/\1/p") $(CXXFLAGS))
endif
CDEPFLAGS   := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*CDEPFLAGS[ ]*=[ ]*\(.*\)/\1/p")
ifneq ($(CONFIG),)
CDEPFLAGS   := $(strip $(shell cat PROJECT | sed -n "s/^[ ]*CDEPFLAGS[ ]*=[ ]*\(.*\)/\1/p") $(CDEPFLAGS))
endif
ifeq ($(CDEPFLAGS),)
CDEPFLAGS   := $(CFLAGS)
endif
CXXDEPFLAGS := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*CXXDEPFLAGS[ ]*=[ ]*\(.*\)/\1/p")
ifneq ($(CONFIG),)
CXXDEPFLAGS := $(strip $(shell cat PROJECT | sed -n "s/^[ ]*CXXDEPFLAGS[ ]*=[ ]*\(.*\)/\1/p") $(CXXDEPFLAGS))
endif
ifeq ($(CXXDEPFLAGS),)
CXXDEPFLAGS   := $(CXXFLAGS)
endif
CLDFLAGS    := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*CLDFLAGS[ ]*=[ ]*\(.*\)/\1/p")
ifneq ($(CONFIG),)
CLDFLAGS    := $(shell cat PROJECT | sed -n "s/^[ ]*CLDFLAGS[ ]*=[ ]*\(.*\)/\1/p") $(CLDFLAGS)
endif
CXXLDFLAGS  := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*CXXLDFLAGS[ ]*=[ ]*\(.*\)/\1/p")
ifneq ($(CONFIG),)
CXXLDFLAGS  := $(shell cat PROJECT | sed -n "s/^[ ]*CXXLDFLAGS[ ]*=[ ]*\(.*\)/\1/p") $(CXXLDFLAGS)
endif
YFLAGS      := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*YFLAGS[ ]*=[ ]*\(.*\)/\1/p")
ifneq ($(CONFIG),)
YFLAGS      := $(shell cat PROJECT | sed -n "s/^[ ]*YFLAGS[ ]*=[ ]*\(.*\)/\1/p") $(YFLAGS)
endif
LEXFLAGS    := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*LEXFLAGS[ ]*=[ ]*\(.*\)/\1/p")
ifneq ($(CONFIG),)
LEXFLAGS    := $(shell cat PROJECT | sed -n "s/^[ ]*LEXFLAGS[ ]*=[ ]*\(.*\)/\1/p") $(LEXFLAGS)
endif
# biblioteka do stworzenia
LIB	    := $(shell cat PROJECT | sed -n 's/^[ ]*LIB[ ]*=[ ]*\(.*\)/\1/p')
# podkatalogi
SUBDIRS     := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*SUBDIRS[ ]*=[ ]*\(.*\)/\1/p")
ifneq ($(CONFIG),)
SUBDIRS     := $(shell cat PROJECT | sed -n 's/^[ ]*SUBDIRS[ ]*=[ ]*\(.*\)/\1/p') $(SUBDIRS)
endif
# pliki zrodlowe
YSOURCES    := $(strip $(wildcard *.y))
LEXSOURCES  := $(strip $(wildcard *.lex))
CYSOURCES   := $(subst .y,.c,$(YSOURCES))
HYSOURCES   := $(subst .y,.h,$(YSOURCES))
CLEXSOURCES := $(subst .lex,.c,$(LEXSOURCES))
CSOURCES    := $(strip $(sort $(wildcard *.c) $(CYSOURCES) $(CLEXSOURCES)))
CPPSOURCES  := $(strip $(wildcard *.cpp))

# pliki obiektowe, ktore zawieraja definicje funkcji main
ifneq ($(strip $(CSOURCES)),)
   CMAINOBJECTS := $(subst .c,.o,$(shell egrep -l 'int[ \n\t]+main[ \n\t]*' $(CSOURCES)))
else
   CMAINOBJECTS :=
endif
ifneq ($(strip $(CPPSOURCES)),)
   CPPMAINOBJECTS := $(subst .cpp,.o,$(shell egrep -l 'int[ \n\t]+main[ \n\t]*' $(CPPSOURCES)))
else
   CPPMAINOBJECTS :=
endif
MAINOBJECTS := $(CMAINOBJECTS) $(CPPMAINOBJECTS)
# pliki wykonywalne (powstaja ze zrodel zawierajacych definicje main)
CALL         := $(subst .o,,$(CMAINOBJECTS))
CPPALL        := $(subst .o,,$(CPPMAINOBJECTS))
ALL	     := $(subst .o,,$(MAINOBJECTS))
# zależności dla kazdego z plikow zrodlowych
CDEPENDS     := $(subst .c,.d,$(CSOURCES))
CPPDEPENDS     := $(subst .cpp,.d,$(CPPSOURCES))
DEPENDS := $(sort $(CDEPENDS) $(CPPDEPENDS))
# wszystkie pliki obiektowe
ALLCOBJECTS  := $(subst .c,.o,$(CSOURCES))
ALLCPPOBJECTS  := $(subst .cpp,.o,$(CPPSOURCES))
ALLOBJECTS := $(ALLCOBJECTS) $(ALLCPPOBJECTS)
# pliki obiektowe, ktore nie zawieraja definicji main
COBJECTS	    := $(filter-out $(MAINOBJECTS),$(ALLCOBJECTS))
CPPOBJECTS	    := $(filter-out $(MAINOBJECTS),$(ALLCPPOBJECTS))
OBJECTS	    := $(filter-out $(MAINOBJECTS),$(ALLOBJECTS))

CCLD	    := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*CCLD[ ]*=[ ]*\(.*\)/\1/p")
ifeq ($(CCLD),)
CCLD	    := $(shell cat PROJECT | sed -n "s/^[ ]*CCLD[ ]*=[ ]*\(.*\)/\1/p")
endif
ifeq ($(CCLD),)
ifeq ($(CPPSOURCES),)
CCLD := $(CC)
else
CCLD := $(CXX)
endif
endif

CXXLD	    := $(shell cat PROJECT | sed -n "s/^[ ]*$(CONFIG)[ _]*CXXLD[ ]*=[ ]*\(.*\)/\1/p")
ifeq ($(CXXLD),)
CXXLD	    := $(shell cat PROJECT | sed -n "s/^[ ]*CXXLD[ ]*=[ ]*\(.*\)/\1/p")
endif
ifeq ($(CXXLD),)
CXXLD := $(CXX)
endif


all: $(DEPENDS) $(OBJECTS) $(ALL) $(LIB)

depend: $(DEPENDS)

$(HYSOURCES) : %.h : %.y
	$(YACC) $(YFLAGS) -o $(subst .h,.c,$@) $<

$(CYSOURCES) : %.c : %.y
	$(YACC) $(YFLAGS) -o $@ $<

$(CLEXSOURCES) : %.c : %.lex
	$(LEX) $(LEXFLAGS) -o $@ $<

$(CDEPENDS) : %.d : %.c
	$(CC) $(CDEPFLAGS) -MM -MT $(subst .c,.o,$<) $< > $@
	printf "\t$(CC) -c $(CFLAGS) -o $(subst .c,.o,$<) $<\n" >> $@

$(CPPDEPENDS) : %.d : %.cpp
	$(CXX) $(CXXDEPFLAGS) -MM -MT $(subst .cpp,.o,$<) $< > $@
	printf "\t$(CXX) -c $(CXXFLAGS) -o $(subst .cpp,.o,$<) $<\n" >> $@

$(CALL) : % : $(ALLOBJECTS)
	$(CCLD) -o $@ $@.o $(OBJECTS) $(CLDFLAGS)

$(CPPALL) : % : $(ALLOBJECTS)
	$(CXXLD) -o $@  $@.o $(OBJECTS) $(CXXLDFLAGS)

$(LIB) : % : $(OBJECTS)
	ar sru $@ $(OBJECTS)

include $(DEPENDS)

clean:
	-rm -f $(ALL) $(ALLOBJECTS) $(DEPENDS) $(LIB) $(CYSOURCES) $(HYSOURCES) $(CLEXSOURCES)

Makefile: $(DEPENDS) .prjconfig

.prjconfig: PROJECT
	-rm -f $(ALL) $(ALLOBJECTS) $(DEPENDS)
	touch .prjconfig

ifneq ($(wildcard Makefile-include),)
include Makefile-include
endif
