// Refer to:
// - "Arduino Mega Pinput Diagram"
// - "ATmega2560 Datasheet" (pg. 68, 96)

#define BUILTIN_LED_PIN             13
#define SPI_MASTER_IN_SLAVE_OUT_PIN 50
#define SPI_MASTER_OUT_SLAVE_IN_PIN 51
#define SPI_SERIAL_CLOCK_PIN        52
#define SPI_SLAVE_SELECT_PIN        53
#define PIN_DEFS(X_MACRO) \
	X_MACRO( 0, E, 0) X_MACRO( 1, E, 1) X_MACRO( 2, E, 4) X_MACRO( 3, E, 5) X_MACRO( 4, G, 5) X_MACRO( 5, E, 3) \
	X_MACRO( 6, H, 3) X_MACRO( 7, H, 4) X_MACRO( 8, H, 5) X_MACRO( 9, H, 6) X_MACRO(10, B, 4) X_MACRO(11, B, 5) \
	X_MACRO(12, B, 6) X_MACRO(13, B, 7) X_MACRO(14, J, 1) X_MACRO(15, J, 0) X_MACRO(16, H, 1) X_MACRO(17, H, 0) \
	X_MACRO(18, D, 3) X_MACRO(19, D, 2) X_MACRO(20, D, 1) X_MACRO(21, D, 0) X_MACRO(22, A, 0) X_MACRO(23, A, 1) \
	X_MACRO(24, A, 2) X_MACRO(25, A, 3) X_MACRO(26, A, 4) X_MACRO(27, A, 5) X_MACRO(28, A, 6) X_MACRO(29, A, 7) \
	X_MACRO(30, C, 7) X_MACRO(31, C, 6) X_MACRO(32, C, 5) X_MACRO(33, C, 4) X_MACRO(34, C, 3) X_MACRO(35, C, 2) \
	X_MACRO(36, C, 1) X_MACRO(37, C, 0) X_MACRO(38, D, 7) X_MACRO(39, G, 2) X_MACRO(40, G, 1) X_MACRO(41, G, 0) \
	X_MACRO(42, L, 7) X_MACRO(43, L, 6) X_MACRO(44, L, 5) X_MACRO(45, L, 4) X_MACRO(46, L, 3) X_MACRO(47, L, 2) \
	X_MACRO(48, L, 1) X_MACRO(49, L, 0) X_MACRO(50, B, 3) X_MACRO(51, B, 2) X_MACRO(52, B, 1) X_MACRO(53, B, 0)

enum PinState // `PinState_output_low` and `PinState_output_high` purposely have a one-to-one correspondence to `false` and `true`.
{
	PinState_output_low,
	PinState_output_high,
	PinState_input_pullup,
	PinState_none
};

static void
set_pin(u8 pin_index, enum PinState status)
{
	// DDR | PORT | STATE
	// 0   | 0    | Default
	// 0   | 1    | Pull-up resistor is activated
	// 1   | 0    | Pin is driven low
	// 1   | 1    | Pin is driven high

	switch (pin_index) // Sets the data direction (i.e. input/output).
	{
		#define CASE(PIN_NUMBER, LETTER, INDEX) \
			case PIN_NUMBER: \
			{ \
				DDR##LETTER ^= (DDR##LETTER & (1 << DD##LETTER##INDEX)) ^ (((status == PinState_output_low) | (status == PinState_output_high)) << DD##LETTER##INDEX); \
			} break;
		PIN_DEFS(CASE);
		#undef CASE
	}

	switch (pin_index) // Sets the port value.
	{
		#define CASE(PIN_NUMBER, LETTER, INDEX) \
			case PIN_NUMBER: \
			{ \
				PORT##LETTER ^= (PORT##LETTER & (1 << PORT##LETTER##INDEX)) ^ (((status == PinState_input_pullup) | (status == PinState_output_high)) << PORT##LETTER##INDEX); \
			} break;
		PIN_DEFS(CASE);
		#undef CASE
	}
}

static bool8
read_pin(u8 pin_index)
{
	switch (pin_index)
	{
		#define CASE(PIN_NUMBER, LETTER, INDEX) \
			case PIN_NUMBER: \
			{ \
				return (PIN##LETTER >> PIN##LETTER##INDEX) & 1; \
			} break;
		PIN_DEFS(CASE);
		#undef CASE

		default:
		{
			return false;
		} break;
	}
}

#undef PIN_DEFS
