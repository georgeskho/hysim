/*************************************************************************/
/*                                                                       */
/*  Copyright (c) 1994 Stanford University                               */
/*                                                                       */
/*  All rights reserved.                                                 */
/*                                                                       */
/*  Permission is given to use, copy, and modify this software for any   */
/*  non-commercial purpose as long as this copyright notice is not       */
/*  removed.  All other uses, including redistribution in whole or in    */
/*  part, are forbidden without prior written permission.                */
/*                                                                       */
/*  This software is provided with absolutely no warranty and no         */
/*  support.                                                             */
/*                                                                       */
/*************************************************************************/

/*  This file contains the declaration of the GlobalMemory 
structure and the maximum number of molecules allowed 
by the program. */

#include "barrier.h"
#include "hart.h"

struct GlobalMemory {
    LOCKDEC(IOLock)
    LOCKDEC(IndexLock)
    LOCKDEC(IntrafVirLock)
    LOCKDEC(InterfVirLock)
    LOCKDEC(KinetiSumLock)
    LOCKDEC(PotengSumLock)
    BARDEC(start)
    BARDEC(InterfBar)
    BARDEC(PotengBar)
    BARDEC(done_barrier)
    int Index;
    double VIR;
    double SUM[3];
    double POTA, POTR, POTRF;
    unsigned long createstart,createend,computestart,computeend;
    unsigned long trackstart, trackend, tracktime;
    unsigned long intrastart, intraend, intratime;
    unsigned long interstart, interend, intertime;
};

extern struct GlobalMemory *gl;

