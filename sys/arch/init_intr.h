/*
 * This file has been modified as part of the FreeMiNT project. See
 * the file Changes.MH for details and dates.
 */

# ifndef _arch_init_intr_h
# define _arch_init_intr_h

# include "mint/mint.h"
# include "mint/emu_tos.h"
# include "arch/intr.h"

extern KBDVEC *kbdvecs;

vector_handler_t install_vector(vector_handler_t *vector, vector_handler_t new_handler);
void    clear_caches_for_changed_vector(vector_handler_t *vector, vector_handler_t old_handler);
void	install_TOS_vectors	(void);
void	restore_TOS_vectors	(void);
long    _cdecl register_trap2(long _cdecl (*dispatch)(void *), int mode, int flag, long extra);

# endif /* _arch_init_intr_h */
