# Issues

[ ] -> open
[X] -> found

[X] 1. U1 (3V3 buck) was ordered with JLC PN C61063 (XLSEMI XL1509-5.0E1), which is 5V output, and it should've been C74193 (XLSEMI XL1509-3.3E1). Regardless, it outputs ~4.38V out with ~5.1V in. MCU can be flashed and all aber das ist nicht richtig.

[X] 2. U2 and L2 were removed bc that caused L2 to overheat, something might be wrong with gate drivers (chips or bootstrap). No power for now.

[X] 3. NTC reads 0V, so either pin is bad/misconfigured, or C31 is shorted or something (~0Ohm between TP13 and GND)

[ ] 4. Some boards have the inductor rotated 90deg!! That would make the boost not work at all

[ ] 5. Vbus sag with pulsed current in a coil around the board. I don't think it's a good idea to have inductors wound in the same axis as the coil we're trying to control, but is there any alternative?


---

Finding: my dumbass didn't configure short circuit rules and there's a group of vias to GND short circuiting the following nets to GND:

- SWA
- SWB
- Gate AH/BH (before gate resistor)
- Vboot_A/B (top side of bootstrap capacitor)
- tempMeas


These are all in the bottom inner layer so it can't be seen on the PCB itself. Fix for now could be a nasty cut traces+magnet wire to destination but that'll require some proper tools... But maybe I'll skip that and probably order again when firmware is ~80% there.

So that explains issues #2 and #3: L2 was overheating bc it's rated at 100mA, and when the boost is disabled, the inductor is getting the 3.3V (well, 4.38V) to the gate driver supply, and since Vboot is shorted to GND, it goes trough R16/R17 to D6/D7 to GND... That's also why the power output (SWA/SWB) was shorted to ground, initially I thought it was some sort of gate driver protection magically turning on the low side switch but no lol.

I think I might have added those two via groups to lower switching loop inductance just before producing outputs and didn't run DRC... That was not a very expensive mistake but a very stupid one for sure. We can use rev1 for improving some other minor things found only after ordering.

---

# Changes to be made
[ ] -> Pending
[S] -> Schematic only
[P] -> Partially done
[X] -> Complete

- [X] 1. Remove violating via groups
- [X] 2. Swap LCSC PN for U1 (XLSEMI XL1509-3.3E1) -> see change #23
- [X] 3. Review LED resistor values: by lowering VCC to the correct 3.3V all LEDs should have a more reasonable brightness, increased to 220R anyways.
- [X] 4. Add external fixed voltage reference, maybe even replacing iRef divider? -> adjusting acq window seems to fix current. Pending tests with real current, but i think it's not needed now that we have a LDO (change #23)
- [X] 5. Add EEPROM?: I don't think we'd need it, and if we really needed it there's a ch32 eeprom emulation using flash, but will give it some thought. So far I don't think we need it, we have plenty of spare flash still with firmware close to 80% of the features, so crossing this one off.
- [X] 6. Review RST button behavior, or think of a way to hw reset the MCU, without firmware -> not necessary, but if needed current design could be made work like that if we flashed with a debugger and not with the bootloader
- [X] 7. Disconnect U2 (MT3608) output without EN pin, PMOS maybe -> not needed, drivers have UVLO at 5.5V
- [X] 8. Remove JLCJLCJLCJLC
- [S] 9. Move power supply section a bit down and leave more clearance with board edge
- [S] 10. Move debug/UART connectors and RST button, also turn them DNP
- [X] 11. Improve bootstrap layout? Works just fine even with the ugly rework, not needed.
- [P] 12. Replace expensive capacitors with something cheaper available as basic JLCPCBA: So far replaced all 22uF with 10uF. Pending proper review with BOM. removed yet another bom line!
- [X] 13. Find cuter output connector? Maybe even SMD so back side stays clean -> found a cute but expensive wago. worth it imo
- [X] 14. Review Rgate, since at 12V V_GS, I_G_pk = 12V/10R = 1.2A which is bigger than XJNG2103 1A -> increased to 15R
- [X] 15. Try to reduce electrolytic cap height? If we have XY space we can have half capacitance, half height but x2 parts -> reduced to 5.6mm, same as connector, without decreasing C or increasing diameter. Doesnt make sense to go lower unless connector goes lower, which is not happening
- [X] 16. R16/R17 should be a bit bigger if they need to be hand replaced -> part of bootstrap, same thing, it works
- [S] 17. Testpoints for gates!!! Even if they're difficult to place
- [X] 18. Add revision number in sch and pcb
- [X] 19. Automatic bootloader entry with a MOSFET or similar instead of button (or parallel to button) -> meh not worth it. The flashing experience through bootloader is not that bad. TI is 200x worse even with a fancy jtag probe
- [S] 20. Add potentiometer for setpoint percentage of power
- [X] 21. Add sillkscreen for board ID
- [P] 22. Fix for issue #4 -> added FT Rotation Offset field and set to 0, but investigation is pending
- [S] 23. Fix for issue #5: Replaced 3V3 buck with LDO which removes change #2. I was scared of the inductor saturating and maybe doing something funny with the analog rail. Playing safe even though it clearly works somehow in the current version.

---

# To be verified w current rev before ordering:

[ ] -> Pending
[P] -> Partially done
[X] -> Complete

- [X] 1. Current measurement: At first it looked fine but I noticed it's a little bit asymmetrical with positive and negative setpoints. not a problem, since the important thing is that the controller is fast, not accurate. But maybe can be fine tuned. I think I'm closing it since it doesnt make much of a difference to have 200mA error when the position controller is jerking the current setpoint between -5A and 5A
- [X] 2. Magnetic field measurement: idk lol. It seems proportional to current without magnet (good!), and sort of checks out with what's expected of magnet. Other than that, lol idk. I guess it's fine. the control loop is more important than the accuracy of this sensor
- [X] 3. USB PD + Voltage measurement: ahh I'll have to be smart about how to test this, since my laptop's usb probably can't do very much. we have LEDs tho, that can be useful -> Firmware was able to request 20V from my laptop charger, I think we're good. but having debug + USBPD would be goated. My idea is to have the current comms stack routed to uart and using it instead of the usb port.
- [X] 4. Temperature measurement: cut traces+jump for avoiding issue #3, then thermal cam to verify I guess. So far the measurement seems reasonable with reality, but to be checked against a better measurement. I used a hairdryer to warm the board and i think it's accurate, my cooking skills allow me to distinguish temperatures above and below 60degC so i believe it's fine. its not that critical, even 10deg error would probably be ok. but i think we're at 2deg error, guesstimating. coil gets quite hot too, could we even measure that?
- [P] 5. Gate driver: cut traces+jump (nasty, 6 nets), then program PWM open loop and use scope, verify bootstrap against SPICE -> after i reworked the board i didnt give a fuck and tested power directly without checking gate driver, i have no waveforms but the board is able to control current pretty well. 5W no problem! next up is 100W from USB PD.
- [P] 6. Power: make em MOSFETs warm. Yeah so 5W so far. More to come after USB PD+comms
