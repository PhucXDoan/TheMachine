static void
init_spi(void)
{
	//
	// Initialize SPI.
	//

	set_pin(SPI_SLAVE_SELECT_PIN       , PinState_output_low);  // Must be driven high or as an output for the SPI to be set as master properly (pg. 195).
	set_pin(SPI_MASTER_OUT_SLAVE_IN_PIN, PinState_output_low);  // Set as output according to the example (pg. 193).
	set_pin(SPI_SERIAL_CLOCK_PIN       , PinState_output_low);  // Set as output according to the example (pg. 193).

	SPCR |=                        // "SPI Control Register"  (pg. 197).
		(1 << SPE)  |              // "SPI Enable"            (pg. 197).
		(1 << MSTR) |              // "Master/Slave Select"   (pg. 197).
		(1 << SPR1) | (0 << SPR0); // "SPI Clock Rate Select" (pg. 198). The speed is later set to be doubled by the "Double SPI Speed Bit". Frequency should be between 100kHz and 400kHz for the SD card ("elm-chan").

	SPSR |=                        // "SPI Status Register"   (pg. 198).
		(1 << SPI2X);              // "Double SPI Speed Bit"  (pg. 198)
}

static void
spi_transmit_byte(u8 value)
{
	SPDR = value;                  // Writing to the "SPI Data Register" triggers a transmission (pg. 199).
	while (!(SPSR & (1 << SPIF))); // "SPI Interrupt Flag" bit is set when the serial transmission is completed (pg. 198).
}

static u8
spi_receive_byte(void)
{
	spi_transmit_byte(0xFF); // Dummy byte to set MOSI high the entire time.
	return SPDR;
}
