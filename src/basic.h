#pragma once
#define false       0
#define true        1
#define countof(XS) (sizeof(XS)/sizeof((XS)[0]))
#define STRINGIFY_(X) #X
#define STRINGIFY(X)  STRINGIFY_(X)
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint8_t  bool8;
typedef uint16_t bool16;
typedef uint32_t bool32;
typedef uint64_t bool64;

static u8* // The longest unsigned integer is "18446744073709551615" which is 20 characters long. A buffer of atleast 21 characters is needed to represent all integers and include null.
cstr_of_u64(u64 value, u8* buffer, i8 max_length)
{
	u8* beginning = buffer + max_length;

	if (max_length >= 1)
	{
		beginning    -= 1;
		beginning[0]  = '\0';

		u64 current = value;
		while (beginning > buffer)
		{
			beginning    -= 1;
			beginning[0]  = '0' + current % 10;
			current       /= 10;
			if (!current)
			{
				break;
			}
		}
	}

	return beginning;
}

static u8* // Assuming two's complement, the longest signed integer is "-9223372036854775808" which is 20 characters long. A buffer of atleast 21 characters is needed to represent all integers and include null.
cstr_of_i64(i64 value, u8* buffer, i8 max_length)
{
	if (value < 0)
	{
		u8* beginning = cstr_of_u64((~(u64) value) + 1, buffer, max_length);

		if (beginning > buffer)
		{
			beginning    -= 1;
			beginning[0]  = '-';
		}

		return beginning;
	}
	else
	{
		return cstr_of_u64(value, buffer, max_length);
	}
}

static i8
sign_i8(i8 x)
{
	return x < 0 ? -1 : x > 0 ? 1 : 0;
}
