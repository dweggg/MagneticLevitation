import numpy as np
import matplotlib.pyplot as plt

# ---- edit these ----
R0     = 10_000      # NTC resistance at T0 [ohm]
T0_C   = 25           # reference temp [C]
BETA   = 3450          # beta coefficient [K]
R_FIX  = 5.1e3      # other resistor in divider [ohm]
ADC_BITS = 12          # ADC resolution
NTC_ON_TOP = True      # True: NTC between Vref and node, R_FIX to GND
                       # False: R_FIX between Vref and node, NTC to GND

T_C = np.linspace(-40, 150, 500)   # temperature sweep [C]
# ---------------------

T0 = T0_C + 273.15
T  = T_C + 273.15

R_ntc = R0 * np.exp(BETA * (1/T - 1/T0))

if NTC_ON_TOP:
    ratio = R_FIX / (R_ntc + R_FIX)      # Vout/Vref
else:
    ratio = R_ntc / (R_ntc + R_FIX)

bits = ratio * (2**ADC_BITS - 1)

plt.figure(figsize=(8, 5))
plt.plot(T_C, bits)
plt.xlabel("Temperature [°C]")
plt.ylabel(f"ADC reading [{ADC_BITS}-bit]")
plt.title("NTC voltage divider -> ADC counts")
plt.grid(True)
plt.tight_layout()
plt.show()