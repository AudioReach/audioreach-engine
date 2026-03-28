/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*===========================================================================*]
[* FILE NAME: audio_filter.c                                                 *]
[* DESCRIPTION:                                                              *]
[*    Basic filters for audio processing                                     *]
[* FUNCTION LIST :                                                           *]
[*    biquad_setup: prepare L16Q13 biquad coeffs from L32Q23 values          *]
[*    biquad_reset: Clean biquad filter history and init accum               *]
[*    biquad_process: Process a frame of samples with biquad filter          *]
[*    biquad_process_io: process samples with biquad (non-inplace version)   *]
[*    multiStageBiquad_setup: prepare biquads in a multi stage biquad struct *]
[*    multiStageBiquad_reset: clean histories and init acum values           *]
[*    multiStageBiquad_process: process samples with multi stage biquad      *]
[*    fir_reset: reset FIR filter memory and index                           *]
[*    fir_process: process samples wiht FIR filter                           *]         *]
[*===========================================================================*/
#include "audio_dsp.h"
#include <string.h>

#include <stdlib.h>
#include <stdio.h>
#include <stringl.h>

#if ((defined __hexagon__) || (defined __qdsp6__))

#ifdef __cplusplus
extern "C"
#else
extern
#endif /* __cplusplus */
void IIR_Biquad(int16 *xin, int16 *pCoef, int16 *pState, int32 pCoef0, int32 nsamples, int32 *yL32 );

#ifdef __cplusplus
extern "C"
#else
extern
#endif /* __cplusplus */
void IIR_Biquad_io(int16 *xin, int16 *pCoef, int16 *pState, int32 pCoef0, int32 nsamples, int32 *yL32, int16 *out);

#ifdef __cplusplus
extern "C"
#else
extern
#endif /* __cplusplus */
void IIR_one_pole
(
    int16           *inplaceBuf,        /* inplace buffer                    */
    int32           *yL40,
    int32           c0L16Q14,
    int32           c1MinusOneL16Q14,
    int16           samples            /* number of sample to process       */
);

#ifdef __cplusplus
extern "C"
#else
extern
#endif /* __cplusplus */
void fir_c16xd32
(
		int32 *memPtr,
		int16 *reverse_coeff,
		int nInputProcSize,
		int address,
		int32 *outPtr
);

#ifdef __cplusplus
extern "C"
#else
extern
#endif
void tuning_filter_fir
(
   int16 *xin,                         /* FIR Filter input                        */
   int16 *coefs,                          /* FIR Filter coeffs                    */
   int32 taps,                            /* Number of filter Taps                */
   int32 length,                          /* Length of input (block)              */
   int32 Qshift,                          /* Q factor of filter coeffs         */
   int16 *yout                               /* FIR Filter output                     */
);

#ifdef __cplusplus
extern "C"
#else
extern
#endif
void tuning_filter_fir_rnd
(
   int16 *xin,                         /* FIR Filter input                        */
   int16 *coefs,                          /* FIR Filter coeffs                    */
   int32 taps,                            /* Number of filter Taps                */
   int32 length,                          /* Length of input (block)              */
   int32 Qshift,                          /* Q factor of filter coeffs         */
   int16 *yout                               /* FIR Filter output                     */
);

#endif


/*===========================================================================*/
/* FUNCTION : simple_lp_reset                                                  */
/*                                                                           */
/* DESCRIPTION: Clean one-pole filter history and initialize accumulator     */
/*                                                                           */
/* INPUTS: filter-> one-pole filter struct                                   */
/* OUTPUTS: filter history set to zeros and accumulator set to proper value. */
/*                                                                           */
/* IMPLEMENTATION NOTES:                                                     */
/*===========================================================================*/
void simple_lp_reset
(
    simpleLPFilt    *filter            /* one-pole filter struct            */
)
{
    /* clean filter memory y(n-1), L16 and L32 */
    filter->yL32 = 0;
} /*-------------------- end of function simple_lp_reset ----------------------*/

#ifndef __qdsp6__
void IIR_one_pole
(
    int16           *inplaceBuf,        /* inplace buffer                    */
    int32           *yL40,
    int32           c0L16Q14,
    int32           c1MinusOneL16Q14,
    int16           samples            /* number of sample to process       */
)
{
    int16 yL16;            // L16 version of y(n-1)
    int16 i;

    int40 tempL40 = *yL40;

    for (i = 0; i < samples; i++)
    {
        /*-- get y(n-1) -- */
        yL16 = s16_extract_s32_h(s32_saturate_s40(tempL40));
        /*-- y(n-1) + c0*x(n) --*/
        tempL40 = s40_mac_s40_s16_s16_shift(tempL40, c0L16Q14, *inplaceBuf, 2);
        /*-- y(n-1) + c0*x(n) + (c1-1) * y(n-1) --*/
        tempL40 = s40_mac_s40_s16_s16_shift(tempL40, c1MinusOneL16Q14, yL16, 2);
        /*-- (accum has << 2, then extract high), equivalent to (>> 14) --*/
        *inplaceBuf++ = s16_extract_s32_h(s32_saturate_s40(tempL40));
    } 

    *yL40 = tempL40;
}
#endif

/*===========================================================================*/
/* FUNCTION : simple_lp_proc                                                */
/*                                                                           */
/* DESCRIPTION: Process a frames of samples with a one-pole filter           */
/*                                                                           */
/* INPUTS: inplaceBuf-> inplace buffer for input and output                  */
/*         filter-> one-pole filter struct                                   */
/*         samples: number of samples to process                             */
/* OUTPUTS: inplaceBuf-> inplace buffer                                      */
/*                                                                           */
/* IMPLEMENTATION NOTES:                                                     */
/*---------------------------------------------------------------------------*/
/*                                                                           */
/*                c0                                                         */
/*    H(z) =  ----------                                                     */
/*            1-c1*z**-1                                                     */
/*                                                                           */
/*  filter difference equation:                                              */
/*                                                                           */
/*    y(n) = c0*x(n) + c1*y(n-1)                                             */
/*         = c0*x(n) +(c1-1)*y(n-1) + y(n-1)                                 */
/*                                                                           */
/*  set one coeff as (c1-1) and add 32 bit version of y(n-1) to              */
/*  prevent accumulation of round-off errors                                 */
/*===========================================================================*/


void simple_lp_proc
(
    int16           *inplaceBuf,        /* inplace buffer                    */
    simpleLPFilt   *filter,            /* one-pole filter struct            */
    int16            samples            /* number of sample to process       */
)
{
    IIR_one_pole(inplaceBuf, &filter->yL32, (int32)(filter->c0L16Q14), (int32)(filter->c1MinusOneL16Q14), samples);
} /*------------------ end of function simple_lp_proc ----------------------*/



/*===========================================================================*/
/* FUNCTION : biquad_setup                                                   */
/*                                                                           */
/* DESCRIPTION: Convert 32 bit values into 16 bit and setup biquad coeffs.   */
/*                                                                           */
/* INPUTS: filter-> biquad filter struct                                     */
/*         coeffs[5]: 32 bit coefficients                                    */
/* OUTPUTS: filter->coeffsL16Q13: L16Q13 biquad coefficients                 */
/*                                                                           */
/* IMPLEMENTATION NOTES:                                                     */
/*      coeffsL16Q13[2] is setup as -b1-1 instead of -b1                     */
/*===========================================================================*/
void biquad_setup
(
    biquadFilter    *filter,            /* biquad filter struct              */
    const int32      coeffs[5]          /* 32 bit filter coeffs              */
)
{
#if ((defined __hexagon__) || (defined __qdsp6__))
    
    int32 tmpL32; 
    int16 *c, *coeffptr;
    uint32 address;
    
     address     = (uint32)filter->coeffsL16Q13;
     address     = address & 0xFFFFFFF8;
     address     = address + 0x8;
     coeffptr        = (int16 *) address;
     filter->coeffIndex = (int16)(coeffptr - filter->coeffsL16Q13);
     c = &filter->coeffsL16Q13[filter->coeffIndex];
     
     /* setup coeffs for each biquad, convert 32 bit to 16 bit */
      /* coeffsL16Q13[0] = a0 */
      tmpL32 = Q6_R_asl_RR_sat(Q6_R_add_RR_sat(Q6_R_asl_RR_sat(coeffs[0], -9),1),-1);
      filter->coeff0  = s16_extract_s32_l(tmpL32);
          
      /* coeffsL16Q13[1] = -b2 */
      tmpL32 = Q6_R_asl_RR_sat(Q6_R_add_RR_sat(Q6_R_asl_RR_sat(coeffs[1], -9),1),-1);
      c[3] = s16_extract_s32_l(tmpL32);

     /* coeffsL16Q13[2] = -b1-1 */
     tmpL32 = s32_sub_s32_s32(coeffs[2], Q23_ONE);
     tmpL32 = Q6_R_asl_RR_sat(Q6_R_add_RR_sat(Q6_R_asl_RR_sat(tmpL32, -9),1),-1);
       c[2] = s16_extract_s32_l(tmpL32); 

     /* coeffsL16Q13[3] = a2 */
     tmpL32 = Q6_R_asl_RR_sat(Q6_R_add_RR_sat(Q6_R_asl_RR_sat(coeffs[3], -9),1),-1);
       c[1] = s16_extract_s32_l(tmpL32); 

     /* coeffsL16Q13[4] = a1 */
     tmpL32 = Q6_R_asl_RR_sat(Q6_R_add_RR_sat(Q6_R_asl_RR_sat(coeffs[4], -9),1),-1);
     c[0] = s16_extract_s32_l(tmpL32);

#else
     
    int32 tmpL32;
    int16 *c = filter->coeffsL16Q13;
     /* coeffsL16Q13[0] = a0 */
    tmpL32 = s32_shl_s32_rnd_sat(coeffs[0], -10);
    c[0] = s16_extract_s32_l(tmpL32);

    /* coeffsL16Q13[1] = -b2 */
    tmpL32 = s32_shl_s32_rnd_sat(coeffs[1], -10);
    c[1] = s16_extract_s32_l(tmpL32);

    /* coeffsL16Q13[2] = -b1-1 */
    tmpL32 = s32_sub_s32_s32(coeffs[2], Q23_ONE);
    tmpL32 = s32_shl_s32_rnd_sat(tmpL32, -10);
    c[2] = s16_extract_s32_l(tmpL32);

    /* coeffsL16Q13[3] = a2 */
    tmpL32 = s32_shl_s32_rnd_sat(coeffs[3], -10);
    c[3] = s16_extract_s32_l(tmpL32);

    /* coeffsL16Q13[4] = a1 */
    tmpL32 = s32_shl_s32_rnd_sat(coeffs[4], -10);
    c[4] = s16_extract_s32_l(tmpL32);
#endif
}  /*--------------------- end of function biquad_setup ---------------------*/


/*===========================================================================*/
/* FUNCTION : biquad_reset                                                   */
/*                                                                           */
/* DESCRIPTION: Clean biquad history and initialize accumulator              */
/*                                                                           */
/* INPUTS: filter-> biquad filter struct                                     */
/* OUTPUTS: filter history set to zeros and accumulator set to proper value. */
/*                                                                           */
/* IMPLEMENTATION NOTES:                                                     */
/*===========================================================================*/
void biquad_reset
(
    biquadFilter    *filter             /* biquad filter struct              */
)
{
    
#if ((defined __hexagon__) || (defined __qdsp6__))
   int16 i, *stateptr;
   uint32 address;
    
    filter->memIndex = 0;
    address     = (uint32)filter->states;
    address     = address & 0xFFFFFFF8;
    address     = address + 0x8;
    stateptr        = (int16 *) address;
    filter->memIndex = (int16)(stateptr - filter->states);
    
    for (i = 0; i < 4; i++)
    {
        filter->states[filter->memIndex + i] = 0;
    } 
    filter->yL32 = Q13_HALF;
    
    
#else
    /* clean filter histories */
    filter->xL16[0] = 0;
    filter->xL16[1] = 0;
    filter->yL16[0] = 0;
    filter->yL16[1] = 0;

    /* initialize yL40 with the value of one half, for rounding */
    filter->yL32 = Q13_HALF;            // 0.5 in Q13


#endif
}  /*------------------------ end of function biquad_reset ------------------*/


/*===========================================================================*/
/* FUNCTION : biquad_process                                                 */
/*                                                                           */
/* DESCRIPTION: Process a number of frames of samples with a biquad          */
/*                                                                           */
/* INPUTS: inplaceBuf-> inplace buffer for input and output                  */
/*         filter-> biquad filter struct                                     */
/*         samples: number of samples to process                             */
/* OUTPUTS: inplaceBuf-> inplace buffer                                      */
/*                                                                           */
/* IMPLEMENTATION NOTES:                                                     */
/*---------------------------------------------------------------------------*/
/*                                                                           */
/*            a0 + a1*z**-1+ a2*z**-2                                        */
/*    H(z) =  -----------------------                                        */
/*            1 + b1*z**-1 + b2*z**-2                                        */
/*                                                                           */
/*  filter difference equation:                                              */
/*                                                                           */
/*    y(n) = a0*x(n) +  a1*x(n-1) + a2*x(n-2) - b1*y(n-1) - b2*y(n-2)        */
/*         = a0*x(n) +  a1*x(n-1) + a2*x(n-2)                                */
/*                                 y(n-1) + (-b1-1)*y(n-1) - b2*y(n-2)       */
/*                                                                           */
/*  set one coeff as (-b1-1) and s16_add_s16_s16 32 bit version of y(n-1) to             */
/*  prevent accumulation of round-off errors                                 */
/*===========================================================================*/
void biquad_process
(
    int16           *inplaceBuf,        /* inplace buffer                    */
    biquadFilter    *filter,            /* biquad filter struct              */
    int32            samples            /* number of samples to process      */
)
{
    
#if ((defined __hexagon__) || (defined __qdsp6__))
    
    int16 *bufPtr = inplaceBuf;
    int16 *coeffptr = &filter->coeffsL16Q13[filter->coeffIndex];
    int16 *stateptr    = &filter->states[filter->memIndex];
    IIR_Biquad(bufPtr,coeffptr, stateptr, (int)filter->coeff0 , samples, &filter->yL32);



#else 

          int32 i;
          int16 *yL16 = filter->yL16;
          int16 *xL16 = filter->xL16; 
          int16 *bufPtr = inplaceBuf;
          int16 *coeffs = filter->coeffsL16Q13;
        
        int32 yL40 = filter->yL32;

    if (coeffs[3]==0 && coeffs[4]==0)  
    {   /*-------------- all-pole version --------------*/
    for (i = 0; i < samples; i++)
        {
            /*-- y(n-1)+a0*x(n) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[0], *bufPtr, 0);
            /*-- y(n-1)+a0*x(n)+(-b2)*y(n-2) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[1], yL16[0], 0);
            /*-- y(n-1)+a0*x(n)+(-b2)*y(n-2)+(-b1-1)*y(n-1) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[2], yL16[1], 0);
            /*-- output --*/
            *bufPtr = s16_extract_s32_h(s32_saturate_s40(s40_shl_s40(yL40, 3)));

            /*-- update filter memories --*/
            /*-- y(n-2) = y(n-1), y(n-1) = y --*/
            yL16[0] = yL16[1], yL16[1] = *bufPtr++;
        }
    }   
    else 
    {   /*---------------- normal version ---------------*/
        for (i = 0; i < samples; i++)
        {
            /*-- y(n-1)+a0*x(n) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[0], *bufPtr, 0);
            /*-- y(n-1)+a0*x(n)+(-b2)*y(n-2) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[1], yL16[0], 0);
            /*-- y(n-1)+a0*x(n)+(-b2)*y(n-2)+(-b1-1)*y(n-1) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[2], yL16[1], 0);
            /*-- y(n-1)+a0*x(n)+(-b2)*y(n-2)+(-b1-1)*y(n-1)+a2*x(n-2) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[3], xL16[0], 0);
            /* y(n-1)+a0*x(n)+(-b2)*y(n-2)+(-b1-1)*y(n-1)+a2*x(n-2)+a1*x(n-1)*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[4], xL16[1], 0); 
 
            /*--- update input memory ---*/
            /*-- x(n-2) = x(n-1), x(n-1)= x --*/
            xL16[0] = xL16[1], xL16[1] = *bufPtr; 
            
            /*-- output y --*/
            *bufPtr = s16_extract_s32_h(s32_saturate_s40(s40_shl_s40(yL40, 3)));
     
            /*-- update output memories --*/
            /*-- y(n-2) = y(n-1), y(n-1) = y --*/
            yL16[0] = yL16[1], yL16[1] = *bufPtr++;
    
        }
    }

    /*-- store accumulator value back --*/
    filter->yL32 = s32_extract_s40_l(yL40);
#endif
}  /*------------------ end of function biquad_process ----------------------*/


/*===========================================================================*/
/* FUNCTION : biquad_process_io                                              */
/*                                                                           */
/* DESCRIPTION: Process a number of frames of samples with a biquad with     */
/*              an input buffer and an output buffer.                        */
/*                                                                           */
/* INPUTS: destBuf-> output buffer                                           */
/*         srcBuf-> input buffer                                             */
/*         filter-> biquad filter struct                                     */
/*         samples: number of samples to process                             */
/* OUTPUTS: destBuf-> output buffer                                          */
/*                                                                           */
/* IMPLEMENTATION NOTES:                                                     */
/*===========================================================================*/
void biquad_process_io
(
    int16           *destBuf,           /* output buffer                     */
    int16           *srcBuf,            /* input buffer                      */
    biquadFilter    *filter,            /* biquad filter struct              */
    int32            samples            /* number of sample to process       */
)
{
    
#if ((defined __hexagon__) || (defined __qdsp6__))
    
    int16 *bufPtr = srcBuf;
    int16 *coeffptr = &filter->coeffsL16Q13[filter->coeffIndex];
    int16 *stateptr    = &filter->states[filter->memIndex];
    IIR_Biquad_io(bufPtr,coeffptr, stateptr, (int)filter->coeff0 , samples, &filter->yL32, destBuf);



#else
    int32 i;
    int16 xInL16;
    int16 *yL16 = filter->yL16;
    int16 *xL16 = filter->xL16; 
    int16 *destPtr = destBuf;
    int16 *srcPtr = srcBuf;
    int16 *coeffs = filter->coeffsL16Q13;
    int40 yL40 = filter->yL32;


    /*-----------------------------------------------------------------------*/
    
    if (coeffs[3]==0 && coeffs[4]==0)  
    {   /*-------------- all-pole version --------------*/
        for (i = 0; i < samples; i++)
        {
            /*-- x(n) --*/
            xInL16 = *srcPtr++;
            /*-- y(n-1)+a0*x(n) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[0], xInL16,  0);
            /*-- y(n-1)+a0*x(n)+(-b2)*y(n-2) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[1], yL16[0], 0);
            /*-- y(n-1)+a0*x(n)+(-b2)*y(n-2)+(-b1-1)*y(n-1) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[2], yL16[1], 0);
            /*-- output --*/
            *destPtr = s16_extract_s32_h(s32_saturate_s40(s40_shl_s40(yL40, 3)));

            /*-- update filter memories --*/
            /*-- y(n-2) = y(n-1), y(n-1) = y --*/
            yL16[0] = yL16[1], yL16[1] = *destPtr++;
        }
    }   
    else 
    {   /*---------------- normal version ---------------*/
        for (i = 0; i < samples; i++)
        {
            /*-- x(n) --*/
            xInL16 = *srcPtr++;
            /*-- y(n-1)+a0*x(n) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[0], xInL16,  0);
            /*-- y(n-1)+a0*x(n)+(-b2)*y(n-2) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[1], yL16[0], 0);
            /*-- y(n-1)+a0*x(n)+(-b2)*y(n-2)+(-b1-1)*y(n-1) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[2], yL16[1], 0);
            /*-- y(n-1)+a0*x(n)+(-b2)*y(n-2)+(-b1-1)*y(n-1)+a2*x(n-2) --*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[3], xL16[0], 0);
            /* y(n-1)+a0*x(n)+(-b2)*y(n-2)+(-b1-1)*y(n-1)+a2*x(n-2)+a1*x(n-1)*/
            yL40 = s40_mac_s40_s16_s16_shift(yL40, coeffs[4], xL16[1], 0); 
            /*-- output --*/
            *destPtr = s16_extract_s32_h(s32_saturate_s40(s40_shl_s40(yL40, 3)));
     
            /*-- update filter memories --*/
            /*-- y(n-2) = y(n-1), y(n-1) = y --*/
            yL16[0] = yL16[1], yL16[1] = *destPtr++;
            /*-- x(n-2) = x(n-1), x(n-1)= x --*/
            xL16[0] = xL16[1], xL16[1] = xInL16;     
        }
    }
    /*-- store accumulator value back --*/
    filter->yL32 = s32_extract_s40_l(yL40);
#endif
}  /*----------------- end of function biquad_process_io --------------------*/


/*===========================================================================*/
/* FUNCTION : multiStageBiquad_setup                                         */
/*                                                                           */
/* DESCRIPTION: Setup coeffs for multi stage biquad filter struct.           */
/*                                                                           */
/* INPUTS: filter-> multi stage biquad filter struct                         */
/*         filterSpec: coeff struct for multi stage biquad filter            */
/* OUTPUTS: filter-> coeffs get set up                                       */
/*                                                                           */
/* IMPLEMENTATION NOTES:                                                     */
/*===========================================================================*/
void multiStageBiquad_setup
(
    multiStageBiquadFilter      *filter,        /* multi stage biquad struct */
    const multiStageBiquadSpec   filterSpec     /* coeff struct              */
)
{
    int16 i;

    /* assign number of biquad filters in this multi stage biquad filter */
    filter->stages = filterSpec.stages;

    /* setup coeffs for each biquad, convert 32 bit to 16 bit */
    for (i = 0; i < filter->stages; i++)
    {   
        biquad_setup(&filter->biquads[i], filterSpec.coeffs[i]);
    }
}  /*---------------- end of function multiStageBiquad_setup ----------------*/


/*===========================================================================*/
/* FUNCTION : multiStageBiquad_reset                                         */
/*                                                                           */
/* DESCRIPTION: Clean all biquad history and initialize accumulator.         */
/*                                                                           */
/* INPUTS: filter-> multi stage biquad filter struct                         */
/* OUTPUTS: reset each biquad                                                */
/*                                                                           */
/* IMPLEMENTATION NOTES:                                                     */
/*===========================================================================*/
void multiStageBiquad_reset
(
    multiStageBiquadFilter      *filter         /* multi stage biquad struct */
)
{
    int16 i;
    for (i = 0; i < filter->stages; i++)
    {
        biquad_reset(&filter->biquads[i]);
    }
}  /*----------------- end of function multiStageBiquad_reset ---------------*/


/*===========================================================================*/
/* FUNCTION : multiStageBiquad_process                                       */
/*                                                                           */
/* DESCRIPTION: Process a number of frams of samples using                   */
/*              multi stage biquad                                           */
/*                                                                           */
/* INPUTS: inplaceBuf-> inplace buffer for input and output samples          */
/*         filter-> multi stage biquad filter struct                         */
/*         samples: number of samples to process                             */
/* OUTPUTS: inplaceBuf-> inplace buffer                                      */
/*                                                                           */
/* IMPLEMENTATION NOTES:                                                     */
/*      Number of samples and buffer size should be coordinated properly     */
/*      before calling this function. This funtion itself doesn't ensure     */
/*      safe memory boundaries.                                              */
/*===========================================================================*/
void multiStageBiquad_process
(
    int16                       *inplaceBuf,    /* inplace buffer            */
    multiStageBiquadFilter      *filter,        /* multi stage biquad struct */
    int32                        samples        /* num of samples to process */
)
{
     int16 i;
    /* iterate biquad process for each stage */
    for (i = 0; i < filter->stages; i++)
    {
        biquad_process(inplaceBuf, &filter->biquads[i], samples);
    }

}  /*---------------- end of function multiStageBiquad_process --------------*/

/*===========================================================================*/
/* FUNCTION : fir_reset                                                      */
/*                                                                           */
/* DESCRIPTION: Clean FIR filter buffer and reset memory index.              */
/*                                                                           */
/* INPUTS: filter-> FIR filter struct                                        */
/*         data_width: bit with of data (16 or 32)                           */
/* OUTPUTS: filter history and index set to zeros.                           */
/*                                                                           */
/* IMPLEMENTATION NOTES:                                                     */
/*===========================================================================*/
void fir_reset_v1(fir_filter_t *filter, int32 data_width)
{
    int32 i;
    int16 *ptr16 = NULL;
    int32 *ptr32 = NULL;

    // reset filter memory index
    filter->mem_idx = 0;

    // clean filter histories
    switch (data_width) {
    case 16:
       ptr16 = (int16 *)filter->history;
       for (i = 0; i < filter->taps; ++i) {
          *ptr16++ = 0;
       }
       break;

    case 32:
       ptr32 = (int32 *)filter->history;
       for (i = 0; i < filter->taps; ++i) {
          *ptr32++ = 0;
       }
       break;

    default:
       return;
    }

}  //------------------------ end of function fir_reset ---------------------

#if ((defined __hexagon__) || (defined __qdsp6__))
void fir_process_general
(
    firFilter       *filter,            /* fir filter struct                 */
    int16           *destBuf,           /* output buffer                     */
    int16           *srcBuf,            /* input buffer                      */
    int16            samples,           /* samples to process                */
    int16            Qx                 /* Q factor of filter coeffs         */
)
{
    int16   i, j, shift;
    int16   *memPtr = filter->xL16 + filter->memIndex;
    int16   *destPtr = destBuf;
    int16   *srcPtr = srcBuf;
    int16   *coeffPtr;
    int32    yL32;

    /*-----------------------------------------------------------------------*/

    for(i = 0; i < samples; i++)
    {
        /* updat the "current" sample with input sample */
        *memPtr = *srcPtr++;

        /* reset pointer to coefficients */
        coeffPtr = filter->coeffsL16;

        /* reset accumulator */
        yL32 = 0;

        for (j = 0; j < filter->taps; j++)
        {
            /* convolution, y = sum (c[k] * x[n-k]) , k = 0, ..., taps-1 */
            yL32 = Q6_R_mpyacc_RlRl_sat(yL32,*coeffPtr++,*memPtr++);

            /* manage circular buffer */
            if (memPtr == filter->xL16 + filter->taps)
            {
                memPtr = filter->xL16;
            }
        } /* end of for (j = 0; j < filter->taps; i++) */

        /* decrease memory pointer one sample */
        memPtr--;

        /* manage circular buffer */
        if (memPtr < filter->xL16)
        {
            memPtr = filter->xL16 + filter->taps - 1;
        }

        /* determine the shift amount according to Q factor */
        shift = s16_sub_s16_s16(16, Qx);

        /* in-place update output sample into buffer */
        *destPtr++ = s16_extract_s32_h(Q6_R_asl_RR_sat(yL32, shift));

    } /* end of for(i = 0; i < samples; i++) */

    /*------------------- update index in the filter ------------------------*/
    filter->memIndex = (int16)(memPtr - filter->xL16);

} /*----------------------- end of function fir_process ---------------------*/

/*-----------------------------------------------------------------------------
FUNCTION : fir_process_c**xd**

DESCRIPTION: fir filter processing functions
For 16/32 bit coeffs and signal data with, the process functions
takes four forms:
fir_process_c16xd16: 16 bit coeff, 16 bit data
fir_process_c16xd32: 16 bit coeff, 32 bit data
fir_process_c32xd16: 32 bit coeff, 16 bit data
fir_process_c32xd32: 32 bit coeff, 32 bit data

INPUTS: filter-> FIR filter struct
dest-> output buffer
src-> input buffer
samples: number of samples to process
qx: q factor of filter coeffs
OUTPUTS: dest

IMPLEMENTATION NOTES:
-----------------------------------------------------------------------------*/

void fir_process_c16xd16(fir_filter_t *filter, int16 *destBuf, int16 *srcBuf, int32 samples, int16 qx)
{
    uint32 nZeroPadInput;
    int32 nOffsetIntoInputBuf = 0;
    int32 nInputProcSize = 0;
    int32 fir_len = 0;

    int16 *memPtr = (int16*)filter->history;
    int16 *outPtr = memPtr + MAX_FIR_TAPS + MAX_FIR_BLOCK_SIZE + 4;

    int16 *coeffPtr = (int16 *)filter->coeffs;
    int16 *reverse_coeff = (int16*)(((uintptr_t)filter->reverse_coeff + 0x7) & (uintptr_t)-0x8);

    //int64 y64;
    for (int i = 1; i <= filter->taps; i++) {
        reverse_coeff[filter->taps -i] = *coeffPtr++;
    }

    nZeroPadInput = (filter->taps) & 0x3;
    nZeroPadInput = nZeroPadInput ? (4 - nZeroPadInput) : 0;

    // pad zeros to make multiple of 4
    fir_len = filter->taps + nZeroPadInput;
    for (int i = filter->taps; i <= fir_len; i++)
        reverse_coeff[i] = 0;

    while (samples)
    {
        // nInputProcSize = MIN(Input Size , Block Size)
        nInputProcSize = (samples > MAX_FIR_BLOCK_SIZE) ? MAX_FIR_BLOCK_SIZE : samples;

        //Zero pad so that size of input block to Fir is a multiple of 4
        nZeroPadInput = (nInputProcSize) & 0x3;
        if (nZeroPadInput) {
            nZeroPadInput = 4 - nZeroPadInput;
        }
        samples -= nInputProcSize;

        // [<-----STATE(=TAPS-1)----->][<------INPUT------>][<-1msPADDING->]
        //              InputDataStart^(misaligned to 4-byte by -1 sample)
        // Copy Input buffer into structure to feed into BKFIR
        memsmove((memPtr + filter->taps - 1), nInputProcSize * sizeof(int16),
                 (srcBuf + nOffsetIntoInputBuf),
                 nInputProcSize * sizeof(int16));
        //==================================================================
//        for (int i = 0; i < nInputProcSize; ++i) {
//
//            // update "current" sample with the new input
//            // filter_mem[idx] = *src++;
//
//            // reset coeff ptr & accum
//            coeffPtr = (int16 *)filter->coeffs;
//            y64 = 0;
//
//            for (int j = 0; j < (int)address; ++j) {
//                // convolution, y = sum (c[k] * x[n-k]) , k = 0, ..., taps-1
//                y64 = s64_mac_s64_s16_s16_s1(y64, filter->reverse_coeff[j], memPtr[i + j]);
//                // wrap around circular buf
//                // idx = s32_modwrap_s32_u32(idx+1, taps);
//            }  // end of j loop
//
//            // process last sample, then current idx points to the next input position
//            //y64 = s64_mac_s64_s16_s16_s1(y64, *coeffPtr++, filter_mem[i + j]);
//
//            // shift and output sample
//            *outPtr++ = s16_extract_s64_h_sat(s64_shl_s64(y64, 0));
//
//        }  // end of i loop

        //====================================================================
//        // Invoke Filtering routine
        tuning_filter_fir(memPtr, /* (int16*) FIR Filter input        */
                          reverse_coeff, /* (int16*) FIR Filter coeffs       */
                          fir_len, /* (int32) Number of filter Taps    */
                          nInputProcSize + nZeroPadInput, /* (int32) Length of input block    */
                          (int32) -1, /* (int32) Q factor of filter coeffs*/
                          outPtr /* (int16*) FIR Filter output       */
                          );

        // Copy to destination Buffer
        memsmove(destBuf + nOffsetIntoInputBuf,
                 (nInputProcSize * sizeof(int16)), outPtr,
                 (nInputProcSize * sizeof(int16)));

        nOffsetIntoInputBuf += nInputProcSize;

        // Copy Filter States
        memsmove(memPtr, ((filter->taps - 1) * sizeof(int16)),
                 (memPtr + nInputProcSize),
                 ((filter->taps - 1) * sizeof(int16)));
    }
}


/* 16 coeff, 16 data with rounding*/
void fir_process_c16xd16_rnd(fir_filter_t *filter, int16 *destBuf, int16 *srcBuf, int32 samples, int16 qx)
{
    uint32 nZeroPadInput;
    int32 nOffsetIntoInputBuf = 0;
    int32 nInputProcSize = 0;
    int32 fir_len = 0;

    int16 *memPtr = (int16*)filter->history;
    int16 *outPtr = memPtr + MAX_FIR_TAPS + MAX_FIR_BLOCK_SIZE + 4;

    int16 *coeffPtr = (int16 *)filter->coeffs;
    int16 *reverse_coeff = (int16*)(((uintptr_t)filter->reverse_coeff + 0x7) & (uintptr_t)-0x8);

    //int64 y64;
    for (int i = 1; i <= filter->taps; i++) {
        reverse_coeff[filter->taps -i] = *coeffPtr++;
    }

    nZeroPadInput = (filter->taps) & 0x3;
    nZeroPadInput = nZeroPadInput ? (4 - nZeroPadInput) : 0;

    // pad zeros to make multiple of 4
    fir_len = filter->taps + nZeroPadInput;
    for (int i = filter->taps; i <= fir_len; i++)
        reverse_coeff[i] = 0;

    while (samples)
    {
        // nInputProcSize = MIN(Input Size , Block Size)
        nInputProcSize = (samples > MAX_FIR_BLOCK_SIZE) ? MAX_FIR_BLOCK_SIZE : samples;

        //Zero pad so that size of input block to Fir is a multiple of 4
        nZeroPadInput = (nInputProcSize) & 0x3;
        if (nZeroPadInput) {
            nZeroPadInput = 4 - nZeroPadInput;
        }
        samples -= nInputProcSize;
		
        // [<-----STATE(=TAPS-1)----->][<------INPUT------>][<-1msPADDING->]
        //              InputDataStart^(misaligned to 4-byte by -1 sample)
        // Copy Input buffer into structure to feed into BKFIR
        memsmove((memPtr + filter->taps - 1), nInputProcSize * sizeof(int16),
                 (srcBuf + nOffsetIntoInputBuf),
                 nInputProcSize * sizeof(int16));
        //==================================================================
//        for (int i = 0; i < nInputProcSize; ++i) {
//
//            // update "current" sample with the new input
//            // filter_mem[idx] = *src++;
//
//            // reset coeff ptr & accum
//            coeffPtr = (int16 *)filter->coeffs;
//            y64 = 0;
//
//            for (int j = 0; j < (int)address; ++j) {
//                // convolution, y = sum (c[k] * x[n-k]) , k = 0, ..., taps-1
//                y64 = s64_mac_s64_s16_s16_s1(y64, filter->reverse_coeff[j], memPtr[i + j]);
//                // wrap around circular buf
//                // idx = s32_modwrap_s32_u32(idx+1, taps);
//            }  // end of j loop
//
//            // process last sample, then current idx points to the next input position
//            //y64 = s64_mac_s64_s16_s16_s1(y64, *coeffPtr++, filter_mem[i + j]);
//
//            // shift and output sample
//            *outPtr++ = s16_extract_s64_h_sat(s64_shl_s64(y64, 0));
//
//        }  // end of i loop

        //====================================================================
//        // Invoke Filtering routine
        tuning_filter_fir_rnd(memPtr, /* (int16*) FIR Filter input        */
                          reverse_coeff, /* (int16*) FIR Filter coeffs       */
                          fir_len, /* (int32) Number of filter Taps    */
                          nInputProcSize + nZeroPadInput, /* (int32) Length of input block    */
                          (int32) -1, /* (int32) Q factor of filter coeffs*/
                          outPtr /* (int16*) FIR Filter output       */
                          );

        // Copy to destination Buffer
        memsmove(destBuf + nOffsetIntoInputBuf,
                 (nInputProcSize * sizeof(int16)), outPtr,
                 (nInputProcSize * sizeof(int16)));


        nOffsetIntoInputBuf += nInputProcSize;


        // Copy Filter States
        memsmove(memPtr, ((filter->taps - 1) * sizeof(int16)),
                 (memPtr + nInputProcSize),
                 ((filter->taps - 1) * sizeof(int16)));
    }
}


void fir_process_c16xd32(fir_filter_t *filter, int32 *destBuf, int32 *srcBuf, int32 samples, int16 qx)
{
    uint32 fir_len;
    int32 nOffsetIntoInputBuf = 0;
    int32 nInputProcSize = 0;

    int32 *memPtr = (int32*)filter->history;
    int32 *outPtr = memPtr + MAX_FIR_TAPS + MAX_FIR_BLOCK_SIZE + 4;

    int16 *coeffPtr = (int16 *) filter->coeffs;
    int16 *reverse_coeff = (int16*) (((uintptr_t) filter->reverse_coeff + 0x7) & (uintptr_t) -0x8);

    for (int i = 1; i <= filter->taps; i++) {
        reverse_coeff[filter->taps - i] = *coeffPtr++;
    }

    fir_len = filter->taps;
    if(filter->taps & 0x1)
    {
    	reverse_coeff[filter->taps] = 0;
    	fir_len++;
    }

    while (samples)
    {
        nInputProcSize = (samples > MAX_FIR_BLOCK_SIZE) ? MAX_FIR_BLOCK_SIZE : samples;

        samples -= nInputProcSize;
        // [<-----STATE(=TAPS-1)----->][<------INPUT------>][<-1msPADDING->]
        //              InputDataStart^(misaligned to 4-byte by -1 sample)
        // Copy Input buffer into structure to feed into BKFIR
        memsmove((memPtr + filter->taps - 1),
                 nInputProcSize * sizeof(int32),
                 (srcBuf + nOffsetIntoInputBuf),
                 nInputProcSize * sizeof(int32));
        //==================================================================
        fir_c16xd32(memPtr, reverse_coeff, nInputProcSize, fir_len, outPtr);

        memsmove(destBuf + nOffsetIntoInputBuf, nInputProcSize * sizeof(int32),
                 outPtr, nInputProcSize * sizeof(int32) );

        nOffsetIntoInputBuf += nInputProcSize;

        // Copy Filter States
        memsmove(memPtr, (filter->taps - 1) * sizeof(int32),
                 (memPtr + nInputProcSize), (filter->taps - 1) * sizeof(int32) );
    }
}

#else
/*===========================================================================*/
/* FUNCTION : fir_reset                                                      */
/*                                                                           */
/* DESCRIPTION: Clean FIR filter buffer and reset memory index.              */
/*                                                                           */
/* INPUTS: filter-> FIR filter struct                                        */
/* OUTPUTS: filter history and index set to zeros.                           */
/*                                                                           */
/* IMPLEMENTATION NOTES:                                                     */
/*===========================================================================*/
uint32 fir_reset
(
    firFilter       *filter             /* fir filter struct                 */
)
{
    int16 i;

    /* double check maximum allowed filter taps */
    if (filter->taps > MAX_FIR_TAPS)
    {
        return PPERR_FILTER_INVALID_TAPCOUNT;
    }
    
    /* clean filter histories */
    for (i = 0; i < filter->taps; i++)
    {
        filter->xL16[i] = 0;
    }

    /* reset filter memory index */
    filter->memIndex = 0;
    return PPSUCCESS;
}  /*------------------------ end of function fir_reset ---------------------*/


/*===========================================================================*/
/* FUNCTION : fir_process                                                    */
/*                                                                           */
/* DESCRIPTION: In-place process a block of samples with FIR filter.         */
/*                                                                           */
/* INPUTS: filter-> FIR filter struct                                        */
/*         destBuf-> output buffer                                           */
/*         srcBuf-> input buffer                                             */
/*         sample: number of samples to process                              */
/*         Qx: Q factor of filter coeffs                                     */
/* OUTPUTS: inplaceBuf-> input/output buffer                                 */
/*                                                                           */
/* IMPLEMENTATION NOTES:                                                     */
/*      Q factor should be more than 13. Otherwise the shift of mac will     */
/*  exceed the limit of 3.                                                   */
/*===========================================================================*/
void fir_process
(
    firFilter       *filter,            /* fir filter struct                 */
    int16           *destBuf,           /* output buffer                     */
    int16           *srcBuf,            /* input buffer                      */
    int16            samples,           /* samples to process                */
    int16            Qx                 /* Q factor of filter coeffs         */
)
{
    int16   i, j, shift;
    int16   *memPtr = filter->xL16 + filter->memIndex;
    int16   *destPtr = destBuf;
    int16   *srcPtr = srcBuf;
    int16   *coeffPtr;
    int40    yL40;

    /*-----------------------------------------------------------------------*/

    for(i = 0; i < samples; i++)
    {
        /* updat the "current" sample with input sample */
        *memPtr = *srcPtr++;

        /* reset pointer to coefficients */
        coeffPtr = filter->coeffsL16;

        /* reset accumulator */
        yL40 = 0;

        for (j = 0; j < filter->taps; j++)
        {
            /* convolution, y = sum (c[k] * x[n-k]) , k = 0, ..., taps-1 */
            yL40 = s40_mac_s40_s16_s16_shift(yL40, *coeffPtr++, *memPtr++, 0);

            /* manage circular buffer */
            if (memPtr == filter->xL16 + filter->taps)
            {
                memPtr = filter->xL16;
            }
        } /* end of for (j = 0; j < filter->taps; i++) */

        /* decrease memory pointer one sample */
        memPtr--;

        /* manage circular buffer */
        if (memPtr < filter->xL16)
        {
            memPtr = filter->xL16 + filter->taps - 1;
        }

        /* determine the shift amount according to Q factor */
        shift = s16_sub_s16_s16(16, Qx);

        /* in-place update output sample into buffer */
        *destPtr++ = s16_extract_s32_h(s32_saturate_s40(s40_shl_s40(yL40, shift)));

    } /* end of for(i = 0; i < samples; i++) */

    /*------------------- update index in the filter ------------------------*/
    filter->memIndex = (int16)(memPtr - filter->xL16);

} /*----------------------- end of function fir_process ---------------------*/

void fir_process_general
(
    firFilter       *filter,            /* fir filter struct                 */
    int16           *destBuf,           /* output buffer                     */
    int16           *srcBuf,            /* input buffer                      */
    int16            samples,           /* samples to process                */
    int16            Qx                 /* Q factor of filter coeffs         */
)
{
    int16   i, j, shift;
    int16   *memPtr = filter->xL16 + filter->memIndex;
    int16   *destPtr = destBuf;
    int16   *srcPtr = srcBuf;
    int16   *coeffPtr;
    int40    yL40;

    /*-----------------------------------------------------------------------*/

    for(i = 0; i < samples; i++)
    {
        /* updat the "current" sample with input sample */
        *memPtr = *srcPtr++;

        /* reset pointer to coefficients */
        coeffPtr = filter->coeffsL16;

        /* reset accumulator */
        yL40 = 0;

        for (j = 0; j < filter->taps; j++)
        {
            /* convolution, y = sum (c[k] * x[n-k]) , k = 0, ..., taps-1 */
            yL40 = s40_mac_s40_s16_s16_shift(yL40, *coeffPtr++, *memPtr++, 0);

            /* manage circular buffer */
            if (memPtr == filter->xL16 + filter->taps)
            {
                memPtr = filter->xL16;
            }
        } /* end of for (j = 0; j < filter->taps; i++) */

        /* decrease memory pointer one sample */
        memPtr--;

        /* manage circular buffer */
        if (memPtr < filter->xL16)
        {
            memPtr = filter->xL16 + filter->taps - 1;
        }

        /* determine the shift amount according to Q factor */
        shift = s16_sub_s16_s16(16, Qx);

        /* in-place update output sample into buffer */
        *destPtr++ = s16_extract_s32_h(s32_saturate_s40(s40_shl_s40(yL40, shift)));

    } /* end of for(i = 0; i < samples; i++) */

    /*------------------- update index in the filter ------------------------*/
    filter->memIndex = (int16)(memPtr - filter->xL16);

} /*----------------------- end of function fir_process ---------------------*/


/*-----------------------------------------------------------------------------
   FUNCTION : fir_process_c**xd**
   
   DESCRIPTION: fir filter processing functions
           For 16/32 bit coeffs and signal data with, the process functions
           takes four forms:
           fir_process_c16xd16: 16 bit coeff, 16 bit data
           fir_process_c16xd32: 16 bit coeff, 32 bit data
           fir_process_c32xd16: 32 bit coeff, 16 bit data
           fir_process_c32xd32: 32 bit coeff, 32 bit data

   INPUTS: filter-> FIR filter struct 
           dest-> output buffer
           src-> input buffer
           samples: number of samples to process
           qx: q factor of filter coeffs
   OUTPUTS: dest

   IMPLEMENTATION NOTES:
-----------------------------------------------------------------------------*/
/* 16 coeff, 16 data */
void fir_process_c16xd16(fir_filter_t *filter, int16 *dest, int16 *src, int32 samples, int16 qx)
{
   int32   i, j;
   int16   shift;
   int32   idx = filter->mem_idx;
   int32   taps = filter->taps;
   int16   *filter_mem = (int16 *)filter->history;
   int16   *coeff_ptr = NULL;
   int64   y64;

   // determine the up-shift amount according to Q factor
   shift = s16_sub_s16_s16(15, qx);

   for (i = 0; i < samples; ++i) {

      // update "current" sample with the new input
      filter_mem[idx] = *src++;

      // reset coeff ptr & accum
      coeff_ptr = (int16 *)filter->coeffs;
      y64 = 0;

      for (j = 0; j < taps-1; ++j) {
         // convolution, y = sum (c[k] * x[n-k]) , k = 0, ..., taps-1
         y64 = s64_mac_s64_s16_s16_s1(y64, *coeff_ptr++, filter_mem[idx]);
         // wrap around circular buf
         idx = s32_modwrap_s32_u32(idx+1, taps);
      } // end of j loop
      
      // process last sample, then current idx points to the next input position
      y64 = s64_mac_s64_s16_s16_s1(y64, *coeff_ptr++, filter_mem[idx]);


      // shift and output sample
      *dest++ = s16_extract_s64_h_sat(s64_shl_s64(y64, shift));

   } // end of i loop

   // update index in filter struct
   filter->mem_idx = idx;
}

/* 16 coeff, 16 data with rounding*/
void fir_process_c16xd16_rnd(fir_filter_t *filter, int16 *dest, int16 *src, int32 samples, int16 qx)
{
	int32   i, j;
	int16   shift;
	int32   idx = filter->mem_idx;
	int32   taps = filter->taps;
	int16   *filter_mem = (int16 *)filter->history;
	int16   *coeff_ptr = NULL;
	int64   y64;

	// determine the up-shift amount according to Q factor
	shift = s16_sub_s16_s16(15, qx);

	for (i = 0; i < samples; ++i) {

		// update "current" sample with the new input
		filter_mem[idx] = *src++;

		// reset coeff ptr & accum
		coeff_ptr = (int16 *)filter->coeffs;
		y64 = 0;

		for (j = 0; j < taps - 1; ++j) {
			// convolution, y = sum (c[k] * x[n-k]) , k = 0, ..., taps-1
			y64 = s64_mac_s64_s16_s16_s1(y64, *coeff_ptr++, filter_mem[idx]);
			// wrap around circular buf
			idx = s32_modwrap_s32_u32(idx + 1, taps);
		} // end of j loop

		// process last sample, then current idx points to the next input position
		y64 = s64_mac_s64_s16_s16_s1(y64, *coeff_ptr++, filter_mem[idx]);


		// shift and output sample
		*dest++ = s16_extract_s64_h_sat(s64_add_s64_s32(s64_shl_s64(y64, shift), 0x8000));

	} // end of i loop

	// update index in filter struct
	filter->mem_idx = idx;
}


/* 16 coeff, 32 data */
void fir_process_c16xd32(fir_filter_t *filter, int32 *dest, int32 *src, int32 samples, int16 qx)
{
   int32   i, j;
   int32   idx = filter->mem_idx;
   int32   taps = filter->taps;
   int32   *filter_mem = (int32 *)filter->history;
   int16   *coeff_ptr = NULL;
   int64   y64;
   int16   neg_qx = -qx;   

   for (i = 0; i < samples; ++i) {
      // update "current" sample with new input
      filter_mem[idx] = *src++;

      // reset coeff ptr & accum
      coeff_ptr = (int16 *)filter->coeffs;
      y64 = 0;

      for (j = 0; j < taps-1; ++j) {
         // convolution, y = sum (c[k] * x[n-k]) , k = 0, ..., taps-1
         y64 = s64_mac_s32_s32(y64, filter_mem[idx], (int32)*coeff_ptr++);
         idx = s32_modwrap_s32_u32(idx+1, taps);
      } // end of j loop

      // process last sample, then current idx points to the next input position
      y64 = s64_mac_s32_s32(y64, filter_mem[idx], (int32)(*coeff_ptr++));

      // shift and output sample
      *dest++ = s32_saturate_s64(s64_shl_s64(y64, neg_qx));

   } // end of i loop

   // update index in filter struct
   filter->mem_idx = idx;
}

#endif

/* 32 coeff, 16 data */
void fir_process_c32xd16(fir_filter_t *filter, int16 *dest, int16 *src, int32 samples, int16 qx)
{
   int32   i, j;
   int32   idx = filter->mem_idx;
   int32   taps = filter->taps;
   int16   *filter_mem = (int16 *)filter->history;
   int32   *coeff_ptr = NULL;
   int64   y64;
   int16   neg_qx = -qx;   

   for (i = 0; i < samples; ++i) {

      // update "current" sample with the new input
      filter_mem[idx] = *src++;

      // reset coeff ptr & accum
      coeff_ptr = (int32 *)filter->coeffs;
      y64 = 0;

      for (j = 0; j < taps-1; ++j) {
         // convolution, y = sum (c[k] * x[n-k]) , k = 0, ..., taps-1
         y64 = s64_mac_s32_s32(y64, (int32)filter_mem[idx], *coeff_ptr++);
         idx = s32_modwrap_s32_u32(idx+1, taps);
      } 

      // process last sample, then current idx points to the next input position
      y64 = s64_mac_s32_s32(y64, (int32)filter_mem[idx], *coeff_ptr++);

      // shift and output sample
      *dest++ = s16_saturate_s32(s32_saturate_s64(s64_shl_s64(y64, neg_qx)));

   } // end of i loop

   // update index in filter struct
   filter->mem_idx = idx;
}

/* 32 coeff, 32 data */
void fir_process_c32xd32(fir_filter_t *filter, int32 *dest, int32 *src, int32 samples, int16 qx)
{
   int32   i, j;
   int32   idx = filter->mem_idx;
   int32   taps = filter->taps;
   int32   *filter_mem = (int32 *)filter->history;
   int32   *coeff_ptr = NULL;
   int64   y64;
   int16   neg_qx = -qx;   

   for (i = 0; i < samples; ++i) {

      // update "current" sample with new input
      filter_mem[idx] = *src++;

      // reset coeff ptr & accum
      coeff_ptr = (int32 *)filter->coeffs;
      y64 = 0;

      for (j = 0; j < taps-1; ++j) {
         // convolution, y = sum (c[k] * x[n-k]) , k = 0, ..., taps-1
         y64 = s64_mac_s32_s32(y64, filter_mem[idx], *coeff_ptr++);
         idx = s32_modwrap_s32_u32(idx+1, taps);
      } // end of j loop

      // process last sample, then current idx points to the next input position
      y64 = s64_mac_s32_s32(y64, filter_mem[idx], *coeff_ptr++);

      // shift and output sample
      *dest++ = s32_saturate_s64(s64_shl_s64(y64, neg_qx));

   } // end of i loop

   // update index in filter struct
   filter->mem_idx = idx;
}



/*===========================================================================*/
/* FUNCTION : fir_reset                                                      */
/*                                                                           */
/* DESCRIPTION: Clean FIR filter buffer and reset memory index.              */
/*                                                                           */
/* INPUTS: filter-> FIR filter struct                                        */
/*         data_width: bit with of data (16 or 32)                           */
/* OUTPUTS: filter history and index set to zeros.                           */
/*                                                                           */
/* IMPLEMENTATION NOTES:                                                     */
/*===========================================================================*/
void fir_reset1(fir_filter_t *filter, int32 data_width)
{
    int32 i;
    int16 *ptr16 = NULL;
    int32 *ptr32 = NULL;

    // reset filter memory index 
    filter->mem_idx = 0;

    // clean filter histories
    switch (data_width) {
    case 16:
       ptr16 = (int16 *)filter->history;
       for (i = 0; i < filter->taps; ++i) {
          *ptr16++ = 0;
       }
       break;

    case 32:
       ptr32 = (int32 *)filter->history;
       for (i = 0; i < filter->taps; ++i) {
          *ptr32++ = 0;
       }
       break;
    default:
       return;
    }

}  //------------------------ end of function fir_reset ---------------------


