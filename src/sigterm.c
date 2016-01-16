/* > sigter.c
 *
 */

#include <signal.h>
#include "extern.h"

/*
 * Used to tell the main() code to exit gracefully.
 */

volatile sig_atomic_t _running = 1;

void sigterm_handler(int arg)
{
	_running = 0;
}
