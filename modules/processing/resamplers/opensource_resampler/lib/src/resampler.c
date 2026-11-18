////////////////////////////////////////////////////////////////////////////
//                         **** RESAMPLER ****                            //
//                     Sinc-based Audio Resampling                        //
//                Copyright (c) 2006 - 2023 David Bryant.                 //
//                          All Rights Reserved.                          //
//      Distributed under the BSD Software License (see license.txt)      //
////////////////////////////////////////////////////////////////////////////

/*
 * SPF-adapted version of resampler.c.
 *
 * Changes from the original github_lib/resampler.c:
 *   1. Include "resampler.h" (SPF version) instead of the original header.
 *      The SPF resampler.h includes osrc_resampler_port.h which overrides
 *      malloc/calloc/free/fprintf with SPF equivalents BEFORE any standard
 *      library headers are processed.
 *   2. Removed #include <stdio.h> and #include <stdlib.h> — these are
 *      superseded by the portability layer.
 *   3. Added #ifndef M_PI guard (already present in original, kept as-is).
 *   4. No algorithmic changes whatsoever.
 *
 * The portability layer (osrc_resampler_port.h) ensures:
 *   - All calloc() calls use posal_memory_malloc() + memset()
 *   - All malloc() calls use posal_memory_malloc()
 *   - All free() calls use posal_memory_free()
 *   - All fprintf(stderr, ...) calls use AR_MSG(DBG_ERROR_PRIO, ...)
 *   - The heap used is g_osrc_rs_heap_id, set by osrc_rs_set_heap_id()
 *     immediately before each resampleFixedRatioInit() / resampleInit() call.
 */

/* SPF-adapted header — includes osrc_resampler_port.h which overrides
   malloc/calloc/free/fprintf before any stdlib headers */
#include "resampler.h"

#ifdef ENABLE_EXTRAPOLATION
#include "extrapolator.h"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void   init_filter(Resample *cxt, artsample_t *filter, double fraction);
static double subsample_no_interpolate_precise(Resample *cxt, artsample_t *source, double offset);
static double subsample_interpolate_precise(Resample *cxt, artsample_t *source, double offset);
static double subsample_no_interpolate(Resample *cxt, artsample_t *source, double offset);
static double subsample_interpolate(Resample *cxt, artsample_t *source, double offset);
static unsigned long gcd(unsigned long a, unsigned long b);

/* ============================================================================
   resampleInit
   ============================================================================ */
Resample *resampleInit(int numChannels, int numTaps, int numFilters,
                        double lowpassRatio, int flags)
{
   Resample *cxt = calloc(1, sizeof(Resample));
   int       i;

   if (lowpassRatio > 0.0 && lowpassRatio < 1.0)
      flags |= INCLUDE_LOWPASS;
   else
   {
      flags &= ~INCLUDE_LOWPASS;
      lowpassRatio = 1.0;
   }

   if ((numTaps & 3) || numTaps <= 0 || numTaps > 1024)
   {
      fprintf(stderr, "must 4-1024 filter taps, and a multiple of 4!\n");
      return NULL;
   }

   if (numFilters < 1 || numFilters > 1024)
   {
      fprintf(stderr, "must be 1-1024 filters!\n");
      return NULL;
   }

   cxt->lowpassRatio = lowpassRatio;
   cxt->numChannels  = numChannels;
   cxt->numSamples   = numTaps * 16;
   cxt->numFilters   = numFilters;
   cxt->numTaps      = numTaps;
   cxt->flags        = flags;

   /* note that we actually have one more than the specified number of filters */
   cxt->filters    = calloc(cxt->numFilters + 1, sizeof(artsample_t *));
   cxt->tempFilter = malloc(numTaps * sizeof(double));

   for (i = 0; i <= cxt->numFilters; ++i)
   {
      int j;
      cxt->filters[i] = calloc(cxt->numTaps, sizeof(artsample_t));

      if (i < cxt->numFilters)
         init_filter(cxt, cxt->filters[i], (double)i / cxt->numFilters);
      else
         for (j = 0; j < cxt->numTaps; ++j)
            cxt->filters[cxt->numFilters][(j + 1) % cxt->numTaps] = cxt->filters[0][j];
   }

   cxt->filters[0][cxt->numTaps - 1]          = 0.0;
   cxt->filters[cxt->numFilters][0]            = 0.0;

   free(cxt->tempFilter);
   cxt->tempFilter = NULL;

   cxt->buffers = calloc(numChannels, sizeof(artsample_t *));
   for (i = 0; i < numChannels; ++i)
      cxt->buffers[i] = calloc(cxt->numSamples, sizeof(artsample_t));

   cxt->outputOffset = numTaps / 2;
   cxt->inputIndex   = numTaps;

#ifdef ENABLE_EXTRAPOLATION
   if (cxt->flags & EXTRAPOLATE_ENDPOINTS)
      cxt->flags |= EXTRAPOLATE_PREFILL;
#endif

#ifdef ENABLE_THREADS
   if (numChannels > 1 && (flags & RESAMPLE_MULTITHREADED))
      cxt->workers = workersInit(numChannels);
#endif

   if (sizeof(artsample_t) == 4 && (cxt->flags & EXTEND_CONVOLUTION_MATH))
      cxt->subsample = (cxt->flags & SUBSAMPLE_INTERPOLATE) ?
                       subsample_interpolate_precise : subsample_no_interpolate_precise;
   else
      cxt->subsample = (cxt->flags & SUBSAMPLE_INTERPOLATE) ?
                       subsample_interpolate : subsample_no_interpolate;

   return cxt;
}

/* ============================================================================
   resampleFixedRatioInit
   ============================================================================ */
Resample *resampleFixedRatioInit(int numChannels, int numTaps, int maxFilters,
                                  double sourceRate, double destinRate,
                                  int lowpassFreq, int flags)
{
   double    lowpassRatio   = lowpassFreq / (destinRate / 2.0);
   double    resampleRatio  = destinRate / sourceRate;
   Resample *cxt;

   if (lowpassFreq > destinRate / 2.0)
   {
      fprintf(stderr, "lowpass frequency must be lower than destination Nyquist!\n");
      return NULL;
   }

   if (sourceRate == floor(sourceRate) && destinRate == floor(destinRate) &&
       !(flags & NO_FILTER_REDUCTION))
   {
      unsigned long factor =
         (unsigned long)destinRate / gcd((unsigned long)sourceRate, (unsigned long)destinRate);

      if (factor <= (unsigned long)maxFilters)
      {
         flags &= ~SUBSAMPLE_INTERPOLATE;
         maxFilters = (int)factor;

         if (maxFilters & (maxFilters - 1))
            flags |= RESAMPLER_SNAP_OFFSET;
      }
   }

   if (!lowpassFreq && (flags & INCLUDE_LOWPASS) && destinRate < sourceRate)
   {
      lowpassRatio = 1.0 - (7.5 / numTaps / resampleRatio);

      if (lowpassRatio < 0.8)
         lowpassRatio = 0.8;

      if (lowpassRatio < resampleRatio)
         lowpassRatio = resampleRatio;
   }

   cxt = resampleInit(numChannels, numTaps, maxFilters,
                      lowpassRatio * resampleRatio, flags | RESAMPLE_FIXED_RATIO);

   if (cxt)
      cxt->fixedRatio = destinRate / sourceRate;

   return cxt;
}

/* ============================================================================
   Query helpers
   ============================================================================ */
double resampleGetLowpassRatio(Resample *cxt)
{
   return cxt->lowpassRatio;
}

int resampleGetNumFilters(Resample *cxt)
{
   return cxt->numFilters;
}

int resampleInterpolationUsed(Resample *cxt)
{
   return cxt->flags & SUBSAMPLE_INTERPOLATE;
}

/* ============================================================================
   resampleReset
   ============================================================================ */
void resampleReset(Resample *cxt)
{
   int i;

   for (i = 0; i < cxt->numChannels; ++i)
      memset(cxt->buffers[i], 0, cxt->numSamples * sizeof(artsample_t));

   cxt->outputOffset = cxt->numTaps / 2;
   cxt->inputIndex   = cxt->numTaps;

   if (cxt->flags & EXTRAPOLATE_ENDPOINTS)
      cxt->flags |= EXTRAPOLATE_PREFILL;

   cxt->flags &= ~RESAMPLER_FLUSHED;
}

/* ============================================================================
   Internal flush helpers
   ============================================================================ */
#ifdef ENABLE_THREADS
static int resampleProcessChannelJob(void *ptr, void *sync_not_used);
#endif

#ifdef ENABLE_EXTRAPOLATION
static void prefillAllChannels(Resample *cxt), postfillAllChannels(Resample *cxt);
#else
static void postfillAllChannels(Resample *cxt);
#endif

static void postfillAllChannels(Resample *cxt)
{
   int c;

   if (cxt->numSamples - cxt->inputIndex < cxt->numTaps / 2)
   {
      for (c = 0; c < cxt->numChannels; ++c)
         memmove(cxt->buffers[c],
                 cxt->buffers[c] + cxt->numSamples - cxt->numTaps,
                 cxt->numTaps * sizeof(artsample_t));

      cxt->outputOffset -= cxt->numSamples - cxt->numTaps;
      cxt->inputIndex   -= cxt->numSamples - cxt->numTaps;
   }

   for (c = 0; c < cxt->numChannels; ++c)
   {
      memset(cxt->buffers[c] + cxt->inputIndex, 0,
             (cxt->numSamples - cxt->inputIndex) * sizeof(artsample_t));
#ifdef ENABLE_EXTRAPOLATION
      if (cxt->flags & EXTRAPOLATE_ENDPOINTS)
         extrapolate_forward(cxt->buffers[c] + cxt->inputIndex - cxt->numTaps / 2,
                             cxt->numTaps / 2, cxt->numTaps / 2);
#endif
   }

   cxt->flags |= RESAMPLER_FLUSHED;
   cxt->inputIndex += cxt->numTaps / 2;
}

#ifdef ENABLE_EXTRAPOLATION
static void prefillAllChannels(Resample *cxt)
{
   int num_samples = cxt->inputIndex - cxt->numTaps, c;

   if (num_samples >= 8)
      for (c = 0; c < cxt->numChannels; ++c)
         extrapolate_reverse(cxt->buffers[c] + cxt->inputIndex, num_samples,
                             cxt->numTaps - num_samples);
}
#endif

/* ============================================================================
   resampleProcess  (non-interleaved)
   ============================================================================ */
ResampleResult resampleProcess(Resample *cxt,
                                const artsample_t *const *input, int numInputFrames,
                                artsample_t *const *output, int numOutputFrames,
                                double ratio)
{
   if (cxt->flags & RESAMPLE_FIXED_RATIO)
      ratio = cxt->fixedRatio;

   if (cxt->flags & RESAMPLER_FLUSHED)
      numInputFrames = 0;

#ifdef ENABLE_THREADS
   if (cxt->workers)
   {
      Resample      *worker_contexts = calloc(cxt->numChannels, sizeof(Resample));
      ResampleResult res             = { 0, 0 };
      int            ch;

      for (ch = 0; ch < cxt->numChannels; ++ch)
      {
         Resample *wcxt = worker_contexts + ch;
         *wcxt          = *cxt;
         if (input) wcxt->input = input[ch] - 1;
         wcxt->numInputFrames  = numInputFrames;
         wcxt->output          = output[ch] - 1;
         wcxt->numOutputFrames = numOutputFrames;
         wcxt->cbuffer         = cxt->buffers[ch];
         wcxt->ratio           = ratio;
         wcxt->stride          = 1;
         wcxt->res             = res;

         workersEnqueueJob(cxt->workers, resampleProcessChannelJob, wcxt,
                           ch < cxt->numChannels - 1 ? WaitForAvailableWorkerThread
                                                     : DontUseWorkerThread);
      }

      workersWaitAllJobs(cxt->workers);
      res  = worker_contexts[0].res;
      *cxt = worker_contexts[0];
      free(worker_contexts);
      return res;
   }
   else if (cxt->numChannels == 1)
   {
      if (input) cxt->input = input[0] - 1;
      cxt->numInputFrames        = numInputFrames;
      cxt->output                = output[0] - 1;
      cxt->numOutputFrames       = numOutputFrames;
      cxt->cbuffer               = cxt->buffers[0];
      cxt->ratio                 = ratio;
      cxt->stride                = 1;
      cxt->res.output_generated  = 0;
      cxt->res.input_used        = 0;
      resampleProcessChannelJob(cxt, NULL);
      return cxt->res;
   }
   else
   {
#endif
   int            half_taps = cxt->numTaps / 2, i;
   ResampleResult res       = { 0, 0 };
   double         offset2   = 0.0;

   if (numInputFrames < 0)
      postfillAllChannels(cxt);

   while (numOutputFrames > 0)
   {
      if (cxt->outputOffset + offset2 >= cxt->inputIndex - half_taps)
      {
         if (numInputFrames > 0)
         {
            if (cxt->inputIndex == cxt->numSamples)
            {
               for (i = 0; i < cxt->numChannels; ++i)
                  memmove(cxt->buffers[i],
                          cxt->buffers[i] + cxt->numSamples - cxt->numTaps,
                          cxt->numTaps * sizeof(artsample_t));

               cxt->outputOffset -= cxt->numSamples - cxt->numTaps;
               cxt->inputIndex   -= cxt->numSamples - cxt->numTaps;
            }

            for (i = 0; i < cxt->numChannels; ++i)
               cxt->buffers[i][cxt->inputIndex] = input[i][res.input_used];

            cxt->inputIndex++;
            res.input_used++;
            numInputFrames--;
         }
         else
            break;
      }
      else
      {
#ifdef ENABLE_EXTRAPOLATION
         if (cxt->flags & EXTRAPOLATE_PREFILL)
         {
            cxt->flags &= ~EXTRAPOLATE_PREFILL;
            prefillAllChannels(cxt);
         }
#endif
         for (i = 0; i < cxt->numChannels; ++i)
            output[i][res.output_generated] =
               cxt->subsample(cxt, cxt->buffers[i], cxt->outputOffset + offset2);

         offset2 = ++res.output_generated / ratio;
         numOutputFrames--;
      }
   }

   cxt->outputOffset += offset2;

   if (cxt->flags & RESAMPLER_SNAP_OFFSET)
      cxt->outputOffset =
         floor(cxt->outputOffset) +
         floor((cxt->outputOffset - floor(cxt->outputOffset)) * cxt->numFilters + 0.5) /
            cxt->numFilters;

   return res;
#ifdef ENABLE_THREADS
   }
#endif
}

/* ============================================================================
   resampleProcessInterleaved
   ============================================================================ */
ResampleResult resampleProcessInterleaved(Resample *cxt,
                                           const artsample_t *input, int numInputFrames,
                                           artsample_t *output, int numOutputFrames,
                                           double ratio)
{
   if (cxt->flags & RESAMPLE_FIXED_RATIO)
      ratio = cxt->fixedRatio;

   if (cxt->flags & RESAMPLER_FLUSHED)
      numInputFrames = 0;

#ifdef ENABLE_THREADS
   if (cxt->workers)
   {
      Resample      *worker_contexts = calloc(cxt->numChannels, sizeof(Resample));
      ResampleResult res             = { 0, 0 };
      int            ch;

      for (ch = 0; ch < cxt->numChannels; ++ch)
      {
         Resample *wcxt = worker_contexts + ch;
         *wcxt          = *cxt;
         if (input) wcxt->input = input + ch - cxt->numChannels;
         wcxt->numInputFrames  = numInputFrames;
         wcxt->output          = output + ch - cxt->numChannels;
         wcxt->numOutputFrames = numOutputFrames;
         wcxt->cbuffer         = cxt->buffers[ch];
         wcxt->stride          = cxt->numChannels;
         wcxt->ratio           = ratio;
         wcxt->res             = res;

         workersEnqueueJob(cxt->workers, resampleProcessChannelJob, wcxt,
                           ch < cxt->numChannels - 1 ? WaitForAvailableWorkerThread
                                                     : DontUseWorkerThread);
      }

      workersWaitAllJobs(cxt->workers);
      res  = worker_contexts[0].res;
      *cxt = worker_contexts[0];
      free(worker_contexts);
      return res;
   }
   else if (cxt->numChannels == 1)
   {
      if (input) cxt->input = input - 1;
      cxt->numInputFrames        = numInputFrames;
      cxt->output                = output - 1;
      cxt->numOutputFrames       = numOutputFrames;
      cxt->cbuffer               = cxt->buffers[0];
      cxt->ratio                 = ratio;
      cxt->stride                = 1;
      cxt->res.output_generated  = 0;
      cxt->res.input_used        = 0;
      resampleProcessChannelJob(cxt, NULL);
      return cxt->res;
   }
   else
   {
#endif
   int            half_taps = cxt->numTaps / 2, i;
   ResampleResult res       = { 0, 0 };
   double         offset2   = 0.0;

   if (numInputFrames < 0)
      postfillAllChannels(cxt);

   while (numOutputFrames > 0)
   {
      if (cxt->outputOffset + offset2 >= cxt->inputIndex - half_taps)
      {
         if (numInputFrames > 0)
         {
            if (cxt->inputIndex == cxt->numSamples)
            {
               for (i = 0; i < cxt->numChannels; ++i)
                  memmove(cxt->buffers[i],
                          cxt->buffers[i] + cxt->numSamples - cxt->numTaps,
                          cxt->numTaps * sizeof(artsample_t));

               cxt->outputOffset -= cxt->numSamples - cxt->numTaps;
               cxt->inputIndex   -= cxt->numSamples - cxt->numTaps;
            }

            for (i = 0; i < cxt->numChannels; ++i)
               cxt->buffers[i][cxt->inputIndex] = *input++;

            cxt->inputIndex++;
            res.input_used++;
            numInputFrames--;
         }
         else
            break;
      }
      else
      {
#ifdef ENABLE_EXTRAPOLATION
         if (cxt->flags & EXTRAPOLATE_PREFILL)
         {
            cxt->flags &= ~EXTRAPOLATE_PREFILL;
            prefillAllChannels(cxt);
         }
#endif
         for (i = 0; i < cxt->numChannels; ++i)
            *output++ = cxt->subsample(cxt, cxt->buffers[i], cxt->outputOffset + offset2);

         offset2 = ++res.output_generated / ratio;
         numOutputFrames--;
      }
   }

   cxt->outputOffset += offset2;

   if (cxt->flags & RESAMPLER_SNAP_OFFSET)
      cxt->outputOffset =
         floor(cxt->outputOffset) +
         floor((cxt->outputOffset - floor(cxt->outputOffset)) * cxt->numFilters + 0.5) /
            cxt->numFilters;

   return res;
#ifdef ENABLE_THREADS
   }
#endif
}

/* ============================================================================
   resampleProcessAndFlush / resampleProcessAndFlushInterleaved
   ============================================================================ */
ResampleResult resampleProcessAndFlush(Resample *cxt,
                                        const artsample_t *const *input, int numInputFrames,
                                        artsample_t *const *output, int numOutputFrames,
                                        double ratio)
{
   artsample_t  **output_array = calloc(sizeof(artsample_t *), cxt->numChannels);
   ResampleResult res          = { 0, 0 }, fres;
   int            c;

   for (c = 0; c < cxt->numChannels; ++c)
      output_array[c] = output[c];

   res = resampleProcess(cxt, input, numInputFrames, output_array, numOutputFrames, ratio);

   if ((numInputFrames -= res.input_used) != 0 || (numOutputFrames -= res.output_generated) == 0)
   {
      free(output_array);
      return res;
   }

   for (c = 0; c < cxt->numChannels; ++c)
      output_array[c] += res.output_generated;

   fres = resampleProcess(cxt, NULL, -1, output_array, numOutputFrames, ratio);
   res.output_generated += fres.output_generated;

   free(output_array);
   return res;
}

ResampleResult resampleProcessAndFlushInterleaved(Resample *cxt,
                                                   const artsample_t *input, int numInputFrames,
                                                   artsample_t *output, int numOutputFrames,
                                                   double ratio)
{
   ResampleResult res = { 0, 0 }, fres;

   res = resampleProcessInterleaved(cxt, input, numInputFrames, output, numOutputFrames, ratio);

   if ((numInputFrames -= res.input_used) != 0 || (numOutputFrames -= res.output_generated) == 0)
      return res;

   output += res.output_generated * cxt->numChannels;
   fres    = resampleProcessInterleaved(cxt, NULL, -1, output, numOutputFrames, ratio);
   res.output_generated += fres.output_generated;

   return res;
}

/* ============================================================================
   Thread worker (only compiled when ENABLE_THREADS is defined)
   ============================================================================ */
#ifdef ENABLE_THREADS
static int resampleProcessChannelJob(void *ptr, void *sync_not_used)
{
   Resample *cxt       = ptr;
   int       half_taps = cxt->numTaps / 2;
   double    offset2   = 0.0;

   if (cxt->numInputFrames < 0)
   {
      if (cxt->numSamples - cxt->inputIndex < cxt->numTaps / 2)
      {
         memmove(cxt->cbuffer,
                 cxt->cbuffer + cxt->numSamples - cxt->numTaps,
                 cxt->numTaps * sizeof(artsample_t));
         cxt->outputOffset -= cxt->numSamples - cxt->numTaps;
         cxt->inputIndex   -= cxt->numSamples - cxt->numTaps;
      }

      memset(cxt->cbuffer + cxt->inputIndex, 0,
             (cxt->numSamples - cxt->inputIndex) * sizeof(artsample_t));

#ifdef ENABLE_EXTRAPOLATION
      if (cxt->flags & EXTRAPOLATE_ENDPOINTS)
         extrapolate_forward(cxt->cbuffer + cxt->inputIndex - cxt->numTaps / 2,
                             cxt->numTaps / 2, cxt->numTaps / 2);
#endif

      cxt->flags |= RESAMPLER_FLUSHED;
      cxt->inputIndex += cxt->numTaps / 2;
   }

   while (cxt->numOutputFrames > 0)
   {
      if (cxt->outputOffset + offset2 >= cxt->inputIndex - half_taps)
      {
         if (cxt->numInputFrames > 0)
         {
            if (cxt->inputIndex == cxt->numSamples)
            {
               memmove(cxt->cbuffer,
                       cxt->cbuffer + cxt->numSamples - cxt->numTaps,
                       cxt->numTaps * sizeof(artsample_t));
               cxt->outputOffset -= cxt->numSamples - cxt->numTaps;
               cxt->inputIndex   -= cxt->numSamples - cxt->numTaps;
            }

            cxt->cbuffer[cxt->inputIndex++] = *(cxt->input += cxt->stride);
            cxt->res.input_used++;
            cxt->numInputFrames--;
         }
         else
            break;
      }
      else
      {
#ifdef ENABLE_EXTRAPOLATION
         if (cxt->flags & EXTRAPOLATE_PREFILL)
         {
            int num_samples = cxt->inputIndex - cxt->numTaps;
            if (num_samples >= 8)
               extrapolate_reverse(cxt->cbuffer + cxt->inputIndex, num_samples,
                                   cxt->numTaps - num_samples);
            cxt->flags &= ~EXTRAPOLATE_PREFILL;
         }
#endif
         *(cxt->output += cxt->stride) =
            cxt->subsample(cxt, cxt->cbuffer, cxt->outputOffset + offset2);
         offset2 = ++(cxt->res.output_generated) / cxt->ratio;
         cxt->numOutputFrames--;
      }
   }

   cxt->outputOffset += offset2;

   if (cxt->flags & RESAMPLER_SNAP_OFFSET)
      cxt->outputOffset =
         floor(cxt->outputOffset) +
         floor((cxt->outputOffset - floor(cxt->outputOffset)) * cxt->numFilters + 0.5) /
            cxt->numFilters;

   return 0;
}
#endif /* ENABLE_THREADS */

/* ============================================================================
   resampleGetRequiredSamples / resampleGetExpectedOutput
   ============================================================================ */
unsigned int resampleGetRequiredSamples(Resample *cxt, int numOutputFrames, double ratio)
{
   int            half_taps   = cxt->numTaps / 2;
   int            input_index = cxt->inputIndex;
   double         offset      = cxt->outputOffset;
   ResampleResult res         = { 0, 0 };

   if (cxt->flags & RESAMPLE_FIXED_RATIO)
      ratio = cxt->fixedRatio;

   while (numOutputFrames > 0)
   {
      if (offset >= input_index - half_taps)
      {
         if (input_index == cxt->numSamples)
         {
            offset      -= cxt->numSamples - cxt->numTaps;
            input_index -= cxt->numSamples - cxt->numTaps;
         }
         input_index++;
         res.input_used++;
      }
      else
      {
         offset += (1.0 / ratio);
         numOutputFrames--;
      }
   }

   return res.input_used;
}

unsigned int resampleGetExpectedOutput(Resample *cxt, int numInputFrames, double ratio)
{
   int            half_taps   = cxt->numTaps / 2;
   int            input_index = cxt->inputIndex;
   double         offset      = cxt->outputOffset;
   ResampleResult res         = { 0, 0 };

   if (cxt->flags & RESAMPLE_FIXED_RATIO)
      ratio = cxt->fixedRatio;

   if (cxt->flags & RESAMPLER_FLUSHED)
      numInputFrames = 0;
   else if (numInputFrames < 0)
      input_index += half_taps;

   while (1)
   {
      if (offset >= input_index - half_taps)
      {
         if (numInputFrames > 0)
         {
            if (input_index == cxt->numSamples)
            {
               offset      -= cxt->numSamples - cxt->numTaps;
               input_index -= cxt->numSamples - cxt->numTaps;
            }
            input_index++;
            numInputFrames--;
         }
         else
            break;
      }
      else
      {
         offset += (1.0 / ratio);
         res.output_generated++;
      }
   }

   return res.output_generated;
}

/* ============================================================================
   resampleAdvancePosition / resampleGetPosition
   ============================================================================ */
void resampleAdvancePosition(Resample *cxt, double delta)
{
   if (delta < 0.0)
      fprintf(stderr, "resampleAdvancePosition() can only advance forward!\n");
   else if (!(cxt->flags & SUBSAMPLE_INTERPOLATE) && floor(delta) != delta)
      fprintf(stderr,
              "resampleAdvancePosition() cannot advance partial samples without interpolation!\n");
   else
      cxt->outputOffset += delta;
}

double resampleGetPosition(Resample *cxt)
{
   return cxt->outputOffset + (cxt->numTaps / 2.0) - cxt->inputIndex;
}

/* ============================================================================
   resampleFree
   ============================================================================ */
void resampleFree(Resample *cxt)
{
   if (cxt)
   {
      int i;

      for (i = 0; i <= cxt->numFilters; ++i)
         free(cxt->filters[i]);

      free(cxt->filters);

      for (i = 0; i < cxt->numChannels; ++i)
         free(cxt->buffers[i]);

      free(cxt->buffers);

#ifdef ENABLE_THREADS
      if (cxt->workers)
         workersDeinit(cxt->workers);
#endif

      free(cxt);
   }
}

/* ============================================================================
   GCD helper
   ============================================================================ */
static unsigned long gcd(unsigned long a, unsigned long b)
{
   while (b)
   {
      unsigned long t = (a %= b);
      a               = b;
      b               = t;
   }
   return a;
}

/* ============================================================================
   Convolution kernel — apply_filter
   ============================================================================ */
#if !defined(_MSC_VER) || defined(__clang__) || defined(__llvm__) || \
   defined(__INTEL_COMPILER) || defined(__INTEL_LLVM_COMPILER)

static double apply_filter(artsample_t *A, artsample_t *B, int num_taps)
{
   int         i   = num_taps - 1;
   artsample_t sum = 0.0;

   do
   {
      sum += (A[0] * B[0]) + (A[i] * B[i]);
      A++;
      B++;
   } while ((i -= 2) > 0);

   return sum;
}

static double apply_filter_precise(artsample_t *A, artsample_t *B, int num_taps)
{
   double sum = 0.0;

   do
      sum += (double)*A++ * *B++;
   while (--num_taps);

   return sum;
}

#else

static double apply_filter(artsample_t *A, artsample_t *B, int num_taps)
{
   int         i   = num_taps - 1;
   artsample_t sum = 0.0;

   do
   {
      sum += (A[0] * B[0]) + (A[i] * B[i]) + (A[1] * B[1]) + (A[i - 1] * B[i - 1]);
      A += 2;
      B += 2;
   } while ((i -= 4) > 0);

   return sum;
}

static double apply_filter_precise(artsample_t *A, artsample_t *B, int num_taps)
{
   int    i   = num_taps - 1;
   double sum = 0.0;

   do
   {
      sum += ((double)A[0] * B[0]) + ((double)A[i] * B[i]) + ((double)A[1] * B[1]) +
             ((double)A[i - 1] * B[i - 1]);
      A += 2;
      B += 2;
   } while ((i -= 4) > 0);

   return sum;
}

#endif

/* ============================================================================
   init_filter
   ============================================================================ */
static void init_filter(Resample *cxt, artsample_t *filter, double fraction)
{
   double       filter_sum = 0.0, scaler, error;
   const double a0         = 0.35875;
   const double a1         = 0.48829;
   const double a2         = 0.14128;
   const double a3         = 0.01168;
   int          i;

   for (i = 0; i < cxt->numTaps; ++i)
   {
      double dist  = fabs((cxt->numTaps / 2 - 1) + fraction - i) * M_PI;
      double ratio = dist / (cxt->numTaps / 2);
      double value;

      if (dist != 0.0)
      {
         value = sin(dist * cxt->lowpassRatio) / (dist * cxt->lowpassRatio);

         if (cxt->flags & BLACKMAN_HARRIS)
            value *= a0 + a1 * cos(ratio) + a2 * cos(2 * ratio) + a3 * cos(3 * ratio);
         else
            value *= 0.5 * (1.0 + cos(ratio));
      }
      else
         value = 1.0;

      filter_sum += cxt->tempFilter[i] = value;
   }

   scaler = 1.0 / filter_sum;
   error  = 0.0;

   for (i = cxt->numTaps / 2;
        i < cxt->numTaps;
        i = cxt->numTaps - i - (i >= cxt->numTaps / 2))
   {
      filter[i] = (cxt->tempFilter[i] *= scaler) - error;
      error += filter[i] - cxt->tempFilter[i];
   }
}

/* ============================================================================
   Subsample functions
   ============================================================================ */
static double subsample_no_interpolate(Resample *cxt, artsample_t *source, double offset)
{
   int fi = (int)floor((offset - floor(offset)) * cxt->numFilters + 0.5);

   source += (int)floor(offset);

   if (!(cxt->flags & INCLUDE_LOWPASS) && !(fi % cxt->numFilters))
      return source[fi / cxt->numFilters];

   return apply_filter(cxt->filters[fi], source - cxt->numTaps / 2 + 1, cxt->numTaps);
}

static double subsample_interpolate(Resample *cxt, artsample_t *source, double offset)
{
   double frac = offset - floor(offset);
   int    fi   = (int)floor(frac *= cxt->numFilters);

   frac -= fi;
   source += (int)floor(offset) - cxt->numTaps / 2 + 1;

   return (apply_filter(cxt->filters[fi], source, cxt->numTaps) * (1.0 - frac)) +
          (apply_filter(cxt->filters[fi + 1], source, cxt->numTaps) * frac);
}

static double subsample_no_interpolate_precise(Resample *cxt, artsample_t *source, double offset)
{
   int fi = (int)floor((offset - floor(offset)) * cxt->numFilters + 0.5);

   source += (int)floor(offset);

   if (!(cxt->flags & INCLUDE_LOWPASS) && !(fi % cxt->numFilters))
      return source[fi / cxt->numFilters];

   return apply_filter_precise(cxt->filters[fi], source - cxt->numTaps / 2 + 1, cxt->numTaps);
}

static double subsample_interpolate_precise(Resample *cxt, artsample_t *source, double offset)
{
   double frac = offset - floor(offset);
   int    fi   = (int)floor(frac *= cxt->numFilters);

   frac -= fi;
   source += (int)floor(offset) - cxt->numTaps / 2 + 1;

   return (apply_filter_precise(cxt->filters[fi], source, cxt->numTaps) * (1.0 - frac)) +
          (apply_filter_precise(cxt->filters[fi + 1], source, cxt->numTaps) * frac);
}