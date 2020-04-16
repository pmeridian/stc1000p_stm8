/**
  \file config.h
   
  \author G. Icking-Konert
  \date 2013-11-22
  \version 0.1
   
  \brief project specific settings
   
  project specific configuration header file
  Select STM8 device and activate optional options
*/

/*-----------------------------------------------------------------------------
    MODULE DEFINITION FOR MULTIPLE INCLUSION
-----------------------------------------------------------------------------*/
#ifndef _CONFIG_H_
#define _CONFIG_H_

//#include <stdint.h>

// select board to set STM8 family, memory size etc. 
//#include "muBoard_config.h"

/// alternatively select STM8 device directly. For supported devices see file "stm8as.h"
//#define STM8S103      

/// required for timekeeping (1ms interrupt)
//#define USE_TIM4_UPD_ISR

/// define UART send & receive ISRs
//#define USE_UART1_RXF_ISR
//#define USE_UART1_TXE_ISR
#define USE_TIM2_UPD_ISR

//#define OVBSC

/*-----------------------------------------------------------------------------
    END OF MODULE DEFINITION FOR MULTIPLE INLUSION
-----------------------------------------------------------------------------*/
#endif  // _CONFIG_H_
