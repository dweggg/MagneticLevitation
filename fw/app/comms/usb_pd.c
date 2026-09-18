#include "ch32fun.h"

/*
 * The project already uses CH32Fun's USB PD implementation. Defining this
 * before including usbpd.h makes this translation unit provide the library's
 * implementation; other files only see the declarations from usbpd.h.
 */
#define USBPD_IMPLEMENTATION
#include "usbpd.h"

#include "usb_pd.h"

static uint8_t usb_pd_available_power_w;
static int usb_pd_has_selected_power;

/**
 * Calculate the maximum power represented by one source PDO.
 *
 * Fixed and variable PDOs describe voltage and current directly. PPS
 * describes a maximum voltage and current, so the maximum advertised power
 * is calculated from those two limits. Battery PDOs and other augmented PDOs
 * are ignored because USBPD_SelectPDO() in the bundled library does not build
 * their corresponding request objects.
 *
 * Returns the maximum power in milliwatts, or zero when the PDO is not
 * supported by this wrapper.
 */
static uint32_t usb_pd_get_pdo_power_mw(const USBPD_SourcePDO_t *pdo)
{
	uint32_t voltage_mv;
	uint32_t current_ma;

	switch (pdo->Header.PDOType)
	{
		case eUSBPD_PDO_FIXED:
			voltage_mv = pdo->FixedSupply.VoltageIn50mV * 50u;
			current_ma = pdo->FixedSupply.MaxCurrentIn10mA * 10u;
			break;

		case eUSBPD_PDO_VARIABLE:
			voltage_mv = pdo->VariableSupply.MaxVoltageIn50mV * 50u;
			current_ma = pdo->VariableSupply.MaxCurrentIn10mA * 10u;
			break;

		case eUSBPD_PDO_AUGMENTED:
			if (!USBPD_IsPPS(pdo))
			{
				return 0;
			}

			voltage_mv = pdo->SPR_PPS.MaxVoltageIn100mV * 100u;
			current_ma = pdo->SPR_PPS.MaxCurrentIn50mA * 50u;
			break;

		case eUSBPD_PDO_BATTERY:
		default:
			return 0;
	}

	return (voltage_mv * current_ma) / 1000u;
}

/**
 * Get the voltage used to compare two supported PDOs with equal power.
 *
 * Higher voltage wins when two PDOs advertise the same power. For PPS this
 * is the maximum programmable voltage; for the other supported types it is
 * the fixed or maximum voltage.
 */
static uint32_t usb_pd_get_pdo_voltage_mv(const USBPD_SourcePDO_t *pdo)
{
	if (USBPD_IsPPS(pdo))
	{
		return pdo->SPR_PPS.MaxVoltageIn100mV * 100u;
	}

	if (pdo->Header.PDOType == eUSBPD_PDO_VARIABLE)
	{
		return pdo->VariableSupply.MaxVoltageIn50mV * 50u;
	}

	return pdo->FixedSupply.VoltageIn50mV * 50u;
}

/**
 * Select the highest-power PDO advertised by the source.
 *
 * The wrapped library supports requests for fixed, variable and SPR PPS PDOs
 * through USBPD_SelectPDO(). Battery, EPR AVS and SPR AVS PDOs are skipped
 * because that function does not construct their required RDO format.
 *
 * Returns non-zero when a request was successfully queued.
 */
static int usb_pd_select_max_power_pdo(void)
{
	USBPD_SPR_CapabilitiesMessage_t *capabilities;
	const size_t count = USBPD_GetCapabilities(&capabilities);
	uint8_t best_index = 0;
	uint32_t best_power_mw = 0;
	uint32_t best_voltage_mv = 0;
	int found = 0;

	for (size_t i = 0; i < count; ++i)
	{
		const USBPD_SourcePDO_t *pdo = &capabilities->Source[i];
		const uint32_t power_mw = usb_pd_get_pdo_power_mw(pdo);
		const uint32_t voltage_mv = usb_pd_get_pdo_voltage_mv(pdo);

		if (power_mw == 0)
		{
			continue;
		}

		if (!found || power_mw > best_power_mw ||
			(power_mw == best_power_mw && voltage_mv > best_voltage_mv))
		{
			found = 1;
			best_index = (uint8_t)i;
			best_power_mw = power_mw;
			best_voltage_mv = voltage_mv;
		}
	}

	if (!found)
	{
		return 0;
	}

	const USBPD_SourcePDO_t *best_pdo = &capabilities->Source[best_index];
	const uint32_t pps_voltage_100mv = USBPD_IsPPS(best_pdo)
		? best_pdo->SPR_PPS.MaxVoltageIn100mV
		: 0u;

	if (USBPD_SelectPDO(best_index, pps_voltage_100mv) != eUSBPD_OK)
	{
		return 0;
	}

	/* USB PD power is specified in watts; truncate any fractional watt. */
	usb_pd_available_power_w = (uint8_t)(best_power_mw / 1000u);
	usb_pd_has_selected_power = 1;

	return 1;
}

void init_pins_usb_pd(void)
{
	/*
	 * The name is kept for consistency with the other application modules.
	 * CH32Fun performs the actual USB PD peripheral and CC setup here.
	 */
	USBPD_Init(FUNCONF_USE_5V_VDD ? eUSBPD_VCC_5V0 : eUSBPD_VCC_3V3);

	usb_pd_available_power_w = 0;
	usb_pd_has_selected_power = 0;
}

void task_usb_pd(void)
{
	const USBPD_Result_e result = USBPD_SinkNegotiate();

	if (result == eUSBPD_BUSY)
	{
		return;
	}

	if (result != eUSBPD_OK)
	{
		/*
		 * A failed negotiation can leave the library waiting for a response.
		 * Reset it so the next task invocation can start a fresh negotiation.
		 */
		USBPD_Reset();
		usb_pd_available_power_w = 0;
		usb_pd_has_selected_power = 0;
		return;
	}

	if (!usb_pd_has_selected_power)
	{
		usb_pd_select_max_power_pdo();
	}
}

int usb_pd_negotiating(void)
{
	return USBPD_GetState() != eSTATE_PS_RDY || !usb_pd_has_selected_power;
}

uint8_t usb_pd_get_available_power_w(void)
{
	return usb_pd_available_power_w;
}
