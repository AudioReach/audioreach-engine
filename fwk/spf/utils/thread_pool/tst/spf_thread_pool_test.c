/***
 * \file spf_thread_pool_test.c
 * \brief
 *    This file tests the Thread Pool functionality.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spf_thread_pool_test.h"

/* thread id of the thread pool tester */
posal_thread_t test_thread_id;

typedef struct spf_tp_test_job
{
   uint32_t       bit_mask;       // unique ID and the signal mask associated with this job
   posal_signal_t job_signal_ptr; // signal for this job

   spf_thread_pool_job_t job; // job pushed to the thread pool

   uint32_t fact_numb; // number for which factorial calculation is done
} spf_tp_test_job;

static ar_result_t spf_thread_pool_test_main(void *arg_ptr);

/********************************************************************************/

ar_result_t spf_thread_pool_test_init()
{
   AR_MSG(DBG_LOW_PRIO, "Thread Pool Tests entry.");

   uint32_t    thread_stack_size = 1024;
   uint32_t    thread_priority   = 50;
   ar_result_t result            = AR_EOK;

   if (AR_FAILED(result = posal_thread_launch(&test_thread_id,
                                              "THREAD_POOL_TESTS",
                                              thread_stack_size,
                                              thread_priority,
                                              spf_thread_pool_test_main,
                                              (void *)NULL,
                                              POSAL_HEAP_DEFAULT)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Launching thread for Thread Pool Test- FAILED.");
   }

   return result;
}

ar_result_t spf_thread_pool_test_deinit()
{
   ar_result_t result;
   posal_thread_join(test_thread_id, &result);

   AR_MSG(DBG_LOW_PRIO, "Thread Pool Tests completed.");

   return result;
}

/********************************************************************************/
// Factorial
static inline uint32_t fact(uint32_t num)
{
   uint32_t fact_result = 1;
   for (uint32_t i = 1; i <= num; i++)
   {
      fact_result *= i;
   }
   return fact_result;
}

// Job function which executes in the thread pool context
static inline ar_result_t spf_thread_pool_test_job_func(void *job_context_ptr)
{
   spf_tp_test_job *job_ptr = (spf_tp_test_job *)job_context_ptr;

   AR_MSG(DBG_LOW_PRIO,
          "Starting job with mask 0x%x, thread priority 0x%x",
          job_ptr->bit_mask,
          posal_thread_prio_get());

   // Run the job function
   uint32_t fact_result = fact(job_ptr->fact_numb);

   AR_MSG(DBG_LOW_PRIO,
          "Completed job with mask 0x%x, thread priority 0x%x, fact_result %lu, number %lu",
          job_ptr->bit_mask,
          posal_thread_prio_get(),
          fact_result,
          job_ptr->fact_numb);

   // Signal is set to notify the tester about job completion.
   posal_signal_send(job_ptr->job_signal_ptr);

   return AR_EOK;
}

/********************************************************************************/
static ar_result_t spf_thread_pool_test_job_init(spf_tp_test_job *job_ptr,
                                                 posal_channel_t *job_channel_ptr,
                                                 uint32_t         fact_num)
{
   ar_result_t result = AR_EOK;

   // Initialize to zero
   memset(job_ptr, 0, sizeof(spf_tp_test_job));

   // Assign the number for factorial calculation
   job_ptr->fact_numb = fact_num;

   // Create signal so that main test thread can wait for this job completion
   if (AR_EOK != (result = posal_signal_create(&(job_ptr->job_signal_ptr), POSAL_HEAP_DEFAULT)))
   {
      AR_MSG(DBG_ERROR_PRIO, "TP Signal creation failed");
      return result;
   }

   // Add signal to the channel which will be used by the main test thread to wait
   if (AR_EOK != (result = posal_channel_add_signal(*job_channel_ptr, job_ptr->job_signal_ptr, 0)))
   {
      AR_MSG(DBG_ERROR_PRIO, "TP Signal Init failed");
      posal_signal_destroy(&(job_ptr->job_signal_ptr));
      return result;
   }

   // Cache the signal bitmask in the channel, this can also be used as an unique identifier for this job
   job_ptr->bit_mask = posal_signal_get_channel_bit(job_ptr->job_signal_ptr);

   // Assign the job function and context to run in the thread pool's worker thread
   job_ptr->job.job_func_ptr    = &spf_thread_pool_test_job_func;
   job_ptr->job.job_context_ptr = (void *)job_ptr;

   return result;
}

static void spf_thread_pool_test_job_deinit(spf_tp_test_job *job_ptr)
{
   // If signal is initialized then destroy it
   if (job_ptr->job_signal_ptr)
   {
      posal_signal_destroy(&(job_ptr->job_signal_ptr));
   }
}

static ar_result_t spf_thread_pool_test_main(void *arg_ptr)
{
   ar_result_t     result   = AR_EOK;
   uint32_t        NUM_JOBS = 10;
   posal_channel_t job_channel_ptr;
   uint32_t fact_nums[]              = { 12, 3, 11, 4, 10, 5, 9, 6, 8, 7 };
   uint32_t expected_channel_bitmask = 0;

   // Thread Pool instance
   spf_thread_pool_inst_t *tp_inst_ptr1 = NULL;

   // Memory for each job
   spf_tp_test_job *job_array =
      (spf_tp_test_job *)posal_memory_malloc(sizeof(spf_tp_test_job) * NUM_JOBS, POSAL_HEAP_DEFAULT);

   if (NULL == job_array)
   {
      AR_MSG(DBG_ERROR_PRIO, "Thread Pool Tests: Malloc Failed !!");

      return AR_ENOMEMORY;
   }

   // Create a Channel for synchronization
   if (AR_EOK != (result = posal_channel_create(&job_channel_ptr, POSAL_HEAP_DEFAULT)))
   {
      AR_MSG(DBG_HIGH_PRIO, "Thread Pool Test - Channel creation failed");
      return result;
   }

   // Initialize each job
   for (uint32_t i = 0; i < NUM_JOBS; i++)
   {
      result = spf_thread_pool_test_job_init(&job_array[i], &job_channel_ptr, fact_nums[i]);
      if (AR_FAILED(result))
      {
         goto cleanup;
      }
   }

   // Get a thread pool instance to run the jobs
   if (AR_EOK !=
       (result = spf_thread_pool_get_instance(&tp_inst_ptr1,
                                              POSAL_HEAP_DEFAULT,
                                              50,    /* thread priority for jobs to run */
                                              FALSE, /* is_dedicated_pool */
                                              1024,  /* job function minimum stack requirement */
                                              4,     /* number of worker threads required to handle parallel jobs */
                                              0 /*log id.*/)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Thread Pool Test - Instance creation failed");
      goto cleanup;
   }

   // Push half of the jobs to the Thread Pool
   for (int i = 0; i < NUM_JOBS / 2; i++)
   {
      expected_channel_bitmask |= job_array[i].bit_mask;

      if (AR_FAILED(result = spf_thread_pool_push_job(tp_inst_ptr1, &job_array[i].job, 0)))
      {
         expected_channel_bitmask ^= job_array[i].bit_mask;

         AR_MSG(DBG_ERROR_PRIO, "Thread Pool Test - Job addition failed");
         goto cleanup;
      }
   }

   // Update Thread Pool instance parameters
   AR_MSG(DBG_MED_PRIO, "Thread Pool test: Updating parameters");
   result = spf_thread_pool_update_instance(&tp_inst_ptr1, 17 * 1024, 5 /*added one more worker thread*/, 0);
   if (AR_FAILED(result))
   {
      AR_MSG(DBG_ERROR_PRIO, "Thread Pool Test - instance update failed");
      goto cleanup;
   }

   // Push remaining Jobs to the Thread Pool
   for (int i = NUM_JOBS / 2; i < NUM_JOBS; i++)
   {
      expected_channel_bitmask |= job_array[i].bit_mask;

      if (AR_FAILED(result = spf_thread_pool_push_job(tp_inst_ptr1, &job_array[i].job, 0)))
      {
         expected_channel_bitmask ^= job_array[i].bit_mask;
         AR_MSG(DBG_ERROR_PRIO, "Thread Pool Test - Job addition failed");
         goto cleanup;
      }
   }

cleanup:
   // Clear the signal bit once each task is completed
   while (expected_channel_bitmask)
   {
      expected_channel_bitmask ^= posal_channel_wait(job_channel_ptr, expected_channel_bitmask);
   }

   AR_MSG(DBG_HIGH_PRIO, "Thread Pool Test - All Jobs are completed.");

   for (int i = 0; i < NUM_JOBS; i++)
   {
      spf_thread_pool_test_job_deinit(&job_array[i]);
   }

   spf_thread_pool_release_instance(&tp_inst_ptr1, 0);

   AR_MSG(DBG_MED_PRIO, "Thread Pool Test complete.");

   return result;
}
