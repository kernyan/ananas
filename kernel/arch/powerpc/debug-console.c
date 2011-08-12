#include <ananas/types.h>
#include <ananas/debug-console.h>
#include <ananas/console.h>
#include <ananas/wii/usbgecko.h>
#include <ofw.h>
#include "options.h"

void usbgecko_putch(char ch);

void
debugcon_init()
{
#ifdef OPTION_WII
	usbgecko_init();
#endif
}

void
debugcon_putch(int ch)
{
#ifdef OPTION_OFW
	if (console_tty == NULL)
		ofw_putch(ch);
#endif
#ifdef OPTION_WII
	usbgecko_putch(ch);
#endif
}

int
debugcon_getch()
{
	return 0;
}

/* vim:set ts=2 sw=2: */
