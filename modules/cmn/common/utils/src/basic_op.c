/*
# Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/***************************************************************************
*
*   File Name:  basic_op.c (mathevrc.c with op counting added)
*
*   Purpose:  Contains functions which implement the primitive
*     arithmetic operations.
*
*      The functions in this file are listed below.  Some of them are
*      defined in terms of other basic operations.  One of the
*      routines, s16_saturate_s32() is static.  This is not a basic
*      operation, and is not referenced outside the scope of this
*      file.
*
*
*       s16_saturate_s32()
*       s32_saturate_s64()
*       u16_abs_s16_sat()
*       s16_add_s16_s16()
*       s16_add_s16_s16_sat()
*       s16_and_s16_s16()
*       s32_and_s32_s32()
*       s16_complement_s16()
*       clearOverflow()
*       s16_extract_s32_h()
*       s32_extract_s64_l()
*       u32_extract_s64_l()
*       s16_extract_s64_h()
*       s16_extract_s32_l()
*       u16_extract_s32_l()
*       s16_extract_s64_l()
*       isOverflow()
*       u32_abs_s32_sat()
*       s16_min_s16_s16()
*       s32_min_s32_s32()
*       s32_max_s32_s32()
*       s16_max_s16_s16()
*       s32_add_s32_s32()
*       s64_add_s32_s32()
*       s64_add_s32_u32()
*       s64_add_s64_s64()
*       s64_add_s64_s32()
*       s32_add_s32_s32_sat()
*       s32_deposit_s16_l()
*       s32_mac_s32_s16_s16_sat()
*       s64_mac_s64_s16_s16_shift()
*       s64_mac_s64_s16_s16_shift_sat()
*       s32_msu_s32_s16_s16_sat()
*       s64_mult_s32_s16_shift()
*       s64_mult_s32_s16()
*       s64_mult_u32_s16()
*       s64_mult_s32_u16_shift()
*       s64_mult_u32_s16_shift()
*       s32_mult_s32_s32_rnd_sat()
*       s64_mult_s32_u32_shift()
*       s64_mult_lp_s32_u32_shift()
*       s64_mult_s32_s32_shift()
*       s64_mult_lp_s32_s32_shift()
*       s64_mult_s16_u16_shift()
*       s32_mult_s16_u16()
*       s64_mult_s16_s16_shift()
*       s32_mult_s16_s16()
*       u32_mult_u16_u16()
*       s64_mult_u16_u16_shift()
*       s32_mult_s16_s16_shift_sat()
*       s32_neg_s32_sat()
*       s64_shl_s64()
*       s32_shl_s32_rnd_sat()
*       s32_shl_s32_sat()
*       s32_shr_s32_sat()
*       s32_sub_s32_s32()
*       s32_sub_s32_s32_sat()
*       s16_mac_s32_s16_s16_sat_rnd()
*       s16_msu_s32_s16_s16_sat_rnd()
*       s16_norm_s32()
*       s16_norm_s16()
*       s32_cl1_s32()
*       popOverflow()
*       s16_round_s32_sat()
*       setOverflow()
*       s16_shl_s16_sat_rnd()
*       s16_shl_s16_sat()
*       s16_shr_s16_sat()
*       s16_sub_s16_s16()
*       s64_sub_s64_s64()
*       s16_sub_s16_s16_sat()
*       s32_mls_s32_s16_sat()
*       s16_norm_s64()
*       s32_deposit_s16_h()
*       s32_mult_s32_s16_rnd_sat()
*       changed()
*       change_if_valid()
**************************************************************************/

/*_________________________________________________________________________
|                                                                         |
|                            Include Files                                |
|_________________________________________________________________________|
*/

#include "audio_basic_op.h"
#include "ar_defs.h"
#ifdef WMOPS_FX
#include "const_fx.h"
#endif

int giOverflow = 0;
int giOldOverflow = 0;

// local function:
#if !defined(s16_saturate_s32)
/***************************************************************************
*
*   FUNCTION NAME: s16_saturate_s32
*
*   PURPOSE:
*
*     Limit the 32 bit input to the range of a 16 bit word.
*
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   KEYWORDS: saturation, limiting, limit, s16_saturate_s32, 16 bits
*
*************************************************************************/

int16 s16_saturate_s32(int32 var1)
{
    int16 swOut;

    if (var1 > SHORTWORD_MAX)
    {
        swOut = SHORTWORD_MAX;
        giOverflow = 1;
    }
    else if (var1 < SHORTWORD_MIN)
    {
        swOut = SHORTWORD_MIN;
        giOverflow = 1;
    }
    else
        swOut = (int16) var1;       /* automatic type conversion */

#ifdef WMOPS_FX
    counter_fx.saturate++;
#endif
    return (swOut);

}
#endif // #if !defined(s16_saturate_s32)

/***************************************************************************/
/***************************************************************************/
/*------------------------------ Public Functions -------------------------*/
/***************************************************************************/
/***************************************************************************/

#if !defined(s32_saturate_s64)

/***************************************************************************
*
*   FUNCTION NAME: s32_saturate_s64
*
*   PURPOSE:
*
*     Limit the input int64 (possibly exceeding 32 bit dynamic
*     range) having to the 32 output wordsize.
*
*   INPUTS:
*
*     var1
*                     A int64 whose range is
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*                     i.e. a 64 bit number. Not modified.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit long integer (int32) where the DSP's
*                     rules of saturation are followed:
*                     for: 0x8000 0000 <= dVal1 <= 0x7fff ffff
*                      input == output, no saturation
*                     for: 0x8000 0000 > dVal1 output 0x8000 0000
*                     for: dVal1 > 0x7fff ffff output 0x7fff ffff
*
*   KEYWORDS: saturation, limiting, limit, s16_saturate_s32, 32 bits
*
*************************************************************************/
int32 s32_saturate_s64(int64 var1)
{

    if (var1 > (int64) LONGWORD_MAX)
    {
        var1 = (int64) LONGWORD_MAX;
        giOverflow = 1;
    }
    else if (var1 < (int64) LONGWORD_MIN)
    {
        var1 = (int64) LONGWORD_MIN;
        giOverflow = 1;
    }

#ifdef WMOPS_FX
    counter_fx.saturate++;
#endif

    return ((long) var1);

}
#endif // #if !defined(s32_saturate_s64)

#if !defined(u16_abs_s16_sat)
/***************************************************************************
*
*   FUNCTION NAME: u16_abs_s16_sat
*
*   PURPOSE:
*
*     Take the absolute value of the 16 bit input.  An input of
*     -0x8000 results in a return value of 0x7fff.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0x0000 0000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*     Take the absolute value of the 16 bit input.  An input of
*     -0x8000 results in a return value of 0x7fff.
*
*   KEYWORDS: absolute value, abs
*
*************************************************************************/
uint16 u16_abs_s16_sat(int16 var1)
{
    int16 swOut;

    if (var1 == SHORTWORD_MIN)
    {
        swOut = SHORTWORD_MAX;
        giOverflow = 1;
    }
    else
    {
        if (var1 < 0)
            swOut = -var1;
        else
            swOut = var1;
    }

#ifdef WMOPS_FX
    counter_fx.abs_sat++;
#endif

    return (swOut);

}
#endif //#if !defined(u16_abs_s16_sat)

#if !defined(s16_add_s16_s16)
/***************************************************************************
*
*   FUNCTION NAME: s16_add_s16_s16
*
*   PURPOSE:
*
*     Perform the addition of the two 16 bit input variable WITHOUT
*     saturation.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*     Perform the addition of the two 16 bit input variable without
*     saturation.
*
*     swOut = var1 + var2
*
*     No protection from overflow.
*   KEYWORDS: s16_add_s16_s16, addition
*
*************************************************************************/

int16 s16_add_s16_s16(int16 var1, int16 var2)
{
    int16 swOut;

    swOut =  var1 + var2;

#ifdef WMOPS_FX
    counter_fx.add16++;
#endif

    return (swOut);

}
#endif //#if !defined(s16_add_s16_s16)

#if !defined(s16_add_s16_s16_sat)
/***************************************************************************
*
*   FUNCTION NAME: s16_add_s16_s16_sat
*
*   PURPOSE:
*
*     Perform the addition of the two 16 bit input variable with
*     saturation.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*     Perform the addition of the two 16 bit input variable with
*     saturation.
*
*     swOut = var1 + var2
*
*     swOut is set to 0x7fff if the operation results in an
*     overflow.  swOut is set to 0x8000 if the operation results
*     in an underflow.
*
*   KEYWORDS: s16_add_s16_s16, addition
*
*************************************************************************/
int16 s16_add_s16_s16_sat(int16 var1, int16 var2)
{
    int32 L_sum;
    int16 swOut;

    L_sum = (int32) var1 + var2;
    swOut = s16_saturate_s32(L_sum);

#ifdef WMOPS_FX
    counter_fx.add16_sat++;
    counter_fx.saturate--;
#endif

    return (swOut);

}
#endif //#if !defined(s16_add_s16_s16_sat)

#if !defined(s16_and_s16_s16)
/***************************************************************************
*
*   FUNCTION NAME: s16_and_s16_s16
*
*   PURPOSE:
*
*     Perform the logical AND of the two 16 bit input variables
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000<= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000<= var1 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     outputL16
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000<= var1 <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*     Perform the logical AND of the two 16 bit input variables
*
*     outputL16 = var1 & var2
*
*
*   KEYWORDS: and, logical and
*
*************************************************************************/

int16 s16_and_s16_s16(int16 var1, int16 var2)
{
    int16 outputL16;

    outputL16 = var1 & var2;
#ifdef WMOPS_FX
    counter_fx.and++;
#endif

    return (outputL16);

}
#endif //#if !defined(s16_and_s16_s16)

#if !defined(s32_and_s32_s32)
/***************************************************************************
*
*   FUNCTION NAME: s32_and_s32_s32
*
*   PURPOSE:
*
*     Perform the logical AND of the two 32 bit input variables
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     outputL32
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*
*     Perform the logical AND of the two 32 bit input variables
*
*     outputL32 = var1 & var2
*
*
*   KEYWORDS: and, logical and
*
*************************************************************************/

int32 s32_and_s32_s32(int32 var1, int32 var2)
{
    int32 outputL32;

    outputL32 = var1 & var2;
#ifdef WMOPS_FX
    counter_fx.and++;
#endif

    return (outputL32);

}
#endif //#if !defined(s32_and_s32_s32)

#if !defined(s16_complement_s16)
/****************************************************************************
*
*     FUNCTION NAME: s16_complement_s16
*
*     PURPOSE:
*
*        performs 1's complement operation
*
*     INPUTS:
*
*       var1: 16-bit value to be 1's complemented.
*
*
*     OUTPUTS:            1' complemented out of the input.
*
*     RETURN VALUE:        1' complemented out of the input.
*
*
*     KEYWORDS: s16_complement_s16
*
***************************************************************************/
int16 s16_complement_s16(int16 var1)
{
    int16 out16;

    out16 = ~var1;

#ifdef WMOPS_FX
    counter_fx.complement++;
#endif

    return out16;

}
#endif //#if !defined(s16_complement_s16)

#if !defined(clearOverflow)
/****************************************************************************
*
*     FUNCTION NAME: clearOverflow
*
*     PURPOSE:
*
*        Clear the overflow flag
*
*     INPUTS:
*
*       none
*
*
*     OUTPUTS:             global overflow flag is cleared
*                          previous value stored in giOldOverflow
*
*     RETURN VALUE:        previous value of overflow
*
*
*     KEYWORDS: saturation, limit, overflow
*
***************************************************************************/
int clearOverflow(void)
{
    giOldOverflow = giOverflow;
    giOverflow = 0;

#ifdef WMOPS_FX
    counter_fx.clearOverflow++;
#endif

    return (giOldOverflow);

}
#endif /* if !defined(clearOverflow) */

#if !defined(s16_extract_s32_h)
/***************************************************************************
*
*   FUNCTION NAME: s16_extract_s32_h
*
*   PURPOSE:
*
*     Extract the 16 MS bits of a 32 bit int32.  Return the 16 bit
*     number as a int16.  This is used as a "truncation" of a fractional
*     number.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*   KEYWORDS: assign, truncate
*
*************************************************************************/
int16 s16_extract_s32_h(int32 var1)
{
    int16 var2;

    var2 = (int16) (UMAX_16 & (var1 >> 16));

#ifdef WMOPS_FX
    counter_fx.extract++;
#endif

    return (var2);

}
#endif //#if !defined(s16_extract_s32_h)

#if !defined(s32_extract_s64_l)
/***************************************************************************
*
*   FUNCTION NAME: s32_extract_s64_l
*
*   PURPOSE:
*
*     Extract the 32 LS bits of a 64 bit int64.  Return the 32 bit
*     number as a int32.  This is used as a "truncation" of a fractional
*     number.
*
*   INPUTS:
*
*     var1
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     lwOut
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x 8000 0000<= lwOut <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*
*   KEYWORDS: assign, truncate
*
*************************************************************************/
int32 s32_extract_s64_l(int64 var1)
{
    int32 var2;

    var2 = (int32) (UMAX_32 & (var1 ));

#ifdef WMOPS_FX
    counter_fx.extract++;
#endif

    return (var2);

}
#endif //#if !defined(s32_extract_s64_l)

#if !defined(u32_extract_s64_l)
/***************************************************************************
*
*   FUNCTION NAME: u32_extract_s64_l
*
*   PURPOSE:
*
*     Extract the 32 LS bits of a 64 bit int64.  Return the 32 bit
*     number as a uint32.  This is used as a "truncation" of a fractional
*     number.
*
*   INPUTS:
*
*     var1
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     lwOut
*                     32 bit unsigned integer (uint32) whose value
*                     falls in the range
*                     0x 0000 0000<= lwOut <= 0xffff ffff.
*
*   IMPLEMENTATION:
*
*   KEYWORDS: assign, truncate
*
*************************************************************************/
uint32 u32_extract_s64_l(int64 var1)
{
    uint32 var2;

    var2 = (uint32) (UMAX_32 & (var1 ));

#ifdef WMOPS_FX
    counter_fx.extract++;
#endif

    return (var2);

}
#endif //#if !defined(s32_extract_s64_l)

#if !defined(s16_extract_s64_h)
/***************************************************************************
*
*   FUNCTION NAME: s16_extract_s64_h
*
*   PURPOSE:
*
*     Extract the 16 MS bits of a 64 bit int64.  Return the 16 bit
*     number as a int16.  This is used as a "truncation" of a fractional
*     number.
*
*   INPUTS:
*
*     var1
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*   KEYWORDS: assign, truncate
*
*************************************************************************/
int16 s16_extract_s64_h(int64 var1)
{
    int16 var2;

    var2 = (int16) (UMAX_16 & (var1 >> 16));

#ifdef WMOPS_FX
    counter_fx.extract++;
#endif

    return (var2);

}
#endif //#if !defined(s16_extract_s64_h)

#if !defined(s16_extract_s64_h_rnd)
/***************************************************************************
*
*   FUNCTION NAME: s16_extract_s64_h_rnd
*
*   PURPOSE:
*
*     Extract the 16 MS bits of a 64 bit int64 with rounding.  Return the 16 bit
*     number as a int16.  This is used as a "truncation" of a fractional
*     number.
*
*   INPUTS:
*
*     var1
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*   KEYWORDS: assign, truncate
*
*************************************************************************/
int16 s16_extract_s64_h_rnd(int64 var1)
{
	int16 var2;
	var1 = var1 + (int64)0x8000;
	var2 = (int16) (UMAX_16 & (var1 >> 16));
#ifdef WMOPS_FX
	counter_fx.extract++;
#endif
	return (var2);
}
#endif //#if !defined(s16_extract_s64_h_rnd)

#if !defined(s16_extract_s32_l)
/***************************************************************************
*
*   FUNCTION NAME: s16_extract_s32_l
*
*   PURPOSE:
*
*     Extract the 16 LS bits of a 32 bit int32.  Return the 16 bit
*     number as a int16.  The upper portion of the input int32
*     has no impact whatsoever on the output.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*
*   KEYWORDS: extract, assign
*
*************************************************************************/
int16 s16_extract_s32_l(int32 var1)
{
    int16 var2;

    var2 = (int16) (UMAX_16 & var1);

#ifdef WMOPS_FX
    counter_fx.extract++;
#endif

    return (var2);

}
#endif //#if !defined(s16_extract_s32_l)

#if !defined(u16_extract_s32_l)
/***************************************************************************
*
*   FUNCTION NAME: u16_extract_s32_l
*
*   PURPOSE:
*
*     Extract the 16 LS bits of a 32 bit int32.  Return the 16 bit
*     number as a uint16.  The upper portion of the input int32
*     has no impact whatsoever on the output.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short unsigned integer (uint16) whose value
*                     falls in the range
*                     0x0000 0000 <= swOut <= 0x0000 ffff.
*
*
*   KEYWORDS: extract, assign
*
*************************************************************************/
uint16 u16_extract_s32_l(int32 var1)
{
    uint16 var2;

    var2 = (uint16) (UMAX_16 & var1);

#ifdef WMOPS_FX
    counter_fx.extract++;
#endif

    return (var2);

}
#endif //#if !defined(u16_extract_s32_l)

#if !defined(s16_extract_s64_l)
/***************************************************************************
*
*   FUNCTION NAME: s16_extract_s64_l
*
*   PURPOSE:
*
*     Extract the 16 LS bits of a 64 bit int64.  Return the 16 bit
*     number as a int16.  This is used as a "truncation" of a fractional
*     number.
*
*   INPUTS:
*
*     var1
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x 7fff ffff ffff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*   KEYWORDS: assign, truncate
*
*************************************************************************/

int16 s16_extract_s64_l(int64 var1)
{
    int16 var2;

    var2 = (int16) (UMAX_16 & var1);

#ifdef WMOPS_FX
    counter_fx.extract++;
#endif

    return (var2);

}
#endif //#if !defined(s16_extract_s64_l)

#if !defined(isOverflow)
/****************************************************************************
*
*     FUNCTION NAME: isOverflow
*
*     PURPOSE:
*
*        Check to see whether an overflow/saturation/limiting has occurred
*
*     INPUTS:
*
*       none
*
*
*     OUTPUTS:             none
*
*     RETURN VALUE:        1 if overflow has been flagged
*                          0 otherwise
*
*     KEYWORDS: saturation, limit, overflow
*
***************************************************************************/
int isOverflow(void)
{
    return (giOverflow);
}
#endif // #if !defined(isOverflow)

#if !defined(u32_abs_s32_sat)
/***************************************************************************
*
*   FUNCTION NAME: u32_abs_s32_sat
*
*   PURPOSE:
*
*     Take the absolute value of the 32 bit input.  An input of
*     0x8000 0000 results in a return value of 0x7fff ffff.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*
*
*   KEYWORDS: absolute value, abs
*
*************************************************************************/
uint32 u32_abs_s32_sat(int32 var1)
{
    int32 L_Out;

    if (var1 == LONGWORD_MIN)
    {
        L_Out = LONGWORD_MAX;
        giOverflow = 1;
    }
    else
    {
        if (var1 < 0)
            L_Out = -var1;
        else
            L_Out = var1;
    }

#ifdef WMOPS_FX
    counter_fx.abs_sat++;
#endif

    return (L_Out);

}
#endif //#if !defined(u32_abs_s32_sat)

#if !defined(s16_min_s16_s16)
/***************************************************************************
*
*   FUNCTION NAME: s16_min_s16_s16
*
*   PURPOSE:
*
*     Perform the addition of the two 16 bit input variables
*
*   INPUTS:
*
*     var1
*                     16 bit long signed integer (int16) whose value
*                     falls in the range
*                     0x0000 8000 <= var1 <= 0x0000 7fff .
*     var2
*                     16 bit long signed integer (int16) whose value
*                     falls in the range
*                     0x0000 8000 <= var1 <= 0x0000 7fff .
*
*   OUTPUTS:
*
*     min of var1 and var2
*
*   RETURN VALUE:
*
*     out
*                     16 bit long signed integer (int16) whose value
*                     falls in the range
*                     0x0000 8000 <= var1 <= 0x0000 7fff .*
*   IMPLEMENTATION:
*
*     gets the minimum of the inputs.
*   KEYWORDS: minimum, optimum
*
*************************************************************************/
int16 s16_min_s16_s16(int16 var1, int16 var2)
{
    int16 out;
    out = (var1<var2)?var1:var2;

#ifdef WMOPS_FX
    counter_fx.minmax++;
#endif

    return (out);

} /* end of s16_min_s16_s16 func.*/
#endif //#if !defined(s16_min_s16_s16)

#if !defined(s32_min_s32_s32)
/***************************************************************************
*
*   FUNCTION NAME: s32_min_s32_s32
*
*   PURPOSE:
*
*     Perform the addition of the two 32 bit input variables
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     min of var1 and var2
*
*   RETURN VALUE:
*
*     out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*   IMPLEMENTATION:
*
*     gets the minimum of the inputs.
*   KEYWORDS: minimum, optimum
*
*************************************************************************/
int32 s32_min_s32_s32(int32 var1, int32 var2)
{
    int32 out;

    out = (var1<var2)?var1:var2;

#ifdef WMOPS_FX
    counter_fx.minmax++;
#endif

    return (out);

} /* end of s32_min_s32_s32 func.*/
#endif //#if !defined(s32_min_s32_s32)

#if !defined(s32_max_s32_s32)
/***************************************************************************
*
*   FUNCTION NAME: s32_max_s32_s32
*
*   PURPOSE:
*
*     Perform the max of the two 32 bit input variables
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     max of var1 and var2
*
*   RETURN VALUE:
*
*     out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*   IMPLEMENTATION:
*
*     gets the maximum of the inputs.
*   KEYWORDS: maximum, optimum
*
*************************************************************************/
int32 s32_max_s32_s32(int32 var1, int32 var2)
{
    int32 out;

    out = (var1>var2)?var1:var2;

#ifdef WMOPS_FX
    counter_fx.minmax++;
#endif

    return (out);

} /* end of s32_max_s32_s32 func.*/
#endif //#if !defined(s32_max_s32_s32)

#if !defined(s16_max_s16_s16)
/***************************************************************************
*
*   FUNCTION NAME: s16_max_s16_s16
*
*   PURPOSE:
*
*     Perform the addition of the two 16 bit input variables
*
*   INPUTS:
*
*     var1
*                     16 bit long signed integer (int16) whose value
*                     falls in the range
*                     0x0000 8000 <= var1 <= 0x0000 7fff .
*     var2
*                     16 bit long signed integer (int16) whose value
*                     falls in the range
*                     0x0000 8000 <= var1 <= 0x0000 7fff .
*
*   OUTPUTS:
*
*     max of var1 and var2
*
*   RETURN VALUE:
*
*     out
*                     16 bit long signed integer (int16) whose value
*                     falls in the range
*                     0x0000 8000 <= var1 <= 0x0000 7fff .*
*   IMPLEMENTATION:
*
*     gets the maximum of the inputs.
*   KEYWORDS: maximum, optimum
*
*************************************************************************/
int16 s16_max_s16_s16(int16 var1, int16 var2)
{
    int16 out;

    out = (var1>var2)?var1:var2;

#ifdef WMOPS_FX
    counter_fx.minmax++;
#endif

    return (out);

} /* end of s16_max_s16_s16 func.*/
#endif //#if !defined(s16_max_s16_s16)

#if !defined(s32_add_s32_s32)
/***************************************************************************
*
*   FUNCTION NAME: s32_add_s32_s32
*
*   PURPOSE:
*
*     Perform the addition of the two 32 bit input variables
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var2 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*
*     Perform the addition of the two 32 bit input variables
*     L_Out = var1 + var2
*
*     The result might overflow and there is no protection against it.
*   KEYWORDS: s32_add_s32_s32, addition
*
*************************************************************************/
int32 s32_add_s32_s32(int32 var1, int32 var2)
{
    int32 L_Sum;

    L_Sum = var1 + var2;

#ifdef WMOPS_FX
    counter_fx.add32++;
#endif

    return (L_Sum);

}
#endif //#if !defined(s32_add_s32_s32)

#if !defined(s64_add_s32_s32)
/***************************************************************************
*
*   FUNCTION NAME: s64_add_s32_s32
*
*   PURPOSE:
*
*     Perform the addition of the two 32 bit input variables
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var2 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   IMPLEMENTATION:
*
*     Perform the addition of the two 32 bit input variables
*     L_Out = var1 + var2
*
*     The result might overflow and there is no protection against it.
*   KEYWORDS: s64_add_s32_s32, addition
*
*************************************************************************/
int64 s64_add_s32_s32(int32 var1, int32 var2)
{
    int64 L_Sum;

    L_Sum = (int64) var1 + (int64) var2;

#ifdef WMOPS_FX
    counter_fx.add32++;
#endif

    return (L_Sum);

}
#endif //#if !defined(s64_add_s32_s32)

#if !defined(s64_add_s32_u32)
/***************************************************************************
*
*   FUNCTION NAME: s64_add_s32_u32
*
*   PURPOSE:
*
*     Perform the addition of the two 32 bit input variables
*
*   INPUTS:
*
*     L_var1
*                     32 bit long signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= L_var1 <= 0x7fff ffff.
*     L_var2
*                     32 bit long unsigned integer (uint32) whose value
*                     falls in the range
*                     0x0 <= L_var2 <= 0xffff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 <= L_var1 <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*
*     Perform the addition of the two 32 bit input variables
*     L_Out = L_var1 + L_var2
*
*     The result might overflow and there is no protection against it.
*   KEYWORDS: add, addition
*
***************************************************************************/
int64 s64_add_s32_u32(int32 var1, uint32 var2)
{
   int64 L_Sum;

   L_Sum = (int64) var1 + (int64)var2;

#ifdef WMOPS_FX
   counter_fx.add64++;
#endif

   return (L_Sum);
}
#endif //#if !defined(s64_add_s32_u32)

#if !defined(s64_add_s64_s64)
/***************************************************************************
*
*   FUNCTION NAME: s64_add_s64_s64
*
*   PURPOSE:
*
*     Perform the addition of the two 64 bit input variables
*
*   INPUTS:
*
*     var1
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*     var2
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   IMPLEMENTATION:
*
*     Perform the addition of the two 64 bit input variables
*     L_Out = var1 + var2
*
*     The result might overflow and there is no protection against it.
*   KEYWORDS: s64_add_s64_s64, addition
*
*************************************************************************/
int64 s64_add_s64_s64(int64 var1, int64 var2)
{
    int64 L_Sum;

    L_Sum =  var1 + var2;

#ifdef WMOPS_FX
    counter_fx.add64++;
#endif

    return (L_Sum);
}
#endif //#if !defined(s64_add_s64_s64)


#if !defined(s64_add_s64_s32)
/***************************************************************************
*
*   FUNCTION NAME: s64_add_s64_s32
*
*   PURPOSE:
*
*     Perform the addition of the two 32 bit input variables
*
*   INPUTS:
*
*     var1
*                     64 bit signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*     var2
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var2 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   IMPLEMENTATION:
*
*     Perform the addition of the a 32 bit and a 64 bit input variables
*     L_Out = var1 + var2
*
*     The result might overflow and there is no protection against it.
*   KEYWORDS: s64_add_s64_s32, addition
*
*************************************************************************/
int64 s64_add_s64_s32(int64 var1, int32 var2)
{
    int64 L_Sum;

    L_Sum = (int64) var1 + var2;

#ifdef WMOPS_FX
    counter_fx.add64++;
#endif

    return (L_Sum);
}
#endif //#if !defined(s64_add_s64_s32)

#if !defined(s32_add_s32_s32_sat)
/***************************************************************************
*
*   FUNCTION NAME: s32_add_s32_s32_sat
*
*   PURPOSE:
*
*     Perform the addition of the two 32 bit input variables with
*     saturation.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var2 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*
*     Perform the addition of the two 32 bit input variables with
*     saturation.
*
*     L_Out = var1 + var2
*
*     L_Out is set to 0x7fff ffff if the operation results in an
*     overflow.  L_Out is set to 0x8000 0000 if the operation
*     results in an underflow.
*
*   KEYWORDS: s32_add_s32_s32_sat, addition
*
*************************************************************************/
int32 s32_add_s32_s32_sat(int32 var1, int32 var2)
{
    int32 L_Sum;
    int64 dSum;

    dSum = (int64) var1 + (int64) var2;

    /* safely saturate the 64-bit result back to 32-bit */
    L_Sum = s32_saturate_s64(dSum);  // OP_COUNT(-4);

#ifdef WMOPS_FX
    counter_fx.saturate--;
    counter_fx.add32_sat++;
#endif

    return (L_Sum);
}
#endif //#if !defined(s32_add_s32_s32_sat)

#if !defined(s32_deposit_s16_l)
/***************************************************************************
*
*   FUNCTION NAME: s32_deposit_s16_l
*
*   PURPOSE:
*
*     Put the 16 bit input into the 16 LSB's of the output int32 with
*     sign extension i.e. the top 16 bits are set to either 0 or 0xffff.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0xffff 8000 <= var1 <= 0x0000 7fff.
*
*   KEYWORDS: deposit, assign
*
*************************************************************************/

int32 s32_deposit_s16_l(int16 var1)
{
    int32 L_Out;

    L_Out = var1;

#ifdef WMOPS_FX
    counter_fx.deposit++;
#endif

    return (L_Out);
}
#endif //#if !defined(s32_deposit_s16_l)

#if !defined(s32_mac_s32_s16_s16_sat)
/***************************************************************************
*
*   FUNCTION NAME: s32_mac_s32_s16_s16_sat
*
*   PURPOSE:
*
*     Multiply accumulate.  Multiply two 16 bit
*     numbers together with saturation.  Add that result to the
*     32 bit input with saturation.  Return the 32 bit result.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     var3
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var2 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*
*     Multiply two 16 bit numbers together.
*     Add that result to the 32 bit input with saturation.
*     Return the 32 bit result.
*
*     Please note that this is not a true multiply accumulate as
*     most processors would implement it.  The 0x8000*0x8000
*     causes and overflow for this instruction.  On most
*     processors this would cause an overflow only if the 32 bit
*     input added to it were positive or zero.
*
*   KEYWORDS: mac, multiply accumulate
*
*************************************************************************/

int32 s32_mac_s32_s16_s16_sat(int32 var3, int16 var1, int16 var2)
{
    int32 L_product;

    L_product = (int32) var1 *var2;  /* integer multiply */

    L_product = s32_add_s32_s32_sat(var3, L_product);  // OP_COUNT(-2);

#ifdef WMOPS_FX
    counter_fx.mac16_sat++;
    counter_fx.add32_sat--;
#endif

    return (L_product);
}
#endif // #if !defined(s32_mac_s32_s16_s16_sat)

#if !defined(s64_mac_s64_s16_s16_shift)
/***************************************************************************
*
*   FUNCTION NAME: s64_mac_s64_s16_s16_shift
*
*   PURPOSE:
*
*     Multiply accumulate.  Multiply two 16 bit
*     numbers.  Shift the result and add that result to the
*     64 bit input.  Return the 64 bit result.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     var3
*                     64 bit signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var2 <= 0x7fff ffff ffff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   IMPLEMENTATION:
*
*     Multiply two 16 bit numbers together.
*
*     Shift the result and add it to the 64 bit input.
*     Return the 64 bit result.
*
*   KEYWORDS: mac, multiply accumulate
*
*************************************************************************/

int64 s64_mac_s64_s16_s16_shift(int64 var3, int16 var1, int16 var2, int16 shift)
{
    int32 productL32;
    int64 resultL64;

    productL32 = (int32) var1 *var2;  /* integer multiply */
    if(shift > 0)
    {
        resultL64 = ((int64)productL32)<<shift;
    }
    else
    {
        /* no overflow possible in mult */
        resultL64 = ((int64)productL32)>>(-shift);
    }
    resultL64 = resultL64 + var3;


#ifdef WMOPS_FX
    counter_fx.mac16++;
#endif

    return (resultL64);
}
#endif //#if !defined(s64_mac_s64_s16_s16_shift)

#if !defined(s64_mac_s64_s16_s16_shift_nosat)
/***************************************************************************
*
*   FUNCTION NAME: s64_mac_s64_s16_s16_shift_nosat
*
*   PURPOSE:
*
*     Multiply accumulate.  Multiply two 16 bit
*     numbers.  Shift the result and add that result to the
*     64 bit input.  Return the 64 bit result without saturation.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     var3
*                     64 bit signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var2 <= 0x7fff ffff ffff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   IMPLEMENTATION:
*
*     Multiply two 16 bit numbers together.
*
*     Shift the result and add it to the 64 bit input.
*     Return the 64 bit result without saturation.
*
*   KEYWORDS: mac, multiply accumulate
*
*************************************************************************/

int64 s64_mac_s64_s16_s16_shift_nosat(int64 var3, int16 var1, int16 var2, int16 shift)
{
    int32 productL32;
    int64 resultL64;

    productL32 = (int32) var1 *var2;  /* integer multiply */
    if(shift > 0)
    {
        resultL64 = ((int64)productL32)<<shift;
    }
    else
    {
        /* no overflow possible in mult */
        resultL64 = ((int64)productL32)>>(-shift);
    }
    resultL64 = resultL64 + var3;


#ifdef WMOPS_FX
    counter_fx.mac16++;
#endif

    return (resultL64);
}
#endif //#if !defined(s64_mac_s64_s16_s16_shift_nosat)


#if !defined(s64_mac_s64_s16_s16_shift_sat)
/***************************************************************************
*
*   FUNCTION NAME: s64_mac_s64_s16_s16_shift_sat
*
*   PURPOSE:
*
*     Multiply accumulate.  Multiply two 16 bit
*     numbers . Shift the result and add that result to the
*     64 bit input with saturation.  Return the 64 bit result.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     var3
*                     64 bit  signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var2 <= 0x7fff ffff ffff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   IMPLEMENTATION:
*
*     Multiply two 16 bit numbers.
*
*     Shift the result and add that result to the 64 bit input with saturation.
*     Return the 64 bit result.
*
*   KEYWORDS: mac, multiply accumulate
*
*************************************************************************/

int64 s64_mac_s64_s16_s16_shift_sat(int64 var3, int16 var1, int16 var2, int16 shift)
{
    int32 resultL32;
    int64 resultL64;

    resultL64 = s64_mac_s64_s16_s16_shift(var3, var1, var2, shift);
    resultL32 = s32_saturate_s64(resultL64);

#ifdef WMOPS_FX
    counter_fx.saturate--;
    counter_fx.mac16--;
    counter_fx.mac16_sat++;
#endif

    return (resultL32);
}
#endif // #if !defined(s64_mac_s64_s16_s16_shift_sat)

#if !defined(s32_msu_s32_s16_s16_sat)
/***************************************************************************
*
*   FUNCTION NAME: s32_msu_s32_s16_s16_sat
*
*   PURPOSE:
*
*     Multiply and subtract.  Multiply two 16 bit
*     numbers together with saturation.  Subtract that result from
*     the 32 bit input with saturation.  Return the 32 bit result.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     var3
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var2 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*
*     Multiply two 16 bit numbers together with
*     saturation.  The only numbers which will cause saturation on
*     the multiply are 0x8000 * 0x8000.
*
*     Subtract that result from the 32 bit input with saturation.
*     Return the 32 bit result.
*
*     Please note that this is not a true multiply accumulate as
*     most processors would implement it.  The 0x8000*0x8000
*     causes and overflow for this instruction.  On most
*     processors this would cause an overflow only if the 32 bit
*     input added to it were negative or zero.
*
*   KEYWORDS: mac, multiply accumulate, msu
*
*************************************************************************/

int32 s32_msu_s32_s16_s16_sat(int32 var3, int16 var1, int16 var2)
{
    int32 L_product;

    L_product = (int32) var1 *var2;  /* integer multiply */
    if (L_product == (int32) LONGWORD_HALF)
    {
        /* the event 0x8000 * 0x8000, the only possible saturation
        * in the multiply */
        L_product = s32_saturate_s64((int64) var3 - (int64)(LONGWORD_MIN)); // OP_COUNT(-4);

#ifdef WMOPS_FX
        counter_fx.saturate--;
#endif

    }
    else
    {
        /* no overflow possible in mult */
        L_product <<= 1;
        L_product = s32_sub_s32_s32_sat(var3, L_product);  // OP_COUNT(-2); /* LT 6/96 */

#ifdef WMOPS_FX
        counter_fx.add32_sat--;
#endif

    }

#ifdef WMOPS_FX
    counter_fx.mac16_sat++;
#endif

    return (L_product);
}
#endif //#if !defined(s32_msu_s32_s16_s16_sat)

#if !defined(s64_mult_s32_s16_shift)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_s32_s16_shift
*
*   PURPOSE:
*
*     Perform multiplication of a 32-bit number with 16-bit number with
*     Q factor compensation.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     shift
*                    16 bit number that gives shift for Q factor compensation.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   IMPLEMENTATION:
*     multiplies a 32 -bit number with 16-bit number and shifts
*   it left or right.
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_s32_s16_shift( int32 var1, int16 var2, int16 shift)
{
    int64 resultL64;

	resultL64 = s64_mult_s32_s32(var1, (int32)var2);
	resultL64 = s64_shl_s64(resultL64,shift-16);


    return resultL64;
} 
#endif //#if !defined(s64_mult_s32_s16_shift)

#if !defined(s64_mult_s32_s16)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_s32_s16
*
*   PURPOSE:
*
*     Perform multiplication of a 32-bit number with 16-bit number.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long long signed integer (int64).
*
*   IMPLEMENTATION:
*     multiplies a 32 -bit number with 16-bit number and save into 64 bit
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_s32_s16(int32 var1, int16 var2)
{
    int32 tempProdL32;
    int64 resultL64;
    int16 inL32LW;
    int16 inL32MW;

    inL32LW = s16_extract_s32_l(var1);
    inL32MW = s16_extract_s32_h(var1);

    resultL64 = s64_shl_s64(s32_mult_s16_s16(var2, inL32MW), 16);
    tempProdL32 = s32_mult_s16_u16(var2, inL32LW);
    resultL64 = s64_add_s64_s64(resultL64, tempProdL32);

#ifdef WMOPS_FX
    counter_fx.mult_32_16++;
    counter_fx.extract -= 2;
    counter_fx.sh--;
    counter_fx.mult16 -=2;
    counter_fx.add64--;
#endif

    return resultL64;
} /* End of s64_mult_s32_s16 function*/
#endif // !defined(s64_mult_s32_s16)

#if !defined(s64_mult_u32_s16)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_u32_s16
*
*   PURPOSE:
*
*     Perform multiplication of a 32-bit number with 16-bit number.
*
*   INPUTS:
*
*     var1
*                     32 bit unsigned integer (uint32) whose value
*                     falls in the range 0x0000 0000 <= var1 <= 0xffff ffff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long long signed integer (int64).
*
*   IMPLEMENTATION:
*     multiplies a 32 -bit number with 16-bit number and save into 64 bit
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_u32_s16(uint32 var1, int16 var2)
{
    int32 tempProdL32;
    int64 resultL64;
    int16 inL32LW;
    int16 inL32MW;

    inL32LW = s16_extract_s32_l(var1);
    inL32MW = s16_extract_s32_h(var1);

    resultL64 = s64_shl_s64(s32_mult_s16_u16(var2, inL32MW), 16);
    tempProdL32 = s32_mult_s16_u16(var2, inL32LW);
    resultL64 = s64_add_s64_s64(resultL64, tempProdL32);

#ifdef WMOPS_FX
    counter_fx.mult_32_16++;
    counter_fx.extract -= 2;
    counter_fx.sh--;
    counter_fx.mult16 -=2;
    counter_fx.add64--;
#endif

    return resultL64;
} /* End of s64_mult_u32_s16 function*/
#endif // !defined(s64_mult_u32_s16)

#if !defined(s64_mult_s32_u16_shift)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_s32_u16_shift
*
*   PURPOSE:
*
*     Perform multiplication of a 32-bit number with 16-bit number with
*     Q factor compensation.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     shift
*                    16 bit number that gives shift for Q factor compensation.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   IMPLEMENTATION:
*     multiplies a 32 -bit number with 16-bit number and shifts
*   it left or right.
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_s32_u16_shift( int32 var1, uint16 var2, int16 shift)
{
    int64 resultL64;


	resultL64 = s64_mult_s32_s32(var1, (int32)var2);
	resultL64 = s64_shl_s64(resultL64,shift-16);


    return resultL64;
} 
#endif //#if !defined(s64_mult_s32_u16_shift)

#if !defined(s64_mult_u32_s16_shift)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_u32_s16_shift
*
*   PURPOSE:
*
*     Perform multiplication of a 32-bit number with 16-bit number with
*     Q factor compensation.
*
*   INPUTS:
*
*     var1
*                     32 bit unsigned integer (int32) whose value
*                     falls in the range 0x0000 0000 <= var1 <= 0xffff ffff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     shift
*                    16 bit number that gives shift for Q factor compensation.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   IMPLEMENTATION:
*     multiplies a 32 -bit number with 16-bit number and shifts
*   it left or right.
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_u32_s16_shift( uint32 var1, int16 var2, int16 shift)
{
    int64 resultL64;


	resultL64 = s64_mult_s32_u32((int32)var2, var1);
	resultL64 = s64_shl_s64(resultL64,shift-16);


    return resultL64;
} 
#endif //#if !defined(s64_mult_u32_s16_shift)

#if !defined(s32_mult_s32_s32_rnd_sat)
/***************************************************************************
*
*   FUNCTION NAME: s32_mult_s32_s32_rnd_sat
*
*   PURPOSE:
*
*     Perform a fractional multipy of the two 32 bit input numbers
*     with rounding and saturation.  Output a 32 bit number.
*
*   INPUTS:
*
*     var1
*                     32 bit short signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff 0000.
*     var2
*                     32 bit short signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var2 <= 0x7fff 0000.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*     Multiplies two 32 bit numbers and produces a 32-bit number as output.
*     It does the following operations:
*      resultL64 = in1L32 * in2L32;
*      s32_sat_s40_round(resultL64) to 32 bits.
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int32 s32_mult_s32_s32_rnd_sat(int32 var1, int32 var2)
{
    int32 resultL32;
    int16 in1MW, in2MW;
    uint16 in1LW, in2LW;
    int32 temp1ProdL32, temp2ProdL32;
    uint32 prodUL32;
    int64 tempSumL64;
    uint32 lProdL32;

    in1LW = (uint16) s16_extract_s32_l(var1); // lower word of first input
    in2LW = (uint16) s16_extract_s32_l(var2); // lower word of second input

    in2MW = s16_extract_s32_h(var2);          // higher word of first input
    in1MW = s16_extract_s32_h(var1);          // higher word of second input


    prodUL32 = u32_mult_u16_u16(in1LW, in2LW);
    temp1ProdL32 =  s32_mult_s16_u16(in1MW, in2LW);
    temp2ProdL32 =  s32_mult_s16_u16(in2MW, in1LW);

    lProdL32 = (uint32)s16_extract_s32_h(prodUL32);
    tempSumL64 = s64_add_s32_s32(temp1ProdL32, temp2ProdL32);
    tempSumL64 = s64_add_s64_s32(tempSumL64, (int32)lProdL32);
    tempSumL64 = s64_add_s64_s32(tempSumL64, (int32)0x8000);    // rounding

    temp2ProdL32 = s32_mult_s16_s16(in1MW, in2MW);

    tempSumL64 = s64_shl_s64(tempSumL64,-16);
    tempSumL64 = s64_add_s64_s32(tempSumL64, temp2ProdL32);

    resultL32 = s32_saturate_s64(tempSumL64);

#ifdef WMOPS_FX
    counter_fx.mult_32_32++;
    counter_fx.saturate--;
    counter_fx.extract -= 5;
    counter_fx.mult16 -=4;
    counter_fx.add32--;
    counter_fx.add64 -=3;
    counter_fx.sh--;
#endif

    return (resultL32);
}
#endif //#if !defined(s32_mult_s32_s32_rnd_sat)

#if !defined(s64_mult_s32_u32_shift)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_s32_u32_shift
*
*   PURPOSE:
*
*     Perform a fractional multipy of the two 32 bit input numbers
*     with shift.  Output a 32 bit number.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     32 bit unsigned integer (uint32) whose value
*                     falls in the range 0x8000 0000 <= var2 <= 0x7fff ffff.
*     shift
*                    shift value of 3 is allowed since thats what is allowed in DSP
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long signed integer (int64)
*
*   IMPLEMENTATION:
*     Multiplies two 32 bit numbers and produces a 64-bit number as output.
*     It does the following operations:
*      resultL64 = in1L32 * uin2L32 << shift;
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_s32_u32_shift(int32 var1, uint32 var2, int16 shift)
{
	int64 resultL64;

	resultL64 = s64_mult_s32_u32(var1, var2);
	resultL64 = s64_shl_s64(resultL64,shift-32);


	return resultL64;
}
#endif //#if !defined(s64_mult_s32_u32_shift)

#if !defined(s64_mult_lp_s32_u32_shift)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_lp_s32_u32_shift
*
*   PURPOSE:
*
*     Perform a low precision fractional multipy of the two 32 bit input numbers
*     with shift.  Output a 64 bit number.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     32 bit unsigned integer (uint32) whose value
*                     falls in the range 0x8000 0000 <= var2 <= 0x7fff ffff.
*     shift
*                    shift value of 3 is allowed since thats what is allowed in DSP
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long signed integer (int64)
*
*   IMPLEMENTATION:
*     Multiplies two 32 bit numbers and produces a 64-bit number as output.
*     It does the following operations:
*      resultL64 = in1L32 * uin2L32 << shift;
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_lp_s32_u32_shift(int32 var1, uint32 var2, int16 shift)
{
    int16 in1MW;
    uint16 in1LW, in2LW, uin2MW;
    int32 temp1ProdL32, temp2ProdL32;
    int64 tempSumL64;

    in1LW = (uint16) s16_extract_s32_l(var1);     // lower word of first input
    in2LW = (uint16) s16_extract_s32_l(var2);     // lower word of second input

    in1MW = s16_extract_s32_h(var1);            // higher word of first input
    uin2MW = s16_extract_s32_h(var2);           // higher word of second input

    //temp1ProdL32 is   signed32 = signed * unSigned
    //temp1ProdL32 is unSigned32 = unSigned * unSigned
    temp1ProdL32 =  s32_mult_s16_u16(in1MW, in2LW);
    temp2ProdL32 =  u32_mult_u16_u16(uin2MW, in1LW);

    // cross product terms added.
    //tempSumL64 is signed64 = signed32 + unSigned32
    //that is why we need to use s64_add_s64_u32()
    //to save unSigned number from becoming sign extended
    tempSumL64 = s64_add_s32_u32(temp1ProdL32, temp2ProdL32);

    // cross product
    tempSumL64 = s64_shl_s64(tempSumL64,shift);

    temp2ProdL32 = s32_mult_s16_u16(in1MW, uin2MW);

    tempSumL64 = s64_shl_s64(tempSumL64,-16);
    tempSumL64 = s64_add_s64_s64(tempSumL64, s64_shl_s64(temp2ProdL32,shift));

#ifdef WMOPS_FX
    counter_fx.mult_32_32++;
    counter_fx.extract -= 5;
    counter_fx.mult16 -=3;
    counter_fx.add32--;
    counter_fx.add64 -=2;
    counter_fx.sh -= 4;
#endif

    return (tempSumL64);
}
#endif // #if !defined(s64_mult_lp_s32_u32_shift)

#if !defined(s64_mult_s32_s32_shift)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_s32_s32_shift
*
*   PURPOSE:
*
*     Perform full precision multiplication of two 32 bit input numbers
*     with shift.  Output a 64 bit number.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var2 <= 0x7fff ffff.
*     shift
*                     16 bit number that gives shift for Q factor compensation.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long signed integer (int64)
*
*   IMPLEMENTATION:
*     Multiplies two 32 bit numbers and produces a 64-bit number as output.
*     
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_s32_s32_shift(int32 var1, int32 var2, int16 shift)
{
   int64 resultL64;

   resultL64 = s64_mult_s32_s32(var1, var2);
   resultL64 = s64_shl_s64(resultL64,shift-32);


   return resultL64;
}
#endif //#if !defined(s64_mult_s32_s32_shift)

#if !defined(s64_mult_lp_s32_s32_shift)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_lp_s32_s32_shift
*
*   PURPOSE:
*
*     Perform a low precision multipy of the two 32 bit input numbers with shift
*     Output a 64 bit number.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff 7fff.
*     var2
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var2 <= 0x7fff 7fff.
*     shift
*                    shift value of 3 is allowed since thats what is allowed in DSP
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long signed integer (int64)
*
*   IMPLEMENTATION:
*     Multiplies two 32 bit numbers and produces a 64-bit number as output.
*     It does the following operations:
*      resultL64 = in1L32 * in2L32 << shift;
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_lp_s32_s32_shift(int32 var1, int32 var2, int16 shift)
{
    int16 in1MW, in2MW;
    uint16 in1LW, in2LW;
    int32 temp1ProdL32, temp2ProdL32;
    int64 tempSumL64;

    in1LW = (uint16) s16_extract_s32_l(var1); // lower word of first input
    in2LW = (uint16) s16_extract_s32_l(var2); // lower word of second input

    in1MW = s16_extract_s32_h(var1);          // higher word of first input
    in2MW = s16_extract_s32_h(var2);          // higher word of second input

    temp1ProdL32 =  s32_mult_s16_u16(in1MW, in2LW);
    temp2ProdL32 =  s32_mult_s16_u16(in2MW, in1LW);

    tempSumL64 = s64_add_s32_s32(temp1ProdL32, temp2ProdL32);
    tempSumL64 = s64_shl_s64(tempSumL64,shift);

    temp2ProdL32 = s32_mult_s16_s16(in1MW, in2MW);

    tempSumL64 = s64_shl_s64(tempSumL64,-16);
    tempSumL64 = s64_add_s64_s64(tempSumL64, s64_shl_s64(temp2ProdL32,shift));

#ifdef WMOPS_FX
    //counter_fx.mult_32_32++;
    counter_fx.mult_32_32 = counter_fx.mult_32_32  + 1.0;
    counter_fx.extract -= 5;
    counter_fx.mult16 -=3;
    counter_fx.add32--;
    counter_fx.add64 -=2;
    counter_fx.sh -= 4;
#endif

    return (tempSumL64);
}
#endif //#if !defined(s64_mult_lp_s32_s32_shift)

#if !defined(s64_mult_s16_u16_shift)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_s16_u16_shift
*
*   PURPOSE:
*
*     Perform a fractional multipy of the two 16 bit input numbers
*     with out saturation.  Output a 32 bit number.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   IMPLEMENTATION:
*
*     Multiply the two 16 bit input numbers and shift the result.
*     Output the 64 - bit result.
*
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_s16_u16_shift(int16 var1, uint16 var2, int16 shift)
{
    int64 resultL64;
    int32 productL32;

    productL32 = (int32) var1*var2;

    if(shift > 0)
    { /* positive shift indicating left shift */
        resultL64 = (int64) productL32 << shift ;
    }
    else
    { /* negative shift indicating right shift */
        resultL64 = (int64) productL32 >> (-shift);
    } /* end of shifting */

#ifdef WMOPS_FX
    counter_fx.mult16++;
#endif

    return (resultL64);
}
#endif //#if !defined(s64_mult_s16_u16_shift)

#if !defined(s32_mult_s16_u16)
/***************************************************************************
*
*   FUNCTION NAME: s32_mult_s16_u16
*
*   PURPOSE:
*
*     Perform a multiplication of a signed number with unsigned number.
*     output is a signed 32-bit number.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short unsigned integer (uint16) whose value
*                     falls in the range 0x0000 0000 <= var2 <= 0x0000 ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     productL32
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*   Does a signed times unsigned 16 bit multiplication and returns the value.
*
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/

int32 s32_mult_s16_u16(int16 var1, uint16 var2)
{
   int32 productL32;

   productL32 = (int32)var1*var2;

#ifdef WMOPS_FX
   counter_fx.mult16++;
#endif

   return (productL32);
}
#endif //#if !defined(s32_mult_s16_u16)

#if !defined(s64_mult_s16_s16_shift)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_s16_s16_shift
*
*   PURPOSE:
*
*     Perform a fractional multipy of the two 16 bit input numbers
*     with out saturation.  Output a 32 bit number.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                    64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*   IMPLEMENTATION:
*
*     Multiply the two 16 bit input numbers and shift the result.
*     Output the 64 - bit result.
*
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_s16_s16_shift(int16 var1, int16 var2, int16 shift)
{
    int64 productL64;
    int32 productL32;

    productL32 = (int32) var1 *var2;  /* integer multiply */

    if(shift > 0)
    { /* positive shift indicating left shift */
        productL64 = (int64) productL32 << shift ;
    }
    else
    { /* negative shift indicating right shift */
        productL64 = (int64) productL32 >> (-shift) ;
    } /* end of shifting */


#ifdef WMOPS_FX
    counter_fx.mult16++;
#endif

    return (productL64);
}
#endif //#if !defined(s64_mult_s16_s16_shift)

#if !defined(s32_mult_s16_s16)
/***************************************************************************
*
*   FUNCTION NAME: s32_mult_s16_s16
*
*   PURPOSE:
*
*     Perform a fractional multipy of the two 16 bit input numbers
*     with saturation.  Output a 32 bit number.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*
*     Multiply the two the two 16 bit input numbers. If the
*     result is within this range, left shift the result by one
*     and output the 32 bit number.  The only possible overflow
*     occurs when var1==var2==-0x8000.  In this case output
*     0x7fff ffff.
*
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int32 s32_mult_s16_s16(int16 var1, int16 var2)
{
    int32 productL32;

    productL32 = (int32) var1 *var2;  /* integer multiply */

#ifdef WMOPS_FX
    counter_fx.mult16++;
#endif

    return (productL32);
}
#endif //#if !defined(s32_mult_s16_s16)

#if !defined(u32_mult_u16_u16)
/***************************************************************************
*
*   FUNCTION NAME: u32_mult_u16_u16
*
*   PURPOSE:
*
*     Perform a fractional multipy of the two 16 bit input unsigned numbers
*     with saturation.  Output a 32 bit unsigned number.
*
*   INPUTS:
*
*     var1
*                     16 bit short unsigned integer (uint16) whose value
*                     falls in the range 0x0000 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short unsigned integer (uint16) whose value
*                     falls in the range 0x0000 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit long unsigned integer (uint32) whose value
*                     falls in the range
*                     0x0000 0000 <= var1 <= 0xffff ffff.
*
*   IMPLEMENTATION:
*
*     Multiply the two the two 16 bit input numbers.
*
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
uint32 u32_mult_u16_u16(uint16 var1, uint16 var2)
{
    uint32 productL32;

    productL32 = (uint32) var1 *var2; /* integer multiply */

#ifdef WMOPS_FX
    counter_fx.mult16++;
#endif

    return (productL32);
}
#endif //#if !defined(u32_mult_u16_u16)

#if !defined(s64_mult_u16_u16_shift)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_u16_u16_shift
*
*   PURPOSE:
*
*     Perform a fractional multipy of the two 16 bit input unsigned numbers
*     with saturation.  Output a 32 bit unsigned number.
*
*   INPUTS:
*
*     var1
*                     16 bit short unsigned integer (uint16) whose value
*                     falls in the range 0x0000 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short unsigned integer (uint16) whose value
*                     falls in the range 0x0000 8000 <= var2 <= 0x0000 7fff.
*
*     shift           a singed value shift.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long unsigned integer (uint64) whose value
*                     falls in the range
*                     0x0000 0000 0000 0000 <= var1 <= 0xffff ffff ffff ffff.
*
*   IMPLEMENTATION:
*
*     Multiply the two the two 16 bit input numbers.
*
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_u16_u16_shift(uint16 var1, uint16 var2, int16 shift)
{
    uint32 productL32;
    int64  outputL64;

    productL32 = (uint32) var1 *var2;   /* integer multiply */

    outputL64 = s64_shl_s64(productL32, shift);

#ifdef WMOPS_FX
    counter_fx.mult16++;
    counter_fx.sh--;
#endif

    return (outputL64);
}
#endif //#if !defined(s64_mult_u16_u16_shift)

#if !defined(s32_mult_s16_s16_shift_sat)
/***************************************************************************
*
*   FUNCTION NAME: s32_mult_s16_s16_shift_sat
*
*   PURPOSE:
*
*     Perform a fractional multipy of the two 16 bit input numbers
*     with saturation.  Output a 32 bit number.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= L_var1 <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*
*     Multiply the two the two 16 bit input numbers. If the
*     result is within this range, left shift the result by one
*     and output the 32 bit number.  The only possible overflow
*     occurs when var1==var2==-0x8000.  In this case output
*     0x7fff ffff.
*
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int32 s32_mult_s16_s16_shift_sat(int16 var1, int16 var2)
    {
  int32 productL32;

  if (var1 == SHORTWORD_MIN && var2 == SHORTWORD_MIN)
    {
      productL32 = LONGWORD_MAX;        /* overflow */
      giOverflow = 1;
    }
  else
    {
      productL32 = (int32) var1 *var2;  /* integer multiply */

      productL32 = productL32 << 1;
    }

#ifdef WMOPS_FX
  counter_fx.mult16_sat++;
#endif

  return (productL32);
}
#endif //#if !defined(s32_mult_s16_s16_shift_sat)

#if !defined(s32_neg_s32_sat)
/***************************************************************************
*
*   FUNCTION NAME: s32_neg_s32_sat
*
*   PURPOSE:
*
*     Negate the 32 bit input. 0x8000 0000's negated value is
*     0x7fff ffff.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0001 <= var1 <= 0x7fff ffff.
*
*   KEYWORDS: s16_neg_s16_sat, negative
*
*************************************************************************/
int32 s32_neg_s32_sat(int32 var1)
{
    int32 L_Out;

    if (var1 == LONGWORD_MIN)
    {
        L_Out = LONGWORD_MAX;
        giOverflow = 1;
    }
    else
        L_Out = -var1;

#ifdef WMOPS_FX
    counter_fx.neg_sat++;
#endif

    return (L_Out);
}
#endif //#if !defined(s32_neg_s32_sat)

#if !defined(s64_shl_s64)
/***************************************************************************
*
*   FUNCTION NAME: s64_shl_s64
*
*   PURPOSE:
*
*    shift a 64 bit value left or right depending on the shift value.
*
*   INPUTS:
*
*     var1
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     var1
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var1 <= 0x7fff ffff ffff ffff.
*
*
*   IMPLEMENTATION:
*
*
*
*   KEYWORDS:
*
*************************************************************************/
int64 s64_shl_s64(int64 var1, int16 shift)
{
    int64 resultL64;

    if(shift > 0)
    {
        resultL64 = var1 << shift;
    }
    else
    {
        resultL64 = var1 >> (-shift);
    }
#ifdef WMOPS_FX
    counter_fx.sh++;
#endif

    return resultL64;
}
#endif //#if !defined(s64_shl_s64)

#if !defined(s32_shl_s32_rnd_sat)
/***************************************************************************
*
*   FUNCTION NAME: s32_shl_s32_rnd_sat
*
*   PURPOSE:
*
*     Shift and round.  Perform a shift right. After shifting, use
*     the last bit shifted out of the LSB to round the result up
*     or down.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*
*   IMPLEMENTATION:
*
*     Shift and round.  Perform a shift right. After shifting, use
*     the last bit shifted out of the LSB to round the result up
*     or down.  This is just like s16_shl_s16_sat_rnd above except that the
*     input/output is 32 bits as opposed to 16.
*
*     if var2 is positve perform a arithmetic left shift
*     with saturation (see s32_shl_s32_sat() above).
*
*     If var2 is zero simply return var1.
*
*     If var2 is negative perform a arithmetic right shift (s32_shr_s32_sat)
*     of var1 by (-var2)+1.  Add the LS bit of the result to
*     var1 shifted right (s32_shr_s32_sat) by -var2.
*
*     Note that there is no constraint on var2, so if var2 is
*     -0xffff 8000 then -var2 is 0x0000 8000, not 0x0000 7fff.
*     This is the reason the s32_shl_s32_sat function is used.
*
*
*   KEYWORDS:
*
*************************************************************************/
int32 s32_shl_s32_rnd_sat(int32 var1, int16 shift)
{
    int32 L_Out, L_rnd;

    if (shift < -31)
    {
        L_Out = 0;
    }
    else if (shift < 0)
    {
        /* right shift */
        L_rnd = s32_shl_s32_sat(var1, (int16)(shift + 1)) & 0x1;
        L_Out = s32_add_s32_s32_sat(s32_shl_s32_sat(var1, shift), L_rnd);

#ifdef WMOPS_FX
        counter_fx.sh_sat-=2;
        counter_fx.add32_sat--;
#endif

    }
    else
    {
        L_Out = s32_shl_s32_sat(var1, shift);

#ifdef WMOPS_FX
        counter_fx.sh_sat--;
#endif

    }

#ifdef WMOPS_FX
    counter_fx.sh_sat++;
#endif

    return (L_Out);
}
#endif //#if !defined(l_s16_shl_s16_sat_rnd)

#if !defined(s32_shl_s32_sat)
/***************************************************************************
*
*   FUNCTION NAME: s32_shl_s32_sat
*
*   PURPOSE:
*
*     Arithmetic shift left (or right).
*     Arithmetically shift the input left by var2.   If var2 is
*     negative then an arithmetic shift right (s32_shr_s32_sat) of var1 by
*     -var2 is performed.
*
*   INPUTS:
*
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*
*   IMPLEMENTATION:
*
*     Arithmetically shift the 32 bit input left by var2.  This
*     operation maintains the sign of the input number. If var2 is
*     negative then an arithmetic shift right (s32_shr_s32_sat) of var1 by
*     -var2 is performed.  See description of s32_shr_s32_sat for details.
*
*     Equivalent to the Full-Rate GSM ">> n" operation.  Note that
*     ANSI-C does not guarantee operation of the C ">>" or "<<"
*     operator for negative numbers.
*
*   KEYWORDS: shift, arithmetic shift left,
*
*************************************************************************/
int32 s32_shl_s32_sat(int32 var1, int16 shift)
{
    int32 L_Mask, L_Out = 0;
    int i, iOverflow = 0;

    if (shift == 0 || var1 == 0)
    {
        L_Out = var1;
    }
    else if (shift < 0)
    {
        if (shift <= -31)
        {
            if (var1 > 0)
                L_Out = 0;
            else
                L_Out = UMAX_32;
        }
        else
        {
            L_Out = s32_shr_s32_sat(var1, (int16)(-shift));

#ifdef WMOPS_FX
            counter_fx.sh_sat--;
#endif

        }
    }
    else
    {
        if (shift >= 31)
            iOverflow = 1;
        else
        {
            if (var1 < 0)
                L_Mask = LONGWORD_SIGN; /* sign bit mask */
            else
                L_Mask = 0x0;
            L_Out = var1;
            for (i = 0; i < shift && !iOverflow; i++)
            {
                /* check the sign bit */
                L_Out = (L_Out & LONGWORD_MAX) << 1;
                if ((L_Mask ^ L_Out) & LONGWORD_SIGN)
                    iOverflow = 1;
            }
        }

        if (iOverflow)
        {
            /* s16_saturate_s32 */
            if (var1 > 0)
                L_Out = LONGWORD_MAX;
            else
                L_Out = LONGWORD_MIN;

            giOverflow = 1;
        }
    }

#ifdef WMOPS_FX
    counter_fx.sh_sat++;
#endif

    return (L_Out);
}
#endif //#if !defined(s32_shl_s32_sat)

#if !defined(s32_shl_s32)
/***************************************************************************
*
*   FUNCTION NAME: s32_shl_s32
*
*   PURPOSE:
*
*     Arithmetic shift left (or right) without saturation.
*     Arithmetically shift the input left by var2.   If var2 is
*     negative then an arithmetic shift right (s32_shr_s32) of var1 by
*     -var2 is performed.
*
*   INPUTS:
*
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*
*   IMPLEMENTATION:
*
*     Arithmetically shift the 32 bit input left by var2.  This
*     operation maintains the sign of the input number. 
*
*     Equivalent to the Full-Rate GSM ">> n" operation.  Note that
*     ANSI-C does not guarantee operation of the C ">>" or "<<"
*     operator for negative numbers.
*
*   KEYWORDS: shift, arithmetic shift left,
*
*************************************************************************/
int32 s32_shl_s32(int32 var1, int16 shift)
{
    int32 L_Out;

    if (shift == 0 || var1 == 0)
    {
        L_Out = var1;
    }
    else if (shift < 0)
    {
        // perform right shift 
        L_Out = s32_shr_s32(var1, (int16)(-shift));
    }
    else
    {
        L_Out = var1 << shift;
    }

#ifdef WMOPS_FX
    counter_fx.sh_sat++;
#endif

    return (L_Out);    
}
#endif //#if !defined(s32_shl_s32)

#if !defined(s32_shr_s32_sat)
/***************************************************************************
*
*   FUNCTION NAME: s32_shr_s32_sat
*
*   PURPOSE:
*
*     Arithmetic shift right (or left).
*     Arithmetically shift the input right by var2.   If var2 is
*     negative then an arithmetic shift left (s32_shl_s32_sat) of var1 by
*     -var2 is performed.
*
*   INPUTS:
*
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*
*   IMPLEMENTATION:
*
*     Arithmetically shift the input right by var2.  This
*     operation maintains the sign of the input number. If var2 is
*     negative then an arithmetic shift left (s32_shl_s32_sat) of var1 by
*     -var2 is performed.  See description of s32_shl_s32_sat for details.
*
*     The input is a 32 bit number, as is the output.
*
*     Equivalent to the Full-Rate GSM ">> n" operation.  Note that
*     ANSI-C does not guarantee operation of the C ">>" or "<<"
*     operator for negative numbers.
*
*   KEYWORDS: shift, arithmetic shift right,
*
*************************************************************************/
int32 s32_shr_s32_sat(int32 var1, int16 shift)
{
    int32 L_Mask, L_Out;

    if (shift == 0 || var1 == 0)
    {
        L_Out = var1;
    }
    else if (shift < 0)
    {
        /* perform a left shift */
        /*----------------------*/
        if (shift <= -31)
        {
            /* s16_saturate_s32 */
            if (var1 > 0)
            {
                L_Out = LONGWORD_MAX;
                giOverflow = 1;
            }
            else
            {
                L_Out = LONGWORD_MIN;
                giOverflow = 1;
            }
        }
        else
        {
            L_Out = s32_shl_s32_sat(var1, (int16)(-shift));  //  OP_COUNT(-2);

#ifdef WMOPS_FX
            counter_fx.sh_sat--;
#endif

        }
    }
    else
    {

        if (shift >= 31)
        {
            if (var1 > 0)
                L_Out = 0;
            else
                L_Out = UMAX_32;
        }
        else
        {
            L_Mask = 0;
            if (var1 < 0)
            {
                L_Mask = ~L_Mask << (32 - shift);
            }
            var1 >>= shift;
            L_Out = L_Mask | var1;
        }
    }

#ifdef WMOPS_FX
    counter_fx.sh_sat++;
#endif

    return (L_Out);
}
#endif //#if !defined(s32_shr_s32_sat)

#if !defined(s32_shr_s32)
/***************************************************************************
*
*   FUNCTION NAME: s32_shr_s32
*
*   PURPOSE:
*
*     Arithmetic shift right (or left) without saturation.
*     Arithmetically shift the input right by var2.   If var2 is
*     negative then an arithmetic shift left (s32_shl_s32) of var1 by
*     -var2 is performed.
*
*   INPUTS:
*
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*
*   IMPLEMENTATION:
*
*     Arithmetically shift the input right by var2.  This
*     operation maintains the sign of the input number. If var2 is
*     negative then an arithmetic shift left (s32_shl_s32) of var1 by
*     -var2 is performed.  See description of s32_shl_s32 for details.
*
*     The input is a 32 bit number, as is the output.
*
*     Equivalent to the Full-Rate GSM ">> n" operation.  Note that
*     ANSI-C does not guarantee operation of the C ">>" or "<<"
*     operator for negative numbers.
*
*   KEYWORDS: shift, arithmetic shift right,
*
*************************************************************************/
int32 s32_shr_s32(int32 var1, int16 shift)
{
    int32 L_Mask, L_Out;

    if (shift == 0 || var1 == 0)
    {
        L_Out = var1;
    }
    else if (shift < 0)
    {
        /* perform a left shift */
        /*----------------------*/
        L_Out = s32_shl_s32(var1, (int16)(-shift));  //  OP_COUNT(-2);

#ifdef WMOPS_FX
            counter_fx.sh_sat--;
#endif

    }
    else
    {
        L_Mask = 0;
        if (var1 < 0)
        {
            L_Mask = (~L_Mask) << (32 - shift);
        }
        var1 >>= shift;
        L_Out = L_Mask | var1;
    }

#ifdef WMOPS_FX
    counter_fx.sh_sat++;
#endif

    return (L_Out);    
}
#endif //#if !defined(s32_shr_s32)

#if !defined(s32_sub_s32_s32)
/***************************************************************************
*
*   FUNCTION NAME: s32_sub_s32_s32
*
*   PURPOSE:
*
*     Perform the subtraction of the two 32 bit input variables with
*     saturation.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var2 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*
*     Perform the subtraction of the two 32 bit input variables with
*     saturation.
*
*     L_Out = var1 - var2
*
*     L_Out is set to 0x7fff ffff if the operation results in an
*     overflow.  L_Out is set to 0x8000 0000 if the operation
*     results in an underflow.
*
*   KEYWORDS: s16_sub_s16_s16, subtraction
*
*************************************************************************/
int32 s32_sub_s32_s32(int32 var1, int32 var2)
{
    int32 L_Sum;

    L_Sum = var1 - var2;

#ifdef WMOPS_FX
    counter_fx.add32++;
#endif

    return (L_Sum);
}
#endif //#if !defined(s32_sub_s32_s32)

#if !defined(s32_sub_s32_s32_sat)
/***************************************************************************
*
*   FUNCTION NAME: s32_sub_s32_s32_sat
*
*   PURPOSE:
*
*     Perform the subtraction of the two 32 bit input variables with
*     saturation.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var2 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*
*     Perform the subtraction of the two 32 bit input variables with
*     saturation.
*
*     L_Out = var1 - var2
*
*     L_Out is set to 0x7fff ffff if the operation results in an
*     overflow.  L_Out is set to 0x8000 0000 if the operation
*     results in an underflow.
*
*   KEYWORDS: s16_sub_s16_s16, subtraction
*
*************************************************************************/
int32 s32_sub_s32_s32_sat(int32 var1, int32 var2)
{
    int32 L_Sum;
    int64 dSum;

    dSum = (int64) var1 - (int64) var2;

    /* safely saturate the 64-bit result back to 32-bit */
    L_Sum = s32_saturate_s64(dSum);

#ifdef WMOPS_FX
    counter_fx.saturate--;
    counter_fx.add32_sat++;
#endif

    return (L_Sum);
}
#endif //#if !defined(s32_sub_s32_s32_sat)

#if !defined(s16_mac_s32_s16_s16_sat_rnd)
/***************************************************************************
*
*   FUNCTION NAME:s16_mac_s32_s16_s16_sat_rnd
*
*   PURPOSE:
*
*     Multiply accumulate and round.  Multiply two 16
*     bit numbers together with saturation.  Add that result to
*     the 32 bit input with saturation.  Finally round the result
*     into a 16 bit number.
*
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     var3
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var2 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*     Multiply two 16 bit numbers together with
*     saturation.  The only numbers which will cause saturation on
*     the multiply are 0x8000 * 0x8000.
*
*     Add that result to the 32 bit input with saturation.
*     Round the 32 bit result by adding 0x0000 8000 to the input.
*     The result may overflow due to the s16_add_s16_s16.  If so, the result
*     is saturated.  The 32 bit rounded number is then shifted
*     down 16 bits and returned as a int16.
*
*     Please note that this is not a true multiply accumulate as
*     most processors would implement it.  The 0x8000*0x8000
*     causes and overflow for this instruction.  On most
*     processors this would cause an overflow only if the 32 bit
*     input added to it were positive or zero.
*
*   KEYWORDS: mac, multiply accumulate, macr
*
*************************************************************************/
int16 s16_mac_s32_s16_s16_sat_rnd(int32 var3, int16 var1, int16 var2)
{
    return (s16_round_s32_sat(s32_mac_s32_s16_s16_sat(var3, var1, var2)));
}
#endif //#if !defined(s16_mac_s32_s16_s16_sat_rnd)

#if !defined(s16_msu_s32_s16_s16_sat_rnd)
/***************************************************************************
*
*   FUNCTION NAME:  s16_msu_s32_s16_s16_sat_rnd
*
*   PURPOSE:
*
*     Multiply subtract and round.  Multiply two 16
*     bit numbers together with saturation.  Subtract that result from
*     the 32 bit input with saturation.  Finally round the result
*     into a 16 bit number.
*
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     var3
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var2 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*     Multiply two 16 bit numbers together with
*     saturation.  The only numbers which will cause saturation on
*     the multiply are 0x8000 * 0x8000.
*
*     Subtract that result from the 32 bit input with saturation.
*     Round the 32 bit result by adding 0x0000 8000 to the input.
*     The result may overflow due to the s16_add_s16_s16.  If so, the result
*     is saturated.  The 32 bit rounded number is then shifted
*     down 16 bits and returned as a int16.
*
*     Please note that this is not a true multiply accumulate as
*     most processors would implement it.  The 0x8000*0x8000
*     causes and overflow for this instruction.  On most
*     processors this would cause an overflow only if the 32 bit
*     input added to it were positive or zero.
*
*   KEYWORDS: mac, multiply accumulate, macr
*
*************************************************************************/
int16 s16_msu_s32_s16_s16_sat_rnd(int32 var3, int16 var1, int16 var2)
{
    return (s16_round_s32_sat(s32_msu_s32_s16_s16_sat(var3, var1, var2)));
}
#endif //#if !defined(s16_msu_s32_s16_s16_sat_rnd)


#if !defined(s16_neg_s16_sat)
/***************************************************************************
*
*   FUNCTION NAME: s16_neg_s16_sat
*
*   PURPOSE:
*
*     Negate the 16 bit input. 0x8000's negated value is 0x7fff.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8001 <= swOut <= 0x0000 7fff.
*
*   KEYWORDS: s16_neg_s16_sat, negative, invert
*
*************************************************************************/
int16 s16_neg_s16_sat(int16 var1)
{
    int16 swOut;

    if (var1 == SHORTWORD_MIN)
    {
        swOut = SHORTWORD_MAX;
        giOverflow = 1;
    }
    else
        swOut = -var1;

#ifdef WMOPS_FX
    counter_fx.neg_sat++;
#endif

    return (swOut);
}
#endif //#if !defined(s16_neg_s16_sat)

#if !defined(s16_norm_s32)
/***************************************************************************
*
*   FUNCTION NAME: s16_norm_s32
*
*   PURPOSE:
*
*     Get normalize shift count:
*
*     A 32 bit number is input (possiblly unnormalized).  Output
*     the positive (or zero) shift count required to normalize the
*     input.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0 <= swOut <= 31
*
*
*
*   IMPLEMENTATION:
*
*     Get normalize shift count:
*
*     A 32 bit number is input (possiblly unnormalized).  Output
*     the positive (or zero) shift count required to normalize the
*     input.
*
*     If zero in input, return 0 as the shift count.
*
*     For non-zero numbers, count the number of left shift
*     required to get the number to fall into the range:
*
*     0x4000 0000 >= normlzd number >= 0x7fff ffff (positive number)
*     or
*     0x8000 0000 <= normlzd number < 0xc000 0000 (negative number)
*
*     Return the number of shifts.
*
*     This instruction corresponds exactly to the Full-Rate "norm"
*     instruction.
*
*   KEYWORDS: norm, normalization
*
*************************************************************************/
int16 s16_norm_s32(int32 var1)
{
    int16 swShiftCnt;

    if (var1 != 0)
    {
        if (!(var1 & LONGWORD_SIGN))
        {
            /* positive input */
            for (swShiftCnt = 0; !(var1 <= LONGWORD_MAX && var1 >= LONGWORD_HALF);
                swShiftCnt++)
            {
                var1 = var1 << 1;
            }
        }
        else
        {
            /* negative input */
            for (swShiftCnt = 0;
                !(var1 >= LONGWORD_MIN && var1 < (int32) 0xc0000000L);
                swShiftCnt++)
            {
                var1 = var1 << 1;
            }
        }
    }
    else
    {
        swShiftCnt = 0;
    }

#ifdef WMOPS_FX
    counter_fx.norm++;
#endif

    return (swShiftCnt);
}
#endif //#if !defined(s16_norm_s32)

#if !defined(s16_norm_s16)
/***************************************************************************
*
*   FUNCTION NAME: s16_norm_s16
*
*   PURPOSE:
*
*     Get normalize shift count:
*
*     A 16 bit number is input (possiblly unnormalized).  Output
*     the positive (or zero) shift count required to normalize the
*     input.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0 <= swOut <= 15
*
*
*
*   IMPLEMENTATION:
*
*     Get normalize shift count:
*
*     A 16 bit number is input (possiblly unnormalized).  Output
*     the positive (or zero) shift count required to normalize the
*     input.
*
*     If zero in input, return 0 as the shift count.
*
*     For non-zero numbers, count the number of left shift
*     required to get the number to fall into the range:
*
*     0x4000 >= normlzd number >= 0x7fff (positive number)
*     or
*     0x8000 <= normlzd number <  0xc000 (negative number)
*
*     Return the number of shifts.
*
*     This instruction corresponds exactly to the Full-Rate "norm"
*     instruction.
*
*   KEYWORDS: norm, normalization
*
*************************************************************************/
int16 s16_norm_s16(int16 var1)
{
    short swShiftCnt;
    int32 var;

    var = s32_deposit_s16_h(var1);
    swShiftCnt = s16_norm_s32(var);

#ifdef WMOPS_FX
    counter_fx.norm++;
    counter_fx.deposit--;
    counter_fx.norm--;
#endif

    return (swShiftCnt);
}
#endif //#if !defined(s16_norm_s16)

#if !defined(s32_cl1_s32)
/***************************************************************************
*
*   FUNCTION NAME: s32_cl1_s32
*
*   PURPOSE:
*
*     Count leading ones:
*
*     A 32 bit number is input (possiblly unnormalized).  Output
*     the number of consecutive 1's starting with the most significant bit
*
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000<= var1 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*     cnt
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0 <= cnt <= 31
*
*
*
*   IMPLEMENTATION:
*
*
*   KEYWORDS: leading ones
*
*************************************************************************/
int32 s32_cl1_s32(int32 var1)
{
    int32 cnt = 0;
    unsigned int mask = 1 << (31-cnt); //start from MSB

    while (0 != (var1 & mask ))
    {
        cnt++;
        mask = 1 << (31-cnt);
    }

#ifdef WMOPS_FX
    counter_fx.norm++;
    counter_fx.deposit--;
    counter_fx.norm--;
#endif

    return (cnt);
}
#endif //#if !defined(s32_cl1_s32)

#if !defined(popOverflow)
/****************************************************************************
*
*     FUNCTION NAME: popOverflow
*
*     PURPOSE:
*
*        Pull the old overflow state from the "stack".  Replace the current
*        overflow status with its predecessor.
*
*     INPUTS:
*
*       none
*
*
*     OUTPUTS:             none
*
*     RETURN VALUE:        value of datum about the be lost (usually the
*                          temporary saturation state)
*
*     KEYWORDS: saturation, limit, overflow
*
***************************************************************************/
int popOverflow(void)
{
    int i;

    i = giOverflow;
    giOverflow = giOldOverflow;
    return (i);
}
#endif // #if !defined(popOverflow)

#if !defined(s16_round_s32_sat)
/***************************************************************************
*
*   FUNCTION NAME: s16_round_s32_sat
*
*   PURPOSE:
*
*     Round the 32 bit int32 into a 16 bit shortword with saturation.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*     Perform a two's s16_complement_s16 round on the input int32 with
*     saturation.
*
*     This is equivalent to adding 0x0000 8000 to the input.  The
*     result may overflow due to the s16_add_s16_s16.  If so, the result is
*     saturated.  The 32 bit rounded number is then shifted down
*     16 bits and returned as a int16.
*
*
*   KEYWORDS: round
*
*************************************************************************/
int16 s16_round_s32_sat(int32 var1)
{
    int32 L_Prod;

    L_Prod = s32_add_s32_s32_sat(var1, 0x00008000L);    /* round MSP */

#ifdef WMOPS_FX
    counter_fx.round_sat++;
    counter_fx.add32_sat--;
#endif

    return (s16_extract_s32_h(L_Prod));
}
#endif //#if !defined(round)

#if !defined(setOverflow)
/****************************************************************************
*
*     FUNCTION NAME: set overflow
*
*     PURPOSE:
*
*        Clear the overflow flag
*
*     INPUTS:
*
*       none
*
*
*     OUTPUTS:             global overflow flag is cleared
*                          previous value stored in giOldOverflow
*
*     RETURN VALUE:        previous value of overflow
*
*
*     KEYWORDS: saturation, limit, overflow
*
***************************************************************************/
int setOverflow(void)
{
    giOldOverflow = giOverflow;
    giOverflow = 1;
    return (giOldOverflow);
}
#endif

#if !defined(s16_shl_s16_sat_rnd)
/***************************************************************************
*
*   FUNCTION NAME: s16_shl_s16_sat_rnd
*
*   PURPOSE:
*
*     Shift and round.  Perform a shift right. After shifting, use
*     the last bit shifted out of the LSB to round the result up
*     or down.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*
*   IMPLEMENTATION:
*
*     Shift and round.  Perform a shift right. After shifting, use
*     the last bit shifted out of the LSB to round the result up
*     or down.
*
*     If var2 is positive perform a arithmetic left shift
*     with saturation (see s16_shl_s16_sat() above).
*
*     If var2 is zero simply return var1.
*
*     If var2 is negative perform a arithmetic right shift (s16_shr_s16_sat)
*     of var1 by (-var2)+1.  Add the LS bit of the result to var1
*     shifted right (s16_shr_s16_sat) by -var2.
*
*     Note that there is no constraint on var2, so if var2 is
*     -0xffff 8000 then -var2 is 0x0000 8000, not 0x0000 7fff.
*     This is the reason the s16_shl_s16_sat function is used.
*
*
*   KEYWORDS:
*
*************************************************************************/
int16 s16_shl_s16_sat_rnd(int16 var1, int16 var2)
{
    int16 swOut, swRnd;

    if (var2 >= 0)
    {
        swOut = s16_shl_s16_sat(var1, var2);   // OP_COUNT(-1);

#ifdef WMOPS_FX
        counter_fx.sh_sat--;
#endif

    }
    else
    {
        /* right shift */
        if (var2 < -15)
        {
            swOut = 0;
        }
        else
        {
            swRnd = s16_shl_s16_sat(var1, (int16)(var2 + 1)) & 0x1;
            swOut = s16_add_s16_s16_sat(s16_shl_s16_sat(var1, var2), swRnd);

#ifdef WMOPS_FX
            counter_fx.sh_sat-=2;
            counter_fx.add16--;
#endif

        }
    }

#ifdef WMOPS_FX
    counter_fx.sh_sat++;
#endif

    return (swOut);
}
#endif // #if !defined(s16_shl_s16_sat_rnd)

#if !defined(s16_shl_s16_sat)
/***************************************************************************
*
*   FUNCTION NAME: s16_shl_s16_sat
*
*   PURPOSE:
*
*     Arithmetically shift the input left by var2.
*
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*     If Arithmetically shift the input left by var2.  If var2 is
*     negative then an arithmetic shift right (s16_shr_s16_sat) of var1 by
*     -var2 is performed.  See description of s16_shr_s16_sat for details.
*     When an arithmetic shift left is performed the var2 LS bits
*     are zero filled.
*
*     The only exception is if the left shift causes an overflow
*     or underflow.  In this case the LS bits are not modified.
*     The number returned is 0x8000 in the case of an underflow or
*     0x7fff in the case of an overflow.
*
*     The s16_shl_s16_sat is equivalent to the Full-Rate GSM "<< n" operation.
*     Note that ANSI-C does not guarantee operation of the C ">>"
*     or "<<" operator for negative numbers - it is not specified
*     whether this shift is an arithmetic or logical shift.
*
*   KEYWORDS: asl, arithmetic shift left, shift
*
*************************************************************************/

int16 s16_shl_s16_sat(int16 var1, int16 shift)
{
    int16 swOut;
    int32 L_Out;

    if (shift == 0 || var1 == 0)
    {
        swOut = var1;
    }
    else if (shift < 0)
    {
        /* perform a right shift */
        /*-----------------------*/
        if (shift <= -15)
        {
            if (var1 < 0)
                swOut = 0xffff;
            else
                swOut = 0x0;
        }
        else
        {
            swOut = s16_shr_s16_sat(var1, (int16)(-shift));

#ifdef WMOPS_FX
            counter_fx.sh_sat--;
#endif

        }
    }
    else
    {
        /* shift > 0 */
        if (shift >= 15)
        {
            /* s16_saturate_s32 */
            if (var1 > 0)
                swOut = SHORTWORD_MAX;
            else
                swOut = SHORTWORD_MIN;
            giOverflow = 1;
        }
        else
        {
            L_Out = (int32) var1 *(1 << shift);

            swOut = (int16) L_Out;    /* copy low portion to swOut, overflow
                                    * could have hpnd */
            if (swOut != L_Out)
            {
                /* overflow  */
                if (var1 > 0)
                    swOut = SHORTWORD_MAX;      /* s16_saturate_s32 */
                else
                    swOut = SHORTWORD_MIN;      /* s16_saturate_s32 */
                giOverflow = 1;
            }
        }
    }

#ifdef WMOPS_FX
    counter_fx.sh_sat++;
#endif

    return (swOut);
}
#endif //#if !defined(s16_shl_s16_sat)

#if !defined(s16_shl_s16)
/***************************************************************************
*
*   FUNCTION NAME: s16_shl_s16
*
*   PURPOSE:
*
*     Arithmetically shift the input left by var2 without saturation
*
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*     If Arithmetically shift the input left by var2.  If var2 is
*     negative then an arithmetic shift right (s16_shr_s16) of var1 by
*     -var2 is performed.  See description of s16_shr_s16 for details.
*     When an arithmetic shift left is performed the var2 LS bits
*     are zero filled.
*
*     The only exception is if the left shift causes an overflow
*     or underflow.  In this case the LS bits are not modified.
*     The number returned is 0x8000 in the case of an underflow or
*     0x7fff in the case of an overflow.
*
*     The s16_shl_s16_sat is equivalent to the Full-Rate GSM "<< n" operation.
*     Note that ANSI-C does not guarantee operation of the C ">>"
*     or "<<" operator for negative numbers - it is not specified
*     whether this shift is an arithmetic or logical shift.
*
*   KEYWORDS: asl, arithmetic shift left, shift
*
*************************************************************************/

int16 s16_shl_s16(int16 var1, int16 shift)
{
    int16 swOut;
    int32 L_Out;

    if (shift == 0 || var1 == 0)
    {
        swOut = var1;
    }
    else if (shift < 0)
    {
        /* perform a right shift */
        /*-----------------------*/
        swOut = s16_shr_s16(var1, (int16)(-shift));
    }
    else
    {
        L_Out = (int32) var1 *(1 << shift);

        swOut = (int16) L_Out;    /* copy low portion to swOut, overflow */
    }

#ifdef WMOPS_FX
    counter_fx.sh_sat++;
#endif

    return (swOut);    
}
#endif //#if !defined(s16_shl_s16)

#if !defined(s16_shr_s16_sat)
/***************************************************************************
*
*   FUNCTION NAME: s16_shr_s16_sat
*
*   PURPOSE:
*
*     Arithmetic shift right (or left).
*     Arithmetically shift the input right by var2.   If var2 is
*     negative then an arithmetic shift left (s16_shl_s16_sat) of var1 by
*     -var2 is performed.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*     Arithmetically shift the input right by var2.  This
*     operation maintains the sign of the input number. If var2 is
*     negative then an arithmetic shift left (s16_shl_s16_sat) of var1 by
*     -var2 is performed.  See description of s16_shl_s16_sat for details.
*
*     Equivalent to the Full-Rate GSM ">> n" operation.  Note that
*     ANSI-C does not guarantee operation of the C ">>" or "<<"
*     operator for negative numbers.
*
*   KEYWORDS: shift, arithmetic shift right,
*
*************************************************************************/
int16 s16_shr_s16_sat(int16 var1, int16 shift)
{
    int16 swMask, swOut;

    if (shift == 0 || var1 == 0)
        swOut = var1;

    else if (shift < 0)
    {
        /* perform an arithmetic left shift */
        /*----------------------------------*/
        if (shift <= -15)
        {
            /* s16_saturate_s32 */
            if (var1 > 0)
                swOut = SHORTWORD_MAX;
            else
                swOut = SHORTWORD_MIN;
            giOverflow = 1;
        }
        else
        {
            swOut = s16_shl_s16_sat(var1, (int16)(-shift));      // OP_COUNT(-1);

#ifdef WMOPS_FX
            counter_fx.sh_sat--;
#endif

        }
    }
    else
    {
        /* positive shift count */
        /*----------------------*/

        if (shift >= 15)
        {
            if (var1 < 0)
                swOut = 0xffff;
            else
                swOut = 0x0;
        }
        else
        {
            /* take care of sign extension */
            /*-----------------------------*/

            swMask = 0;
            if (var1 < 0)
            {
                swMask = ~swMask << (16 - shift);
            }

            var1 >>= shift;
            swOut = swMask | var1;
        }
    }

#ifdef WMOPS_FX
    counter_fx.sh_sat++;
#endif

    return (swOut);
}
#endif //#if !defined(s16_shr_s16_sat)

#if !defined(s16_shr_s16)
/***************************************************************************
*
*   FUNCTION NAME: s16_shr_s16
*
*   PURPOSE:
*
*     Arithmetic shift right (or left) without saturation.
*     Arithmetically shift the input right by var2.   If var2 is
*     negative then an arithmetic shift left (s16_shl_s16) of var1 by
*     -var2 is performed.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*     Arithmetically shift the input right by var2.  This
*     operation maintains the sign of the input number. If var2 is
*     negative then an arithmetic shift left (s16_shl_s16) of var1 by
*     -var2 is performed.  See description of s16_shl_s16 for details.
*
*     Equivalent to the Full-Rate GSM ">> n" operation.  Note that
*     ANSI-C does not guarantee operation of the C ">>" or "<<"
*     operator for negative numbers.
*
*   KEYWORDS: shift, arithmetic shift right,
*
*************************************************************************/
int16 s16_shr_s16(int16 var1, int16 shift)
{
    int16 swMask, swOut;

    if (shift == 0 || var1 == 0)
    {
        swOut = var1;
    }
    else if (shift < 0)
    {
        /* perform an arithmetic left shift */
        /*----------------------------------*/
        swOut = s16_shl_s16(var1, (int16)(-shift));      // OP_COUNT(-1);

#ifdef WMOPS_FX
            counter_fx.sh_sat--;
#endif
    }
    else
    {
        /* positive shift count */
        /*----------------------*/
        /* take care of sign extension */
        /*-----------------------------*/

        swMask = 0;
        if (var1 < 0)
        {
            swMask = ~swMask << (16 - shift);
        }

        var1 >>= shift;
        swOut = swMask | var1;
    }

#ifdef WMOPS_FX
    counter_fx.sh_sat++;
#endif

    return (swOut);        
}
#endif //#if !defined(s16_shr_s16_sat)

#if !defined(s16_sub_s16_s16)
/***************************************************************************
*
*   FUNCTION NAME: s16_sub_s16_s16
*
*   PURPOSE:
*
*     Perform the subtraction of the two 16 bit input variable with
*     saturation.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*     Perform the subtraction of the two 16 bit input variable
*
*     swOut = var1 - var2
*
*
*   KEYWORDS: s16_sub_s16_s16, subtraction
*
*************************************************************************/
int16 s16_sub_s16_s16(int16 var1, int16 var2)
{
    int16 swOut;


    swOut =  var1 - var2;

#ifdef WMOPS_FX
    counter_fx.add16++;
#endif

    return (swOut);
}
#endif //#if !defined(s16_sub_s16_s16)

#if !defined(s64_sub_s64_s64)
/***************************************************************************
*
*   FUNCTION NAME: s64_sub_s64_s64
*
*   PURPOSE:
*
*     Perform the subtraction of the two 16 bit input variable with
*     saturation.
*
*   INPUTS:
*
*     var1
*                     64 bit short signed integer (int16)
*     var2
*                     64 bit short signed integer (int16)
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     64 bit short signed integer (int16)
*
*   IMPLEMENTATION:
*
*     Perform the subtraction of the two 64 bit input variable
*
*     swOut = var1 - var2
*
*
*   KEYWORDS: s16_sub_s16_s16, subtraction
*
*************************************************************************/
int64 s64_sub_s64_s64(int64 var1, int64 var2)
{
    int64 swOut;

    swOut =  var1 - var2;

#ifdef WMOPS_FX
    counter_fx.add64++;
#endif

    return (swOut);
}
#endif //#if !defined(s64_sub_s64_s64)

#if !defined(s16_sub_s16_s16_sat)
/***************************************************************************
*
*   FUNCTION NAME: s16_sub_s16_s16_sat
*
*   PURPOSE:
*
*     Perform the subtraction of the two 16 bit input variable with
*     saturation.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*     Perform the subtraction of the two 16 bit input variable with
*     saturation.
*
*     swOut = var1 - var2
*
*     swOut is set to 0x7fff if the operation results in an
*     overflow.  swOut is set to 0x8000 if the operation results
*     in an underflow.
*
*   KEYWORDS: s16_sub_s16_s16, subtraction
*
*************************************************************************/
int16 s16_sub_s16_s16_sat(int16 var1, int16 var2)
{
    int32 L_diff;
    int16 swOut;

    L_diff = (int32) var1 - var2;
    swOut = s16_saturate_s32(L_diff);

#ifdef WMOPS_FX
    counter_fx.add16_sat++;
#endif

    return (swOut);
}
#endif //#if !defined(s16_sub_s16_s16_sat)

#if !defined(s32_mls_s32_s16_sat)
/***************************************************************************
*
*   FUNCTION NAME: s32_mls_s32_s16_sat
*
*   PURPOSE:
*
*     Perform a special case of multipy of the one 32 bit signed number
*     with 16 bit signed number.  Output a 32 bit number.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     16 bit signed integer (int16) whose value
*                     falls in the range 0x8000 <= var2 <= 0x7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer
*
*   IMPLEMENTATION:
*
*
*   KEYWORDS: multiply, sat, mac
*
*************************************************************************/
int32   s32_mls_s32_s16_sat( int32 Lv, int16 v )
{
    int32   Temp  ;

    Temp = Lv & (int32) 0x0000ffff ;
    Temp = Temp * (int32) v ;
    Temp = s32_shr_s32_sat( Temp, (int16) 15 ) ;
    Temp = s32_mac_s32_s16_s16_sat( Temp, v, s16_extract_s32_h(Lv) ) ;

    return Temp ;
}
#endif //#if !defined(s32_mls_s32_s16_sat)


#if !defined(s16_norm_s64)
/********************************************************************
* FUNCTION      :  s16_norm_s64().
*
* PURPOSE       :
*           calculates the shift required to normalize a 32
*           bit number.
*
* INPUT ARGUMENTS  :
*           var1        (int64) input in Qin
*
* OUTPUT ARGUMENTS :
*                       None.
*
* INPUT/OUTPUT ARGUMENTS :
*                       None
*
* RETURN ARGUMENTS :
*                       (int16)shift value to use to normalize the value.
*
* Note:        var1 can be negative, if Linput=0, return 0
*
***********************************************************************/
int16 s16_norm_s64(int64 var1)
{
    int16 shift;
    shift = 0;

    if (var1 != 0)
    { // if input!=0
        while ((var1 > MIN_32) && (var1 < MAX_32))
        { // case where input is within 32 bit
            var1 = var1<<1;
            shift++;
        }

        while ((var1 < MIN_32) || (var1 > MAX_32))
        { // case where input is out of 32 bit
            var1 = var1>>1;
            shift--;
        }
    }

#ifdef WMOPS_FX
    counter_fx.norm++;
#endif

    return (shift);
}
#endif //#if !defined(s16_norm_s64)

#if !defined(s32_deposit_s16_h)
/***************************************************************************
*
*   FUNCTION NAME: s32_deposit_s16_h
*
*   PURPOSE:
*
*     Put the 16 bit input into the 16 MSB's of the output Word32.  The
*     LS 16 bits are zeroed.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (Word16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit signed integer (Word32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff 0000.
*
*
*   KEYWORDS: deposit, assign, fractional assign
*
*************************************************************************/
int32 s32_deposit_s16_h(int16 var1)
{
    int32 var2;

    var2 = (int32) var1 << 16;

#ifdef WMOPS_FX
    counter_fx.deposit++;
#endif

    return (var2);
}
#endif //#if !defined(s32_deposit_s16_h)


#if !defined(s32_mult_s32_s16_rnd_sat)
/***************************************************************************
*
*   FUNCTION NAME: s32_mult_s32_s16_rnd_sat
*
*   PURPOSE:
*
*     Perform a fractional multipy of a 32 bit input number and
*     16-bit number with rounding and saturation.  Output a 32 bit number.
*
*   INPUTS:
*
*     var1
*                     32 bit short signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff 0000.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0x8000 <= var2 <= 0x7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     32 bit long signed integer (int32) whose value
*                     falls in the range
*                     0x8000 0000 <= var1 <= 0x7fff ffff.
*
*   IMPLEMENTATION:
*     Multiplies a 32 bit number with 16-bit number and produces a 32-bit number as output.
*     It does the following operations:
*      resultL64 = in1L32 * in2L16;
*      s32_sat_s40_round(resultL64) to 32 bits.
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int32 s32_mult_s32_s16_rnd_sat(int32 var1, int16 var2)
{
    int32 resultL32;
    int16 in1MW;
    uint16 in1LW;
    int32 temp1ProdL32, temp2ProdL32;

    in1LW = (uint16) s16_extract_s32_l(var1); // lower word of first input
    in1MW = s16_extract_s32_h(var1);          // higher word of second input

    temp1ProdL32 = s32_mult_s16_u16(var2, in1LW);
    temp1ProdL32 = s32_add_s32_s32_sat(temp1ProdL32, (int32 )0x4000);  // for rounding purposes
    temp1ProdL32 = s32_shl_s32_sat(temp1ProdL32, -15);                 // right shift

    temp2ProdL32 = s32_mult_s16_s16_shift_sat(var2, in1MW);
    resultL32 = s32_add_s32_s32_sat(temp1ProdL32, temp2ProdL32);

#ifdef WMOPS_FX
    counter_fx.mult_32_16++;
    counter_fx.saturate--;
    counter_fx.extract -= 2;
    counter_fx.mult16 -=3;
    counter_fx.add32--;
    counter_fx.sh--;
#endif

    return (resultL32);
}
#endif //#if !defined(s32_mult_s32_s16_rnd_sat)

/***************************************************************************
* FUNCTION : changed
*
* DESCRIPTION: determine if two values are different
*
* INPUTS:
*         value1:   integer value 1
*         value2:   integer value 2
*
* OUTPUTS:          return TRUE if different, and FALSE if same
*
* IMPLEMENTATION NOTES:
****************************************************************************/
boolean changed(int value1, int value2)
{
    return (value1 == value2) ? FALSE : TRUE;
} /*------------------ end of function changed ------------------------*/


/*****************************************************************************
* FUNCTION : change_if_valid
*
* DESCRIPTION: If the new value is not invalid, assign it to the register
*
* INPUTS: originalValue: value to be changed
*         newValue: new integer value
* OUTPUTS: newValue if it is valid, and origional if invalid
*
* IMPLEMENTATION NOTES:
****************************************************************************/
int change_if_valid
(
    int             originalValue,          /* value to be changed           */
    int             newValue                /* new value, could be invalid   */
)
{
    return (newValue == (int)0xFFFFFFFF) ? originalValue : newValue;
}

#if !defined(s16_extract_s64_h_sat)
/***************************************************************************
*
*   FUNCTION NAME: s16_extract_s64_h_sat
*
*   PURPOSE:
*
*     Extract bits 16 to 31 of a 64 bit int64, with 32-bit saturation.  
*     Return the 16 bit number as a int16.
*
*   INPUTS:
*
*     var1
*                     64 bit long signed integer (int64) whose value
*                     falls in the range
*                     0x 8000 0000 0000 0000<= var1 <= 0x  7fff ffff ffff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     swOut
*                     16 bit short signed integer (int16) whose value
*                     falls in the range
*                     0xffff 8000 <= swOut <= 0x0000 7fff.
*
*   IMPLEMENTATION:
*
*   KEYWORDS: assign, extract, saturate
*
*************************************************************************/

int16 s16_extract_s64_h_sat(int64 var1)
{
    return s16_extract_s32_h(s32_saturate_s64(var1));
}
#endif //#if !defined(s16_extract_s64_h_sat)

#if !defined(s64_mac_s64_s16_s16_s1)
/***************************************************************************
*
*   FUNCTION NAME: s64_mac_s64_s16_s16_s1
*
*   PURPOSE:
*
*     Multiply accumulate.  Fractionally multiply two 16 bit
*     numbers together.  Add that result to the
*     64 bit input. Return the 64 bit result.
*
*   INPUTS:
*
*     var1
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var1 <= 0x0000 7fff.
*     var2
*                     16 bit short signed integer (int16) whose value
*                     falls in the range 0xffff 8000 <= var2 <= 0x0000 7fff.
*     var3
*                     64 bit integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= var2 <= 0x7fff ffff ffff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit signed integer (int64) whose value
*                     falls in the range
*                     0x8000 0000 0000 0000 <= L_Out <= 0x7fff ffff ffff ffff.
*
*   IMPLEMENTATION:
*
*     Fractionally multiply two 16 bit numbers together without
*     saturation into a 64-bit result.
*
*     Add that result to the 64 bit input without saturation.
*     Return the 64 bit result.
*
*   KEYWORDS: mac, multiply accumulate
*
*************************************************************************/
int64 s64_mac_s64_s16_s16_s1(int64 var3, int16 var1, int16 var2)
{
    return s64_mac_s64_s16_s16_shift(var3, var1, var2, 1);
}
#endif // #if !defined(s64_mac_s64_s16_s16_s1)

#if !defined(s64_mult_s32_u16)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_s32_u16
*
*   PURPOSE:
*
*     Perform multiplication of a 32-bit signed number with 16-bit unsigned number.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     16 bit short unsigned integer (uint16) whose value
*                     falls in the range 0x0000 8000 <= var2 <= 0x0000 7fff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long long signed integer (int64).
*
*   IMPLEMENTATION:
*     multiplies a 32 -bit number with 16-bit number and save into 64 bit
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_s32_u16( int32 var1, uint16 var2)
{
    int64 resultL64;
    uint16 inL32LW;
    int16 inL32MW;
    uint32 tempProdUL32;        

    inL32LW = (uint16)s16_extract_s32_l(var1);
    inL32MW = s16_extract_s32_h(var1);

    tempProdUL32  = u32_mult_u16_u16(var2, inL32LW);
    resultL64 = s64_shl_s64(s32_mult_s16_u16(inL32MW,var2),16);
    resultL64 = s64_add_s64_s64(resultL64, tempProdUL32);
    return resultL64;
} /* End of l_mult_s32xs16 function*/
#endif //#if !defined(s64_mult_s32_u16)

#if !defined(s64_mult_s32_s32)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_s32_s32
*
*   PURPOSE:
*
*     Perform a fractional multipy of the two 32 bit input numbers.
*     Output a 32 bit number.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var2 <= 0x7fff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long signed integer (int64)
*
*   IMPLEMENTATION:
*     Multiplies two 32 bit numbers and produces a 64-bit number as output.
*     It does the following operations:
*      resultL64 = in1L32 * in2L32 ;
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_s32_s32(int32 var1, int32 var2)
{
    return (int64) var1 * var2;
}
#endif //#if !defined(s64_mult_s32_s32)


#if !defined(s64_mult_s32_u32)
/***************************************************************************
*
*   FUNCTION NAME: s64_mult_s32_u32
*
*   PURPOSE:
*
*     Perform multipy of the two 32 bit input numbers.
*     Output a 64 bit number.
*
*   INPUTS:
*
*     var1
*                     32 bit signed integer (int32) whose value
*                     falls in the range 0x8000 0000 <= var1 <= 0x7fff ffff.
*     var2
*                     32 bit unsigned integer (uint32) whose value
*                     falls in the range 0x0000 0000 <= var2 <= 0xffff ffff.
*
*   OUTPUTS:
*
*     none
*
*   RETURN VALUE:
*
*     L_Out
*                     64 bit long signed integer (int64)
*
*   IMPLEMENTATION:
*     Multiplies two 32 bit numbers and produces a 64-bit number as output.
*     It does the following operations:
*      resultL64 = in1L32 * uin2L32 ;
*   KEYWORDS: multiply, mult, mpy
*
*************************************************************************/
int64 s64_mult_s32_u32(int32 var1, uint32 var2)
{
	return (var1 * (int64)var2);
}
#endif  // !defined(s64_mult_s32_u32)




