# Makefile
TOP = ..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
TARGET_CFLAGS += $(CFLAGS_$(OS_CLASS)) $(CFLAGS_$(T_A))

############################################
#  Configuration Options
############################################

# ugly hack; windRiver's ccxxx does not define __vxworks
# on some targets :-(
CFLAGS_vxWorks += -D__vxworks

INC += drvUniverseDma.h

LIBRARY_RTEMS += drvUniverseDma

SRCS_RTEMS-mvme5500 += drvUniverseDma.c
SRCS_vxWorks        += drvUniverseDma.c
SRCS += $(SRCS_$(T_A))


include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
