#ifndef _INPUT_KEY_EVENTS_H
#define _INPUT_KEY_EVENTS_H

/**
 * @brief 除标准鼠标按键外使用HID Usage Tables定义的按键码
 * References:
 *  https://www.usb.org/sites/default/files/hut1_6.pdf
 */

// 鼠标按键
#define KEY_MOUSE_LEFT	 0x01
#define KEY_MOUSE_RIGHT	 0x02
#define KEY_MOUSE_MIDDLE 0x03

// General Desktop Page
// ...
#define KEY_GDP_SYSTEM_POWER_DOWN	0x81
#define KEY_GDP_SYSTEM_SLEEP		0x82
#define KEY_GDP_SYSTEM_WAKEUP		0x83
#define KEY_GDP_SYSTEM_CONTEXT_MENU 0x84
#define KEY_GDP_SYSTEM_MAIN_MENU	0x85
#define KEY_GDP_SYSTEM_APP_MENU		0x86
#define KEY_GDP_SYSTEM_MENU_HELP	0x87
#define KEY_GDP_SYSTEM_MENU_EXIT	0x88
#define KEY_GDP_SYSTEM_MENU_SELECT	0x89
// ...
#define KEY_GDP_SYSTEM_COLD_RESTART 0x8E
#define KEY_GDP_SYSTEM_WARM_RESTART 0x8F
// ...

// Keyboard/Keypad Page
#define KEY_A				   0x04
#define KEY_B				   0x05
#define KEY_C				   0x06
#define KEY_D				   0x07
#define KEY_E				   0x08
#define KEY_F				   0x09
#define KEY_G				   0x0A
#define KEY_H				   0x0B
#define KEY_I				   0x0C
#define KEY_J				   0x0D
#define KEY_K				   0x0E
#define KEY_L				   0x0F
#define KEY_M				   0x10
#define KEY_N				   0x11
#define KEY_O				   0x12
#define KEY_P				   0x13
#define KEY_Q				   0x14
#define KEY_R				   0x15
#define KEY_S				   0x16
#define KEY_T				   0x17
#define KEY_U				   0x18
#define KEY_V				   0x19
#define KEY_W				   0x1A
#define KEY_X				   0x1B
#define KEY_Y				   0x1C
#define KEY_Z				   0x1D
#define KEY_1				   0x1E
#define KEY_2				   0x1F
#define KEY_3				   0x20
#define KEY_4				   0x21
#define KEY_5				   0x22
#define KEY_6				   0x23
#define KEY_7				   0x24
#define KEY_8				   0x25
#define KEY_9				   0x26
#define KEY_0				   0x27
#define KEY_ENTER			   0x28
#define KEY_ESC				   0x29
#define KEY_BACKSPACE		   0x2A
#define KEY_TAB				   0x2B
#define KEY_SPACE			   0x2C
#define KEY_MINUS			   0x2D // '-'
#define KEY_EQUAL			   0x2E // '='
#define KEY_LEFTBRACE		   0x2F // '['
#define KEY_RIGHTBRACE		   0x30 // ']'
#define KEY_BACKSLASH		   0x31 // '\'
#define KEY_SEMICOLON		   0x33 // ';'
#define KEY_APOSTROPHE		   0x34 // '\''
#define KEY_GRAVE			   0x35 // '`'
#define KEY_COMMA			   0x36 // ','
#define KEY_DOT				   0x37 // '.'
#define KEY_SLASH			   0x38 // '/'
#define KEY_CAPSLOCK		   0x39
#define KEY_F1				   0x3A
#define KEY_F2				   0x3B
#define KEY_F3				   0x3C
#define KEY_F4				   0x3D
#define KEY_F5				   0x3E
#define KEY_F6				   0x3F
#define KEY_F7				   0x40
#define KEY_F8				   0x41
#define KEY_F9				   0x42
#define KEY_F10				   0x43
#define KEY_F11				   0x44
#define KEY_F12				   0x45
#define KEY_PRINTSCREEN		   0x46
#define KEY_SCROLLLOCK		   0x47
#define KEY_PAUSE			   0x48
#define KEY_INSERT			   0x49
#define KEY_HOME			   0x4A
#define KEY_PAGEUP			   0x4B
#define KEY_DELETE			   0x4C
#define KEY_END				   0x4D
#define KEY_PAGEDOWN		   0x4E
#define KEY_RIGHT			   0x4F
#define KEY_LEFT			   0x50
#define KEY_DOWN			   0x51
#define KEY_UP				   0x52
#define KEY_NUMLOCK			   0x53
#define KEY_KEYPAD_SLASH	   0x54 // '/'
#define KEY_KEYPAD_ASTERISK	   0x55 // '*'
#define KEY_KEYPAD_MINUS	   0x56 // '-'
#define KEY_KEYPAD_PLUS		   0x57 // '+'
#define KEY_KEYPAD_ENTER	   0x58
#define KEY_KEYPAD_1		   0x59
#define KEY_KEYPAD_2		   0x5A
#define KEY_KEYPAD_3		   0x5B
#define KEY_KEYPAD_4		   0x5C
#define KEY_KEYPAD_5		   0x5D
#define KEY_KEYPAD_6		   0x5E
#define KEY_KEYPAD_7		   0x5F
#define KEY_KEYPAD_8		   0x60
#define KEY_KEYPAD_9		   0x61
#define KEY_KEYPAD_0		   0x62
#define KEY_KEYPAD_DOT		   0x63
#define KEY_NON_US_64		   0x64 // "<>" or "\|"
#define KEY_APPLICATION		   0x65
#define KEY_POWER			   0x66
#define KEY_KEYPAD_EQUAL	   0x67 // '='
#define KEY_F13				   0x68
#define KEY_F14				   0x69
#define KEY_F15				   0x6A
#define KEY_F16				   0x6B
#define KEY_F17				   0x6C
#define KEY_F18				   0x6D
#define KEY_F19				   0x6E
#define KEY_F20				   0x6F
#define KEY_F21				   0x70
#define KEY_F22				   0x71
#define KEY_F23				   0x72
#define KEY_F24				   0x73
#define KEY_EXECUTE			   0x74
#define KEY_HELP			   0x75
#define KEY_MENU			   0x76
#define KEY_SELECT			   0x77
#define KEY_STOP			   0x78
#define KEY_AGAIN			   0x79
#define KEY_UNDO			   0x7A
#define KEY_CUT				   0x7B
#define KEY_COPY			   0x7C
#define KEY_PASTE			   0x7D
#define KEY_FIND			   0x7E
#define KEY_MUTE			   0x7F
#define KEY_VOLUMEUP		   0x80
#define KEY_VOLUMEDOWN		   0x81
#define KEY_LOCKING_CAPSLOCK   0x82
#define KEY_LOCKING_NUMLOCK	   0x83
#define KEY_LOCKING_SCROLLLOCK 0x84
#define KEY_KEYPAD_COMMA	   0x85
#define KEY_KEYPAD_EQUAL_SIGN  0x86
#define KEY_INTERNATIONAL1	   0x87
#define KEY_INTERNATIONAL2	   0x88
#define KEY_INTERNATIONAL3	   0x89
#define KEY_INTERNATIONAL4	   0x8A
#define KEY_INTERNATIONAL5	   0x8B
#define KEY_INTERNATIONAL6	   0x8C
#define KEY_INTERNATIONAL7	   0x8D
#define KEY_INTERNATIONAL8	   0x8E
#define KEY_INTERNATIONAL9	   0x8F
#define KEY_LANG1			   0x90 // "Hangul/English"
#define KEY_LANG2			   0x91 // "Hanja"
#define KEY_LANG3			   0x92 // "Katakana"
#define KEY_LANG4			   0x93 // "Hiragana"
#define KEY_LANG5			   0x94 // "Zenkaku/Hankaku"
#define KEY_LANG6			   0x95 // "Reserved"
#define KEY_LANG7			   0x96 // "Reserved"
#define KEY_LANG8			   0x97 // "Reserved"
#define KEY_LANG9			   0x98 // "Reserved"
#define KEY_ALTERASE		   0x99
#define KEY_SYSREQ			   0x9A
#define KEY_CANCEL			   0x9B
#define KEY_CLEAR			   0x9C
#define KEY_PRIOR			   0x9D
#define KEY_RETURN			   0x9E
#define KEY_SEPARATOR		   0x9F
#define KEY_OUT				   0xA0
#define KEY_OPER			   0xA1
#define KEY_CLEARAGAIN		   0xA2
#define KEY_CRSEL			   0xA3
#define KEY_EXSEL			   0xA4

#define KEY_KEYPAD_00				   0xB0
#define KEY_KEYPAD_000				   0xB1
#define KEY_KEYPAD_THOUSANDS_SEPARATOR 0xB2
#define KEY_KEYPAD_DECIMAL_SEPARATOR   0xB3
#define KEY_KEYPAD_CURRENCY_UNIT	   0xB4
#define KEY_KEYPAD_CURRENCY_SUBUNIT	   0xB5
#define KEY_KEYPAD_LEFT_PARENTHESIS	   0xB6 // '('
#define KEY_KEYPAD_RIGHT_PARENTHESIS   0xB7 // ')'
#define KEY_KEYPAD_LEFT_BRACE		   0xB8 // '{'
#define KEY_KEYPAD_RIGHT_BRACE		   0xB9 // '}'
#define KEY_KEYPAD_TAB				   0xBA
#define KEY_KEYPAD_BACKSPACE		   0xBB
#define KEY_KEYPAD_A				   0xBC
#define KEY_KEYPAD_B				   0xBD
#define KEY_KEYPAD_C				   0xBE
#define KEY_KEYPAD_D				   0xBF
#define KEY_KEYPAD_E				   0xC0
#define KEY_KEYPAD_F				   0xC1
#define KEY_KEYPAD_XOR				   0xC2 // '^'
#define KEY_KEYPAD_CARET			   0xC3 // '^'
#define KEY_KEYPAD_PERCENT			   0xC4 // '%'
#define KEY_KEYPAD_LESS_THAN		   0xC5 // '<'
#define KEY_KEYPAD_GREATER_THAN		   0xC6
#define KEY_KEYPAD_AMPERSAND		   0xC7 // '&'
#define KEY_KEYPAD_AMPERSAND_AMPERSAND 0xC8 // "&&"
#define KEY_KEYPAD_PIPE				   0xC9 // '|'
#define KEY_KEYPAD_PIPE_PIPE		   0xCA // "||"
#define KEY_KEYPAD_COLON			   0xCB // ':'
#define KEY_KEYPAD_HASH				   0xCC // '#'
#define KEY_KEYPAD_SPACE			   0xCD
#define KEY_KEYPAD_AT				   0xCE // '@'
#define KEY_KEYPAD_EXCLAMATION		   0xCF // '!'
#define KEY_KEYPAD_MEMORY_STORE		   0xD0
#define KEY_KEYPAD_MEMORY_RECALL	   0xD1
#define KEY_KEYPAD_MEMORY_CLEAR		   0xD2
#define KEY_KEYPAD_MEMORY_ADD		   0xD3
#define KEY_KEYPAD_MEMORY_SUBTRACT	   0xD4
#define KEY_KEYPAD_MEMORY_MULTIPLY	   0xD5
#define KEY_KEYPAD_MEMORY_DIVIDE	   0xD6
#define KEY_KEYPAD_PLUS_MINUS		   0xD7 // '±'
#define KEY_KEYPAD_CLEAR			   0xD8
#define KEY_KEYPAD_CLEAR_ENTRY		   0xD9
#define KEY_KEYPAD_BINARY			   0xDA
#define KEY_KEYPAD_OCTAL			   0xDB
#define KEY_KEYPAD_DECIMAL			   0xDC
#define KEY_KEYPAD_HEXADECIMAL		   0xDD

#define KEY_LEFTCTRL   0xE0
#define KEY_LEFTSHIFT  0xE1
#define KEY_LEFTALT	   0xE2
#define KEY_LEFTGUI	   0xE3
#define KEY_RIGHTCTRL  0xE4
#define KEY_RIGHTSHIFT 0xE5
#define KEY_RIGHTALT   0xE6
#define KEY_RIGHTGUI   0xE7

// Consumer Page (0x0C)
#define KEY_CP_CONSUMER_CONTROL					 0x01
#define KEY_CP_NUMERIC_KEYPAD					 0x02
#define KEY_CP_PROGRAMMABLE_BUTTONS				 0x03
#define KEY_CP_MICROPHONE						 0x04
#define KEY_CP_HEADPHONE						 0x05
#define KEY_CP_GRAPHICS_EQUALIZER				 0x06
#define KEY_CP_PLUS10							 0x20
#define KEY_CP_PLUS100							 0x21
#define KEY_CP_AM_PM							 0x22
#define KEY_CP_POWER							 0x30
#define KEY_CP_RESET							 0x31
#define KEY_CP_SLEEP							 0x32
#define KEY_CP_SLEEP_AFTER						 0x33
#define KEY_CP_SLEEP_MODE						 0x34
#define KEY_CP_ILLUMINATION						 0x35
#define KEY_CP_FUNCTION_BUTTONS					 0x36
#define KEY_CP_MENU								 0x40
#define KEY_CP_MENU_PICK						 0x41
#define KEY_CP_MENU_UP							 0x42
#define KEY_CP_MENU_DOWN						 0x43
#define KEY_CP_MENU_LEFT						 0x44
#define KEY_CP_MENU_RIGHT						 0x45
#define KEY_CP_MENU_ESCAPE						 0x46
#define KEY_CP_MENU_VALUE_INCREASE				 0x47
#define KEY_CP_MENU_VALUE_DECREASE				 0x48
#define KEY_CP_DATA_ON_SCREEN					 0x60
#define KEY_CP_CLOSED_CAPTION					 0x61
#define KEY_CP_CLOSED_CAPTION_SELECT			 0x62
#define KEY_CP_VCR_TV							 0x63
#define KEY_CP_BROADCAST_MODE					 0x64
#define KEY_CP_SNAPSHOT							 0x65
#define KEY_CP_STILL							 0x66
#define KEY_CP_PIC_IN_PIC_TOGGLE				 0x67
#define KEY_CP_PIC_IN_PIC_SWAP					 0x68
#define KEY_CP_RED_MENU							 0x69
#define KEY_CP_GREEN_MENU						 0x6A
#define KEY_CP_BLUE_MENU						 0x6B
#define KEY_CP_YELLOW_MENU						 0x6C
#define KEY_CP_ASPECT							 0x6D
#define KEY_CP_3D_MODE_SELECT					 0x6E
#define KEY_CP_DISPLAY_BRIGHTNESS_INCREMENT		 0x6F
#define KEY_CP_DISPLAY_BRIGHTNESS_DECREMENT		 0x70
#define KEY_CP_DISPLAY_BRIGHTNESS				 0x71
#define KEY_CP_DISPLAY_BACKLIGHT_TOGGLE			 0x72
#define KEY_CP_DISPLAY_SET_BRIGHTNESS_TO_MINIMUM 0x73
#define KEY_CP_DISPLAY_SET_BRIGHTNESS_TO_MAXIMUM 0x74
#define KEY_CP_DISPLAY_AUTOBRIGHTNESS_MODE		 0x75
// ...
#define KEY_CP_KEYBOARD_BRIGHTNESS_INCREMENT  0x7F
#define KEY_CP_KEYBOARD_BRIGHTNESS_DECREMENT  0x80
#define KEY_CP_KEYBOARD_BACKLIGHT_SET_LEVEL	  0x81
#define KEY_CP_KEYBOARD_BACKLIGHT_OOC		  0x82
#define KEY_CP_KEYBOARD_BACKLIGHT_SET_MINIMUM 0x83
#define KEY_CP_KEYBOARD_BACKLIGHT_SET_MAXIMUM 0x84
#define KEY_CP_KEYBOARD_BACKLIGHT_AUTO		  0x85
// ...
#define KEY_CP_PLAY					 0xB0
#define KEY_CP_PAUSE				 0xB1
#define KEY_CP_RECORD				 0xB2
#define KEY_CP_FAST_FORWARD			 0xB3
#define KEY_CP_REWIND				 0xB4
#define KEY_CP_NEXT_TRACK			 0xB5
#define KEY_CP_PREVIOUS_TRACK		 0xB6
#define KEY_CP_STOP					 0xB7
#define KEY_CP_EJECT				 0xB8
#define KEY_CP_RANDOM_PLAY			 0xB9
#define KEY_CP_SELECT_DISC			 0xBA
#define KEY_CP_ENTER_DISC			 0xBB
#define KEY_CP_REPEAT				 0xBC
#define KEY_CP_TRACKING				 0xBD
#define KEY_CP_TRACK_NORMAL			 0xBE
#define KEY_CP_SLOW_TRACKING		 0xBF
#define KEY_CP_FRAME_FORWARD		 0xC0
#define KEY_CP_FRAME_BACK			 0xC1
#define KEY_CP_MARK					 0xC2
#define KEY_CP_CLEAR_MARK			 0xC3
#define KEY_CP_REPEAT_FROM_MARK		 0xC4
#define KEY_CP_RETURN_TO_MARK		 0xC5
#define KEY_CP_SEARCH_MARK_FORWARD	 0xC6
#define KEY_CP_SEARCH_MARK_BACKWARDS 0xC7
#define KEY_CP_COUNTER_RESET		 0xC8
#define KEY_CP_SHOW_COUNTER			 0xC9
#define KEY_CP_TRACKING_INCREMENT	 0xCA
#define KEY_CP_TRACKING_DECREMENT	 0xCB
#define KEY_CP_STOP_EJECT			 0xCC
#define KEY_CP_PLAY_PAUSE			 0xCD
#define KEY_CP_PLAY_SKIP			 0xCE
#define KEY_CP_VOICE_COMMAND		 0xCF
// ...
#define KEY_CP_VOLUME			0xE0
#define KEY_CP_BALANCE			0xE1
#define KEY_CP_MUTE				0xE2
#define KEY_CP_BASS				0xE3
#define KEY_CP_TREBLE			0xE4
#define KEY_CP_BASS_BOOST		0xE5
#define KEY_CP_SURROUND_MODE	0xE6
#define KEY_CP_LOUDNESS			0xE7
#define KEY_CP_MPX				0xE8
#define KEY_CP_VOLUME_INCREMENT 0xE9
#define KEY_CP_VOLUME_DECREMENT 0xEA
// ...
#define KEY_CP_AL_CONSUMER_CONTROL_CONFIGURATION 0x183
// ...
#define KEY_CP_AL_EMAIL_READER 0x18A
// ...
#define KEY_CP_AL_CALC 0x192
// ...
#define KEY_CP_AL_INTERNET_BROWSER 0x196
// ...
#define KEY_CP_AL_FILE_BROWSER 0x1B4
// ...
#define KEY_CP_AL_RESEARCH_SEARCH_BROWSER 0x1C6
// ...
#define KEY_CP_AC_HOME			0x223
#define KEY_CP_AC_BACK			0x224
#define KEY_CP_AC_FORWARD		0x225
#define KEY_CP_AC_STOP			0x226
#define KEY_CP_AC_REFRESH		0x227
#define KEY_CP_AC_PREVIOUS_LINK 0x228
#define KEY_CP_AC_NEXT_LINK		0x229
#define KEY_CP_AC_BOOKMARKS		0x22A
// ...

#endif