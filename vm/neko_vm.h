/* ************************************************************************ */
/*																			*/
/*  Neko Virtual Machine													*/
/*  Copyright (c)2005 Motion-Twin											*/
/*																			*/
/* This library is free software; you can redistribute it and/or			*/
/* modify it under the terms of the GNU Lesser General Public				*/
/* License as published by the Free Software Foundation; either				*/
/* version 2.1 of the License, or (at your option) any later version.		*/
/*																			*/
/* This library is distributed in the hope that it will be useful,			*/
/* but WITHOUT ANY WARRANTY; without even the implied warranty of			*/
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU		*/
/* Lesser General Public License or the LICENSE file for more details.		*/
/*																			*/
/* ************************************************************************ */
#ifndef _NEKO_VM_H
#define _NEKO_VM_H
#include "neko.h"

typedef void (*neko_printer)( const char *data, int size, void *param );
typedef void (*thread_main_func)( void *param );

typedef struct _neko_vm neko_vm;

typedef void (*neko_stat_func)( neko_vm *vm, const char *kind, int start );

C_FUNCTION_BEGIN

EXTERN void neko_global_init();
EXTERN void neko_global_free();
EXTERN void neko_gc_major();
EXTERN void neko_gc_loop();
EXTERN void neko_gc_stats( int *heap, int *free );
EXTERN int neko_thread_create( thread_main_func init, thread_main_func main, void *param, void **handle );
EXTERN void neko_thread_blocking( thread_main_func f, void *p );

EXTERN neko_vm *neko_vm_alloc( void *unused );
EXTERN neko_vm *neko_vm_current();
EXTERN value neko_exc_stack( neko_vm *vm );
EXTERN value neko_call_stack( neko_vm *vm );
EXTERN void *neko_vm_custom( neko_vm *vm, vkind k );
EXTERN void neko_vm_set_custom( neko_vm *vm, vkind k, void *v );
EXTERN value neko_vm_execute( neko_vm *vm, void *module );
EXTERN void neko_vm_select( neko_vm *vm );
EXTERN int neko_vm_jit( neko_vm *vm, int enable_jit );
EXTERN value neko_default_loader( char **argv, int argc );
EXTERN void neko_vm_redirect( neko_vm *vm, neko_printer print, void *param );
EXTERN void neko_vm_set_stats( neko_vm *vm, neko_stat_func fstats, neko_stat_func pstats );

EXTERN int neko_is_big_endian();

C_FUNCTION_END

#endif
/* ************************************************************************ */
