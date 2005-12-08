TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# build an ioc application

GTRSUP = $(TOP)
GTRSUP_LIB = $(TOP)/lib/$(T_A)
GTRSUP_BIN = $(TOP)/bin/$(T_A)

PROD_IOC_vxWorks = testGtr
PROD_IOC_RTEMS-mvme2100 = testGtr
PROD_IOC_RTEMS-mvme5500 = testGtr
PROD += $(PROD_IOC_$(T_A))

# testGtr.dbd will be made up from these files:
DBD += testGtr.dbd
testGtr_DBD += base.dbd

#include definitions for any other support applications needed
testGtr_DBD += drvVtr1012.dbd
testGtr_DBD += drvVtr10010.dbd
testGtr_DBD += drvVtr10012.dbd
testGtr_DBD += drvVtr812.dbd
testGtr_DBD += drvSisfadc.dbd

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testGtr_SRCS += testGtr_registerRecordDeviceDriver.cpp
testGtr_SRCS_DEFAULT += testGtrMain.cpp
testGtr_SRCS_vxWorks += -nil-


#The following adds support from base/src/vxWorks
testGtr_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

#Support applications used by this application
testGtr_LIBS += gtr
testGtr_LIBS += $(EPICS_BASE_IOC_LIBS)

#Libraries needed by particular OS classes
OP_SYS_LDLIBS_RTEMS += -lbspExt
#OP_SYS_LDLIBS_RTEMS += -ldrvUniverseDma
OP_SYS_LDLIBS += $(OP_SYS_LDLIBS_$(OS_CLASS))

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

