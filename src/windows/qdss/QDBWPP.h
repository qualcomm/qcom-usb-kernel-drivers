/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                            Q D B W P P. H

GENERAL DESCRIPTION
    This module contains the following:

    - The provider GUID for the driver.
    - Macros for tracing with levels and flags
    - Tracing enumerations using custom type
    - Trace macro that incorporates PRE/POST macros

  Copyright (c) 2020 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifdef EVENT_TRACING

#ifndef QDBWPP_H
#define QDBWPP_H


//
// Software Tracing Definitions 
//
// NOTE: these bits are shared amongst all components so must be kept in sync
// {5F02CA82-B28B-4a95-BA2C-FBED52DDD3DC}
#define WPP_CONTROL_GUIDS \
        WPP_DEFINE_CONTROL_GUID(QCUSB,( 5F02CA82, B28B, 4a95, BA2C, FBED52DDD3DC),  \
        WPP_DEFINE_BIT(WPP_DRV_MASK_CONTROL)              \
        WPP_DEFINE_BIT(WPP_DRV_MASK_READ)                 \
        WPP_DEFINE_BIT(WPP_DRV_MASK_WRITE)                \
        WPP_DEFINE_BIT(WPP_DRV_MASK_ENCAP)                \
        WPP_DEFINE_BIT(WPP_DRV_MASK_POWER)                \
        WPP_DEFINE_BIT(WPP_DRV_MASK_STATE)                \
        WPP_DEFINE_BIT(WPP_DRV_MASK_RDATA)                \
        WPP_DEFINE_BIT(WPP_DRV_MASK_TDATA)                \
        WPP_DEFINE_BIT(WPP_DRV_MASK_ENDAT)                \
        WPP_DEFINE_BIT(WPP_DRV_MASK_RIRP)                 \
        WPP_DEFINE_BIT(WPP_DRV_MASK_WIRP)                 \
        WPP_DEFINE_BIT(WPP_DRV_MASK_CIRP)                 \
        WPP_DEFINE_BIT(WPP_DRV_MASK_PIRP)                 \
        WPP_DEFINE_BIT(WPP_DRV_MASK_PROTOCOL)             \
        WPP_DEFINE_BIT(WPP_DRV_MASK_MCONTROL)             \
        WPP_DEFINE_BIT(WPP_DRV_MASK_QOS)	          \
	WPP_DEFINE_BIT(WPP_DRV_MASK_DATA_QOS)		  \
        WPP_DEFINE_BIT(WPP_DRV_MASK_DATA_WT)		  \
        WPP_DEFINE_BIT(WPP_DRV_MASK_DATA_RD)		  \
        WPP_DEFINE_BIT(WPP_DRV_MASK_FILTER)               \
        WPP_DEFINE_BIT(WPP_DRV_MASK_UNUSED1)              \
        WPP_DEFINE_BIT(WPP_DRV_MASK_UNUSED2)              \
        WPP_DEFINE_BIT(WPP_DRV_MASK_UNUSED3)              \
        WPP_DEFINE_BIT(WPP_DRV_MASK_UNUSED4)              \
        WPP_DEFINE_BIT(WPP_DRV_MASK_UNUSED5)              \
        WPP_DEFINE_BIT(WPP_DRV_MASK_UNUSED6)              \
        WPP_DEFINE_BIT(WPP_DRV_MASK_UNUSED7)              \
        WPP_DEFINE_BIT(WPP_APP_MASK_UNUSED1)              \
        WPP_DEFINE_BIT(WPP_APP_MASK_UNUSED2)              \
        WPP_DEFINE_BIT(WPP_APP_MASK_UNUSED3)              \
        WPP_DEFINE_BIT(WPP_APP_MASK_UNUSED4))

#define WPP_LEVEL_FORCE    0x0
#define WPP_LEVEL_CRITICAL 0x1
#define WPP_LEVEL_ERROR    0x2
#define WPP_LEVEL_INFO     0x3
#define WPP_LEVEL_DETAIL   0x4
#define WPP_LEVEL_TRACE    0x5
#define WPP_LEVEL_VERBOSE  0x6


#define QCWPP_USER_LEVEL(flags)      WPP_CONTROL(WPP_BIT_ ## flags).Level
#define QCWPP_USER_FLAGS(flags)      WPP_CONTROL(WPP_BIT_ ## flags).Flags[0]

//MACRO: KdPrint
//
//begin_wpp config
//FUNC KdPrint((MSG,...));
//end_wpp

//MACRO: DbgPrint
//
//begin_wpp config
//FUNC DbgPrint(MSG,...);
//end_wpp


//
// QDB_DbgPrint is a custom macro that adds support for levels to the 
// default DoTraceMessage, which supports only flags. In this version, both
// flags and level are conditions for generating the trace message. 
// The preprocessor is told to recognize the function by using the -func argument
// in the RUN_WPP line on the source file. In the source file you will find
// -func:DoTraceLevelMessage(LEVEL,FLAGS,MSG,...). The conditions for triggering 
// this event in the macro are the Levels defined above and the flags 
// defined above and are evaluated by the macro WPP_LEVEL_FLAGS_ENABLED below. 
// 

//MACRO: QDB_DbgPrint
//
//begin_wpp config
//FUNC QDB_DbgPrint(FLAG,LEVEL,(MSG,...));
//end_wpp

#define WPP_FLAG_LEVEL_LOGGER(flag,level) WPP_LEVEL_LOGGER(WPP_DRV_MASK_CONTROL)
#define WPP_FLAG_LEVEL_ENABLED(flag,level) TRUE
#define WPP_FLAG_LEVEL_PRE(flag,level) { \
   if ((pDevContext->DebugMask !=0) && (QCWPP_USER_FLAGS(WPP_DRV_MASK_CONTROL) & (flag)) && (QCWPP_USER_LEVEL(WPP_DRV_MASK_CONTROL) >= level)) {
#define WPP_FLAG_LEVEL_POST(flag,level) ; } }


//
// QDB_DbgPrintG is a custom macro that adds support for levels to the 
// default DoTraceMessage, which supports only flags. In this version, both
// flags and level are conditions for generating the trace message. 
// The preprocessor is told to recognize the function by using the -func argument
// in the RUN_WPP line on the source file. In the source file you will find
// -func:DoTraceLevelMessage(LEVEL,FLAGS,MSG,...). The conditions for triggering 
// this event in the macro are the Levels defined above and the flags 
// defined above and are evaluated by the macro WPP_LEVEL_FLAGS_ENABLED below. 
// 

//MACRO: QDB_DbgPrintG
//
//begin_wpp config
//FUNC QDB_DbgPrintG(GFLAG,GLEVEL,(MSG,...));
//end_wpp

#define WPP_GFLAG_GLEVEL_LOGGER(gflag,glevel) WPP_LEVEL_LOGGER(WPP_DRV_MASK_CONTROL)
#define WPP_GFLAG_GLEVEL_ENABLED(gflag,glevel) TRUE
#define WPP_GFLAG_GLEVEL_PRE(gflag,glevel) { \
   if( (QCWPP_USER_FLAGS(WPP_DRV_MASK_CONTROL) & (gflag)) && (QCWPP_USER_LEVEL(WPP_DRV_MASK_CONTROL) >= glevel ) ) {
#define WPP_GFLAG_GLEVEL_POST(gflag,glevel) ; } }

#endif // QDBWPP_H

#endif //EVENT_TRACING

