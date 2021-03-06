// TODO: merge rol_string´and display_string_running() into one single, nonblocking function
// TODO: avoid flickering while cooling down
// TODO: show the dot for temp_gain_correction (tgc) (2.40 instead of 240)


/*
 * This is a custom firmware for my 'Youyue 858D+' hot-air soldering station.
 * It may or may not be useful to you, always double check if you use it.
 *
 * V1.47
 *
 * 2017    - Florian Dollinger
 * 2015/16 - Robert Spitzenpfeil
 * 2015    - Moritz Augsburger
 *
 * License: GNU GPL v2
 *
 *
 * Developed for / tested on by Robert Spitzenpfeil:
 * -------------------------------------------------
 *
 * Date:	2015-02-01
 * PCB version: 858D V6.0
 * Date code:   20140415
 *
 * Developed for / tested on by Moritz Augsburger:
 * -----------------------------------------------
 *
 * Date:	2015-02-01
 * PCB version: 858D V6.0
 * Date code:   20140415
 * 
 * Reported to work with (I did not test these myself):
 * ----------------------------------------------------
 *
 * PCB version: 858D V4.3
 * Date code:   20130529
 * HW mods:     not tested!
 *
 * ---
 *
 * PCB version: 858D V4.10
 * Date code:	20140112
 * HW mods:	not tested!
 *
 */

/*
 *  Make sure to read and understand '/Docs/modes_of_operation.txt'
 *
 *
 * If you compile and upload using the Arduino-IDE + ISP, choose one of these target boards:
 *
 * LilyPad Arduino w/ ATmega168 (TESTED - WORKS)
 * LilyPad Arduino w/ ATmega328 (TESTED - WORKS)
 *
 * DO NOT USE A BOOTLOADER WITH THE WATCHDOG TIMER
 * 
 * ISP CODE UPLOAD ONLY
 * 
 * Change options in the .h file
 *
 */

#define FW_MAJOR_V 1
#define FW_MINOR_V_A 4
#define FW_MINOR_V_B 7
/*
 * PC5: FAN-speed (A5 in Arduino lingo) (OK)
 * PC3: TIP122.base --> FAN (OK)
 * PC2: fan-current-sense mod (OPTIONAL) - see Docs folder
 * PC0: ADC <-- amplif. thermo couple voltage (A0 in Arduino lingo) (OK)
 * #21: AREF <--- about 2.5V as analog reference for ADC
 * PB1: opto-triac driver !! THIS IS DANGEROUS TO USE !! (OK)
 *
 * PB0: 7-seg digit 0 [common Anode] (OK) 
 * PB7: 7-seg digit 1 [common Anode] (OK)
 * PB6: 7-seg digit 2 [common Anode] (OK)
 *
 * PD0: 7-seg top (OK)
 * PD1: 7-seg bottom left (OK)
 * PD2: 7-seg bottom (OK)
 * PD3: 7-seg top left (OK)
 * PD4: 7-seg dot (OK)
 * PD5: 7-seg bottom right (OK)
 * PD6: 7-seg middle (OK)
 * PD7: 7-seg top right (OK)
 *
 * PB5: SW1 (button1) (OK)
 * PB2: SW2 (button2) (OK)
 * PB4: reed switch (wand cradle sensor) (OK)
 *
 */

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <EEPROM.h>
#include "youyue858d.h"

// FRAMEBUFFERS
//-----------------------------------------------
// Contain the values to be displayed next

// fb is the "raw" data which is directly displayed by the timer-ISR
uint8_t fb[3] = { 0xFF, 0xFF, 0xFF };	// dig0, dig1, dig2
	
// framebuffer is a bit more structured needs and fb_update() to be stored in fb
framebuffer_t framebuffer = { 0x00, 0x00, 0x00, 0, 0, 0, 0 };


// PARAMETERS
//-----------------------------------------------
// min, max, default, value, eep_addr_high, eep_addr_low

CPARAM p_gain = { 0, 999, P_GAIN_DEFAULT, P_GAIN_DEFAULT, 2, 3 };
CPARAM i_gain = { 0, 999, I_GAIN_DEFAULT, I_GAIN_DEFAULT, 4, 5 };
CPARAM d_gain = { 0, 999, D_GAIN_DEFAULT, D_GAIN_DEFAULT, 6, 7 };
CPARAM i_thresh = { 0, 100, I_THRESH_DEFAULT, I_THRESH_DEFAULT, 8, 9 };
CPARAM temp_offset_corr = { -100, 100, TEMP_OFFSET_CORR_DEFAULT, TEMP_OFFSET_CORR_DEFAULT, 10, 11 };
CPARAM temp_gain_corr = { 100, 999, TEMP_GAIN_DEFAULT, TEMP_GAIN_DEFAULT, 30, 31 };
CPARAM temp_setpoint = { 50, 500, TEMP_SETPOINT_DEFAULT, TEMP_SETPOINT_DEFAULT, 12, 13 };
CPARAM temp_averages = { 100, 999, TEMP_AVERAGES_DEFAULT, TEMP_AVERAGES_DEFAULT, 14, 15 };
CPARAM slp_timeout = { 0, 30, SLP_TIMEOUT_DEFAULT, SLP_TIMEOUT_DEFAULT, 16, 17 };
CPARAM fan_only = { 0, 1, 0, 0, 26, 27 };
CPARAM display_adc_raw = { 0, 1, 0, 0, 28, 29 };
#ifdef CURRENT_SENSE_MOD
CPARAM fan_current_min = { 0, 999, FAN_CURRENT_MIN_DEFAULT, FAN_CURRENT_MIN_DEFAULT, 22, 23 };
CPARAM fan_current_max = { 0, 999, FAN_CURRENT_MAX_DEFAULT, FAN_CURRENT_MAX_DEFAULT, 24, 25 };
#else
//
// See youyue858d.h if you want to use the 'FAN-speed mod' (HW changes required)
// The following 2 CPARAM lines need changes in that case
//
CPARAM fan_speed_min = { 120, 180, FAN_SPEED_MIN_DEFAULT, FAN_SPEED_MIN_DEFAULT, 18, 19 };
CPARAM fan_speed_max = { 300, 400, FAN_SPEED_MAX_DEFAULT, FAN_SPEED_MAX_DEFAULT, 20, 21 };
#endif

// KEY STATE
//-----------------------------------------------

volatile uint8_t key_state;	// debounced and inverted key state: bit = 1: key pressed
volatile uint8_t key_press;	// key press detect
volatile uint8_t key_rpt;	// key long press and repeat


volatile uint8_t display_blink;


//////////////////////////////////////////////////////////////////////////
// MAIN
//////////////////////////////////////////////////////////////////////////

int main(void)
{
	init();			// make sure the Arduino-specific stuff is up and running (timers... see 'wiring.c')
	setup_858D();

#ifdef DISPLAY_MCUSR
	HEATER_OFF;
	FAN_ON;
	display_number(_mcusr);
	MCUSR = 0;
	//
	// ATmega168 MCUSR (MSB to LSB): x-x-x-x-WDRF-BORF-EXTRF-PORF
	//
	delay(1000);
#endif

#ifdef USE_WATCHDOG
	if (_mcusr & _BV(WDRF)) {
		// there was a watchdog reset - should never ever happen
		HEATER_OFF;
		FAN_ON;
		while (1) {
			display_string("RST");
			delay(1000);
			clear_display();
			delay(1000);
		}
	}
#endif

	show_firmware_version();
#ifdef USE_WATCHDOG
	test_F_CPU_with_watchdog();
#endif
	fan_test();

#ifdef USE_WATCHDOG
	watchdog_on();
#endif

#ifdef DEBUG
	Serial.begin(2400);
	Serial.println("\nRESET");
#endif


	while (1) {
		
#ifdef DEBUG
		int32_t start_time = micros();
#endif
		static int16_t temp_inst = 0;
		static int32_t temp_accu = 0;
		static int16_t temp_average = 0;
		static int16_t temp_average_previous = 0;

		static int32_t button_input_time = 0;

		static int16_t heater_ctr = 0;
		static int16_t heater_duty_cycle = 0;
		static int16_t error = 0;
		static int32_t error_accu = 0;
		static int16_t velocity = 0;
		static float PID_drive = 0;

		static uint8_t temp_setpoint_saved = 1;
		static int32_t temp_setpoint_saved_time = 0;

		static uint32_t heater_start_time = 0;
		
		static uint32_t temp_low_fanonly_firstTime = 0;
		static uint32_t temp_low_firstTime = 0;
				
		static bool temp_low_fanonly_firstEvent = 1;
		static bool temp_low_firstEvent = 1;
		
		static bool fan_off_allowed = 0;
		static bool was_hot_before = 0;
		

		uint16_t adc_raw = analogRead(A0);	// need raw value later, store it here and avoid 2nd ADC read
		

		temp_inst = (adc_raw / (temp_gain_corr.value / 100)) + temp_offset_corr.value;	// approx. temp in °C

		if (temp_inst < 0) {
			temp_inst = 0;
		}
		// pid loop / heater handling
		if (fan_only.value == 1 || REEDSW_CLOSED) {
			HEATER_OFF;
			heater_start_time = millis();
			clear_dot();
		} else if (REEDSW_OPEN
					&& (temp_setpoint.value >= temp_setpoint.value_min)
					&& (temp_average < MAX_TEMP_ERR)
					&& ((millis() - heater_start_time) < ((uint32_t) (slp_timeout.value) * 60 * 1000))) {

			FAN_ON;

			error = temp_setpoint.value - temp_average;
			velocity = temp_average_previous - temp_average;

			if (abs(error) < i_thresh.value) {
				// if close enough to target temperature use PID control
				error_accu += error;
			} else {
				// otherwise only use PD control (avoids issues with error_accu growing too large
				error_accu = 0;
			}

			PID_drive =
			    error * (p_gain.value / P_GAIN_SCALING) + error_accu * (i_gain.value / I_GAIN_SCALING) +
			    velocity * (d_gain.value / D_GAIN_SCALING);

			heater_duty_cycle = (int16_t) (PID_drive);

			if (heater_duty_cycle > HEATER_DUTY_CYCLE_MAX) {
				heater_duty_cycle = HEATER_DUTY_CYCLE_MAX;
			}

			if (heater_duty_cycle < 0) {
				heater_duty_cycle = 0;
			}

			if (heater_ctr < heater_duty_cycle) {
				set_dot();
				HEATER_ON;
			} else {
				HEATER_OFF;
				clear_dot();
			}

			heater_ctr++;
			if (heater_ctr == PWM_CYCLES) {
				heater_ctr = 0;
			}
		} else {
			HEATER_OFF;
			clear_dot();
		}

		static uint16_t temp_avg_ctr = 0;

		temp_accu += temp_inst;
		temp_avg_ctr++;

		if (temp_avg_ctr == (uint16_t) (temp_averages.value)) {
			temp_average_previous = temp_average;
			temp_average = temp_accu / temp_averages.value;
			temp_accu = 0;
			temp_avg_ctr = 0;
		}
		
		
		/////////////////////////////////
		// FAN CRADLE HANDLING
		/////////////////////////////////
		//
		// TODO: On the second run the fan switches off immediately, why?
		

		if(temp_average <= FAN_OFF_TEMP){

			// Nur im ersten Druchlauf nach einer heißen Phase:
			// Zeit merken zu der dieser Zustand zum ersten Mal erreicht wurde
			
			if(was_hot_before && temp_low_firstEvent){
				temp_low_firstTime = millis();
				temp_low_firstEvent = 0;
			}
			
			// In weiteren Durchläufen:
			// Vergangene Zeit berechnen seit erreichen dieses Events
			// Sofern die Zeit >= FAN_OFF_TEMP_DELAY_MILLI
			//   Erlaubnis zum Abschalten des Lüfters geben
			
			if(!temp_low_firstEvent){
				
				if((millis() - temp_low_firstTime) >= FAN_OFF_TEMP_DELAY_MILLI)
					fan_off_allowed = 1;
			}
				
		} else {
			
			// reset variables
			was_hot_before = 1;
			temp_low_firstEvent = 1;
			
			fan_off_allowed = 0;
			
		}
		

		if (temp_average >= FAN_ON_TEMP) {
			
			FAN_ON;
			
		} else if (REEDSW_CLOSED && fan_only.value == 1 && (temp_average <= FAN_OFF_TEMP_FANONLY)) {
			
			//FAN_OFF;
						
		} else if (REEDSW_CLOSED && fan_only.value == 0 && (temp_average <= FAN_OFF_TEMP)) {
			
			if(fan_off_allowed)	FAN_OFF;
			
		} else if (REEDSW_OPEN) {
			FAN_ON;
		}
		
		
		
		/////////////////////////////////
		// MENU KEY HANDLING
		/////////////////////////////////
		
		// CHANGING THE TEMPERATURE
		
		// - INCREASE BY 1
		if (get_key_short(1 << KEY_UP)) {
			button_input_time = millis();
			if (temp_setpoint.value < temp_setpoint.value_max) {
				temp_setpoint.value++;
			}
			temp_setpoint_saved = 0;
			
		// - DECREASE BY 1
		} else if (get_key_short(1 << KEY_DOWN)) {
			button_input_time = millis();
			if (temp_setpoint.value > temp_setpoint.value_min) {
				temp_setpoint.value--;
			}
			temp_setpoint_saved = 0;
			
		// - INCREASE BY 10
		} else if (get_key_long_r(1 << KEY_UP) || get_key_rpt_l(1 << KEY_UP)) {
			button_input_time = millis();
			if (temp_setpoint.value < (temp_setpoint.value_max - 10)) {
				temp_setpoint.value += 10;
			} else {
				temp_setpoint.value = temp_setpoint.value_max;
			}
			temp_setpoint_saved = 0;
			
		// - DECREASE BY 10
		} else if (get_key_long_r(1 << KEY_DOWN) || get_key_rpt_l(1 << KEY_DOWN)) {
			button_input_time = millis();

			if (temp_setpoint.value > (temp_setpoint.value_min + 10)) {
				temp_setpoint.value -= 10;
			} else {
				temp_setpoint.value = temp_setpoint.value_min;
			}

			temp_setpoint_saved = 0;
			
		// ENTER THE MENU
		} else if (get_key_common_l(1 << KEY_UP | 1 << KEY_DOWN)) {
			HEATER_OFF;	// security reasons, delay below!
#ifdef USE_WATCHDOG
			watchdog_off();
#endif
			delay(uint16_t(20.48 * (REPEAT_START - 3) + 1));
			if (get_key_long_r(1 << KEY_UP | 1 << KEY_DOWN)) {
				change_config_parameter(&p_gain, "P");
				change_config_parameter(&i_gain, "I");
				change_config_parameter(&d_gain, "D");
				change_config_parameter(&i_thresh, "ITH");
				change_config_parameter(&temp_offset_corr, "TOF");
				change_config_parameter(&temp_gain_corr, "TGN");
				change_config_parameter(&temp_averages, "AVG");
				change_config_parameter(&slp_timeout, "SLP");
				change_config_parameter(&display_adc_raw, "ADC");
#ifdef CURRENT_SENSE_MOD
				change_config_parameter(&fan_current_min, "FCL");
				change_config_parameter(&fan_current_max, "FCH");
#else
				change_config_parameter(&fan_speed_min, "FSL");
				change_config_parameter(&fan_speed_max, "FSH");
#endif

			// FAN ONLY MODE
			// This mode is entered only at setup_858D() below
			} else {
				get_key_press(1 << KEY_UP | 1 << KEY_DOWN);	// clear inp state
				fan_only.value ^= 0x01;
				temp_setpoint_saved = 0;
				if (fan_only.value == 0) {
					button_input_time = millis();	// show set temp after disabling fan only mode
				}
				display_blink = 0;	// make sure we start displaying "FAN" or set temp
			}
#ifdef USE_WATCHDOG
			watchdog_on();
#endif
		}
		
		
		/////////////////////////////////
		// TEMPERATURE SECURITY
		/////////////////////////////////
		
		if (temp_average >= MAX_TEMP_ERR) {
			// something might have gone terribly wrong
			HEATER_OFF;
			FAN_ON;
#ifdef USE_WATCHDOG
			watchdog_off();
#endif
			while (1) {
				// stay here until the power is cycled
				// make sure the user notices the error by blinking "FAN"
				// and don't resume operation if the error goes away on its own
				//
				// possible reasons to be here:
				//
				// * wand is not connected (false temperature reading)
				// * thermo couple has failed
				// * true over-temperature condition
				//
				display_string("*C");
				delay(1000);
				display_string("ERR");
				delay(2000);
				clear_display();
				delay(1000);
			}
		}
		
		
		/////////////////////////////////
		// DISPLAY OUTPUT
		/////////////////////////////////
		
		if ((millis() - button_input_time) < SHOW_SETPOINT_TIMEOUT) {
			if (display_blink < 5) {
				clear_display();
			} else {
				display_number(temp_setpoint.value);	// show temperature setpoint
			}
		} else {
			if (temp_setpoint_saved == 0) {
				set_eeprom_saved_dot();
				eep_save(&temp_setpoint);
				eep_save(&fan_only);
				temp_setpoint_saved_time = millis();
				temp_setpoint_saved = 1;
			} else if (temp_average <= SAFE_TO_TOUCH_TEMP) {
				if (fan_only.value == 1) {
					display_string("FAN");
				} else {
					display_string("---");
				}
			} else if (fan_only.value == 1) {
				if (display_blink < 20) {
					display_string("FAN");
				} else {
					display_number(temp_average);
				}
			} else if (display_adc_raw.value == 1) {
				display_number(adc_raw);
			} else if (abs((int16_t) (temp_average) - (int16_t) (temp_setpoint.value)) < TEMP_REACHED_MARGIN) {
				display_number(temp_setpoint.value);	// avoid showing insignificant fluctuations on the display (annoying)
			} else {
				display_number(temp_average);
			}
		}

		if ((millis() - temp_setpoint_saved_time) > 500) {
			clear_eeprom_saved_dot();
		}

		fb_update();

#if defined(WATCHDOG_TEST) && defined(USE_WATCHDOG)
		// watchdog test
		if (temp_average > 100) {
			delay(150);
		}
#endif

#ifdef USE_WATCHDOG
		wdt_reset();
#endif

#ifdef DEBUG
		int32_t stop_time = micros();
		Serial.println(stop_time - start_time);
#endif
	}

}

void setup_858D(void)
{
	HEATER_OFF;
	DDRB |= _BV(PB1);	// set as output for TRIAC control

	DDRB &= ~(_BV(PB5) | _BV(PB2));	// set as inputs (switches)
	PORTB |= (_BV(PB5) | _BV(PB2));	// pull-up on

	DDRB &= ~_BV(PB4);	// set as input (reed sensor)
	PORTB |= _BV(PB4);	// pull-up on

	FAN_OFF;
	DDRC |= _BV(PC3);	// set as output (FAN control)

	DDRD |= 0xFF;		// all as outputs (7-seg segments)
	DDRB |= (_BV(PB0) | _BV(PB6) | _BV(PB7));	// 7-seg digits 1,2,3

#ifdef CURRENT_SENSE_MOD
	DDRC &= ~_BV(PC2);	// set as input
#endif

	setup_timer1_ctc();	// needed for background display refresh

	analogReference(EXTERNAL);	// use external 2.5V as ADC reference voltage (VCC / 2)

	if (EEPROM.read(0) != 0x22) {
		// check if the firmware was just flashed and the EEPROM is therefore empty
		// assumption: full chip erase with ISP programmer (preserve eeprom fuse NOT set!)
		// if so, restore default parameter values & write a 'hint' to address 0
		restore_default_conf();
		EEPROM.write(0, 0x22);
	}

	if (SW0_PRESSED && SW1_PRESSED) {
		restore_default_conf();
	} else if (SW0_PRESSED) {
		display_string("FAN");
		delay(1000);
		display_string("TST");
		delay(1000);
		FAN_ON;
		while (1) {
			uint16_t fan;
			delay(500);
#ifdef CURRENT_SENSE_MOD
			fan = analogRead(A2);
#else				//CURRENT_SENSE_MOD
			fan = analogRead(A5);
#endif				//CURRENT_SENSE_MOD
			display_number(fan);
		}
	}

	eep_load(&p_gain);
	eep_load(&i_gain);
	eep_load(&d_gain);
	eep_load(&i_thresh);
	eep_load(&temp_offset_corr);
	eep_load(&temp_gain_corr);
	eep_load(&temp_setpoint);
	eep_load(&temp_averages);
	eep_load(&slp_timeout);
	eep_load(&fan_only);
	eep_load(&display_adc_raw);
#ifdef CURRENT_SENSE_MOD
	eep_load(&fan_current_min);
	eep_load(&fan_current_max);
#else
	eep_load(&fan_speed_min);
	eep_load(&fan_speed_max);
#endif
}

void clear_display(void)
{
	framebuffer.digit[0] = 255;
	framebuffer.digit[1] = 255;
	framebuffer.digit[2] = 255;
	framebuffer.dot[0] = 0;
	framebuffer.dot[1] = 0;
	framebuffer.dot[2] = 0;
	framebuffer.changed = 1;
	fb_update();
}

void change_config_parameter(CPARAM * param, const char *string)
{
	display_string(string);
	delay(750);		// let the user read what is shown

	uint8_t loop = 1;

	while (loop == 1) {
		if (get_key_short(1 << KEY_UP)) {
			if (param->value < param->value_max) {
				param->value++;
			}
		} else if (get_key_short(1 << KEY_DOWN)) {
			if (param->value > param->value_min) {
				param->value--;
			}
		} else if (get_key_long_r(1 << KEY_UP) || get_key_rpt_l(1 << KEY_UP)) {
			if (param->value < param->value_max - 10) {
				param->value += 10;
			}
		} else if (get_key_long_r(1 << KEY_DOWN) || get_key_rpt_l(1 << KEY_DOWN)) {
			if (param->value > param->value_min + 10) {
				param->value -= 10;
			}
		} else if (get_key_common(1 << KEY_UP | 1 << KEY_DOWN)) {
			loop = 0;
		}

		display_number(param->value);
	}
	set_eeprom_saved_dot();
	eep_save(param);
	delay(1000);
	clear_eeprom_saved_dot();
}

void eep_save(CPARAM * param)
{
	// make sure NOT to save invalid parameter values
	if ((param->value >= param->value_min) && (param->value <= param->value_max)) {
		// nothing to do
	} else {
		// reset to sensible minimum
		param->value = param->value_default;
	}
	EEPROM.update(param->eep_addr_high, highByte(param->value));
	EEPROM.update(param->eep_addr_low, lowByte(param->value));
}

void eep_load(CPARAM * param)
{
	int16_t tmp = (EEPROM.read(param->eep_addr_high) << 8) | EEPROM.read(param->eep_addr_low);

	// make sure NOT to restore invalid parameter values
	if ((tmp >= param->value_min) && (tmp <= param->value_max)) {
		// the value was good, so we use it
		param->value = tmp;
	} else {
		// reset to sensible value
		param->value = param->value_default;
	}
}

void restore_default_conf(void)
{
	p_gain.value = p_gain.value_default;
	i_gain.value = i_gain.value_default;
	d_gain.value = d_gain.value_default;
	i_thresh.value = i_thresh.value_default;
	temp_offset_corr.value = temp_offset_corr.value_default;
	temp_gain_corr.value = temp_gain_corr.value_default;
	temp_setpoint.value = temp_setpoint.value_default;
	temp_averages.value = temp_averages.value_default;
	slp_timeout.value = slp_timeout.value_default;
	fan_only.value = 0;
	display_adc_raw.value = 0;
#ifdef CURRENT_SENSE_MOD
	fan_current_min.value = fan_current_min.value_default;
	fan_current_max.value = fan_current_max.value_default;
#else
	fan_speed_min.value = fan_speed_min.value_default;
	fan_speed_max.value = fan_speed_max.value_default;
#endif

	eep_save(&p_gain);
	eep_save(&i_gain);
	eep_save(&d_gain);
	eep_save(&i_thresh);
	eep_save(&temp_offset_corr);
	eep_save(&temp_gain_corr);
	eep_save(&temp_setpoint);
	eep_save(&temp_averages);
	eep_save(&slp_timeout);
	eep_save(&fan_only);
	eep_save(&display_adc_raw);
#ifdef CURRENT_SENSE_MOD
	eep_save(&fan_current_min);
	eep_save(&fan_current_max);
#else
	eep_save(&fan_speed_min);
	eep_save(&fan_speed_max);
#endif
}

void set_dot(void)
{
	framebuffer.dot[0] = 1;
	framebuffer.changed = 1;
	fb_update();
}

void clear_dot(void)
{
	framebuffer.dot[0] = 0;
	framebuffer.changed = 1;
	fb_update();
}

void set_eeprom_saved_dot(void)
{
	framebuffer.dot[1] = 1;
	framebuffer.changed = 1;
	fb_update();
}

void clear_eeprom_saved_dot(void)
{
	framebuffer.dot[1] = 0;
	framebuffer.changed = 1;
	fb_update();
}



 /**
  * \brief display a number.
  *
  * This function displays a number on the 7-segment display.
  * Related functions:
  * display_char()
  * display_string()
  *
  * \param[in] number     number to display (negative numbers => underflow on the display)
  * \return    void
  */
void display_number(int16_t number)
{
	if (number < 0) {
		framebuffer.dot[0] = 1;
		framebuffer.dot[1] = 1;
		framebuffer.dot[2] = 1;
		number = -number;
	} else {
		// don't clear framebuffer[3], as this is the heater-indicator
		framebuffer.dot[1] = 0;
		framebuffer.dot[2] = 0;
	}

	framebuffer.digit[0] = (uint8_t) (number % 10);
	number /= 10;
	framebuffer.digit[1] = (uint8_t) (number % 10);
	number /= 10;
	framebuffer.digit[2] = (uint8_t) (number % 10);
	framebuffer.changed = 1;
	fb_update();
}


 /**
  * \brief display a character.
  *
  * This function displays a character at a given position on the 7-segment display.
  *
  * \param[in] digit     position (0-2)
  * \param[in] character character to display (e.g. 'A' or '1')
  * \param[in] dot       if > 0: activate the dot behind the given position
  * \return    void
  */
void display_char(uint8_t digit, uint8_t character, uint8_t dot)
{
	uint8_t portout = 0xFF;
	
	// 8 Bit
	// -> '0'-Bit: active
	// -> '1'-Bit: not active
	
	//  0b0000_0000
	//	  |||| |||'--- top
	//    |||| ||'---- bottom-left
	//    |||| |'----- bottom
	//    |||| '------ top-left
	//    |||'-------- dot (on the right side of the segment)
	//    ||'--------- bottom-right
	//    |'---------- middle
	//    '----------- top-right
	
	
	switch (character) {
	case 0:
		// ~0b‭1010_1111‬ = 0b0101_0000
		portout = (uint8_t) (~0xAF);	// activate segments for displaying a '0'
		break;
	case 1:
		// ~0b‭1010_0000‬ = 0b0101_1111
		portout = (uint8_t) (~0xA0);	// '1'
		break;
	case 2:
		// ~0b‭1100_0111‬ = 0b0011_1000
		portout = (uint8_t) (~0xC7);	// '2'
		break;
	case 3:
		// ~0b‭1110_0101‬ = 0b0001_1010
		portout = (uint8_t) (~0xE5);	// '3'
		break;
	case 4:
		portout = (uint8_t) (~0xE8);	// '4'
		break;
	case 5:
		portout = (uint8_t) (~0x6D);	// '5'
		break;
	case 6:
		portout = (uint8_t) (~0x6F);	// '6'
		break;
	case 7:
		// ~0b‭10100001‬ = 0b01011110
		portout = (uint8_t) (~0xA1);	// '7'
		break;
	case 8:
		portout = (uint8_t) (~0xEF);	// '8'
		break;
	case 9:
		portout = (uint8_t) (~0xE9);	// '9'
		break;
	case '-':
		portout = (uint8_t) (~0x40);	// '-'
		break;
	case '.':
		// ~0b0001_0000 = 0b1110_1111
		portout = (uint8_t) (~0x10);	// '.'
		break;
	case 'A':
		portout = (uint8_t) (~0xEB);	// 'A'
		break;
	case 'B':
		portout = (uint8_t) (~0x6E);	// 'b'
		break;
	case 'C':
		portout = (uint8_t) (~0x0F);	// 'C'
		break;
	case 'D':
		portout = (uint8_t) (~0xE6);	// 'd'
		break;
	case 'E':
		portout = (uint8_t) (~0x4F);	// 'E'
		break;
	case 'F':
		portout = (uint8_t) (~0x4B);	// 'F'
		break;
	case 'G':
		portout = (uint8_t) (~0x6F);	// 'G'
		break;
	case 'H':
		portout = (uint8_t) (~0x6A);	// 'h'
		break;
	case 'I':
		portout = (uint8_t) (~0x20);	// 'i'
		break;
	case 'L':
		portout = (uint8_t) (~0x0E);	// 'L'
		break;
	case 'N':
		portout = (uint8_t) (~0xAB);	// 'N'
		break;
	case 'O':
		portout = (uint8_t) (~0x66);	// 'o'
		break;
	case 'P':
		portout = (uint8_t) (~0xCB);	// 'P'
		break;
	case 'R':
		portout = (uint8_t) (~0x42);	// 'r'
		break;
	case 'S':
		portout = (uint8_t) (~0x6D);	// 'S'
		break;
	case 'T':
		portout = (uint8_t) (~0x4E);	// 't'
		break;
	case 'U':
		portout = (uint8_t) (~0x26);	// 'u'          
		break;
	case 'V':
		portout = (uint8_t) (~0x26);	// 'v'
		break;
	case '*':
		portout = (uint8_t) (~0xC9);	// '°'
		break;
	case ' ':
	case 255:
		portout = (uint8_t) (0xFF);		// segments OFF
		break;
	default:
		portout = (uint8_t) (~0x10);	// '.'
		break;
	}

	if (dot)
		portout &= (~0x10);	// '.'

	fb[digit] = portout;
}


 /**
  * \brief display a string.
  *
  * This function displays a string on the 7-segment display.
  * It reads only the first three characters.
  *
  * \param[in] *string     pointer to the string to display
  * \return    void
  */
void display_string(const char *string)
{
	framebuffer.digit[0] = 255;
	framebuffer.digit[1] = 255;
	framebuffer.digit[2] = 255;
	framebuffer.dot[0] = 0;
	framebuffer.dot[1] = 0;
	framebuffer.dot[2] = 0;

	uint8_t ctr;

	for (ctr = 0; ctr <= 2; ctr++) {
		// read the first 3 characters of the string
		if (string[ctr] == '\0') {
			break;
			} else {
			framebuffer.digit[2 - ctr] = string[ctr];
		}
	}
	framebuffer.changed = 1;
	fb_update();
}

 /**
  * \brief display a string.
  *
  * This function displays a running string on the 7-segment display.
  * It reads out and displays every character until a '\0' is reached.
  * CAUTION! Always be sure that your string is null-terminated
  * CAUTION! This functions is blocking...
  * 
  * The standard delay is 500ms
  *
  * \param[in] *string     pointer to the c-string to display
  * \return    void
  */
void display_string_running(const char* string)
{
	// empty string, return
	if(string[0] == '\0')
		return;
		
	// string isn't long enough for "running", hence display normally
	if(string[1] == '\0' | string[2] == '\0' | string[3] == '\0')
		display_string(string);
	
	// if you get here, the string seems to be ok	
	for(uint8_t i = 0; string[i+2] != '\0'; i++){
		display_string(&string[i]);
		delay(500);
	}
	
}


/*
 * ROTATES THE CSTRING
 * to the left by 1
 */
void rol_string(char* text){
	
	int len = strlen(text);
	char tmp = text[0];
	
	for(int i = 0; i <= len-2; i++){
		text[i] = text[i+1];
	}
	
	text[len-1] = tmp;
}




void fan_test(void)
{
	HEATER_OFF;

	// if the wand is not in the cradle when powered up, go into a safe mode
	// and display an error

 	char displaytext[] = "CRADLE ";

	while (!REEDSW_CLOSED) {
 		display_string(displaytext);
		rol_string(displaytext);
		delay(500);
	}

FAN_ON;
delay(3000);
#ifdef CURRENT_SENSE_MOD
	uint16_t fan_current = analogRead(A2);

	if ((fan_current < (uint16_t) (fan_current_min.value)) || (fan_current > (uint16_t) (fan_current_max.value))) {
#else				//CURRENT_SENSE_MOD
	uint16_t fan_speed = analogRead(A5);

	if ((fan_speed < (uint16_t) (fan_speed_min.value)) || (fan_speed > (uint16_t) (fan_speed_max.value))) {
#endif				//CURRENT_SENSE_MOD

		// the fan is not working as it should
		FAN_OFF;
		while (1) {
#ifdef CURRENT_SENSE_MOD
			display_string_running("FANCUR");
#else				//CURRENT_SENSE_MOD
			display_string_running("FANSPD");
#endif				//CURRENT_SENSE_MOD
			delay(2000);
			clear_display();
			delay(1000);
		}
	}

	FAN_OFF;

}

void show_firmware_version(void)
{
	framebuffer.digit[0] = FW_MINOR_V_B;	// dig0
	framebuffer.digit[1] = FW_MINOR_V_A;	// dig1
	framebuffer.digit[2] = FW_MAJOR_V;	// dig2
	framebuffer.dot[0] = 0;	// dig0.dot
	framebuffer.dot[1] = 0;	// dig1.dot
	framebuffer.dot[2] = 1;	// dig2.dot
	framebuffer.changed = 1;
	fb_update();
	delay(2000);
}

void setup_timer1_ctc(void)
{
	// ATmega168 running at 8MHz internal RC oscillator
	// Timer1 (16bit) Settings:
	// prescaler (frequency divider) values:   CS12    CS11   CS10
	//                                           0       0      0    stopped
	//                                           0       0      1      /1  
	//                                           0       1      0      /8  
	//                                           0       1      1      /64
	//                                           1       0      0      /256 
	//                                           1       0      1      /1024
	//                                           1       1      0      external clock on T1 pin, falling edge
	//                                           1       1      1      external clock on T1 pin, rising edge
	//
	uint8_t _sreg = SREG;	/* save SREG */
	cli();			/* disable all interrupts while messing with the register setup */

	/* set prescaler to 256 */
	TCCR1B &= ~(_BV(CS11) | _BV(CS10));
	TCCR1B |= _BV(CS12);

	/* set WGM mode 4: CTC using OCR1A */
	TCCR1A &= ~(_BV(WGM10) | _BV(WGM11));
	TCCR1B |= _BV(WGM12);
	TCCR1B &= ~_BV(WGM13);

	/* normal operation - disconnect PWM pins */
	TCCR1A &= ~(_BV(COM1A1) | _BV(COM1A0) | _BV(COM1B1) | _BV(COM1B0));

	/* set top value for TCNT1 */
	OCR1A = 640;		// key debouncing every 20.48ms
	OCR1B = 8;		// new segment every 256µs, complete display update every 6ms <=> 160Hz

	/* enable COMPA and COMPB isr */
	TIMSK1 |= _BV(OCIE1A) | _BV(OCIE1B);

	/* restore SREG with global interrupt flag */
	SREG = _sreg;
}

ISR(TIMER1_COMPB_vect)
{
	static uint8_t digit = 0;

	digit++;

	if (digit == 24) {
		digit = 0;
	}

	uint8_t bm;
	// explicit switch is faster than variable shifting
	switch (digit & 0x07) {
	case 0:
		bm = ~(1 << 0);
		break;
	case 1:
		bm = ~(1 << 1);
		break;
	case 2:
		bm = ~(1 << 2);
		break;
	case 3:
		bm = ~(1 << 3);
		break;
	case 4:
		bm = ~(1 << 4);
		break;
	case 5:
		bm = ~(1 << 5);
		break;
	case 6:
		bm = ~(1 << 6);
		break;
	case 7:
		bm = (uint8_t) ~ (1 << 7);
		break;
	}

	// all segments OFF (set HIGH, as current sinks)
	SEGS_OFF;

	switch (digit / 8) {
	case 0:
		DIG0_ON;	// turn on digit #0 (from right)
		PORTD = fb[0] | bm;
		DIG1_OFF;
		DIG2_OFF;
		break;
	case 1:
		DIG1_ON;	// #1
		PORTD = fb[1] | bm;
		DIG0_OFF;
		DIG2_OFF;
		break;
	case 2:
		DIG2_ON;	// #2
		PORTD = fb[2] | bm;
		DIG0_OFF;
		DIG1_OFF;
		break;
	default:
		DIG0_OFF;
		DIG1_OFF;
		DIG2_OFF;
		break;
	}

	if (OCR1B == 640) {
		OCR1B = 8;
	} else {
		OCR1B += 8;
	}
}

ISR(TIMER1_COMPA_vect)
{
	// explained in https://www.mikrocontroller.net/articles/Entprellung#Komfortroutine_.28C_f.C3.BCr_AVR.29
	static uint8_t ct0, ct1, rpt;
	uint8_t i;

	i = key_state ^ ~KEY_PIN;	// key changed ?
	ct0 = ~(ct0 & i);	// reset or count ct0
	ct1 = ct0 ^ (ct1 & i);	// reset or count ct1
	i &= ct0 & ct1;		// count until roll over ?
	key_state ^= i;		// then toggle debounced state
	key_press |= key_state & i;	// 0->1: key press detect

	if ((key_state & REPEAT_MASK) == 0)	// check repeat function
		rpt = REPEAT_START;	// start delay
	if (--rpt == 0) {
		rpt = REPEAT_NEXT;	// repeat delay
		key_rpt |= key_state & REPEAT_MASK;
	}

	if (++display_blink > 50)
		display_blink = 0;
}

///////////////////////////////////////////////////////////////////
//
// check if a key has been pressed. Each pressed key is reported
// only once
//
uint8_t get_key_press(uint8_t key_mask)
{
	cli();			// read and clear atomic !
	key_mask &= key_press;	// read key(s)
	key_press ^= key_mask;	// clear key(s)
	sei();
	return key_mask;
}

///////////////////////////////////////////////////////////////////
//
// check if a key has been pressed long enough such that the
// key repeat functionality kicks in. After a small setup delay
// the key is reported being pressed in subsequent calls
// to this function. This simulates the user repeatedly
// pressing and releasing the key.
//
uint8_t get_key_rpt(uint8_t key_mask)
{
	cli();			// read and clear atomic !
	key_mask &= key_rpt;	// read key(s)
	key_rpt ^= key_mask;	// clear key(s)
	sei();
	return key_mask;
}

///////////////////////////////////////////////////////////////////
//
// check if a key is pressed right now
//
uint8_t get_key_state(uint8_t key_mask)
{
	key_mask &= key_state;
	return key_mask;
}

///////////////////////////////////////////////////////////////////
//
uint8_t get_key_short(uint8_t key_mask)
{
	cli();			// read key state and key press atomic !
	return get_key_press(~key_state & key_mask);
}

///////////////////////////////////////////////////////////////////
//
uint8_t get_key_long(uint8_t key_mask)
{
	return get_key_press(get_key_rpt(key_mask));
}

uint8_t get_key_long_r(uint8_t key_mask)
{				// if repeat function needed
	return get_key_press(get_key_rpt(key_press & key_mask));
}

uint8_t get_key_rpt_l(uint8_t key_mask)
{				// if long function needed
	return get_key_rpt(~key_press & key_mask);
}

uint8_t get_key_common(uint8_t key_mask)
{
	return get_key_press((key_press & key_mask) == key_mask ? key_mask : 0);
}

uint8_t get_key_common_l(uint8_t key_mask)
{
	return get_key_state((key_press & key_mask) == key_mask ? key_mask : 0);
}

#ifdef USE_WATCHDOG
void watchdog_off(void)
{
	wdt_reset();
	MCUSR &= ~_BV(WDRF);	// clear WDRF, as it overrides WDE-bit in WDTCSR-reg and leads to endless reset-loop (15ms timeout after wd-reset)
	wdt_disable();
}

void watchdog_on(void)
{
	wdt_reset();
	MCUSR &= ~_BV(WDRF);	// clear WDRF, as it overrides WDE-bit in WDTCSR-reg and leads to endless reset-loop (15ms timeout after wd-reset)
	wdt_enable(WDTO_120MS);
}

void watchdog_off_early(void)
{
	wdt_reset();
	_mcusr = MCUSR;
	MCUSR &= ~_BV(WDRF);	// clear WDRF, as it overrides WDE-bit in WDTCSR-reg and leads to endless reset-loop (15ms timeout after wd-reset)
	wdt_disable();
}

void test_F_CPU_with_watchdog(void)
{
/*
 * Hopefully cause a watchdog reset if the CKDIV8 FUSE is set (F_CPU 1MHz instead of 8MHz)
 *
 */
	wdt_reset();
	MCUSR &= ~_BV(WDRF);	// clear WDRF, as it overrides WDE-bit in WDTCSR-reg and leads to endless reset-loop (15ms timeout after wd-reset)
	wdt_enable(WDTO_120MS);
	delay(40);		// IF "CKDIV8" fuse is erroneously set, this should delay by 8x40 = 320ms & cause the dog to bite!

	watchdog_off();		// IF we got to here, F_CPU is OK.
}
#endif

void fb_update()
{
	if (!framebuffer.changed)
		return;

	uint8_t _sreg = SREG;	/* save SREG */
	cli();			/* disable all interrupts to avoid half-updated screens */

	for (uint8_t digit = 0; digit < 3; digit++) {
		display_char(digit, framebuffer.digit[digit], framebuffer.dot[digit]);
	}
	framebuffer.changed = 0;

	SREG = _sreg;
}
