// Refer to:
// - https://web.archive.org/web/20221001174157/https://www.xanthium.in/how-to-avr-atmega328p-microcontroller-usart-uart-embedded-programming-avrgcc
// - "ATmega2560 Datasheet" ("(pg. N)" refers to page number `N` of this resource)

static void
init_uart(void)
{
	UBRR0   = 103;        // UBRR Stands for "USART Baud Rate Register" (pg. 202). Consult "WormFood's AVR Baud Rate" for 16MHz of 9600b/s for the magic number.
	UCSR0B |= 1 << TXEN0; // Enables transmission (pg. 234). 8N1 format is used by default (i.e. 8 data bits, no parity bit, 1 stop bit) (pg. 221).
}

static void
uart_send_byte(u8 value)
{
	while (!(UCSR0A & (1 << UDRE0))); // The UDRE0 bit indicates that we are able to transmit data (pg. 233).
	UDR0 = value;                     // Register to populate the new data (pg. 218).
}

static void
uart_send_bytes(u8* buffer, u16 amount)
{
	for (u16 i = 0; i < amount; i += 1)
	{
		uart_send_byte(buffer[i]);
	}
}

static void
uart_send_cstr(const u8* value)
{
	for (u8 i = 0; value[i]; i += 1)
	{
		uart_send_byte(value[i]);
	}
}

#define uart_send_pstr(STRLIT) uart_send_pstr_nonliteral(PSTR(STRLIT))
static void
uart_send_pstr_nonliteral(const char* value)
{
	for (u8 i = 0; pgm_read_byte(&value[i]); i += 1)
	{
		uart_send_byte(pgm_read_byte(&value[i]));
	}
}

static void
uart_send_u64(u64 value)
{
	u8 buffer[21];
	uart_send_cstr(cstr_of_u64(value, buffer, countof(buffer)));
}

static void
uart_send_i64(i64 value)
{
	u8 buffer[21];
	uart_send_cstr(cstr_of_i64(value, buffer, countof(buffer)));
}

static void
uart_send_b8(u8 value)
{
	for (i8 i = 0; i < 8; i += 1)
	{
		uart_send_byte('0' + ((value >> (7 - i)) & 1));
	}
}

static void
uart_send_h8(u8 value)
{
	uart_send_byte((value >>   4) < 10 ? '0' + (value >>   4) : 'A' + ((value >>   4) - 10));
	uart_send_byte((value &  0xF) < 10 ? '0' + (value &  0xF) : 'A' + ((value &  0xF) - 10));
}
