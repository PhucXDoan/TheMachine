// Refer to:
// - "ATmega2560 Datasheet" ("(pg. N)" refers to page number `N` of this resource)
// - "www.rjhcoding.com/avrc-sd-interface-1.php"
// - "FatFs - Generic FAT Filesystem Module (elm-chan)"

#define SD_SLAVE_SELECT_PIN  30
#define SD_MAX_ATTEMPTS      10000
#define SD_SECTOR_SIZE_LOG2  9                          // The 2's exponent for the size of data-blocks for reading and writing to the SD card. This is asserted, so SD cards with different sized data-blocks are incompatible.
#define SD_SECTOR_SIZE       (1 << SD_SECTOR_SIZE_LOG2) // `2^9` is 512, so sectors in the SD card should be 512 bytes.
#define SD_SPI_IDLE_FLAG     0x1
#define SD_SPI_NULL_TOKEN    0xFF                       // Value used to send and receive from SD card when not sending useful information.
#define SD_SPI_START_TOKEN   0xFE                       // Indicates the start of a data-block for both receiving or transmitting.

static bool8 _sd_inited = false;

static void
_sd_print_response_breakdown(i8 cmd, u8 response)
{
	if (cmd < 0)
	{
		uart_send_pstr("Sent ACMD");
		uart_send_u64(-cmd);
	}
	else
	{
		uart_send_pstr("Sent CMD");
		uart_send_u64(cmd);
	}
	uart_send_pstr(" to SD card.\n");
	uart_send_pstr("Recieved response of '0b");
	uart_send_b8(response);
	uart_send_pstr("`.\n");
	if (response)
	{
		if (response & (1 << 0)) uart_send_pstr("                               ^ In idle state\n");
		if (response & (1 << 1)) uart_send_pstr("                              ^ Erase reset\n");
		if (response & (1 << 2)) uart_send_pstr("                             ^ Illegal command\n");
		if (response & (1 << 3)) uart_send_pstr("                            ^ CRC error\n");
		if (response & (1 << 4)) uart_send_pstr("                           ^ Erase sequence error\n");
		if (response & (1 << 5)) uart_send_pstr("                          ^ Address error\n");
		if (response & (1 << 6)) uart_send_pstr("                         ^ Parameter error\n");
		if (response & (1 << 7)) uart_send_pstr("                        ^ Should be zero\n");
	}
}

// Returning `SD_SPI_START_TOKEN` suggests data-block has been received
// `SD_SPI_NULL_TOKEN` means the response took too long and the procedure timed-out.
// Otherwise, an error response is returned, but this is apparently not consistent and a time-out response (i.e. `SD_SPI_NULL_TOKEN`) could be returned instead.
static u8
_sd_receive_data_block(u8* buffer, i16 amount)
{
	u8  response;
	u16 attempts_left = SD_MAX_ATTEMPTS;
	do
	{
		response       = spi_receive_byte();
		attempts_left -= 1;
	}
	while (response == SD_SPI_NULL_TOKEN && attempts_left);

	if (response == SD_SPI_START_TOKEN)
	{
		for (i16 i = 0; i < amount; i += 1)
		{
			buffer[i] = spi_receive_byte();
		}
	}

	return response;
}

// `0` is the optimal return value as it means the SD card recieved the response.
// `1` suggests that the SD card is in idle mode.
// Otherwise there is likely an error.
static u8
_sd_transmit_command(u8 cmd, u32 args)
{
	spi_transmit_byte((1 << 6) | cmd);
	spi_transmit_byte((args >> 24) & 0xFF);
	spi_transmit_byte((args >> 16) & 0xFF);
	spi_transmit_byte((args >>  8) & 0xFF);
	spi_transmit_byte((args >>  0) & 0xFF);
	spi_transmit_byte // "Cyclic Redundency Check" byte, often ignored.
	(
		cmd == 0 ? 0x95 :
		cmd == 8 ? 0x87 : 0xFF
	);

	u8 response = spi_receive_byte();
	for (i8 i = 0; i < 8 && (response & (1 << 7)); i += 1) // SD card should respond within 8 ticks.
	{
		response = spi_receive_byte();
	}
	return response;
}

static u8 // Returning `SD_SPI_START_TOKEN` suggests that the procedure was successful in receiving the data-block of the SD card's contents, otherwise there's likely an error.
_sd_read(u8* buffer, u32 address)
{
	set_pin(SD_SLAVE_SELECT_PIN, PinState_output_low);

	u8 response = _sd_transmit_command(17, address);
	if (!response)
	{
		response = _sd_receive_data_block(buffer, SD_SECTOR_SIZE);
		if (response == SD_SPI_START_TOKEN)
		{
			spi_receive_byte(); // 16-bit CRC sent for error-checking that we are ignoring.
			spi_receive_byte();

		}
	}

	set_pin(SD_SLAVE_SELECT_PIN, PinState_output_high);

	return response;
}

static u8 // Returns `0` on success, otherwise likely an error.
_sd_write(const u8* buffer, u32 address)
{
	set_pin(SD_SLAVE_SELECT_PIN, PinState_output_low);

	u8 response = _sd_transmit_command(24, address);
	if (!response)
	{
		spi_transmit_byte(SD_SPI_START_TOKEN);
		for (i16 i = 0; i < SD_SECTOR_SIZE; i += 1)
		{
			spi_transmit_byte(buffer[i]);
		}

		u16 attempts_left = SD_MAX_ATTEMPTS;
		do
		{
			response       = spi_receive_byte();
			attempts_left -= 1;
		}
		while (response == SD_SPI_NULL_TOKEN && attempts_left);

		// `0x???00101` : accepted.
		// `0x???01011` : rejected due to CRC error.
		// `0x???01101` : rejected due to write error.
		if ((response & 0xF) == 0x5)
		{
			attempts_left = SD_MAX_ATTEMPTS;
			do
			{
				response       = spi_receive_byte(); // SD card will send `0` to indicate it is busy writing the data.
				attempts_left -= 1;
			}
			while (!response && attempts_left);
			return response == SD_SPI_NULL_TOKEN ? 0 : response;
		}
	}

	set_pin(SD_SLAVE_SELECT_PIN, PinState_output_high);

	return response;
}

static bool8
sd_fwrite(FIL* file, void* buffer, u16 size)
{
	u16 write_amount;
	return f_write(file, buffer, size, &write_amount) == FR_OK && write_amount == size;
}

static bool8
sd_fread(FIL* file, void* buffer, u16 size)
{
	u16 read_amount;
	return f_read(file, buffer, size, &read_amount) == FR_OK && read_amount == size;
}

DSTATUS
disk_status(BYTE pdrv)
{
	return pdrv == 0 && _sd_inited ? 0 : STA_NOINIT;
}

DSTATUS
disk_initialize(BYTE pdrv)
{
	if (pdrv == 0)
	{
		set_pin(SD_SLAVE_SELECT_PIN, PinState_output_high);

		_delay_ms(1.0);                // For powering up; redundant, but just in case.
		for (i8 i = 0; i < 10; i += 1) // Send atleast 72 clock pulses to ready the SPI communication with the SD card.
		{
			spi_transmit_byte(0xFF);
		}

		set_pin(SD_SLAVE_SELECT_PIN, PinState_output_low); // SD card enters SPI mode.

		{ // "CMD0" sets the SD card to the idle state.
			u8  response;
			u16 attempts_left = SD_MAX_ATTEMPTS;
			do
			{
				response       = _sd_transmit_command(0, 0);
				attempts_left -= 1;
			}
			while (response != SD_SPI_IDLE_FLAG && attempts_left);
			if (response != SD_SPI_IDLE_FLAG)
			{
				_sd_print_response_breakdown(0, response);
				uart_send_pstr("Is the SD card in?\n");
				return STA_NOINIT;
			}
		}
		{ // "CMD8" queries the voltage range. If this fails by saying that the command is invalid, then it suggests that the SD card older than v2.0.
			u8 response = _sd_transmit_command(8, 0x1AA); // `0x1AA` is the argument for CMD8 that should be echoed back.
			if (response != SD_SPI_IDLE_FLAG)
			{
				_sd_print_response_breakdown(8, response);
				return STA_NOINIT;
			}
			spi_receive_byte(); // The SD card sends back some data, but we're ignoring it for simplicity.
			spi_receive_byte(); // For robustness, this should be checked and evaluated.
			spi_receive_byte();
			spi_receive_byte();
		}
		{ // "ACMD41" initializes the SD card out of idle state. Only supported on SDCs.
			u8  response;
			u16 attempts_left = SD_MAX_ATTEMPTS;
			do
			{
				response = _sd_transmit_command(55, 0); // "CMD55" that is sent before any "ACMD" command.
				if (response & ~SD_SPI_IDLE_FLAG)
				{
					_sd_print_response_breakdown(55, response);
					return STA_NOINIT;
				}

				response = _sd_transmit_command(41, 1UL << 30); // Bit 30 ("HCS") indicates support for high-capacity cards.
				if (response & ~SD_SPI_IDLE_FLAG)
				{
					_sd_print_response_breakdown(-41, response);
					return STA_NOINIT;
				}

				attempts_left -= 1;
			}
			while ((response & SD_SPI_IDLE_FLAG) && attempts_left);
			if (response & SD_SPI_IDLE_FLAG)
			{
				uart_send_pstr("ACMD41 took too long to initialize the SD card.\n");
				return STA_NOINIT;
			}
		}
		{ // "CMD9" reads the SD card's CSD register which contains useful information like the block size.
			u8 response = _sd_transmit_command(9, 0);
			if (response)
			{
				_sd_print_response_breakdown(9, response);
				return STA_NOINIT;
			}

			u8 csd_register[16];
			response = _sd_receive_data_block(csd_register, countof(csd_register));
			if (response == SD_SPI_START_TOKEN)
			{
				u8 max_read_data_block_length = csd_register[countof(csd_register) - 1 - 80 / 8] & 0xF; // Grabs bits in interval [80, 84) which should have the value `SD_SECTOR_SIZE_LOG2` which is commonly 512 bytes.
				if (max_read_data_block_length != SD_SECTOR_SIZE_LOG2)
				{
					uart_send_pstr("Expecting maximum read data-block length of 512 bytes, instead got 2^");
					uart_send_u64(max_read_data_block_length);
					uart_send_pstr(".\n");
					return STA_NOINIT;
				}
			}
			else
			{
				uart_send_pstr("Failed to get data-block of the CSD register for CMD9 (received response of `0b");
				uart_send_b8(response);
				uart_send_pstr("`).\n");
				return STA_NOINIT;
			}
		}

		_sd_inited = true;

		set_pin(SD_SLAVE_SELECT_PIN, PinState_output_high); // Release SD card from SPI channel.

		return 0;
	}
	else
	{
		return STA_NOINIT;
	}
}

DRESULT
disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
	if (pdrv == 0)
	{
		for (UINT i = 0; i < count; i += 1)
		{
			u8 response = _sd_read(buff + SD_SECTOR_SIZE * i, sector + i);
			if (response != SD_SPI_START_TOKEN)
			{
				uart_send_pstr("Failed to read data-block of SD contents for CMD17 (received response of `0b");
				uart_send_b8(response);
				uart_send_pstr("`).\n");
				return RES_ERROR;
			}
		}
		return RES_OK;
	}
	else
	{
		return RES_NOTRDY;
	}
}

DRESULT
disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
	if (pdrv == 0)
	{
		for (UINT i = 0; i < count; i += 1)
		{
			u8 response = _sd_write(buff + SD_SECTOR_SIZE * i, sector + i); // CMD25 to write multiple data-blocks could be used here instead.
			if (response)
			{
				uart_send_pstr("Failed to write data-block of SD contents for CMD24 (received response of `0b");
				uart_send_b8(response);
				uart_send_pstr("`)\n");
				return RES_ERROR;
			}
		}

		return RES_OK;
	}

	return RES_NOTRDY;
}

DRESULT
disk_ioctl(BYTE pdrv, BYTE cmd, void* buff)
{
	if (pdrv == 0)
	{
		switch (cmd)
		{
			case CTRL_SYNC:
			{
				return RES_OK; // The SD card does need to be flushed.
			} break;

			case GET_SECTOR_COUNT:
			{
				return RES_PARERR; // `FF_USE_MKFS` should be defined to be `0` (by default), so this command should never be called for.
			} break;

			case GET_SECTOR_SIZE:
			{
				*(WORD*) buff = SD_SECTOR_SIZE;
				return RES_OK;
			} break;

			case CTRL_TRIM:
			{
				return RES_PARERR; // `FF_USE_TRIM` should be defined to be `0` (by default), so this command should never be called for.
			} break;

			default:
			{
				return RES_PARERR;
			} break;
		}
	}

	return RES_NOTRDY;
}

DWORD
get_fattime(void)
{
	return 0; // We don't care about time-stamps.
}
