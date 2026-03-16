/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*******************************************************************************
   math.c 	: Math Functions for C-simulation
*******************************************************************************/
#include "audio_exp10.h"
#include "audio_basic_op.h"

#define INVLOG2Q13 27213 /* 0x6a4d: 1/0.30103 = 3.3219  Q13 */
#define EXP10C0_Q15 32701 /* 0x7fbd  [0.9979554 in Q15 */
#define EXP10C1_Q15 21871 /* 0x556f  [0.6674432 in Q15 */
#define EXP10C2_Q15 5615 /* 0x15ef: 0.1713425 in Q15 */ 
/*==============================================================================

FUNCTION       int32_t exp10_fixed(int32_t input)
               input is Q26 in the range [-1,0]

DESCRIPTION    Calculates 10^x using taylor series approximation.
               10^x  = 2^(x/log10(2))=2^(x/.30103)=2^(x*3.3219)
               2^x   = 2^(i+f)=(2^i * 2^f), i=integer, f=fraction

               Computes 2^f using taylor expansion around -0.5:
               2^f = 0.1713425*f*f + 0.6674432*f + 0.9979554
                   = c2*f*f + c1*f + c0

DEPENDENCIES   none

RETURN VALUE   Signed 32 bit value 10^x where x is input Q26 [-1,0]
 
SIDE EFFECTS   none

===============================================================================*/
int32_t exp10_fixed(int32_t input)
{
   int32_t termB, termC;
   uint32_t termA2, fractionalPartSquared;
   int40 termA1Q23;
   int16_t fractionalPart;
   int16_t shiftFactorQ31;

   /* Get fractional Portion and Square it */
   termA1Q23 = s40_mult_s32_s16_shift(input, INVLOG2Q13, 0);

   /* shift the 1-f into the low part of A2 */
   termA2 = ~s32_extract_s40_l(s40_shl_s40(termA1Q23, -7));

   /* get the low part into fractionalPart */
   fractionalPart = s16_extract_s32_l(termA2);
   fractionalPartSquared = u32_mult_u16_u16(fractionalPart, fractionalPart);

   /* Get Integer part */
   termB = (int32_t)s40_shl_s40(termA1Q23, -7);
   shiftFactorQ31 = s16_sub_s16_s16(s16_extract_s32_h(termB), 15);

   /* termC is 2^f*/
   termC = s32_add_s32_s32_sat(s32_deposit_s16_h(EXP10C0_Q15),u32_mult_u16_u16(s16_extract_s32_h(fractionalPartSquared), EXP10C2_Q15));
   termC = s32_sub_s32_s32_sat(termC, u32_mult_u16_u16(EXP10C1_Q15, fractionalPart));

   /* denormalize and return exp10 in Q15 */
   return s32_shl_s32_sat(termC, shiftFactorQ31);
}



