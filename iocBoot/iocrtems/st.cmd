## RTEMS GTR test startup script

< envPaths

## Register all support components
cd ${TOP}
dbLoadDatabase("dbd/testGtr.dbd",0,0)
testGtr_registerRecordDeviceDriver(pdbbase) 

## Load record instances
cd ${TOP}/iocBoot/${IOC}
#< sis3300
< sis3301

iocInit()

dbpf sis3301arm 1
dbpf sis3301autoRestart 1
dbpf sis3301numberPTS 251
dbpf sis3301softTrigger.SCAN ".5 second"
