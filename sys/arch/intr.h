/*
 * This file belongs to FreeMiNT.  It's not in the original MiNT 1.12
 * distribution.  See the file Changes.MH for a detailed log of changes.
 */

# ifndef _m68k_intr_h
# define _m68k_intr_h

# include "mint/mint.h"

typedef void _cdecl (*vector_handler_t)(void);

/* interrupt vectors linked by install_vector() */
extern long old_linef;
extern vector_handler_t old_fpcp_0;
extern vector_handler_t old_fpcp_1;
extern vector_handler_t old_fpcp_2;
extern vector_handler_t old_fpcp_3;
extern vector_handler_t old_fpcp_4;
extern vector_handler_t old_fpcp_5;
extern vector_handler_t old_fpcp_6;

extern vector_handler_t old_exec_os;

# if 0
extern long 	*intr_shadow;
# endif

long _cdecl	reset		(void);
void _cdecl	reboot		(void) NORETURN;
void _cdecl	newmvec		(void);
void _cdecl	newjvec		(void);
long _cdecl	kbdvec_handler		(void);
void _cdecl	kbdclick	(short scancode);
long _cdecl	new_rwabs	(void);
long _cdecl	new_mediach	(void);
long _cdecl	new_getbpb	(void);

void _cdecl	send_packet	(long func, char *buf, char *bufend);

# endif /* _m68k_intr_h */
