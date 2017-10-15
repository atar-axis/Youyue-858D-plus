/* 
	Editor: http://www.visualmicro.com
			visual micro and the arduino ide ignore this code during compilation. this code is automatically maintained by visualmicro, manual changes to this file will be overwritten
			the contents of the Visual Micro sketch sub folder can be deleted prior to publishing a project
			all non-arduino files created by visual micro and all visual studio project or solution files can be freely deleted and are not required to compile a sketch (do not delete your own code!).
			note: debugger breakpoints are stored in '.sln' or '.asln' files, knowledge of last uploaded breakpoints is stored in the upload.vmps.xml file. Both files are required to continue a previous debug session without needing to compile and upload again
	
	Hardware: LilyPad Arduino w/ ATmega328P, Platform=avr, Package=arduino
*/

#define __AVR_ATmega328p__
#define __AVR_ATmega328P__
#define ARDUINO 10608
#define ARDUINO_MAIN
#define F_CPU 8000000L
#define __AVR__
#define F_CPU 8000000L
#define ARDUINO 10608
#define ARDUINO_AVR_LILYPAD
#define ARDUINO_ARCH_AVR
int main(void);
void setup_858D(void);
void clear_display(void);
void change_config_parameter(CPARAM * param, const char *string);
void eep_save(CPARAM * param);
void eep_load(CPARAM * param);
void restore_default_conf(void);
void set_dot(void);
void clear_dot(void);
void set_eeprom_saved_dot(void);
void clear_eeprom_saved_dot(void);
void display_number(int16_t number);
void display_char(uint8_t digit, uint8_t character, uint8_t dot);
void display_string(const char *string);
void display_string_running(const char* string);
void rol_string(char* text);
void fan_test(void);
void fan_test(void);
void show_firmware_version(void);
void setup_timer1_ctc(void);
uint8_t get_key_press(uint8_t key_mask);
uint8_t get_key_rpt(uint8_t key_mask);
uint8_t get_key_state(uint8_t key_mask);
uint8_t get_key_short(uint8_t key_mask);
uint8_t get_key_long(uint8_t key_mask);
uint8_t get_key_long_r(uint8_t key_mask);
uint8_t get_key_rpt_l(uint8_t key_mask);
uint8_t get_key_common(uint8_t key_mask);
uint8_t get_key_common_l(uint8_t key_mask);
void watchdog_off(void);
void watchdog_on(void);
void watchdog_off_early(void);
void test_F_CPU_with_watchdog(void);
void fb_update();

#include "pins_arduino.h" 
#include "Arduino.h"
#include "youyue858d.ino"
