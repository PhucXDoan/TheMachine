#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdint.h>
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverflow"
#include <ff.c>
#pragma GCC diagnostic pop
#include "basic.h"
#include "ATmega2560_pins.c"
#include "ATmega2560_uart.c"
#include "ATmega2560_spi.c"
#include "ATmega2560_timer.c"
#include "ATmega2560_keypad.c"
#include "ATmega2560_lcd.c"
#include "ATmega2560_sd.c"
#include "ATmega2560_mouse.c"

#define MAIN_ABORT(REASON) \
	do \
	{ \
		uart_send_pstr("(" __FILE__ ":"); \
		uart_send_u64(__LINE__); \
		uart_send_pstr(") "); \
		uart_send_pstr(REASON); \
		uart_send_pstr("\n"); \
		goto ABORT; \
	} \
	while (false)
#define MAIN_ABORT_ON_ERROR(REASON) \
	do \
	{ \
		if (REASON) \
		{ \
			uart_send_pstr("(" __FILE__ ":"); \
			uart_send_u64(__LINE__); \
			uart_send_pstr(") "); \
			uart_send_pstr_nonliteral(REASON); \
			uart_send_pstr("\n"); \
			goto ABORT; \
		} \
	} \
	while (false)
#define PROC_ABORT(REASON) return PSTR("[" __FILE__ ":" STRINGIFY(__LINE__) "] " REASON "\n")

// TheMachine depends heavily on these defines. Changing them can cause unexpected errors.
// Refer to:
// - `decompress_word`
#define WORDHUNT_DIMS        4
#define MIN_LETTERS          3
#define ANAGRAMS_MAX_LETTERS 6
#define WORDHUNT_MAX_LETTERS (WORDHUNT_DIMS * WORDHUNT_DIMS)
#define ABSOLUTE_MAX_LETTERS (ANAGRAMS_MAX_LETTERS > WORDHUNT_MAX_LETTERS ? ANAGRAMS_MAX_LETTERS : WORDHUNT_MAX_LETTERS)

#define DIRECTIONS_COUNT 8
static const i8 DIRECTIONS_X[DIRECTIONS_COUNT] PROGMEM = { -1,  0,  1, -1, 1, -1, 0, 1 };
static const i8 DIRECTIONS_Y[DIRECTIONS_COUNT] PROGMEM = { -1, -1, -1,  0, 0,  1, 1, 1 };
#define get_direction_dx(INDEX) ((i8) pgm_read_byte(&DIRECTIONS_X[INDEX]))
#define get_direction_dy(INDEX) ((i8) pgm_read_byte(&DIRECTIONS_Y[INDEX]))

typedef u16 InitialCounts[ABSOLUTE_MAX_LETTERS - MIN_LETTERS + 1]['z' - 'a' + 1]; // Words are sorted descending length.

#define WordEntryCallback(NAME) bool8 NAME(u8* letter_bank, u8* word, u8 word_length)
typedef WordEntryCallback(WordEntryCallback);

#define COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(WORD_LENGTH) ((((WORD_LENGTH) - 1) * 5 + ((WORD_LENGTH - 1) + 2) / 3 + 7) / 8)
union CompressedWordTailBuffer
{
	u8  elems_u8 [ COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(ABSOLUTE_MAX_LETTERS)         ];
	u16 elems_u16[(COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(ABSOLUTE_MAX_LETTERS) + 1) / 2];
};

enum MenuOption
{
	MenuOption_anagrams,
	MenuOption_wordhunt,
	MenuOption_test_mouse,
	MenuOption_more_about_me,
	MenuOption_remake_bank,
	MenuOption_COUNT
};

static u8
decompress_word(u8* dst_word_buffer, u8 word_length, union CompressedWordTailBuffer* compressed_word_tail_buffer)
{
	if (compressed_word_tail_buffer->elems_u8[0] == 0xFF)
	{
		return false;
	}
	else
	{
		// This is a manually unrolled loop. Basic profiling showed a reduction of 5.165s just by doing this.
		// If `ABSOLUTE_MAX_LETTERS` changes, the switch must be updated accordingly so that the first case is
		// `CASE(ABSOLUTE_MAX_LETTERS)` all the way down to `CASE(2)` as last.
		switch (word_length)
		{
			#pragma GCC diagnostic push
			#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
			#define CASE(WORD_LENGTH) case (WORD_LENGTH): dst_word_buffer[(WORD_LENGTH) - 1] = 'a' + ((compressed_word_tail_buffer->elems_u16[((WORD_LENGTH) - 2) / 3] >> ((((WORD_LENGTH) - 2) % 3) * 5)) & ((1 << 5) - 1));
			CASE(16); CASE(15); CASE(14);
			CASE(13); CASE(12); CASE(11);
			CASE(10); CASE( 9); CASE( 8);
			CASE( 7); CASE( 6); CASE( 5);
			CASE( 4); CASE( 3); CASE( 2);
			#pragma GCC diagnostic pop
		}

		return true;
	}
}

static void
set_lcd_to_show_creation_of_bank_file(struct LCD* lcd, bool8 on_bank_file_stage, u8* curr_tick)
{
	if ((*curr_tick ^ (*curr_tick - 1)) & (1 << 6))
	{
		clean_lcd(lcd);

		lcd_send_pstr(lcd, "Creating");
		set_lcd_cursor_pos(lcd, 0, 1);
		if (on_bank_file_stage)
		{
			lcd_send_pstr(lcd, "\"BANK.BIN\"");
		}
		else
		{
			lcd_send_pstr(lcd, "\"MID.BIN\"");
		}

		for (u8 i = 0; i < (*curr_tick >> 6); i += 1)
		{
			lcd_send_pstr(lcd, ".");
		}

		swap_lcd_backbuffer(lcd);
	}

	*curr_tick += 1;
}

#define set_lcd_to_show_success(LCD, MESSAGE) set_lcd_to_show_success_nonliteral((LCD), PSTR(MESSAGE))
static void
set_lcd_to_show_success_nonliteral(struct LCD* lcd, const char* message)
{
	clean_lcd(lcd);
	lcd_send_pstr_nonliteral(lcd, message);
	set_lcd_cursor_pos(lcd, 0, 1);
	lcd_send_pstr(lcd, ":3 success!");
	swap_lcd_backbuffer(lcd);
	_delay_ms(1500.0);
}

#define set_lcd_to_show_failure(LCD, MESSAGE) set_lcd_to_show_failure_nonliteral((LCD), PSTR(MESSAGE))
static void
set_lcd_to_show_failure_nonliteral(struct LCD* lcd, const char* message)
{
	clean_lcd(lcd);
	lcd_send_pstr_nonliteral(lcd, message);
	set_lcd_cursor_pos(lcd, 0, 1);
	lcd_send_pstr(lcd, ":( failed!");
	swap_lcd_backbuffer(lcd);
	_delay_ms(1500.0);
}

#define set_lcd_to_show_querying_of_letters(LCD, TITLE, ...) set_lcd_to_show_querying_of_letters_nonliteral((LCD), PSTR(TITLE), __VA_ARGS__)
static void
set_lcd_to_show_querying_of_letters_nonliteral(struct LCD* lcd, const char* title, u8* letters, u8 letter_count, u8 max_letter_count, bool8 asterisked)
{
	clean_lcd(lcd);

	if (letter_count == max_letter_count)
	{
		set_lcd_cursor_visibility(false);
		lcd_send_pstr(lcd, "* to undo");
	}
	else
	{
		set_lcd_cursor_visibility(true);
		lcd_send_pstr_nonliteral(lcd, title);
	}

	set_lcd_cursor_pos(lcd, 0, 1);
	lcd_send_bytes(lcd, letters, letter_count);
	if (asterisked)
	{
		if (lcd->cursor_x == LCD_DIMS_X - 1)
		{
			lcd_send_byte(lcd, '*');
		}
		else
		{
			lcd_send_byte(lcd, '*');
			lcd->cursor_x -= 1;
		}
	}

	swap_lcd_backbuffer(lcd);
}

#define query_letters(LCD, TITLE, ...) query_letters_nonliteral((LCD), PSTR(TITLE), __VA_ARGS__)
static u32 // Returns a bitmask indicating what letters in the alphabet were chosen. `0` for when the user exit.
query_letters_nonliteral(struct LCD* lcd, const char* title, u8* dst_letters, u8 letter_count)
{
	u8 curr_length = 0;
	while (true)
	{
		set_lcd_to_show_querying_of_letters_nonliteral(lcd, title, dst_letters, curr_length, letter_count, false);

		u8 index = wait_for_keypad_button_press();

		if (index == KEYPAD_DIM * KEYPAD_DIM - 1)
		{
			set_lcd_to_show_querying_of_letters_nonliteral(lcd, title, dst_letters, curr_length, letter_count, true);
			index += wait_for_keypad_button_press();
		}

		if (index <= U'z' - U'a')
		{
			dst_letters[curr_length]  = U'a' + index;
			curr_length              += 1;
		}
		else if (index == 2 * (KEYPAD_DIM * KEYPAD_DIM - 1))
		{
			if (curr_length >= 1)
			{
				curr_length -= 1;
			}
			else
			{
				return 0;
			}
		}

		set_lcd_to_show_querying_of_letters_nonliteral(lcd, title, dst_letters, curr_length, letter_count, false);

		if (curr_length == letter_count)
		{
			if (wait_for_keypad_button_press() == KEYPAD_DIM * KEYPAD_DIM - 1)
			{
				curr_length -= 1;
			}
			else
			{
				break;
			}
		}
	}

	u32 mask = 0;
	for (u8 i = 0; i < letter_count; i += 1)
	{
		mask |= 1UL << (dst_letters[i] - 'a');
	}
	return mask;
}

static const char*
init_bank_bin(FIL* bank_file, InitialCounts dst_initial_counts, struct LCD* lcd, bool8 must_remake)
{
	if (!must_remake)
	{
		switch (f_stat("BANK.BIN", 0))
		{
			case FR_NO_FILE:
			{
				// The rest of the procedures creates "BANK.BIN".
			} break;

			case FR_OK:
			{
				if (f_open(bank_file, "BANK.BIN", FA_READ | FA_WRITE | FA_OPEN_EXISTING))
				{
					PROC_ABORT("\"BANK.BIN\" failed to open.");
				}
				else if (sd_fread(bank_file, dst_initial_counts, sizeof(InitialCounts)))
				{
					return 0;
				}
				else
				{
					PROC_ABORT("Failed to read \"BANK.BIN\".");
				}
			} break;

			default:
			{
				PROC_ABORT("`f_stat` returned unexpected value.");
			} break;
		}
	}

	u32 starting_time_ms   = get_ms();
	u8  lcd_buffering_tick = 0;
	memset(dst_initial_counts, 0, sizeof(InitialCounts));

	FIL mid_file;
	if (f_open(&mid_file, "MID.BIN", FA_READ | FA_WRITE | FA_CREATE_ALWAYS))
	{
		PROC_ABORT("Could not create \"MID.BIN\".");
	}

	{ // Process "WORDS.TXT" into intermediate file "MID.BIN".
		FIL words_file;
		if (f_open(&words_file, "WORDS.TXT", FA_READ))
		{
			PROC_ABORT("Could not read \"WORDS.TXT\".");
		}

		while (!f_eof(&words_file))
		{
			//
			// Get word.
			//

			struct
			{
				u8 length;
				u8 buffer[ABSOLUTE_MAX_LETTERS];
			} word;
			word.length = 0;

			while (true)
			{
				u16 read_amount;
				u8  word_letter;
				if (f_read(&words_file, &word_letter, sizeof(word_letter), &read_amount))
				{
					PROC_ABORT("Failed to read from \"WORDS.TXT\".");
				}
				if (read_amount == 1 && 'a' <= word_letter && word_letter <= 'z')
				{
					if (word.length < countof(word.buffer))
					{
						word.buffer[word.length] = word_letter;
					}
					word.length += 1;
				}
				else
				{
					break;
				}
			}

			//
			// Commit word to "MID.BIN" if it's of legal length and update count.
			//

			if (MIN_LETTERS <= word.length && word.length <= ABSOLUTE_MAX_LETTERS)
			{
				if (!sd_fwrite(&mid_file, &word, 1 + word.length))
				{
					PROC_ABORT("Failed to write to \"MID.BIN\".");
				}
				dst_initial_counts[ABSOLUTE_MAX_LETTERS - word.length][word.buffer[0] - 'a'] += 1;
				set_lcd_to_show_creation_of_bank_file(lcd, false, &lcd_buffering_tick);
			}
		}

		if (f_close(&words_file))
		{
			PROC_ABORT("Failed to close \"WORDS.TXT\".");
		}
	}


	if (f_lseek(&mid_file, 0))
	{
		PROC_ABORT("Failed to seek \"MID.BIN\".");
	}

	{ // Sort "MID.BIN" into "BANK.BIN".
		FIL bank_file;
		if (f_open(&bank_file, "BANK.BIN", FA_WRITE | FA_CREATE_ALWAYS))
		{
			return PSTR("Could not read \"BANK.BIN\".\n");
		}

		if (!sd_fwrite(&bank_file, dst_initial_counts, sizeof(InitialCounts)))
		{
			return PSTR("Failed to write to \"BANK.BIN\".\n");
		}

		InitialCounts written_initial_counts = {0};
		while (!f_eof(&mid_file))
		{
			//
			// Get word from "MID.BIN".
			//

			u8 word_length;
			u8 word_buffer[ABSOLUTE_MAX_LETTERS];
			if (!sd_fread(&mid_file, &word_length, sizeof(word_length)) || !sd_fread(&mid_file, &word_buffer, word_length))
			{
				PROC_ABORT("Failed to read from \"MID.BIN\".");
			}

			//
			// Seek "BANK.BIN".
			//

			{
				u32 seek_offset = sizeof(InitialCounts);
				for (u8 seek_word_length = ABSOLUTE_MAX_LETTERS; seek_word_length > word_length; seek_word_length -= 1)
				{
					for (u8 seek_iniital = 'a'; seek_iniital <= 'z'; seek_iniital += 1)
					{
						seek_offset += dst_initial_counts[ABSOLUTE_MAX_LETTERS - seek_word_length][seek_iniital - 'a'] * COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(seek_word_length);
					}
				}
				for (u8 seek_iniital = 'a'; seek_iniital < word_buffer[0]; seek_iniital += 1)
				{
					seek_offset += dst_initial_counts[ABSOLUTE_MAX_LETTERS - word_length][seek_iniital - 'a'] * COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(word_length);
				}
				seek_offset += written_initial_counts[ABSOLUTE_MAX_LETTERS - word_length][word_buffer[0] - 'a'] * COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(word_length);

				if (f_lseek(&bank_file, seek_offset))
				{
					PROC_ABORT("Failed to seek \"BANK.BIN\".");
				}
			}

			//
			// Compress, write, and increment.
			//

			union CompressedWordTailBuffer compressed_word_tail_buffer = {0};
			for (u8 i = 0; i < word_length - 1; i += 1)
			{
				compressed_word_tail_buffer.elems_u16[i / 3] |= (word_buffer[1 + i] - 'a') << ((i % 3) * 5);
			}
			if (!sd_fwrite(&bank_file, &compressed_word_tail_buffer, COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(word_length)))
			{
				PROC_ABORT("Failed to write \"BANK.BIN\".");
			}

			written_initial_counts[ABSOLUTE_MAX_LETTERS - word_length][word_buffer[0] - 'a'] += 1;
			set_lcd_to_show_creation_of_bank_file(lcd, true, &lcd_buffering_tick);
		}
	}

	if (f_close(&mid_file))
	{
		PROC_ABORT("Failed to close \"MID.BIN\".");
	}

	if (f_unlink("MID.BIN"))
	{
		PROC_ABORT("Failed to remove \"MID.BIN\".");
	}

	uart_send_pstr("Making \"BANK.BIN\" took: ");
	uart_send_u64(get_ms() - starting_time_ms);
	uart_send_pstr("ms.\n");

	set_lcd_to_show_success(lcd, "\"BANK.BIN\" made");

	return 0;
}

static
WordEntryCallback(anagrams_callback)
{
	i8    index_buffer[ANAGRAMS_MAX_LETTERS];
	bool8 valid = true;
	for (u8 word_letter_index = 0; word_letter_index < word_length; word_letter_index += 1) // Iterate through each letter in the word to see if it's in the provided letter-bank.
	{
		for (u8 bank_letter_index = 0; bank_letter_index < ANAGRAMS_MAX_LETTERS; bank_letter_index += 1)
		{
			if (letter_bank[bank_letter_index] == word[word_letter_index])
			{
				// If a required letter is found in the letter-bank, set the high-bit in the corresponding letter in the letter-bank to `1`.
				// This bit is unused anyways, and this will make the comparison equality fail for this specific slot of the letter-bank.
				letter_bank [bank_letter_index] |= 1 << 7;
				index_buffer[word_letter_index]  = bank_letter_index;
				goto NEXT_LETTER;
			}
		}

		valid = false;
		break;

		NEXT_LETTER:;
	}
	for (u8 i = 0; i < ANAGRAMS_MAX_LETTERS; i += 1) // Reset the high-bits of the used letters in the letter-bank back to `0`.
	{
		letter_bank[i] &= ~(1 << 7);
	}

	if (valid)
	{
		play_mouse_anagrams(index_buffer, word_length);
	}

	return valid;
}

static
WordEntryCallback(wordhunt_callback)
{
	for (i8 curr_y = 0; curr_y < WORDHUNT_DIMS; curr_y += 1)
	{
		for (i8 curr_x = 0; curr_x < WORDHUNT_DIMS; curr_x += 1)
		{
			if (letter_bank[curr_y * WORDHUNT_DIMS + curr_x] == word[0])
			{
				letter_bank[curr_y * WORDHUNT_DIMS + curr_x] |= 1 << 7;

				u8 direction_index_buffer[WORDHUNT_MAX_LETTERS - 1];
				u8 direction_index_count = 0;
				u8 curr_direction_index  = 0;
				while (true)
				{
					//
					// Cycle through the directions.
					//

					while (curr_direction_index < DIRECTIONS_COUNT)
					{
						if // Check if we can and should take a step into the direction.
						(
							0 <= curr_x + get_direction_dx(curr_direction_index) && curr_x + get_direction_dx(curr_direction_index) < WORDHUNT_DIMS &&
							0 <= curr_y + get_direction_dy(curr_direction_index) && curr_y + get_direction_dy(curr_direction_index) < WORDHUNT_DIMS &&
							letter_bank[(curr_y + get_direction_dy(curr_direction_index)) * WORDHUNT_DIMS + curr_x + get_direction_dx(curr_direction_index)] == word[direction_index_count + 1]
						)
						{
							direction_index_buffer[direction_index_count]  = curr_direction_index;
							direction_index_count                         += 1;

							if (direction_index_count + 1 == word_length) // We got to the end. Note the fencepost math.
							{
								for (u8 i = 1; i < direction_index_count; i += 1) // Backtrack to beginning and fix up used letters in the grid.
								{
									letter_bank[curr_y * WORDHUNT_DIMS + curr_x] &= ~(1 << 7);
									curr_x                                       -= get_direction_dx(direction_index_buffer[direction_index_count - 1 - i]);
									curr_y                                       -= get_direction_dy(direction_index_buffer[direction_index_count - 1 - i]);
								}
								letter_bank[curr_y * WORDHUNT_DIMS + curr_x] &= ~(1 << 7);

								play_mouse_wordhunt(curr_x, curr_y, direction_index_buffer, word_length - 1);

								return true;
							}
							else // Take step forward.
							{
								curr_x                                       += get_direction_dx(curr_direction_index);
								curr_y                                       += get_direction_dy(curr_direction_index);
								letter_bank[curr_y * WORDHUNT_DIMS + curr_x] |= 1 << 7;
								curr_direction_index                          = 0;
							}
						}
						else
						{
							curr_direction_index += 1;
						}
					}

					//
					// Backtrack and use the next direction.
					//

					letter_bank[curr_y * WORDHUNT_DIMS + curr_x] &= ~(1 << 7);
					if (direction_index_count)
					{
						direction_index_count -= 1;
						curr_x                -= get_direction_dx(direction_index_buffer[direction_index_count]);
						curr_y                -= get_direction_dy(direction_index_buffer[direction_index_count]);
						curr_direction_index   = direction_index_buffer[direction_index_count] + 1;
					}
					else
					{
						break;
					}
				}
			}
		}
	}

	return false;
}

int
main(void)
{
	init_uart();
	init_spi();
	init_timer();
	init_keypad();
	struct LCD lcd = init_lcd();

	set_pin(MOUSE_SLAVE_SELECT_PIN, PinState_output_high);
	set_pin(SD_SLAVE_SELECT_PIN   , PinState_output_high);

	{
		static FATFS file_system;
		if (f_mount(&file_system, "", 1))
		{
			MAIN_ABORT("`f_mount` failed to initialize the file-system.");
		}
	}

	{
		FIL           bank_file;
		InitialCounts initial_counts;
		const char*   error = init_bank_bin(&bank_file, initial_counts, &lcd, false);
		MAIN_ABORT_ON_ERROR(error);
		if (f_close(&bank_file))
		{
			MAIN_ABORT("Failed to close \"BANK.BIN\".");
		}
	}

	for (enum MenuOption menu_option = 0;;)
	{
		while (true)
		{
			clean_lcd(&lcd);
			switch (menu_option)
			{
				case MenuOption_anagrams      : lcd_send_pstr(&lcd, "> Play Anagrams"); break;
				case MenuOption_wordhunt      : lcd_send_pstr(&lcd, "> Play WordHunt"); break;
				case MenuOption_test_mouse    : lcd_send_pstr(&lcd, "> Test Mouse"   ); break;
				case MenuOption_more_about_me : lcd_send_pstr(&lcd, "> More About Me"); break;
				case MenuOption_remake_bank   : lcd_send_pstr(&lcd, "> Redo BANK.BIN"); break;
				case MenuOption_COUNT         : break;
			}
			set_lcd_cursor_pos(&lcd, 0, 1);
			lcd_send_pstr(&lcd, "* to cycle menu");
			swap_lcd_backbuffer(&lcd);

			u8 response = wait_for_keypad_button_press();
			if (response == 0)
			{
				break;
			}
			else if (response == KEYPAD_DIM * KEYPAD_DIM - 1)
			{
				menu_option += 1;
				menu_option %= MenuOption_COUNT;
			}
		}

		switch (menu_option)
		{
			case MenuOption_anagrams:
			case MenuOption_wordhunt:
			{
				WordEntryCallback* callback;
				u8                 letter_bank_size;
				u8                 starting_word_length;
				const char*        game_name;

				if (menu_option == MenuOption_anagrams)
				{
					callback             = anagrams_callback;
					letter_bank_size     = ANAGRAMS_MAX_LETTERS;
					starting_word_length = ANAGRAMS_MAX_LETTERS;
					game_name            = PSTR("Anagrams");
				}
				else if (menu_option == MenuOption_wordhunt)
				{
					callback             = wordhunt_callback;
					letter_bank_size     = WORDHUNT_MAX_LETTERS;
					starting_word_length = 9;
					game_name            = PSTR("WordHunt");
				}

				u8            letter_bank_buffer[ABSOLUTE_MAX_LETTERS];
				FIL           bank_file;
				InitialCounts initial_counts;
				{
					const char* error = init_bank_bin(&bank_file, initial_counts, &lcd, false);
					MAIN_ABORT_ON_ERROR(error);

					u32 seek_offset = sizeof(InitialCounts);
					for (u8 word_length = ABSOLUTE_MAX_LETTERS; word_length > starting_word_length; word_length -= 1)
					{
						for (u8 word_initial = 'a'; word_initial <= 'z'; word_initial += 1)
						{
							seek_offset += initial_counts[ABSOLUTE_MAX_LETTERS - word_length][word_initial - 'a'] * COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(word_length);
						}
					}
					if (f_lseek(&bank_file, seek_offset))
					{
						MAIN_ABORT("Failed to seek \"BANK.BIN\".");
					}
				}

				struct WordEntry
				{
					u8  length;
					u8  initial;
					u16 index;
				}   word_entry_buffer[256];
				u16 word_entry_count = 0;
				u32 letter_mask      = query_letters_nonliteral(&lcd, game_name, letter_bank_buffer, letter_bank_size);
				if (letter_mask)
				{
					{ // Search for words.
						u32 seek_offset_addend  = 0;
						u16 lcd_buffering_tick  = 0;
						u32 keypad_held_time_ms = 0;
						u8  keypad_held_tick    = 0;
						u32 starting_time_ms    = get_ms();
						for (u8 word_length = starting_word_length; word_length >= MIN_LETTERS; word_length -= 1)
						{
							for (u8 word_initial = 'a'; word_initial <= 'z'; word_initial += 1)
							{
								if (letter_mask & (1UL << (word_initial - 'a'))) // If this initial is even one of the user-provided letters.
								{
									if (seek_offset_addend) // In the case the we have skipped over some initial sections.
									{
										if (f_lseek(&bank_file, f_tell(&bank_file) + seek_offset_addend))
										{
											MAIN_ABORT("Failed to seek \"BANK.BIN\".");
										}
										seek_offset_addend = 0;
									}

									u8 word_buffer[ABSOLUTE_MAX_LETTERS];
									word_buffer[0] = word_initial;
									for (u16 initial_index = 0; initial_index < initial_counts[ABSOLUTE_MAX_LETTERS - word_length][word_initial - 'a']; initial_index += 1)
									{
										if (keypad_held(&keypad_held_time_ms, &keypad_held_tick))
										{
											goto STOP_SEARCHING;
										}

										union CompressedWordTailBuffer compressed_word_tail_buffer;
										if (!sd_fread(&bank_file, &compressed_word_tail_buffer, COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(word_length)))
										{
											uart_send_pstr("Failed to read a word from \"BANK.BIN\".\n");
											goto ABORT;
										}
										if (decompress_word(word_buffer, word_length, &compressed_word_tail_buffer))
										{
											for (u8 i = 1; i < word_length; i += 1)
											{
												if (!(letter_mask & (1UL << (word_buffer[i] - 'a'))))
												{
													goto NEXT_WORD;
												}
											}

											if (callback(letter_bank_buffer, word_buffer, word_length)) // If the algorithm determined and has acted, we remember this word for later prompting.
											{
												word_entry_buffer[word_entry_count]  = (struct WordEntry) { .length = word_length, .initial = word_initial, .index = initial_index };
												word_entry_count                    += 1;
												if (word_entry_count == countof(word_entry_buffer))
												{
													goto STOP_SEARCHING;
												}

												uart_send_bytes(word_buffer, word_length);
												uart_send_pstr("\n");

												lcd_buffering_tick = 0;
											}

											NEXT_WORD:;

											if (lcd_buffering_tick == 0)
											{
												clean_lcd(&lcd);
												lcd_send_bytes(&lcd, letter_bank_buffer, letter_bank_size);
												set_lcd_cursor_pos(&lcd, 0, 1);
												lcd_send_bytes(&lcd, word_buffer, word_length);
												swap_lcd_backbuffer(&lcd);
											}
											lcd_buffering_tick += 32;
										}
									}
								}
								else
								{
									seek_offset_addend += initial_counts[ABSOLUTE_MAX_LETTERS - word_length][word_initial - 'a'] * COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(word_length);
								}
							}
						}
						STOP_SEARCHING:;

						uart_send_pstr("Searching took: ");
						uart_send_u64(get_ms() - starting_time_ms);
						uart_send_pstr("ms.\n");
					}

					for (u16 word_entry_index = 0; word_entry_index < word_entry_count; word_entry_index += 1)
					{
						//
						// Go to where the word is in the file.
						//

						u32 seek_offset = sizeof(InitialCounts);
						for (u8 seek_length = ABSOLUTE_MAX_LETTERS; seek_length > word_entry_buffer[word_entry_index].length; seek_length -= 1)
						{
							for (u8 seek_initial = 'a'; seek_initial <= 'z'; seek_initial += 1)
							{
								seek_offset += initial_counts[ABSOLUTE_MAX_LETTERS - seek_length][seek_initial - 'a'] * COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(seek_length);
							}
						}
						for (u8 seek_initial = 'a'; seek_initial < word_entry_buffer[word_entry_index].initial; seek_initial += 1)
						{
							seek_offset += initial_counts[ABSOLUTE_MAX_LETTERS - word_entry_buffer[word_entry_index].length][seek_initial - 'a'] * COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(word_entry_buffer[word_entry_index].length);
						}
						seek_offset += word_entry_buffer[word_entry_index].index * COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(word_entry_buffer[word_entry_index].length);

						if (f_lseek(&bank_file, seek_offset))
						{
							MAIN_ABORT("Failed to seek \"MID.BIN\".");
						}

						//
						// Prompt the user for validity.
						//

						u8 word_buffer[ABSOLUTE_MAX_LETTERS];
						word_buffer[0] = word_entry_buffer[word_entry_index].initial;
						union CompressedWordTailBuffer compressed_word_tail_buffer;
						if (!sd_fread(&bank_file, &compressed_word_tail_buffer, COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(word_entry_buffer[word_entry_index].length)))
						{
							uart_send_pstr("Failed to read a word from \"BANK.BIN\".\n");
							goto ABORT;
						}
						if (decompress_word(word_buffer, word_entry_buffer[word_entry_index].length, &compressed_word_tail_buffer))
						{
							clean_lcd(&lcd);
							lcd_send_pstr(&lcd, "* to invalidate");
							set_lcd_cursor_pos(&lcd, 0, 1);
							lcd_send_bytes(&lcd, word_buffer, word_entry_buffer[word_entry_index].length);
							swap_lcd_backbuffer(&lcd);

							u8 response = wait_for_keypad_button_press();
							if (response == KEYPAD_DIM * KEYPAD_DIM - 1)
							{
								if (f_lseek(&bank_file, f_tell(&bank_file) - COMPRESSED_WORD_TAIL_LENGTH_FROM_WORD_LENGTH(word_entry_buffer[word_entry_index].length)))
								{
									MAIN_ABORT("Failed to seek \"BANK.BIN\".");
								}
								if (!sd_fwrite(&bank_file, &(u8) { 0xFF }, sizeof(u8)))
								{
									MAIN_ABORT("Failed to write to \"BANK.BIN\".");
								}

								clean_lcd(&lcd);
								lcd_send_pstr(&lcd, "Removed!");
								set_lcd_cursor_pos(&lcd, 0, 1);
								lcd_send_bytes(&lcd, word_buffer, word_entry_buffer[word_entry_index].length);
								swap_lcd_backbuffer(&lcd);
								_delay_ms(750.0);
							}
							else // If user holds button for long enough, we stop prompting.
							{
								u32 keypad_held_time_ms = 0;
								u8  keypad_held_tick    = 0;
								while (read_keypad())
								{
									if (keypad_held(&keypad_held_time_ms, &keypad_held_tick))
									{
										goto STOP_QUERYING;
									}
								}
							}
						}
						else
						{
							MAIN_ABORT("Failed to decompress a word.");
						}
					}
					STOP_QUERYING:;

					set_lcd_to_show_success_nonliteral(&lcd, game_name);
				}

				if (f_close(&bank_file))
				{
					MAIN_ABORT("Failed to close \"BANK.BIN\".");
				}
			} break;

			case MenuOption_test_mouse:
			{
				clean_lcd(&lcd);
				lcd_send_pstr(&lcd, "Testing");
				set_lcd_cursor_pos(&lcd, 0, 1);
				lcd_send_pstr(&lcd, "Anagrams...");
				swap_lcd_backbuffer(&lcd);

				play_mouse_anagrams((i8[]){ 0, 0, 0, 0, 0, 0 }, 6);
				play_mouse_anagrams((i8[]){ 1, 1, 1, 1, 1, 1 }, 6);
				play_mouse_anagrams((i8[]){ 2, 2, 2, 2, 2, 2 }, 6);
				play_mouse_anagrams((i8[]){ 0, 1, 2, 3, 4, 5 }, 6);
				play_mouse_anagrams((i8[]){ 1, 3, 5, 0, 2, 4 }, 6);
				play_mouse_wordhunt(3, 3, (u8[]) { 0, 4, 5, 3, 1, 2, 6, 7 }, 8);

				set_lcd_to_show_success(&lcd, "Mouse tested!");
			} break;

			case MenuOption_more_about_me:
			{
				const char* messages[][2] =
					{
						{ PSTR("My name is      "), PSTR("'The Machine'   ") },
						{ PSTR("I was developed "), PSTR("by Phuc X. Doan ") },
						{ PSTR("... against my  "), PSTR("will of course..") },
						{ PSTR("I am simply a   "), PSTR("bordem project  ") },
						{ PSTR("conceived on the"), PSTR("22nd of Oct 2022") },
						{ PSTR("given life on   "), PSTR("8th of Dec 2022!") },
						{ PSTR("Probably depends"), PSTR("on Mr. Phuc...  ") },
						{ PSTR("Anyways, I can  "), PSTR("play word games ") },
						{ PSTR("Well, Anagrams  "), PSTR("and WordHunt... ") },
						{ PSTR("But I'm pretty  "), PSTR("good at them!   ") },
						{ PSTR("In fact, if you "), PSTR("can beat me...  ") },
						{ PSTR("You'll be given "), PSTR("the reward of...") },
						{ PSTR("FREE............"), PSTR("ARBY'S!!!!!!!!!!") },
						{ PSTR("who doesn't love"), PSTR("arby's?         ") },
						{ PSTR("Good luck       "), PSTR(";)              ") }
					};

				for (u8 i = 0; i < countof(messages); i += 1)
				{
					clean_lcd(&lcd);

					for (u8 j = 0; j < LCD_DIMS_X; j += 1)
					{
						lcd_send_byte(&lcd, pgm_read_byte(&messages[i][0][j]));
						swap_lcd_backbuffer(&lcd);
						_delay_ms(50.0);
					}
					set_lcd_cursor_pos(&lcd, 0, 1);
					for (u8 j = 0; j < LCD_DIMS_X; j += 1)
					{
						lcd_send_byte(&lcd, pgm_read_byte(&messages[i][1][j]));
						swap_lcd_backbuffer(&lcd);
						_delay_ms(50.0);
					}

					wait_for_keypad_button_press();
				}
			} break;

			case MenuOption_remake_bank:
			{
				union
				{
					u8  bytes[8];
					u64 packed;
				} input;
				if (query_letters(&lcd, "Password?", input.bytes, countof(input.bytes)))
				{
					if (input.packed == 7305508620784263523ULL)
					{
						FIL           bank_file;
						InitialCounts initial_counts;
						const char*   error = init_bank_bin(&bank_file, initial_counts, &lcd, true);
						MAIN_ABORT_ON_ERROR(error);
						if (f_close(&bank_file))
						{
							MAIN_ABORT("Failed to close \"BANK.BIN\".");
						}
					}
					else
					{
						set_lcd_to_show_failure(&lcd, "Password invalid");
					}
				}
			} break;

			case MenuOption_COUNT: break;
		}
	}

	ABORT:

	f_unmount("");

	clean_lcd(&lcd);
	lcd_send_pstr(&lcd, "ERROR");
	swap_lcd_backbuffer(&lcd);

	while (true)
	{
		set_pin(BUILTIN_LED_PIN, !read_pin(BUILTIN_LED_PIN));
		_delay_ms(100.0);
	}
}
