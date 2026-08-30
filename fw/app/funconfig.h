#ifndef FUNCONFIG_H
#define FUNCONFIG_H

#define CH32X035
#define FUNCONF_SYSTICK_USE_HCLK 1      // Should systick be at 48 MHz (1) or 6MHz (0) on an '003.  Typically set to 0 to divide HCLK by 8.
#define FUNCONF_ENABLE_HPE 1            // Enable hardware interrupt stack.  Very good on QingKeV4, i.e. x035, v10x, v20x, v30x, but questionable on 003. 
                                        // If you are using that, consider using INTERRUPT_DECORATOR as an attribute to your interrupt handlers.
#define FUNCONF_USE_5V_VDD 1
#define FUNCONF_USE_USBPRINTF 0

#endif // FUNCONFIG_H