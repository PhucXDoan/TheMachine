@echo off
setlocal EnableDelayedExpansion

set ATmega2560_COM=3
set ATmega32U4_COM=4
set ATmega32U4_bootloader_COM=5
set WARNINGS= ^
	-Werror -Wall -Wextra -Wpedantic -Wwrite-strings -fmax-errors=1 ^
	-Wno-unused-label -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function -Wno-unused-but-set-variable

if not exist W:\build\ (
	mkdir W:\build\
)

pushd W:\build\
	for /f "tokens=2delims=COM:" %%i in ('mode ^| findstr /RC:"\C\O\M[0-9*]"') do set "com=%%i"

	avr-gcc %WARNINGS% -Os -DF_CPU=16000000 -mmcu=atmega2560 -I W:\deps\FatFs\source\ -c W:\src\ATmega2560_TheMachine.c
	if !ERRORLEVEL! neq 0 (
		goto ABORT
	)

REM	avr-gcc %WARNINGS% -Os -DF_CPU=16000000 -mmcu=atmega32u4 -c W:\src\ATmega32U4_TheMachine.c
REM	if !ERRORLEVEL! neq 0 (
REM		goto ABORT
REM	)

	if !com! == !ATmega2560_COM! (
		avr-gcc -DF_CPU=16000000 -mmcu=atmega2560 -o ATmega2560_TheMachine.elf ATmega2560_TheMachine.o
		if !ERRORLEVEL! neq 0 (
			goto ABORT
		)

		avr-objcopy -O ihex ATmega2560_TheMachine.elf ATmega2560_TheMachine.hex
		if !ERRORLEVEL! neq 0 (
			goto ABORT
		)

		avrdude -V -p atmega2560 -c wiring -P COM%ATmega2560_COM% -D -Uflash:w:ATmega2560_TheMachine.hex
		if !ERRORLEVEL! neq 0 (
			goto ABORT
		)

		start C:\code\misc\PuTTY\putty.exe -load "Default Settings"

REM	) 	else if !com! == !ATmega32U4_bootloader_COM! (
REM		avr-gcc -DF_CPU=16000000 -mmcu=atmega32u4 -o ATmega32U4_TheMachine.elf ATmega32U4_TheMachine.o
REM		if !ERRORLEVEL! neq 0 (
REM			goto ABORT
REM		)
REM
REM		avr-objcopy -O ihex ATmega32U4_TheMachine.elf ATmega32U4_TheMachine.hex
REM		if !ERRORLEVEL! neq 0 (
REM			goto ABORT
REM		)
REM
REM		avrdude -V -p atmega32u4 -c avr109 -b 57900 -P COM%ATmega32U4_bootloader_COM% -D -Uflash:w:ATmega32U4_TheMachine.hex
REM		if !ERRORLEVEL! neq 0 (
REM			goto ABORT
REM		)
REM
REM		REM start C:\code\misc\PuTTY\putty.exe -load "Default Settings"
	) else (
		echo No recognized MCU COM found, or it is currently being used.
	)

	:ABORT
	del *.o *.elf > nul 2>&1
popd W:\build\
