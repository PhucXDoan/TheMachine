// Refer to:
// - "ATmega2560 Datasheet" ("(pg. N)" refers to page number `N` of this resource)

static volatile u32 _timer_ms = 0; // Overflows after ~49.71 days.

ISR (TIMER0_OVF_vect) // Interrupt for when Timer0 overflows (pg. 101).
{
	// The ATmega2560 has a 16MHz crystal clock which would increment the timer after 64 ticks (this is the prescaler value).
	// Since Timer0 is an 8-bit timer, it overflows after 256 increments, to which this interrupt is triggered.
	// Therefore, the amount of time that has passed can be calculated as so:
	//     2^clock_bit_width * prescaler / F_CPU = 256 * 64 / 16,000,000Hz = 1.024ms
	// To make this aligned to milliseconds, the clock will need to begin slightly incremented already. Starting at 6 does the trick.
	//     (256 - 6) * 64 / 16,000,000Hz = 1ms
	_timer_ms += 1;
	TCNT0      = 6;
}

static void
init_timer(void)
{
	TCNT0  = 6;                         // See `ISR (TIMER0_OVF_vect)` for magic number.
	TCCR0B = (1 << CS01) | (1 << CS00); // Sets the prescaler to 64 in the "Timer/Counter Control Register B" (pg. 130).
	TIMSK0 = (1 << TOIE0);              // Enables the overflow interrupt of Timer0 in the "Timer/Counter Interrupt Mask Register" (pg. 131).
	sei();                              // Hardware call to enable global interrupts (pg. 13).
}

static u32
get_ms(void)
{
	cli();
	u32 ms = _timer_ms;
	sei();
	return ms;
}
