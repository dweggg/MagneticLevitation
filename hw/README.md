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