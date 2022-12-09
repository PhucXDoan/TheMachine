// Refer to:
// - "HD44780U 2x16 LCD1602 Datasheet" ("(pg. N)" refers to page number `N` of this resource)

static const u8 LCD_DATA_BUS_PINS[4] = { 2, 3, 4, 5 }; // Data bus 4, 5, 6, and 7 respectively.
#define LCD_REGISTER_SELECT_PIN 6
#define LCD_ENABLING_PIN        7
#define LCD_DIMS_X              16
#define LCD_DIMS_Y              2

struct LCD
{
	u8 curr_display[LCD_DIMS_Y][LCD_DIMS_X];
	u8 backbuffer  [LCD_DIMS_Y][LCD_DIMS_X];
	u8 cursor_x;
	u8 cursor_y;
};

static void
_pulse_lcd_enabling_pin(void)
{
	set_pin(LCD_ENABLING_PIN, PinState_output_low);
	_delay_ms(0.001);                                 // Wait atleast 0.000025ms (i.e. 25ns) for the fall (pg. 49).
	set_pin(LCD_ENABLING_PIN, PinState_output_high);
	_delay_ms(0.001);                                 // Wait atleast 0.000025ms + 0.000450ms (i.e. 25ns + 450ns) for the rise and pulse width (pg. 49).
	set_pin(LCD_ENABLING_PIN, PinState_output_low);
	_delay_ms(0.001);                                 // Wait atleast 0.001000ms (i.e. 1000ns) for the whole pulse cycle (pg. 49).
}

static void
_lcd_send_raw_byte(u8 value)
{
	for (u8 i = 0; i < countof(LCD_DATA_BUS_PINS); i += 1) // High-nibble is transferred first (pg. 22).
	{
		set_pin(LCD_DATA_BUS_PINS[i], ((value >> 4) >> i) & 1);
	}
	_pulse_lcd_enabling_pin();

	for (u8 i = 0; i < countof(LCD_DATA_BUS_PINS); i += 1) // Low-nibble is transferred second (pg. 22).
	{
		set_pin(LCD_DATA_BUS_PINS[i], (value >> i) & 1);
	}
	_pulse_lcd_enabling_pin();
}

static struct LCD // Initialization process of the LCD in 4-bit mode (pg. 23, 46).
init_lcd(void)
{
	_delay_ms(50.0); // Wait more than 40ms for power.

	set_pin(LCD_REGISTER_SELECT_PIN, PinState_output_low);
	set_pin(LCD_DATA_BUS_PINS[0], PinState_output_high); // Drives data bus 4 and 5 high and the rest low for the upcoming pulses.
	set_pin(LCD_DATA_BUS_PINS[1], PinState_output_high);
	set_pin(LCD_DATA_BUS_PINS[2], PinState_output_low);
	set_pin(LCD_DATA_BUS_PINS[3], PinState_output_low);

	_pulse_lcd_enabling_pin();
	_delay_ms(5.0);           // Wait more than 4.1ms.
	_pulse_lcd_enabling_pin();
	_delay_ms(1.0);           // Wait more than 0.100ms.
	_pulse_lcd_enabling_pin();

	set_pin(LCD_DATA_BUS_PINS[0], PinState_output_low); // LCD is to operate in 4-bit mode.
	_pulse_lcd_enabling_pin();

	_lcd_send_raw_byte((1 << 5) | (1 << 3)); // "Function Set" of a 2-line display (pg. 27, 28).

	set_pin(LCD_REGISTER_SELECT_PIN, PinState_output_low); // "Display Control" (pg. 24, 26, 46).
	_lcd_send_raw_byte((1 << 3) | (1 << 2));               // `1 << 2` is the active display bit (i.e. "D") (pg. 24, 26).
	set_pin(LCD_REGISTER_SELECT_PIN, PinState_output_high);

	set_pin(LCD_REGISTER_SELECT_PIN, PinState_output_low);
	_lcd_send_raw_byte(1); // "Clear Display" (pg. 28).
	_delay_ms(2.0);        // Clearing display apparently takes a while. Usage of the busy flag could be helpful here, but a simple delay seems sufficient.
	set_pin(LCD_REGISTER_SELECT_PIN, PinState_output_high);

	set_pin(LCD_REGISTER_SELECT_PIN, PinState_output_low);
	_lcd_send_raw_byte((1 << 2) | (1 << 1)); // "Entry Mode Set" where writing a character increments the cursor.
	set_pin(LCD_REGISTER_SELECT_PIN, PinState_output_high);

	return (struct LCD) {0};
}

static void
set_lcd_cursor_visibility(bool8 active)
{
	set_pin(LCD_REGISTER_SELECT_PIN, PinState_output_low);
	_lcd_send_raw_byte((1 << 3) | (1 << 2) | (!!active << 1)); // `1 << 2` is the active display bit (i.e. "D") (pg. 24, 26).
	set_pin(LCD_REGISTER_SELECT_PIN, PinState_output_high);
}

static void
set_lcd_cursor_pos(struct LCD* lcd, u8 x, u8 y)
{
	lcd->cursor_x = x >= LCD_DIMS_X ? LCD_DIMS_X - 1 : x;
	lcd->cursor_y = y >  0          ? 1              : 0;

	set_pin(LCD_REGISTER_SELECT_PIN, PinState_output_low);
	_lcd_send_raw_byte((1 << 7) | ((lcd->cursor_y ? 0x40 : 0x00) + lcd->cursor_x)); // "Set DDRAM Address" (pg. 24, 29).
	set_pin(LCD_REGISTER_SELECT_PIN, PinState_output_high);
}

static void
swap_lcd_backbuffer(struct LCD* lcd)
{
	u8 orig_x = lcd->cursor_x;
	u8 orig_y = lcd->cursor_y;

	for (u8 y = 0; y < LCD_DIMS_Y; y += 1)
	{
		for (u8 x = 0; x < LCD_DIMS_X; x += 1)
		{
			if (lcd->curr_display[y][x] != lcd->backbuffer[y][x])
			{
				lcd->curr_display[y][x] = lcd->backbuffer[y][x];
				set_lcd_cursor_pos(lcd, x, y);
				_lcd_send_raw_byte(lcd->curr_display[y][x] == 0 ? ' ' : lcd->curr_display[y][x]);
			}
		}
	}

	set_lcd_cursor_pos(lcd, orig_x, orig_y);
}

static void
lcd_send_byte(struct LCD* lcd, u8 value)
{
	lcd->backbuffer[lcd->cursor_y][lcd->cursor_x] = value;

	if (lcd->cursor_x + 1 < LCD_DIMS_X)
	{
		lcd->cursor_x += 1;
	}
}

static void
lcd_send_bytes(struct LCD* lcd, u8* buffer, u16 amount)
{
	u16 copy_amount =
		(u16) LCD_DIMS_X - lcd->cursor_x < amount
			? (u16) LCD_DIMS_X - lcd->cursor_x
			: amount;
	memcpy(lcd->backbuffer[lcd->cursor_y] + lcd->cursor_x, buffer, copy_amount);
	lcd->cursor_x += copy_amount;
}

static void
lcd_send_u64(struct LCD* lcd, u64 value)
{
	u8  buffer[21];
	u8* cstr = cstr_of_u64(value, buffer, countof(buffer));
	lcd_send_bytes(lcd, cstr, buffer + countof(buffer) - cstr - 1);
}

static void
lcd_send_i64(struct LCD* lcd, i64 value)
{
	u8  buffer[21];
	u8* cstr = cstr_of_i64(value, buffer, countof(buffer));
	lcd_send_bytes(lcd, cstr, buffer + countof(buffer) - cstr - 1);
}

#define lcd_send_pstr(LCD, STRLIT) lcd_send_pstr_nonliteral(LCD, PSTR(STRLIT))
static void
lcd_send_pstr_nonliteral(struct LCD* lcd, const char* value)
{
	for (u8 i = 0; pgm_read_byte(&value[i]); i += 1)
	{
		lcd->backbuffer[lcd->cursor_y][lcd->cursor_x] = (u8) pgm_read_byte(&value[i]);

		if (lcd->cursor_x + 1 < LCD_DIMS_X)
		{
			lcd->cursor_x += 1;
		}
		else
		{
			break;
		}
	}
}

static void
clean_lcd(struct LCD* lcd)
{
	memset(lcd->backbuffer, 0, sizeof(lcd->backbuffer));
	set_lcd_cursor_visibility(false);
	lcd->cursor_x = 0;
	lcd->cursor_y = 0;

}
