/*==================================================================
  File Name    : stc1000p.c
  Author       : Mats Staffansson / Emile
  ------------------------------------------------------------------
  Purpose : This files contains the main() function and all the 
            hardware related functions for the STM8 uC.
            It is meant for the STC1000 thermostat hardware WR-032.
            
            The original source is created by Mats Staffansson and
            adapted for the STM8S uC by Emile
  ------------------------------------------------------------------
  STC1000+ is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 
  STC1000+ is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with STC1000+.  If not, see <http://www.gnu.org/licenses/>.
  ------------------------------------------------------------------
  $Log: $
  ==================================================================
*/ 
#include "stc1000p.h"
#include "stc1000p_lib.h"
#include "scheduler.h"
#include "temp.h"
#include "eep.h"

// Global variables
bool      ad_err1 = false; // used for adc range checking
bool      ad_err2 = false; // used for adc range checking
uint8_t   probe2  = 0;     // cached flag indicating whether 2nd probe is active
bool      show_sa_alarm = false; // true = display alarm
bool      sound_alarm   = false; // true = sound alarm
bool      ad_ch   = false; // used in adc_task()
uint16_t  ad_ntc1 = (512L << FILTER_SHIFT);
uint16_t  ad_ntc2 = (512L << FILTER_SHIFT);
int16_t   temp_ntc1;         // The temperature in E-1 �C from NTC probe 1
int16_t   temp_ntc2;         // The temperature in E-1 �C from NTC probe 2
uint8_t   mpx_nr = 0;        // Used in multiplexer() function
// When in SWIM Debug Mode, PORT_D1/SWIM needs to be disabled (= no IO)
uint8_t   portd_leds;        // Contains define PORTD_LEDS
uint8_t   portb, portc, portd, b;// Needed for save_display_state() and restore_display_state()
int16_t   pwr_on_tmr = 1000;     // Needed for 7-segment display test

// External variables, defined in other files
extern uint8_t led_e;                 // value of extra LEDs
extern uint8_t led_10, led_1, led_01; // values of 10s, 1s and 0.1s
extern bool    pwr_on;           // True = power ON, False = power OFF
extern uint8_t sensor2_selected; // DOWN button pressed < 3 sec. shows 2nd temperature / pid_output
extern bool    minutes;          // timing control: false = hours, true = minutes
extern bool    menu_is_idle;     // No menus in STD active
extern bool    fahrenheit;       // false = Celsius, true = Fahrenheit
extern uint16_t cooling_delay;   // Initial cooling delay
extern uint16_t heating_delay;   // Initial heating delay
extern int16_t  setpoint;        // local copy of SP variable
extern int16_t  kc;              // Parameter value for Kc value in %/�C
extern uint8_t  ts;              // Parameter value for sample time [sec.]
extern int16_t  pid_out;         // Output from PID controller in E-1 %

#if defined(OVBSC)
extern uint8_t  prg_state;
extern uint8_t  al_led_10, al_led_1, al_led_01;  // values of 10s, 1s and 0.1s
extern bool     ovbsc_pid_on;
extern bool     ovbsc_pump_on;
extern bool     ovbsc_run_prg;
extern uint16_t countdown;
#endif

/*-----------------------------------------------------------------------------
  Purpose  : This routine saves the current state of the 7-segment display.
             This is necessary since the buttons, but also the AD-channels,
             share the same GPIO bits.
             It uses the global variables portb, portc and portd.
  Variables: -
  Returns  : -
  ---------------------------------------------------------------------------*/
void save_display_state(void)
{
    // Save registers that interferes with LEDs and AD-channels
    portb   = PORT_B.IDR.byte & (CC_10 | CC_1); // Common-Cathode for 10s and 1s
    portc   = PORT_C.IDR.byte & BUTTONS;        // LEDs connected to buttons 
    // Common-Cathode for 0.1s and extras and AD-channels AIN4 and AIN3
    portd   = PORT_D.IDR.byte & (CC_01 | CC_e | AD_CHANNELS); 
    PORT_B.ODR.byte |= (CC_10 | CC_1);  // Disable common-cathode for 10s and 1s
    PORT_D.ODR.byte |= (CC_01 | CC_e);  // Disable common-cathode for 0.1s and extras
} // save_display_state()

/*-----------------------------------------------------------------------------
  Purpose  : This routine restores the current state of the 7-segment display.
             This is necessary since the buttons, but also the AD-channels,
             share the same GPIO bits.
             It uses the global variables portb, portc and portd.
  Variables: -
  Returns  : -
  ---------------------------------------------------------------------------*/
void restore_display_state(void)
{
    PORT_D.ODR.byte &= ~(CC_01 | CC_e | AD_CHANNELS); // init. CC_01, CC_e and AD-channels
    PORT_D.ODR.byte |= portd;                         // restore values for PORTD
    PORT_C.ODR.byte &= ~BUTTONS;                      // init. buttons
    PORT_C.ODR.byte |= portc;                         // restore values for PORTC
    PORT_B.ODR.byte &= ~(CC_10 | CC_1);               // init. CC_10 and CC_1
    PORT_B.ODR.byte |= portb;                         // restore values for PORTB
} // restore_display_state()

/*-----------------------------------------------------------------------------
  Purpose  : This routine multiplexes the 4 segments of the 7-segment displays.
             It runs at 1 kHz, so there's a full update after every 4 msec.
  Variables: -
  Returns  : -
  ---------------------------------------------------------------------------*/
void multiplexer(void)
{
    PORT_C.ODR.byte    &= ~PORTC_LEDS;    // Clear LEDs
    PORT_D.ODR.byte    &= ~portd_leds;    // Clear LEDs
    PORT_B.ODR.byte    |= (CC_10 | CC_1); // Disable common-cathode for 10s and 1s
    PORT_D.ODR.byte    |= (CC_01 | CC_e); // Disable common-cathode for 0.1s and extras
   
    switch (mpx_nr)
    {
        case 0: // output 10s digit
            PORT_C.ODR.byte |= (led_10 & PORTC_LEDS);        // Update PORT_C7..PORT_C3
            PORT_D.ODR.byte |= ((led_10 << 1) & portd_leds); // Update PORT_D3..PORT_D1
            PORT_B.ODR.byte &= ~CC_10;    // Enable  common-cathode for 10s
            if (sound_alarm) ALARM_ON;
            mpx_nr = 1;
            break;
        case 1: // output 1s digit
            PORT_C.ODR.byte |= (led_1 & PORTC_LEDS);        // Update PORT_C7..PORT_C3
            PORT_D.ODR.byte |= ((led_1 << 1) & portd_leds); // Update PORT_D3..PORT_D1
            PORT_B.ODR.byte &= ~CC_1;     // Enable  common-cathode for 1s
            ALARM_OFF;
            mpx_nr = 2;
            break;
        case 2: // output 01s digit
            PORT_C.ODR.byte |= (led_01 & PORTC_LEDS);        // Update PORT_C7..PORT_C3
            PORT_D.ODR.byte |= ((led_01 << 1) & portd_leds); // Update PORT_D3..PORT_D1
            PORT_D.ODR.byte &= ~CC_01;    // Enable common-cathode for 0.1s
            if (sound_alarm) ALARM_ON;
            mpx_nr = 3;
            break;
        case 3: // outputs special digits
            PORT_C.ODR.byte |= (led_e & PORTC_LEDS);        // Update PORT_C7..PORT_C3
            PORT_D.ODR.byte |= ((led_e << 1) & portd_leds); // Update PORT_D3..PORT_D1
            PORT_D.ODR.byte &= ~CC_e;     // Enable common-cathode for extras
            ALARM_OFF;
        default: // FALL-THROUGH (less code-size)
            mpx_nr = 0;
            break;
            //mpx_nr = 0;
            //break;
    } // switch            
} // multiplexer()

/*-----------------------------------------------------------------------------
  Purpose  : This is the interrupt routine for the Timer 2 Overflow handler.
             It runs at 1 kHz and drives the scheduler and the multiplexer.
  Variables: -
  Returns  : -
  ---------------------------------------------------------------------------*/
ISR_HANDLER(TIM2_UPD_ISR, __TIM2_UPD_VECTOR__)
{
    scheduler_isr();  // Run scheduler interrupt function
    if (!pwr_on)
    {   // Display OFF on dispay
	led_10     = LED_O;
	led_1      = led_01 = LED_F;
        led_e      = LED_OFF;
        pwr_on_tmr = 2000; // 2 seconds
    } // if
    else if (pwr_on_tmr > 0)
    {	// 7-segment display test for 2 seconds
        pwr_on_tmr--;
        led_10 = led_1 = led_01 = led_e = LED_ON;
    } // else if
    multiplexer();    // Run multiplexer for Display and Keys
    TIM2.SR1.reg.UIF = 0; // Reset the interrupt otherwise it will fire again straight away.
} // TIM2_UPD_OVF_IRQHandler()

/*-----------------------------------------------------------------------------
  Purpose  : This routine initialises the system clock to run at 16 MHz.
             It uses the internal HSI oscillator.
  Variables: -
  Returns  : -
  ---------------------------------------------------------------------------*/
void initialise_system_clock(void)
{
    CLK.ICKR.byte       = 0;           //  Reset the Internal Clock Register.
    CLK.ICKR.reg.HSIEN = 1;           //  Enable the HSI.
    while (CLK.ICKR.reg.HSIRDY == 0); //  Wait for the HSI to be ready for use.
    CLK.CKDIVR.byte     = 0;           //  Ensure the clocks are running at full speed.
 
    // The datasheet lists that the max. ADC clock is equal to 6 MHz (4 MHz when on 3.3V).
    // Because fMASTER is now at 16 MHz, we need to set the ADC-prescaler to 4.
    ADC1.CR1.reg.SPSEL  = 0x02;        //  Set prescaler to 4, fADC = 4 MHz
    CLK.SWIMCCR.byte    = 0;           //  Set SWIM to run at clock / 2.
    CLK.SWR.byte        = 0xe1;        //  Use HSI as the clock source.
    CLK.SWCR.byte       = 0;           //  Reset the clock switch control register.
    CLK.SWCR.reg.SWEN  = 1;           //  Enable switching.
    while (CLK.SWCR.reg.SWBSY != 0);  //  Pause while the clock switch is busy.
} // initialise_system_clock()

/*-----------------------------------------------------------------------------
  Purpose  : This routine initialises Timer 2 to generate a 1 kHz interrupt.
             16 MHz / (  16 *  1000) = 1000 Hz (1000 = 0x03E8)
  Variables: -
  Returns  : -
  ---------------------------------------------------------------------------*/
void setup_timer2(void)
{
    TIM2.PSCR.byte    = 0x04;  //  Prescaler = 16
    TIM2.ARR.byteH    = 0x03;  //  High byte of 1000
    TIM2.ARR.byteL    = 0xE8;  //  Low  byte of 1000
    TIM2.IER.reg.UIE = 1;     //  Enable the update interrupts
    TIM2.CR1.reg.CEN = 1;     //  Finally enable the timer
} // setup_timer2()

/*-----------------------------------------------------------------------------
  Purpose  : This routine initialises all the GPIO pins of the STM8 uC.
             See stc1000p.h for a detailed description of all pin-functions.
  Variables: -
  Returns  : -
  ---------------------------------------------------------------------------*/
void setup_output_ports(void)
{
    PORT_A.ODR.byte      = 0x00; // Turn off all pins from Port A
    PORT_A.DDR.byte     |= (S3 | COOL | HEAT); // Set as output
    PORT_A.CR1.byte     |= (S3 | COOL | HEAT); // Set to Push-Pull
    PORT_B.ODR.byte      = 0x30; // Turn off all pins from Port B (CC1 = CC2 = H)
    PORT_B.DDR.byte     |= 0x30; // Set PORT_B5..PORT_B4 as output
    PORT_B.CR1.byte     |= 0x30; // Set PORT_B5..PORT_B4 to Push-Pull
    PORT_C.ODR.byte      = 0x00; // Turn off all pins from Port C
    PORT_C.DDR.byte     |= PORTC_LEDS; // Set PORT_C7..PORT_C3 as outputs
    PORT_C.CR1.byte     |= PORTC_LEDS; // Set PORT_C7..PORT_C3 to Push-Pull
    PORT_D.ODR.byte      = 0x30; // Turn off all pins from Port D (CC3 = CCe = H)
    PORT_D.DDR.byte     |= (0x70 | portd_leds); // Set PORT_D6..PORT_D1 as outputs
    PORT_D.CR1.byte     |= (0x70 | portd_leds); // Set PORT_D6..PORT_D1 to Push-Pull
} // setup_output_ports()

/*-----------------------------------------------------------------------------
  Purpose  : This task is called every 500 msec. and processes the NTC 
             temperature probes from NTC1 (PORT_D3/AIN4) and NTC2 (PORT_D2/AIN3)
  Variables: -
  Returns  : -
  ---------------------------------------------------------------------------*/
void adc_task(void)
{
  uint16_t temp;
  uint8_t  i;
  
  // Save registers that interferes with LED's and disable common-cathodes
  DISABLE_INTERRUPTS;      // Disable interrups while reading buttons
  save_display_state();       // Save current state of 7-segment displays
  PORT_D.DDR.byte &= ~AD_CHANNELS;     // Set PORT_D3 (AIN4) and PORT_D2 (AIN3) to inputs
  PORT_D.CR1.byte &= ~AD_CHANNELS;     // Set to floating-inputs (required by ADC)  
  for (i = 0; i < 200; i++) ; // Disable to let input signal settle
  if (ad_ch)
  {  // Process NTC probe 1
     temp       = read_adc(AD_NTC1);
     ad_ntc1    = ((ad_ntc1 - (ad_ntc1 >> FILTER_SHIFT)) + temp);
     temp_ntc1  = ad_to_temp(ad_ntc1,&ad_err1);
     temp_ntc1 += eeprom_read_config(EEADR_MENU_ITEM(tc));
  } // if
#if !(defined(OVBSC))
  else
  {  // Process NTC probe 2
     temp       = read_adc(AD_NTC2);
     ad_ntc2    = ((ad_ntc2 - (ad_ntc2 >> FILTER_SHIFT)) + temp);
     temp_ntc2  = ad_to_temp(ad_ntc2,&ad_err2);
     temp_ntc2 += eeprom_read_config(EEADR_MENU_ITEM(tc2));
  } // else
#endif
  ad_ch = !ad_ch;

  // Since the ADC disables GPIO pins automatically, these need
  // to be set to GPIO output pins again.
  PORT_D.DDR.byte  |= AD_CHANNELS;  // Set PORT_D3 (AIN4) and PORT_D2 (AIN3) as outputs again
  PORT_D.CR1.byte  |= AD_CHANNELS;  // Set PORT_D3 (AIN4) and PORT_D2 (AIN3) to Push-Pull again
  restore_display_state(); // Restore state of 7-segment displays
  ENABLE_INTERRUPTS;    // Re-enable Interrupts
} // adc_task()

/*-----------------------------------------------------------------------------
  Purpose  : This task is called every 100 msec. and creates a slow PWM signal
             from pid_output: T = 12.5 seconds. This signal can be used to
             drive a Solid-State Relay (SSR).
  Variables: pid_out (global) is used
  Returns  : -
  ---------------------------------------------------------------------------*/
void pid_to_time(void)
{
    static uint8_t std_ptt = 1; // state [on, off]
    static uint8_t ltmr    = 0; // #times to set S3 to 0
    static uint8_t htmr    = 0; // #times to set S3 to 1
    uint8_t x;                  // temp. variable
     
    x = (uint8_t)(pid_out >> 3); // divide by 8 to give 1.25 * pid_out
    
    switch (std_ptt)
    {
        case 0: // OFF
            if (ts == 0) std_ptt = 2;
            else if (ltmr == 0)
            {   // End of low-time
                htmr = x; // htmr = 1.25 * pid_out
                if ((htmr > 0) && pwr_on) std_ptt = 1;
            } // if
            else ltmr--; // decrease timer
            S3_OFF;      // S3 output = 0
            led_e &= ~(LED_HEAT | LED_COOL); // disable both LEDs
            break;
        case 1: // ON
            if (ts == 0) std_ptt = 2;
            else if (htmr == 0)
            {   // End of high-time
                ltmr = 125 - x; // ltmr = 1.25 * (100 - pid_out)
                if ((ltmr > 0) || !pwr_on) std_ptt = 0;
            } // if
            else htmr--; // decrease timer
            S3_ON;       // S3 output = 1
            if (kc > 0) led_e |= LED_HEAT; // Heating loop active
            else        led_e |= LED_COOL; // Cooling loop active
            break;
        case 2: // DISABLED
            S3_OFF; // S3 output = 0;
            if (ts > 0) std_ptt = 1;
            break;
    } // switch
} // pid_to_time()

/*-----------------------------------------------------------------------------
  Purpose  : This task is called every 100 msec. and reads the buttons, runs
             the STD and updates the 7-segment display.
  Variables: -
  Returns  : -
  ---------------------------------------------------------------------------*/
void std_task(void)
{
    read_buttons(); // reads the buttons keys, result is stored in _buttons
    menu_fsm();     // Finite State Machine menu
    pid_to_time();  // Make Slow-PWM signal and send to S3 output-port
} // std_task()

#if defined(OVBSC)
/*-----------------------------------------------------------------------------
  Purpose  : This task is called every second and contains the main control
             task for one vessel brew system controller (OVBSC)
  Variables: -
  Returns  : -
  ---------------------------------------------------------------------------*/
void ctrl_task(void)
{
    if (eeprom_read_config(EEADR_MENU_ITEM(CF))) // true = Fahrenheit
         fahrenheit = true;
    else fahrenheit = false;

    ovbsc_fsm(); // run OVBSC Finite State Machine
   // Start with updating the alarm
   if (ad_err1)
   {
       sound_alarm = true;
       RELAYS_OFF; // disable the output relays
       if (menu_is_idle)
       {  // Make it less anoying to nagivate menu during alarm
          led_10 = LED_A;
	  led_1  = LED_L;
          led_01 = LED_1;
	  led_e  = LED_OFF;
       } // if
   } 
   else 
   {
       ts = eeprom_read_config(EEADR_MENU_ITEM(Ts)); // Read Ts [seconds]
       pid_control(ovbsc_pid_on);  // Control PID controller
       if (ovbsc_pump_on) 
       {
           PUMP_ON; // Control pump
           led_e |= LED_COOL; // Pump LED on
       } // if
       else 
       {
           PUMP_OFF;
           led_e &= ~LED_COOL; // Cooling LED off
       } // else
       if (menu_is_idle)           // show counter/temperature if menu is idle
       {
           if (sound_alarm && show_sa_alarm)
           {
               led_10 = al_led_10;
	       led_1  = al_led_1;
	       led_01 = al_led_01;
           } else {
               if (ovbsc_run_prg && (prg_state == PRG_BOIL) || (prg_state == PRG_WAIT_STRIKE))
                    value_to_led(countdown,LEDS_INT);
               else value_to_led(temp_ntc1,LEDS_TEMP);
           } // else
           show_sa_alarm = !show_sa_alarm;
       } // if
   } // else
} // ctrl_task()
#else
/*-----------------------------------------------------------------------------
  Purpose  : This task is called every second and contains the main control
             task for the device. It also calls temperature_control().
  Variables: -
  Returns  : -
  ---------------------------------------------------------------------------*/
void ctrl_task(void)
{
   int16_t sa, diff;
   
    if (eeprom_read_config(EEADR_MENU_ITEM(CF))) // true = Fahrenheit
         fahrenheit = true;
    else fahrenheit = false;
    if (eeprom_read_config(EEADR_MENU_ITEM(HrS))) // true = hours
         minutes = false; // control-timing is in hours 
    else minutes = true;  // control-timing is in minutes

   // Start with updating the alarm
   // cache whether the 2nd probe is enabled or not.
   probe2 = (uint8_t)eeprom_read_config(EEADR_MENU_ITEM(Pb2)); 
   if (ad_err1 || (ad_err2 && probe2))
   {
       sound_alarm = true;
       RELAYS_OFF; // disable the output relays
       if (menu_is_idle)
       {  // Make it less anoying to nagivate menu during alarm
          led_10 = LED_A;
	  led_1  = LED_L;
          if (ad_err1) led_01 = LED_1;
          else         led_01 = LED_2;
	  led_e = LED_OFF;
       } // if
       cooling_delay = heating_delay = 60;
   } else {
       sound_alarm = false; // reset the piezo buzzer
       if(((uint8_t)eeprom_read_config(EEADR_MENU_ITEM(rn))) < THERMOSTAT_MODE)
            led_e |=  LED_SET; // Indicate profile mode
       else led_e &= ~LED_SET;
 
       ts = eeprom_read_config(EEADR_MENU_ITEM(Ts)); // Read Ts [seconds]
       sa = eeprom_read_config(EEADR_MENU_ITEM(SA)); // Show Alarm parameter
       if (sa)
       {
           if (minutes) // is timing-control in minutes?
                diff = temp_ntc1 - setpoint;
	   else diff = temp_ntc1 - eeprom_read_config(EEADR_MENU_ITEM(SP));

	   if (diff < 0) diff = -diff;
	   if (sa < 0)
           {
  	      sa = -sa;
              sound_alarm = (diff <= sa); // enable buzzer if diff is small
	   } else {
              sound_alarm = (diff >= sa); // enable buzzer if diff is large
	   } // if
       } // if
       if (!minutes) setpoint = eeprom_read_config(EEADR_MENU_ITEM(SP));
       if (ts == 0)                // PID Ts parameter is 0?
       {
           temperature_control();  // Run thermostat
           pid_out = 0;            // Disable PID-output
       } // if
       else 
       {
           pid_control(true);      // Run PID controller
           RELAYS_OFF;             // Disable relays
       } // else
       if (menu_is_idle)           // show temperature if menu is idle
       {
           if (sound_alarm && show_sa_alarm)
           {
               led_10 = LED_A;
	       led_1  = LED_L;
	       led_01 = LED_d;
           } else {
               led_e &= ~LED_POINT; // LED in middle, does not seem to work
               switch (sensor2_selected)
               {
                   case 0: value_to_led(temp_ntc1,LEDS_TEMP); 
                           break;
                   case 1: value_to_led(temp_ntc2,LEDS_TEMP); 
                           led_e |= LED_POINT;
                           break;
                   case 2: value_to_led(pid_out  ,LEDS_PERC) ; 
                           break;
               } // switch
           } // else
           show_sa_alarm = !show_sa_alarm;
       } // if
   } // else
} // ctrl_task()

/*-----------------------------------------------------------------------------
  Purpose  : This task is called every minute or every hour and updates the
             current running temperature profile.
  Variables: minutes: timing control: false = hours, true = minutes
  Returns  : -
  ---------------------------------------------------------------------------*/
void prfl_task(void)
{
    static uint8_t min = 0;
    
    if (minutes)
    {   // call every minute
        update_profile();
        min = 0;
    } else {
        if (++min >= 60)
        {   // call every hour
            min = 0;
            update_profile(); 
        } // if
    } // else
} // prfl_task();
#endif

/*-----------------------------------------------------------------------------
  Purpose  : This is the main entry-point for the entire program.
             It initialises everything, starts the scheduler and dispatches
             all tasks.
  Variables: -
  Returns  : -
  ---------------------------------------------------------------------------*/
int main(void)
{
    if (RST.SR.reg.SWIMF) // Check for SWIM Debug Reset
         portd_leds = PORTD_LEDS_SWIM;
    else portd_leds = PORTD_LEDS;
    DISABLE_INTERRUPTS;
    initialise_system_clock(); // Set system-clock to 16 MHz
    setup_output_ports();      // Init. needed output-ports for LED and keys
    setup_timer2();            // Set Timer 2 to 1 kHz
#if !(defined(OVBSC))
    pwr_on = eeprom_read_config(EEADR_POWER_ON); // check pwr_on flag
#endif    
    // Initialise all tasks for the scheduler
    add_task(adc_task ,"ADC",  0,  500); // every 500 msec.
    add_task(std_task ,"STD", 50,  100); // every 100 msec.
    add_task(ctrl_task,"CTL",200, 1000); // every second
#if !(defined(OVBSC))
    add_task(prfl_task,"PRF",300,60000); // every minute / hour
#endif    
    ENABLE_INTERRUPTS;

    while (1)
    {   // background-processes
        dispatch_tasks();       // Run task-scheduler()
        //__wait_for_interrupt(); // do nothing
    } // while
} // main()
