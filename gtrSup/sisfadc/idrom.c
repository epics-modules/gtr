/*idrom.c */
/* adapted from code supplied by struck*/

/* Author:   Marty Kraimer */
/* Date:     06NOV2002     */

/*************************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of California, as
* Operator of Los Alamos National Laboratory. EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <epicsThread.h>

#include "drvSisfadc.h"

#define SIS3300_ONE_WIRE                0x60    /* offset address*/

/* ROM Functions */
#define ONE_WIRE_READ_ROM_CMD           0x33    /* Read ROM       */
#define ONE_WIRE_SKIP_ROM_CMD           0xCC    /* Skip ROM        */
#define ONE_WIRE_SEARCH_ROM_CMD         0xF0    /* Search ROM */

/* MEMORY Functions */
#define ONE_WIRE_WRITE_SCRATCHPAD_CMD   0x0F    /* Write Scratchpad*/
#define ONE_WIRE_READ_SCRATCHPAD_CMD    0xAA    /* Read Scratchpad*/
#define ONE_WIRE_COPY_SCRATCHPAD_CMD    0x55    /* Copy scratchpad*/
#define ONE_WIRE_READ_MEMORY_CMD        0xF0    /* Read Memory*/
#define ONE_WIRE_WR_APPL_REG_CMD        0x99    /* write Application Register*/
#define ONE_WIRE_RD_STATUS_REG_CMD      0x66    /* Read Status Register*/
#define ONE_WIRE_RD_APPL_REG_CMD        0xC3    /* Read Application Register*/
#define ONE_WIRE_COPY_LOCK_APPL_CMD     0x5A    /* Copy & Lock Application Register*/

#define ONE_WIRE_VALIDATION_KEY_CMD     0xA5    /*    */

#define ONE_WIRE_RESET                  0x8000  /* Reset One Wire*/
#define ONE_WIRE_WR_CMD                 0x4000  /* Write command*/
#define ONE_WIRE_RD_CMD                 0x2000  /* Read command */

#define ONE_WIRE_BUSY_BIT               0x8000  /* BUSY bit*/
#define ONE_WIRE_PRESENCE_BIT           0x4000  /* Precence bit*/


#define STATIC
STATIC int idromRead (char *vme_base, unsigned int *read_data);
STATIC int idromWrite(char *vme_base,  unsigned int command);
STATIC int idromReset(char *vme_base);

STATIC int idromRead (char *vme_base, unsigned int *read_data)
{
    volatile unsigned int *addr;
    unsigned int data;

    addr = (unsigned int *)(vme_base + SIS3300_ONE_WIRE);
    epicsThreadSleep(0.01);
    *addr = ONE_WIRE_RD_CMD;
    epicsThreadSleep(0.01);
    data = *addr;
    if((data&ONE_WIRE_BUSY_BIT) == ONE_WIRE_BUSY_BIT) return(-1);
    *read_data = data;
    return(0);
}

STATIC int idromWrite(char *vme_base,  unsigned int command)
{
    volatile unsigned int *addr;
    unsigned int data;

    addr = (unsigned int *)(vme_base + SIS3300_ONE_WIRE);
    epicsThreadSleep(0.01);
    *addr = command;
    epicsThreadSleep(0.01);
    data = *addr;
    if((data&ONE_WIRE_BUSY_BIT) == ONE_WIRE_BUSY_BIT) return(-1);
    return(0);
}

STATIC int idromReset(char *vme_base)
{
    volatile unsigned int *addr;
    unsigned int data;

    addr = (unsigned int *)(vme_base + SIS3300_ONE_WIRE);
    epicsThreadSleep(0.01);
    *addr = ONE_WIRE_RESET;
    epicsThreadSleep(0.01);
    data = *addr;
    if((data&ONE_WIRE_PRESENCE_BIT) != ONE_WIRE_PRESENCE_BIT) return(-1);
    return(0);
}

int idromGetID(char *vme_base, unsigned short *modId,
    unsigned short *clockSpeed,unsigned long *serialNumber)
{
    unsigned int data;
    unsigned short id,speed;
    unsigned long sn;

    if(idromReset(vme_base)) {
        printf("idromReset failed\n");
        return(-1);
    }
    if(idromWrite(vme_base, ONE_WIRE_WR_CMD + ONE_WIRE_SKIP_ROM_CMD)) {
        printf("idromWrite failed\n");
        return(-1);
    }
    if(idromWrite(vme_base, ONE_WIRE_WR_CMD + ONE_WIRE_RD_APPL_REG_CMD)) {
        printf("idromWrite failed\n");
        return(-1);
    }
    if(idromWrite(vme_base, ONE_WIRE_WR_CMD + 0)) {/*memory address*/
        printf("idromWrite failed\n");
        return(-1);
    }
    if(idromRead(vme_base, &data)){printf("idromRead failed\n"); return(-1); }
    id = ((data&0xf0)>>4)*1000 + (data&0x0f)*100;;
    if(idromRead(vme_base, &data)){printf("idromRead failed\n"); return(-1); }
    id += ((data&0xf0)>>4)*10+ (data&0x0f);
    if(idromRead(vme_base, &data)){printf("idromRead failed\n"); return(-1); }
    speed = ((data&0xf0)>>4)*1000 + (data&0x0f)*100;;
    if(idromRead(vme_base, &data)){printf("idromRead failed\n"); return(-1); }
    speed += ((data&0xf0)>>4)*10+ (data&0x0f);
    if(idromRead(vme_base, &data)){printf("idromRead failed\n"); return(-1); }
    sn = (data&0xff)*1000;
    if(idromRead(vme_base, &data)){printf("idromRead failed\n"); return(-1); }
    sn += (data&0xff)*100;
    if(idromRead(vme_base, &data)){printf("idromRead failed\n"); return(-1); }
    sn += (data&0xff)*10;
    if(idromRead(vme_base, &data)){printf("idromRead failed\n"); return(-1); }
    sn += (data&0xff);
   *modId = id;
   *clockSpeed = speed;
   *serialNumber = sn;
    return(0);
}
