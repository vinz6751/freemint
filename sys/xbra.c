/* Helper function to register FreeMiNT functions to a vector using the XBRA convention.
 * Licence:Public domain
 */

#include "mint/xbra.h"

long xbra_hook(long *vector, void _cdecl (*new_handler)()) 
{
	long old_handler;

	/* Detect if the new_handler complies with the XBRA convention and if so,
	 * automatically save the old handler. */
	old_handler = *vector;
	 *vector = (long*)new_handler;
	if (vector[-3] == XBRA_MAGIC && vector[-2] == MINT_MAGIC)
		vector[-1] = *old_handler;

	return old_vector;
}


void xbra_unhook(long *vector)
{
	if (vector[-3] == XBRA_MAGIC && vector[-2] == MINT_MAGIC)
		*vector = vector[-1];
}
