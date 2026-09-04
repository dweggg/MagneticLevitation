# Issues

[ ] -> open
[X] -> found

[X] 1. U1 (3V3 buck) was ordered with JLC PN C61063 (XLSEMI XL1509-5.0E1), which is 5V output, and it should've been C74193 (XLSEMI XL1509-3.3E1). Regardless, it outputs ~4.38V out with ~5.1V in. MCU can be flashed and all aber das ist nicht richtig.

[X] 2. U2 and L2 were removed bc that caused L2 to overheat, something might be wrong with gate drivers (chips or bootstrap). No power for now.

[X] 3. NTC reads 0V, so either pin is bad/misconfigured, or C31 is shorted or something (~0Ohm between TP13 and GND)

[ ] 4. Some boards have the inductor rotated 90deg!! That would make the boost not work at all

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
- [S] 2. Swap LCSC PN for U1 (XLSEMI XL1509-3.3E1)
- [X] 3. Review LED resistor values: by lowering VCC to the correct 3.3V all LEDs should have a more reasonable brightness
- [ ] 4. Add external fixed voltage reference, maybe even replacing iRef divider? -> adjusting acq window seems to fix current. Pending tests with real current (verification #1)
- [P] 5. Add EEPROM?: I don't think we'd need it, and if we really needed it there's a ch32 eeprom emulation using flash, but will give it some thought
- [ ] 6. Review RST button behavior, or think of a way to hw reset the MCU, without firmware
- [ ] 7. Disconnect U2 (MT3608) output without EN pin, PMOS maybe
- [S] 8. Remove JLCJLCJLCJLC
- [ ] 9. Move power supply section a bit down and leave more clearance with board edge
- [S] 10. Move debug/UART connectors and RST button, also turn them DNP
- [ ] 11. Improve bootstrap layout?
- [P] 12. Replace expensive capacitors with something cheaper available as basic JLCPCBA: So far replaced all 22uF with 10uF. Pending proper review
- [ ] 13. Find cuter output connector? Maybe even SMD so back side stays clean
- [ ] 14. Review Rgate, since at 12V V_GS, I_G_pk = 12V/10R = 1.2A which is bigger than XJNG2103 1A
- [ ] 15. Try to reduce electrolytic cap height? If we have XY space we can have half capacitance, half height but x2 parts
- [ ] 16. R16/R17 should be a bit bigger if they need to be hand replaced
- [ ] 17. Testpoints for gates!!! Even if they're difficult to place
- [ ] 18. Add revision number in sch and pcb
- [ ] 19. Automatic bootloader entry with a MOSFET or similar instead of button (or parallel to button)
- [ ] 20. Add potentiometer for setpoint percentage of power

---

# To be verified w current rev before ordering:

This version already flashes with bootloader and USB communication works. 

- [ ] 1. Current measurement: solder wires around shunt, CC'd supply + DMM for 'real', compare w firmware (analytical conversion first)
- [ ] 2. Magnetic field measurement: idk lol
- [ ] 3. USB PD + Voltage measurement: ahh I'll have to be smart about how to test this, since my laptop's usb probably can't do very much. we have LEDs tho, that can be useful
- [ ] 4. Temperature measurement: cut traces+jump for avoiding issue #3, then thermal cam to verify I guess
- [ ] 5. Gate driver: cut traces+jump (nasty, 6 nets), then program PWM open loop and use scope, verify bootstrap against SPICE
- [ ] 6. Power: make em MOSFETs warm
