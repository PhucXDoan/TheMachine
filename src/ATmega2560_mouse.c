#define MOUSE_SLAVE_SELECT_PIN 31

static void
play_mouse_anagrams(i8* index_buffer, u8 word_length)
{
	set_pin(MOUSE_SLAVE_SELECT_PIN, PinState_output_low);

	spi_transmit_byte((0 << 7) | word_length); // High-bit set low indicates an Anagrams packet.
	for (u8 i = 0 ; i < word_length; i += 1)
	{
		spi_transmit_byte(index_buffer[i]);
	}

	set_pin(MOUSE_SLAVE_SELECT_PIN, PinState_output_high);
}

static void
play_mouse_wordhunt(u8 start_x, u8 start_y, u8* direction_index_buffer, u8 direction_index_count)
{
	set_pin(MOUSE_SLAVE_SELECT_PIN, PinState_output_low);

	spi_transmit_byte((1 << 7) | (2 + direction_index_count)); // High-bit set high indicates an WordHunt packet. `2 +` is the `start_x` and `start_y` byte being sent.
	spi_transmit_byte(start_x);
	spi_transmit_byte(start_y);
	for (u8 i = 0 ; i < direction_index_count; i += 1)
	{
		spi_transmit_byte(direction_index_buffer[i]);
	}

	set_pin(MOUSE_SLAVE_SELECT_PIN, PinState_output_high);
}
