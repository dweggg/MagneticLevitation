# Issues

1. U1 (3V3 buck) was ordered with JLC PN C61063 (XLSEMI XL1509-5.0E1), which is 5V output, and it should've been C74193 (XLSEMI XL1509-3.3E1). Regardless, it outputs ~4.38V out with ~5.1V in. MCU can be flashed and all aber das ist nicht richtig.

2. U2 and L2 were removed bc that caused L2 to overheat, something might be wrong with gate drivers (chips or bootstrap). No power for now.

3. NTC reads 0V, so either pin is bad/misconfigured, or C31 is shorted or something (~0Ohm between TP13 and GND)


---

Finding: my dumbass didn't configure short circuit rules and there's a group of vias to GND short circuiting the following nets to GND:

- SWA
- SWB
- Gate AH/BH (before gate resistor)
- Vboot_A/B (top side of bootstrap capacitor)
- tempMeas


These are all in the bottom inner layer so it can't be seen on the PCB itself. Fix for now could be a nasty cut traces+magnet wire to destination but that'll require some proper tools... But maybe I'll skip that and probably order again when firmware is ~80% there.

So that explains issues #2 and #3: L2 was overheating bc it's rated at 100mA, and when the boost is disabled, the inductor is getting the 3.3V (well, 4.3V) to the gate driver supply, and since Vboot is shorted to GND, it goes trough R16/R17 to D6/D7 to GND... That's also why the power output (SWA/SWB) was shorted to ground, initially I thought it was some sort of gate driver protection magically turning on the low side switch but no lol.

I think I might have added those two via groups to lower switching loop inductance just before producing outputs and didn't run DRC... That was not a very expensive mistake but a very stupid one for sure.

---

# Changes to be made
[ ] -> Pending
[S] -> Schematic only
[P] -> Partially done
[X] -> Complete

- [ ] 1. Remove violating via groups
- [S] 2. Swap LCSC PN for U1 (XLSEMI XL1509-3.3E1)
- [P] 3. Review LED resistor values
- [ ] 4. Add external fixed voltage reference, maybe even replacing iRef divider
- [ ] 5. Add EEPROM?
- [ ] 6. Review RST button behavior, or think of a way to HW reset the MCU
- [ ] 7. Disconnect U2 (MT3608) output without EN pin, PMOS maybe
- [S] 8. Remove JLCJLCJLCJLC
- [ ] 9. Move power supply section a bit down and leave more clearance with board edge
- [ ] 10. Move debug/UART connectors and RST button, also turn them DNP
- [ ] 11. Improve bootstrap layout?
- [P] 12. Replace expensive capacitors with something cheaper available as basic JLCPCBA
- [ ] 13. Find cuter output connector? Maybe even SMD so back side stays clean
- [ ] 14. Review Rgate, since at 12V V_GS, I_G_pk = 12V/10R = 1.2A which is bigger than XJNG2103 1A
- [ ] 15. Try to reduce electrolytic cap height? If we have XY space we can have half capacitance, half height but x2 parts
- [ ] 16. R16/R17 should be a bit bigger if they need to be hand-replaced
- [ ] 17. Testpoints for gates!!! Even if they're difficult to place
- [ ] 18. Add revision number in sch and pcb

---

# To be verified w current rev before ordering:

This version already flashes with bootloader and USB communication works. 

- [ ] 1. Current measurement: solder wires around shunt, CC'd supply + DMM for 'real', compare w firmware (analytical conversion first)
- [ ] 2. Magnetic field measurement: idk lol
- [ ] 3. USB PD + Voltage measurement: ahh I'll have to be smart about how to test this, since my laptop's usb probably can't do very much. we have LEDs tho, that can be useful
- [ ] 4. Temperature measurement: cut traces+jump for avoiding issue #3, then thermal cam to verify I guess
- [ ] 5. Gate driver: cut traces+jump (nasty, 6 nets), then program PWM open loop and use scope, verify bootstrap against SPICE
- [ ] 6. Power: make em MOSFETs warm
