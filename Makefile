# Makefile for Asyn gtr support
#
# Created by norume on Fri Oct 29 13:59:30 2004
# Based on the Asyn top template

TOP = .
include $(TOP)/configure/CONFIG

DIRS := configure
DIRS += $(wildcard *[Ss]up)
DIRS += $(wildcard *[Aa]pp)
DIRS += $(wildcard ioc[Bb]oot)

include $(TOP)/configure/RULES_TOP
