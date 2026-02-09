.. zephyr:code-sample:: audioreach_engine_sample
   :name: AudioReach Engine Sample

   Initialize and run AudioReach Engine on Zephyr RTOS.

Overview
********

This sample demonstrates how to initialize the AudioReach Engine (ARE) 
framework on Zephyr RTOS. It initializes the core components including:

* POSAL (Platform Operating System Abstraction Layer)
* GPR (Generic Packet Router) 
* AudioReach Engine  (Signal Processing Framework)

The sample provides a foundation for building audio processing applications
using the AudioReach Engine on Zephyr-supported hardware.

Requirements
************

* AudioReach Engine module
* OpenAMP support for inter-processor communication