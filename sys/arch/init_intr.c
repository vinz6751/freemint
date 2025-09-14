/*
 * This file belongs to FreeMiNT. It's not in the original MiNT 1.12
 * distribution. See the file CHANGES for a detailed log of changes.
 * 
 * Copyright 1990,1991,1992 Eric R. Smith.
 * Copyright 1992,1993,1994 Atari Corporation.
 * All rights reserved.
 * 
 * Please send suggestions, patches or bug reports to
 * the MiNT mailing list.
 * 
 */

# include "mint/mint.h"

# include "mint/asm.h"
# include "mint/xbra.h"

# include "arch/acia.h"
# include "arch/cpu.h"		/* cpush() */
# include "arch/intr.h"		/* old_xxx */
# include "arch/kernel.h"	/* enter_gemdos() */
# include "arch/syscall.h"	/* new_xxx */
# include "arch/tosbind.h"	/* TRAP_xxx() */
# include "arch/tos_vars.h" /* Address of TOS variables */

# include "arch/init_intr.h"

# include "global.h"
# include "cookie.h"
# include "keyboard.h"		/* has_kbdvec */

/* magic number to show that we have captured the reset vector */
# define RES_MAGIC	0x31415926L

/* structures for keyboard/MIDI interrupt vectors */
KBDVEC *kbdvecs;
static long old_kbdvec;
static KBDVEC old_kbdvecs;
static void hook_keyboard_vectors(void);
static void unhook_keyboard_vectors(void);

long old_etv_erm;
long old_resval;	/* old reset validation */
long old_drvbits;	/* BIOS drive map */

/* This is a list of vectors and their new handlers, to be hook with XBRA */
static const struct {
	vector_handler_t *vector;
	vector_handler_t handler;
} vectors_with_xbra[] = {
	{ (vector_handler_t*)VEC_BUS_ERROR, new_bus },
	{ (vector_handler_t*)VEC_ADDRESS_ERROR, new_addr },
	{ (vector_handler_t*)VEC_ILLEGAL_INSTRUCTION, new_ill },
	{ (vector_handler_t*)VEC_DIVISION_BY_ZERO, new_divzero },
	{ (vector_handler_t*)VEC_TRACE, new_trace },
	// VEC_LINE_F is conditioned
	{ (vector_handler_t*)VEC_CHK, new_chk },
	{ (vector_handler_t*)VEC_TRAPV, new_trapv },
	{ (vector_handler_t*)VEC_MMU_CONFIG_ERROR, new_mmuconf},
	{ (vector_handler_t*)VEC_MMU1, new_mmu },
	{ (vector_handler_t*)VEC_MMU2, new_pmmuacc },
	{ (vector_handler_t*)VEC_FORMAT_ERROR, new_format },
	{ (vector_handler_t*)VEC_COPRO_PROTOCOL_VIOLATION, new_cpv },
	{ (vector_handler_t*)VEC_UNINITIALIZED, new_uninit },
	{ (vector_handler_t*)VEC_SPURIOUS_INTERRUPT, new_spurious },
	{ (vector_handler_t*)TRAP0, unused_trap },
	{ (vector_handler_t*)TRAP1, mint_dos },		/* GEMDOS */
#if 0
	/* GEM, is hooked on request */
	{ (vector_handler_t*)TRAP2, unused_trap },
#endif
	{ (vector_handler_t*)TRAP3, unused_trap },
	{ (vector_handler_t*)TRAP4, unused_trap },
	{ (vector_handler_t*)TRAP5, unused_trap },
	{ (vector_handler_t*)TRAP6, unused_trap },
	{ (vector_handler_t*)TRAP7, unused_trap },
	{ (vector_handler_t*)TRAP8, unused_trap },
	{ (vector_handler_t*)TRAP9, unused_trap },
	{ (vector_handler_t*)TRAP10, unused_trap },
	{ (vector_handler_t*)TRAP11, unused_trap },
	{ (vector_handler_t*)TRAP12, unused_trap },
	{ (vector_handler_t*)TRAP13, mint_bios },	/* BIOS   */
	{ (vector_handler_t*)TRAP14, mint_xbios }, 	/* XBIOS  */
#if 0
	/* is used by NVDI 5.00 which isn't even polite enough to link it with XBRA */
	{ (vector_handler_t*)TRAP15, unused_trap },
#endif
	{ (vector_handler_t*)ETV_CRITIC, (vector_handler_t)new_criticerr },
	{ (vector_handler_t*)HDV_RW, (vector_handler_t)new_mediach },
	{ (vector_handler_t*)HDV_MEDIACH, (vector_handler_t)new_rwabs },
	{ (vector_handler_t*)HDV_BPB, (vector_handler_t)new_getbpb }
};

/* This is a list of vectors and where the old handler should be saved */
static const struct {
	vector_handler_t *vector;
	vector_handler_t *save_to;
} math_copro_vectors[] = {
	{ (vector_handler_t*)VEC_FFCP0, &old_fpcp_0 },
	{ (vector_handler_t*)VEC_FFCP1, &old_fpcp_1 },
	{ (vector_handler_t*)VEC_FFCP2, &old_fpcp_2 },
	{ (vector_handler_t*)VEC_FFCP3, &old_fpcp_3 },
	{ (vector_handler_t*)VEC_FFCP4, &old_fpcp_4 },
	{ (vector_handler_t*)VEC_FFCP5, &old_fpcp_5 },
	{ (vector_handler_t*)VEC_FFCP6, &old_fpcp_6 }
};


void
clear_caches_for_changed_vector(vector_handler_t *vector, vector_handler_t old_handler)
{
# ifndef M68000
	cpush ((long *) vector, sizeof (vector)); 
	cpush ((long *) old_handler, sizeof (old_handler));
# endif
}

vector_handler_t
install_vector(vector_handler_t *vector, vector_handler_t new_handler)
{
	vector_handler_t old_handler;

	old_handler = *(vector_handler_t *)vector;
	*(vector_handler_t *)vector = new_handler;

	clear_caches_for_changed_vector(vector, old_handler);

	return *old_handler;
}


/*
 * initialize all interrupt vectors and new trap routines
 * we also get here any TOS variables that we're going to change
 * (e.g. the pointer to the cookie jar) so that rest_intr can
 * restore them.
 */

void
install_TOS_vectors (void)
{
	ushort savesr;
	int i;

	hook_keyboard_vectors();

	/* Documentation says that we should set etv_term using Setexc (this is to give
	 * the OS a chance to maintain it per-program). We need to save it now, before we
	 * hook TRAP #13 */
	old_etv_erm = (long) TRAP_Setexc (ETV_TERM/4, -1UL);

	savesr = splhigh();

	/* Take all traps; notice, that the "old" address for any trap
	 * except #1, #13 and #14 (GEMDOS, BIOS, XBIOS) is lost.
	 * It does not matter, because MiNT does not pass these
	 * calls along anyway.
	 * The main problem is, that activating this makes ROM VDI
	 * no more working, and actually any program that takes a
	 * trap before MiNT is loaded.
	 */

	for (i=0; i<ARRAY_SIZE(vectors_with_xbra); i++)
		xbra_hook (vectors_with_xbra[i].vector, vectors_with_xbra[i].handler);

	if (tosvers >= 0x106)
		xbra_hook ((vector_handler_t*)VEC_LINE_F, new_linef);

	/* We used xbra_hook(trapX, unused_trap) on several traps and we don't care about
	 * the "old vector" (which got overwritten anyway so is likely somewhat undefined)
	 * so we reset it to what it should be. TRAP0 is as good as any other unused trap.
	 */
	xbra_get((vector_handler_t*)TRAP0)->old_handler = v_rte;

	/* Math coprocessor exceptions. These share the same handler but have different
	 * "old handler"s so we can't use xbra_hook. */
	for (i=0; i<ARRAY_SIZE(math_copro_vectors); i++)
		*(math_copro_vectors[i].save_to) = install_vector (math_copro_vectors[i].vector, new_fpcp);

	/* Hook the 200 Hz system timer. Our handler will do its job,
	 * then call the previous handler. Every four interrupts, our handler will
	 * push a fake additional exception stack frame, so when the previous
	 * 200 Hz handler returns with RTE, it will actually call _mint_vbl
	 * to mimic a 50 Hz VBL interrupt.
	 */

	xbra_hook ((vector_handler_t*)p5msvec, mint_5ms);

#if 0	/* this should really not be necessary ... rincewind */
	install_vector (&old_resvec, 0x042aL, reset);
	old_resval = *((long *)0x426L);
	*((long *) 0x426L) = RES_MAGIC;
#endif

	spl (savesr);

	old_drvbits = *((long *) _DRVBITS);

	/* we'll be making GEMDOS calls */
	enter_gemdos ();
}

/* restore all interrupt vectors and trap routines
 *
 * NOTE: This is *not* the approved way of unlinking XBRA trap handlers.
 * Normally, one should trace through the XBRA chain. However, this is
 * a very unusual situation: when MiNT exits, any TSRs or programs running
 * under MiNT will no longer exist, and so any vectors that they have
 * caught will be pointing to never-never land! So we do something that
 * would normally be considered rude, and restore the vectors to
 * what they were before we ran.
 * BUG: we should restore *all* vectors, not just the ones that MiNT caught.
 */

void
restore_TOS_vectors (void)
{
	ushort savesr;
	int i;

	savesr = splhigh();

	unhook_keyboard_vectors();

	for (i=0; i<ARRAY_SIZE(vectors_with_xbra); i++)
		xbra_unhook(vectors_with_xbra[i].vector);

	if (old_linef)
		xbra_unhook((vector_handler_t*)VEC_LINE_F);

#if 0
	*((long *) RESVALID) = old_resval;
	*((long *) RESVECTOR) = old_resvec;
#endif

	*((long *) _DRVBITS) = old_drvbits;

	spl (savesr);
}


long _cdecl
register_trap2(long _cdecl (*dispatch)(void *), int mode, int flag, long extra)
{
	long _cdecl (**handler)(void *) = NULL;
	long *x = NULL;
	long ret = EINVAL;

	DEBUG(("register_trap2(0x%p, %i, %i)", dispatch, mode, flag));

	if (flag == 0)
	{
		handler = &aes_handler;
	}
	else if (flag == 1)
	{
		handler = &vdi_handler;
		x = &gdos_version;
	}

	if (mode == 0)
	{
		/* install */

		if (*handler == NULL)
		{
			DEBUG(("register_trap2: installing handler at 0x%p", dispatch));

			*handler = dispatch;
			if (x)
				*x = extra;
			ret = 0;

			/* if trap #2 is not active install it now */
			if (old_trap2 == 0)
				xbra_hook((vector_handler_t*)TRAP2, mint_trap2); /* trap #2, GEM */
		}
	}
	else if (mode == 1)
	{
		/* deinstall */

		if (*handler == dispatch)
		{
			DEBUG(("register_trap2: removing handler at 0x%p", dispatch));

			*handler = NULL;
			if (x)
				*x = 0;
			ret = 0;
		}
	}

	return ret;
}


static void
hook_keyboard_vectors(void)
{
	ushort savesr;

	kbdvecs = (KBDVEC *) TRAP_Kbdvbase ();
	old_kbdvecs = *kbdvecs; /* structure copy */

#ifndef NO_AKP_KEYBOARD
	if (!has_kbdvec) /* TOS versions without the KBDVEC vector */
	{
		/* We need to hook the ikbdsys vector. Our handler will have to deal
		 * with ACIA registers, and to call the appropriate KBDVEC vectors
		 * for keyboard, mouse, joystick, status and time packets. */
		savesr = splhigh();
		kbdvecs->ikbdsys = (long)ikbdsys_handler;
# ifndef M68000
		cpush(&kbdvecs->ikbdsys, sizeof(long));
# endif
		spl(savesr);
	}
	else
	{
		/* Hook the keyboard interrupt to call ikbd_scan() on keyboard data.
		 * There is an undocumented vector just before the KBDVEC structure.
		 * This vector is called by the TOS ikbdsys routine to process
		 * keyboard-only data. It is exactly what we need to hook.
		 * TOS < 2.00 doesn't know about this vector but the new ikdsys
		 * handler hooked above if we're running over TOS < 2.00 will call it.
		 */
		vector_handler_t *kbdvec = ((vector_handler_t *)kbdvecs)-1;
		xbra_hook (kbdvec, (vector_handler_t)kbdvec_handler);
	}

	/* Workaround for FireTOS and CT60 TOS 2.xx.
	 * Needed because those TOS doesn't call the undocumented kbdvec vector
	 * from their ikbdsys vector handler, besides they install the ikbdsys
	 * routine as a ACIA interrupt handler, so we can't simply replace their
	 * ikbdsys handler by ours. We need to hook a new ACIA handler which
	 * will call our ikbdsys.
	 */
	unsigned short version = 0;
# ifdef __mcoldfire__
	const unsigned short *FT_TOS_VERSION_ADDR = (unsigned short *)0x00e80000;
	if (coldfire_68k_emulation)
		version = *FT_TOS_VERSION_ADDR;
# else
	const unsigned short *CT60_TOS_VERSION_ADDR = (unsigned short *)0xffe80000;
	if (machine == machine_ct60)
		version = *CT60_TOS_VERSION_ADDR;
# endif
	if (version >= 2)
	{
		savesr = splhigh();
		kbdvecs->ikbdsys = (long)ikbdsys_handler;
		cpush(&kbdvecs->ikbdsys, sizeof(long));
		xbra_hook((vector_handler_t*)0x0118L, (vector_handler_t)new_acia);
		spl(savesr);
	}
#endif /* NO_AKP_KEYBOARD */
}


static void
unhook_keyboard_vectors(void)
{
	*kbdvecs = old_kbdvecs;	/* restore keyboard vectors (structure copy) */

#ifndef NO_AKP_KEYBOARD
	if (tosvers < 0x0200)
	{
		*((long *) 0x0118L) = old_acia;
	}
	else
	{
		long *kbdvec = ((long *)kbdvecs)-1;
		*kbdvec = (long) old_kbdvec;
	}
#endif
}

/* EOF */
