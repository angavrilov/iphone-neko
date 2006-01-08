/* ************************************************************************ */
/*																			*/
/*  Neko Standard Library													*/
/*  Copyright (c)2005 Nicolas Cannasse										*/
/*																			*/
/*  This program is free software; you can redistribute it and/or modify	*/
/*  it under the terms of the GNU General Public License as published by	*/
/*  the Free Software Foundation; either version 2 of the License, or		*/
/*  (at your option) any later version.										*/
/*																			*/
/*  This program is distributed in the hope that it will be useful,			*/
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of			*/
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the			*/
/*  GNU General Public License for more details.							*/
/*																			*/
/*  You should have received a copy of the GNU General Public License		*/
/*  along with this program; if not, write to the Free Software				*/
/*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA */
/*																			*/
/* ************************************************************************ */
#include <string.h>
#include <neko.h>
#include <neko_mod.h>
#include <neko_vm.h>
#include <stdio.h>

#define READ_BUFSIZE 64

/**
	<doc>
	<h1>Module</h1>
	<p>
	An API for reflexion of Neko bytecode modules. 
	</p>
	</doc>
**/

static int read_proxy( readp p, void *buf, int size ) {
	value fread = val_array_ptr(p)[0];
	value vbuf = val_array_ptr(p)[1];
	value ret;
	int len;
	if( size < 0 )
		return -1;
	if( size > READ_BUFSIZE )
		vbuf = alloc_empty_string(size);
	ret = val_call3(fread,vbuf,alloc_int(0),alloc_int(size));
	if( !val_is_int(ret) )
		return -1;
	len = val_int(ret);
	if( len < 0 || len > size )
		return -1;
	memcpy(buf,val_string(vbuf),len);
	return len;
}

/**
	module_read : fread:(buf:string -> pos:int -> len:int -> int) -> loader:object -> 'module
	<doc>
	Read a module using the specified read function and the specified loader.
	</doc>
**/
static value module_read( value fread, value loader ) {
	value p;
	neko_module *m;
	val_check_function(fread,3);
	p = alloc_array(2);
	val_array_ptr(p)[0] = fread;
	val_array_ptr(p)[1] = alloc_empty_string(READ_BUFSIZE);
	m = neko_read_module(read_proxy,p,loader);
	if( m == NULL )
		neko_error();
	m->name = alloc_string("");
	return alloc_abstract(neko_kind_module,m);
}

/**
	module_read_path : string list -> name:string -> loader:object -> 'module
	<doc>
	Read a module using the specified search path.
	</doc>
**/
static value module_read_path( value path, value name, value loader ) {
	FILE *f;
	value fname;
	char *mname, *ext;
	neko_module *m;
	val_check(name,string);
	val_check(loader,object);
	mname = val_string(name);
	ext = strrchr(mname,'.');
	if( ext && ext[1] == 'n' && ext[2] == 0 )
		fname = neko_select_file(path,mname,"");
	else
		fname = neko_select_file(path,mname,".n");
	f = fopen(val_string(fname),"rb");
	if( f == NULL ) {
		buffer b = alloc_buffer("Module not found : ");
		buffer_append(b,mname);
		bfailure(b);
	}
	m = neko_read_module(neko_file_reader,f,loader);
	fclose(f);
	if( m == NULL ) {
		buffer b = alloc_buffer("Invalid module : ");
		val_buffer(b,name);
		bfailure(b);
	}
	m->name = alloc_string(val_string(name));
	return alloc_abstract(neko_kind_module,m);
}

/**
	module_exec : 'module -> any
	<doc>Execute the module, return the calculated value</doc>
**/
static value module_exec( value mv ) {
	neko_module *m;
	val_check_kind(mv,neko_kind_module);
	m = (neko_module*)val_data(mv);
	return neko_vm_execute(neko_vm_current(),m);
}

/**
	module_name : 'module -> string
	<doc>Return the module name</doc>
**/
static value module_name( value mv ) {
	neko_module *m;
	val_check_kind(mv,neko_kind_module);
	m = (neko_module*)val_data(mv);
	return m->name;
}

/**
	module_exports : 'module -> object
	<doc>Return the module export table</doc>
**/
static value module_exports( value mv ) {
	neko_module *m;
	val_check_kind(mv,neko_kind_module);
	m = (neko_module*)val_data(mv);
	return m->exports;
}

/**
	module_loader : 'module -> object
	<doc>Return the module loader</doc>
**/
static value module_loader( value mv ) {
	neko_module *m;
	val_check_kind(mv,neko_kind_module);
	m = (neko_module*)val_data(mv);
	return m->loader;
}

/**
	module_nglobals : 'module -> int
	<doc>Return the number of globals for this module</doc>
**/
static value module_nglobals( value mv ) {
	neko_module *m;
	val_check_kind(mv,neko_kind_module);
	m = (neko_module*)val_data(mv);
	return alloc_int(m->nglobals);
}

/**
	module_global_get : 'module -> n:int -> any
	<doc>Get the [n]th global</doc>
**/
static value module_global_get( value mv, value p ) {
	neko_module *m;
	unsigned int pp;
	val_check_kind(mv,neko_kind_module);
	m = (neko_module*)val_data(mv);
	val_check(p,int);
	pp = (unsigned)val_int(p);
	if( pp >= m->nglobals )
		neko_error();
	return m->globals[pp];
}

/**
	module_global_set : 'module -> n:int -> any -> void
	<doc>Set the [n]th global</doc>
**/
static value module_global_set( value mv, value p, value v ) {
	neko_module *m;
	unsigned int pp;
	val_check_kind(mv,neko_kind_module);
	m = (neko_module*)val_data(mv);
	val_check(p,int);
	pp = (unsigned)val_int(p);
	if( pp >= m->nglobals )
		neko_error();
	m->globals[pp] = v;
	return v;
}

/**
	module_code_size : 'module -> int
	<doc>return the codesize of the module</doc>
**/
static value module_code_size( value mv ) {
	val_check_kind(mv,neko_kind_module);
	return alloc_int( ((neko_module*)val_data(mv))->codesize );
}

/**
	module_code_address : 'module -> 'int32
	<doc>return the base address of the module code</doc>
**/
static value module_code_address( value mv ) {
	val_check_kind(mv,neko_kind_module);
	return alloc_int32( ((neko_module*)val_data(mv))->code );
}

/**
	module_code_get : 'module -> n:int -> 'int32
	<doc>return the [n]th code value of the module code</doc>
**/
static value module_code_get( value mv, value p ) {
	neko_module *m;
	unsigned int pp;
	val_check_kind(mv,neko_kind_module);
	m = (neko_module*)val_data(mv);
	pp = val_int(p);
	if( pp < 0 || pp >= m->codesize )
		neko_error();
	return alloc_int32(m->code[pp]);
}

/**
	module_register_fields : 'module -> void
	<doc>register all the fields used by the module with current thread</doc>
**/
static value module_register_fields( value mv ) {
	neko_module *m;
	unsigned int i;
	val_check_kind(mv,neko_kind_module);
	m = (neko_module*)val_data(mv);
	for(i=0;i<m->nfields;i++)
		val_id(val_string(m->fields[i]));
	return val_true;
}

/**
	module_fun_address : function -> int
	<doc>Return the position of a bytecode function in a module</doc>
**/
static value module_fun_address( value f ) {
	neko_module *m;
	if( val_is_int(f) || val_tag(f) != VAL_FUNCTION )
		neko_error();
	m = (neko_module*)((vfunction*)f)->module;
	return alloc_int( (int_val*)((vfunction*)f)->addr - m->code );
}

/**
	module_set_jit : 'module -> string -> void
	<doc>change the JIT code of the module.</doc>
**/
static value module_set_jit( value mv, value jit ) {
	neko_module *m;	
	val_check_kind(mv,neko_kind_module);
	val_check(jit,string);
	m = (neko_module*)val_data(mv);
	if( !val_is_null(m->jit) )
		neko_error();
	m->jit = jit;
	return val_true;
}

/**
	module_fun_jit : function -> 'module -> string -> int -> function
	<doc>
	Make a JIT function from a Bytecode one.
	Should be use with great care ! 
	This is the only function that can crash the VM if used incorrectly.
	</doc>
**/
static value module_fun_jit( value f, value mv, value code, value binpos ) {
	vfunction *fj;
	val_check_kind(mv,neko_kind_module);
	if( val_is_int(f) || val_tag(f) != VAL_FUNCTION )
		neko_error();
	val_check(code,string);
	val_check(binpos,int);
	fj = (vfunction*)alloc_function(module_fun_jit,val_fun_nargs(f),"");
	fj->t = VAL_JITFUN;
	fj->addr = val_string(code)+val_int(binpos);
	fj->module = val_data(mv);	
	return (value)fj;
}

DEFINE_PRIM(module_read,2);
DEFINE_PRIM(module_read_path,3);
DEFINE_PRIM(module_exec,1);
DEFINE_PRIM(module_name,1);
DEFINE_PRIM(module_exports,1);
DEFINE_PRIM(module_loader,1);
DEFINE_PRIM(module_nglobals,1);
DEFINE_PRIM(module_global_get,2);
DEFINE_PRIM(module_global_set,3);
DEFINE_PRIM(module_code_size,1);
DEFINE_PRIM(module_code_get,2);
DEFINE_PRIM(module_code_address,1);
DEFINE_PRIM(module_register_fields,1);
DEFINE_PRIM(module_set_jit,2);
DEFINE_PRIM(module_fun_address,1);
DEFINE_PRIM(module_fun_jit,4);

/* ************************************************************************ */
