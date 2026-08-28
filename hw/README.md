# Issues

1. U1 (3V3 buck) was ordered with JLC PN C61063, which is 5V output. Regardless, it outputs ~4.38V out with ~5.1V in. MCU can be flashed and all but not optimal

2. U2 and L2 were removed bc that caused L2 to overheat, something might be wrong with gate drivers (chips or bootstrap). No power for now.

3. NTC reads 0V, so either pin is bad/misconfigured, or C31 is shorted or something (~0Ohm between TP13 and GND)
