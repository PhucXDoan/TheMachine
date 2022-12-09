// Refer to:
// - "4x4 Matrix Membrane Keypad #27899"

#define KEYPAD_PRESS_THRESHOLD  256
#define KEYPAD_DIM              4
#define KEYPAD_HOLD_DURATION_MS 1000
static const u8 KEYPAD_PINS[2 * KEYPAD_DIM] PROGMEM = { 22, 23, 24, 25, 26, 27, 28, 29 }; // First half of the values refer to the columns (right-to-left) of the keypad; the other half are the rows (bottom-up).

static void
init_keypad(void)
{
	for (u8 i = 0; i < KEYPAD_DIM; i += 1) // Keypad column pins are set to pull-ups.
	{
		set_pin(pgm_read_byte(&KEYPAD_PINS[i]), PinState_input_pullup);
	}

	for (u8 i = KEYPAD_DIM; i < countof(KEYPAD_PINS); i += 1) // Keypad row pins are driven high.
	{
		set_pin(pgm_read_byte(&KEYPAD_PINS[i]), PinState_output_high);
	}
}

static u16 // The least significant bit is the top-right button ("A"), the most significant bit is the bottom-left button ("*"), and the rest of the buttons are traversed column-major.
read_keypad(void)
{
	u16 keypad = 0;

	for (u8 i = 0; i < KEYPAD_DIM * KEYPAD_DIM; i += 1)
	{
		set_pin(pgm_read_byte(&KEYPAD_PINS[KEYPAD_DIM + (KEYPAD_DIM - 1 - i % KEYPAD_DIM)]), PinState_output_low);
		keypad |= (!read_pin(pgm_read_byte(&KEYPAD_PINS[i / KEYPAD_DIM]))) << i;
		set_pin(pgm_read_byte(&KEYPAD_PINS[KEYPAD_DIM + (KEYPAD_DIM - 1 - i % KEYPAD_DIM)]), PinState_output_high);
	}

	return keypad;
}

// Stalls until the user pressed some button on the keypad and then returns the index of the bit pressed according to `read_keypad`.
// To reduce the amount of accidental repeats, there is a slight threshold of time (`KEYPAD_PRESS_THRESHOLD` ticks) that the user needs to press the button for in order it to be considered an actual button press.
// Doesn't behave well when multiple buttons are pressed at the same time, but this case is ignored.
static i8
wait_for_keypad_button_press(void)
{
	u16 ticks       =  0;
	i8  index       = -1;
	u16 old_buttons = -1;
	u16 new_buttons;
	u16 pressed;

	while (ticks < KEYPAD_PRESS_THRESHOLD)
	{
		new_buttons  = read_keypad();
		old_buttons &= new_buttons; // Make note of the fact that some buttons might have been released.
		pressed      = new_buttons & (new_buttons ^ old_buttons);

		if (pressed)
		{
			if (index == __builtin_ctz(pressed))
			{
				ticks += 1;
			}
			else
			{
				ticks = 0;
			}

			index = __builtin_ctz(pressed);
		}
		else
		{
			ticks =  0;
			index = -1;
		}
	}

	return index;
}

static bool8
keypad_held(u32* held_time_ms, u8* tick)
{
	if (*tick == 0)
	{
		if (!read_keypad())
		{
			*held_time_ms = 0;
		}
		else if (*held_time_ms == 0)
		{
			*held_time_ms = get_ms();
		}
		else
		{
			return get_ms() - *held_time_ms >= KEYPAD_HOLD_DURATION_MS;
		}
	}
	*tick += 1;
	return false;
}

