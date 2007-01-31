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
#include <string.h>
#include "neko.h"
#include "objtable.h"
#include "vm.h"
#include "neko_mod.h"

#define MAXCALLS	350

typedef value (*c_prim0)();
typedef value (*c_prim1)(value);
typedef value (*c_prim2)(value,value);
typedef value (*c_prim3)(value,value,value);
typedef value (*c_prim4)(value,value,value,value);
typedef value (*c_prim5)(value,value,value,value,value);
typedef value (*c_primN)(value*,int);
typedef value (*jit_prim)( neko_vm *, void *, value, neko_module * );

extern void neko_setup_trap( neko_vm *vm );
extern void neko_process_trap( neko_vm *vm );
extern int neko_stack_expand( int_val *sp, int_val *csp, neko_vm *vm );
extern char *jit_boot_seq;

EXTERN value val_callEx( value vthis, value f, value *args, int nargs, value *exc ) {
	neko_vm *vm = NEKO_VM();
	value old_this = vm->vthis;
	value old_env = vm->env;
	value ret = val_null;
	jmp_buf oldjmp;
	if( ((int_val)&vm) < (int_val)vm->c_stack_max )
		val_throw(alloc_string("C Stack Overflow"));
	if( vthis != NULL )
		vm->vthis = vthis;
	if( exc ) {
		memcpy(&oldjmp,&vm->start,sizeof(jmp_buf));
		if( setjmp(vm->start) ) {
			*exc = vm->vthis;
			neko_process_trap(vm);
			vm->vthis = old_this;
			vm->env = old_env;
			memcpy(&vm->start,&oldjmp,sizeof(jmp_buf));
			return val_null;
		}
		neko_setup_trap(vm);
	}
	if( val_is_int(f) )
		val_throw(alloc_string("Invalid call"));
	if( val_tag(f) == VAL_PRIMITIVE ) {
		vm->env = ((vfunction *)f)->env;
		if( nargs == ((vfunction*)f)->nargs ) {
			if( nargs > CALL_MAX_ARGS )
				failure("Too many arguments for a call");
			switch( nargs ) {
			case 0:
				ret = ((c_prim0)((vfunction*)f)->addr)();
				break;
			case 1:
				ret = ((c_prim1)((vfunction*)f)->addr)(args[0]);
				break;
			case 2:
				ret = ((c_prim2)((vfunction*)f)->addr)(args[0],args[1]);
				break;
			case 3:
				ret = ((c_prim3)((vfunction*)f)->addr)(args[0],args[1],args[2]);
				break;
			case 4:
				ret = ((c_prim4)((vfunction*)f)->addr)(args[0],args[1],args[2],args[3]);
				break;
			case 5:
				ret = ((c_prim5)((vfunction*)f)->addr)(args[0],args[1],args[2],args[3],args[4]);
				break;
			}
		} else if( ((vfunction*)f)->nargs == -1 )
			ret = (value)((c_primN)((vfunction*)f)->addr)(args,nargs);
		else
			val_throw(alloc_string("Invalid call"));		
		if( ret == NULL )
			val_throw( (value)((vfunction*)f)->module );		
	} else if( (val_tag(f)&7) == VAL_FUNCTION ) {
		if( nargs == ((vfunction*)f)->nargs )  {
			int n;
			if( vm->csp + 4 >= vm->sp - nargs && !neko_stack_expand(vm->sp,vm->csp,vm) ) {
				if( exc ) {
					neko_process_trap(vm);
					memcpy(&vm->start,&oldjmp,sizeof(jmp_buf));	
				}
				failure("Stack Overflow");
			} else {
				for(n=0;n<nargs;n++)
					*--vm->sp = (int_val)args[n];
				vm->env = ((vfunction*)f)->env;
				if( val_tag(f) == VAL_FUNCTION ) {
					*++vm->csp = (int_val)callback_return;
					*++vm->csp = 0;
					*++vm->csp = 0;
					*++vm->csp = 0;
					ret = neko_interp(vm,((vfunction*)f)->module,(int_val)val_null,(int_val*)((vfunction*)f)->addr);
				} else {
					neko_module *m = (neko_module*)((vfunction*)f)->module;
					ret = ((jit_prim)jit_boot_seq)(vm,((vfunction*)f)->addr,val_null,m);					
				}
			}
		}
		else
			val_throw(alloc_string("Invalid call"));
	} else
		val_throw(alloc_string("Invalid call"));
	if( exc ) {
		neko_process_trap(vm);
		memcpy(&vm->start,&oldjmp,sizeof(jmp_buf));	
	}
	vm->vthis = old_this;
	vm->env = old_env;
	return ret;
}

EXTERN value val_callN( value f, value *args, int nargs ) {
	return val_callEx(NULL,f,args,nargs,NULL);
}

EXTERN value val_ocallN( value o, field f, value *args, int nargs ) {
	return val_callEx(o,val_field(o,f),args,nargs,NULL);
}

EXTERN value val_call0( value f ) {
	return val_callN(f,NULL,0);
}

EXTERN value val_call1( value f, value v ) {
	return val_callN(f,&v,1);
}

EXTERN value val_call2( value f, value v1, value v2 ) {
	value args[2] = { v1, v2 };
	return val_callN(f,args,2);
}

EXTERN value val_call3( value f, value arg1, value arg2, value arg3 ) {
	value args[3] = { arg1, arg2, arg3 };
	return val_callN(f,args,3);
}

EXTERN value val_ocall0( value o, field f ) {
	return val_ocallN(o,f,NULL,0);
}

EXTERN value val_ocall1( value o, field f, value arg ) {
	return val_ocallN(o,f,&arg,1);
}

EXTERN value val_ocall2( value o, field f, value arg1, value arg2 ) {
	value args[2] = { arg1, arg2 };
	return val_ocallN(o,f,args,2);
}

EXTERN value val_this() {
	return (value)NEKO_VM()->vthis;
}

/* ************************************************************************ */
