# Exercise 1 – Dimmer with a rotary encoder    
  
Implement a program for switching LEDs on/off and dimming them. The program should work as follows:  
• Rot_Sw, the push button on the rotary encoder shaft is the on/off button. When button is pressed  
the state of LEDs is toggled. Program must require the button to be released before the LEDs toggle  
again. Holding the button may not cause LEDs to toggle multiple times.  
• Rotary encoder is used to control brightness of the LEDs. Turning the knob clockwise increases  
brightness and turning counter-clockwise reduces brightness. If LEDs are in OFF state turning the  
knob has no effect.  
• When LED state is toggled to ON the program must use same brightness of the LEDs they were at  
when they were switched off. If LEDs are ON and dimmed to 0% then pressing the button will set  
50% brightness immediately.  
• PWM frequency divider must be configured to output 1 MHz frequency and PWM frequency must  
be 1 kHz.  
You must use GPIO interrupts for detecting the encoder turns. You may not have any application logic in  
the ISR all application logic (switching led on/off, brightness control) must be in your main program.  
