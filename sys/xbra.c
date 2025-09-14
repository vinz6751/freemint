/* Helper function to register FreeMiNT functions to a vector using the XBRA convention.
 * Licence: Public domain
 */

#include "mint/xbra.h"


xbra_t *xbra_get(const vector_handler_t *vector)
{
	xbra_t *xbra = &((xbra_t*)*vector)[-1];
	return (xbra->magic == XBRA_MAGIC && xbra->id == MINT_MAGIC) ? xbra : 0L;
}


vector_handler_t xbra_hook(vector_handler_t *vector, vector_handler_t new_handler) 
{
	xbra_t *xbra = xbra_get(&new_handler);

	xbra->old_handler = *vector;
	*vector = new_handler;

	clear_caches_for_changed_vector(vector, xbra->old_handler);

	return xbra->old_handler;
}


void xbra_unhook(vector_handler_t *vector)
{
	vector_handler_t current_handler = *vector;
	*vector = xbra_get(vector)->old_handler;

	clear_caches_for_changed_vector(vector, current_handler);
}
