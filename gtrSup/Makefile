#*************************************************************************
# Copyright (c) 2002 The University of Chicago, as Operator of Argonne
#     National Laboratory.
# Copyright (c) 2002 The Regents of the University of California, as
#     Operator of Los Alamos National Laboratory.
# EPICS BASE Versions 3.13.7
# and higher are distributed subject to a Software License Agreement found
# in file LICENSE that is included with this distribution. 
#*************************************************************************
TOP = ..
include $(TOP)/configure/CONFIG

GTRSUP = $(TOP)/gtrSup
#USR_CFLAGS += -DDEBUG
#USR_CFLAGS += -DHAS_UNIVERSEDMA

LIBRARY_IOC += gtr
gtr_LIBS += $(EPICS_BASE_IOC_LIBS)

SRC_DIRS += $(GTRSUP)/gtr
INC += drvGtr.h
INC += epicsDma.h
SRCS += devGtr.c drvGtr.c
VME_ONLY_SRCS += epicsDma.c 
DBD += gtr.dbd

SRC_DIRS += $(GTRSUP)/sisfadc
VME_ONLY_SRCS += drvSisfadc.c idrom.c
DBD += drvSisfadc.dbd

SRC_DIRS += $(GTRSUP)/vtr1012
VME_ONLY_SRCS += drvVtr1012.c
DBD += drvVtr1012.dbd

SRC_DIRS += $(GTRSUP)/vtr10010
VME_ONLY_SRCS += drvVtr10010.c
DBD += drvVtr10010.dbd

SRC_DIRS += $(GTRSUP)/vtr10012
VME_ONLY_SRCS += drvVtr10012.c
DBD += drvVtr10012.dbd

SRC_DIRS += $(GTRSUP)/vtr812
VME_ONLY_SRCS += drvVtr812.c
DBD += drvVtr812.dbd



SRCS_RTEMS-mvme2100 += $(VME_ONLY_SRCS)
SRCS_RTEMS-mvme5500 += $(VME_ONLY_SRCS)
SRCS_vxWorks        += $(VME_ONLY_SRCS)
SRCS += $(SRCS_$(T_A))


include $(TOP)/configure/RULES
