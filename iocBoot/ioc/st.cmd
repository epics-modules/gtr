# Example vxWorks startup file

# Following must be added for many board support packages
#cd <full path to target bin directory>

< cdCommands

cd topbin
ld < iocCore
ld < exampleLib

cd top
dbLoadDatabase("dbd/testGtr.dbd")
testGtr_registerRecordDeviceDriver(pdbbase) 

cd startup
< vtr10010
< vtr1012
< vtr10012
< vtr10012_8
< vtr8014
< vtr812
< sis3300
< sis3301

iocInit()
