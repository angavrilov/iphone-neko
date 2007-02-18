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
#include <stdio.h>
#include <stdlib.h>
#include "neko_mod.h"
#include "vm.h"
#define PARAMETER_TABLE
#define STACK_TABLE
#include "opcodes.h"

DEFINE_KIND(neko_kind_module);

/* Endianness macros. */
#ifndef LITTLE_ENDIAN
#	define LITTLE_ENDIAN 1
#endif
#ifndef BIG_ENDIAN
#	define BIG_ENDIAN 2
#endif
#ifdef NEKO_WINDOWS
#	define BYTE_ORDER LITTLE_ENDIAN
#endif
#ifndef BYTE_ORDER
#	warning BYTE_ORDER unknown, assuming BIG_ENDIAN
#	define BYTE_ORDER BIG_ENDIAN
#endif

/* *_TO_LE(X) converts (X) to little endian. */
#if BYTE_ORDER == LITTLE_ENDIAN
#	define LONG_TO_LE(X) (X)
#	define SHORT_TO_LE(X) (X)
#else
#	define LONG_TO_LE(X) ((((X) >> 24) & 0xff) | \
		(((X) >> 8) & 0xff00) | (((X) & 0xff00) << 8) | \
	       	(((X) & 0xff) << 24))
#	define SHORT_TO_LE(X) ((((X) >> 8) & 0xff) | (((X) & 0xff) << 8))
#endif

#define MAXSIZE 0x100
#define ERROR() { return NULL; }
#define READ(buf,len) if( r(p,buf,len) == -1 ) ERROR()

#ifdef NEKO_64BITS

static void read_long( reader r, readp p, unsigned int *i ) {
	unsigned char c[4];
	int n;
	r(p,c,4);
	n = c[0] | (c[1] << 8) | (c[2] << 16) | (c[3] << 24);
	*i = LONG_TO_LE(n);
}

static void read_short( reader r, readp p, unsigned short *i ) {
	unsigned char c[2];
	int n;
	r(p,c,2);
	n = c[0] | (c[1] << 8);
	*i = SHORT_TO_LE(n);
}

#	define READ_LONG(var) read_long(r,p,&(var))
#	define READ_SHORT(var) read_short(r,p,&(var))

#else

#	define READ_LONG(var) READ(&(var), 4); var = LONG_TO_LE(var)
#	define READ_SHORT(var) READ(&(var), 2); var = SHORT_TO_LE(var)

#endif

extern field id_loader;
extern field id_exports;
extern value *neko_builtins;
extern value alloc_module_function( void *m, int_val pos, int nargs );
extern void neko_module_jit( neko_module *m );

EXTERN int neko_is_big_endian() {
#if BYTE_ORDER == LITTLE_ENDIAN
	return 0;
#else
	return 1;
#endif
}

static int read_string( reader r, readp p, char *buf ) {
	int i = 0;
	char c;
	while( i < MAXSIZE ) {
		if( r(p,&c,1) == -1 )
			return -1;
		buf[i++] = c;
		if( c == 0 )
			return i;
	}
	return -1;
}

static value get_builtin( neko_module *m, field id ) {
	value f = val_field(*neko_builtins,id);
	if( val_is_null(f) ) {
		unsigned int i;
		for(i=0;i<m->nfields;i++)
			if( val_id(val_string(m->fields[i])) == id ) {
				buffer b = alloc_buffer("Builtin not found : ");
				val_buffer(b,m->fields[i]);
				bfailure(b);
			}
		failure("Builtin not found");
	}
	return f;
}

#define UNKNOWN  ((unsigned char)-1)

static int neko_check_stack( neko_module *m, unsigned char *tmp, unsigned int i, int stack, int istack ) {
	unsigned int itmp;
	while( true ) {
		int c = (int)m->code[i];
		int s = stack_table[c];
		if( tmp[i] == UNKNOWN )
			tmp[i] = stack;
		else if( tmp[i] != stack )
			return 0;
		else
			return 1;
		if( s == P )
			stack += (int)m->code[i+1];
		else if( s == -P )
			stack -= (int)m->code[i+1];
		else
			stack += s;
		// 4 because it's the size of a push-infos needed in case of subcall
		if( stack < istack || stack >= MAX_STACK_PER_FUNCTION - 4 )
			return 0;
		switch( c ) {
		case Jump:
		case JumpIf:
		case JumpIfNot:
		case Trap:
			itmp = (int)(((int_val*)m->code[i+1]) - m->code);
			if( tmp[itmp] == UNKNOWN ) {
				if( c == Trap )
					stack -= s;
				if( !neko_check_stack(m,tmp,itmp,stack,istack) )
					return 0;
				if( c == Trap )
					stack += s;
			}
			else if( tmp[itmp] != stack )
				return 0;
			if( c == Jump )
				return 1;
			break;
		case JumpTable:
			itmp = (int)m->code[i+1];
			i += itmp;
			while( itmp > 0 ) {
				itmp -= 2;
				if( m->code[i - itmp] != Jump )
					return 0;
				if( !neko_check_stack(m,tmp,i - itmp,stack,istack) )
					return 0;
			}
			break;
		case AccStack:
		case SetStack:
			if( m->code[i+1] >= stack )
				return 0;
			break;
		case AccStack0:
			if( 0 >= stack )
				return 0;
			break;
		case AccStack1:
			if( 1 >= stack )
				return 0;
			break;
		case Last:
			if( stack != 0 )
				return 0;
			return 1;
		case Ret:
			if( m->code[i+1] != stack )
				return 0;
			return 1;
		case ObjCall:
			stack--;
			if( stack < istack )
				return 0;
			break;
		case TailCall:
			if( stack - (m->code[i+1] & 7) < istack || (m->code[i+1]>>3) != stack )
				return 0;
			return 1;
		}
		i += parameter_table[c]?2:1;
	}
	return 1;
}

static value read_debug_infos( reader r, readp p, char *tmp ) {
	unsigned int i;
	int curpos = 0;
	value curfile;
	unsigned int npos;
	unsigned int nfiles;
	unsigned char c,c2;
	value files;
	value pos, pp;
	int lot_of_files = 0;
	READ(&c,1);
	if( c >= 0x80 ) {
		READ(&c2,1);
		nfiles = ((c & 0x7F) << 8) | c2;
		lot_of_files = 1;
	} else
		nfiles = c;
	if( nfiles == 0 )
		ERROR();
	files = alloc_array(nfiles);
	for(i=0;i<nfiles;i++) {
		if( read_string(r,p,tmp) == -1 )
			ERROR();
		val_array_ptr(files)[i] = alloc_string(tmp);
	}
	READ_LONG(npos);
	curfile = val_array_ptr(files)[0];
	pos = alloc_array(npos);
	i = 0;
	pp = NULL;
	while( i < npos ) {
		READ(&c,1);
		if( c & 1 ) {
			c >>= 1;
			if( lot_of_files ) {
				READ(&c2,1);
				nfiles = (c << 8) | c2;
			} else
				nfiles = c;
			if( nfiles >= (unsigned int)val_array_size(files) )
				ERROR();
			curfile = val_array_ptr(files)[nfiles];
			pp = NULL;
		} else if( c & 2 ) {
			int delta = c >> 6;
			int count = (c >> 2) & 15;
			if( i + count > npos )
				ERROR();
			if( pp == NULL ) {
				pp = alloc_array(2);
				val_array_ptr(pp)[0] = curfile;
				val_array_ptr(pp)[1] = alloc_int(curpos);
			}
			while( count > 0 ) {
				val_array_ptr(pos)[i] = pp;
				count--;
				i++;
			}
			curpos += delta;
			if( delta != 0 ) pp = NULL;
		} else if( c & 4 ) {
			curpos += c >> 3;
			pp = alloc_array(2);
			val_array_ptr(pp)[0] = curfile;
			val_array_ptr(pp)[1] = alloc_int(curpos);
			val_array_ptr(pos)[i++] = pp;
		} else {
			unsigned char b2;
			unsigned char b3;
			READ(&b2,1);
			READ(&b3,1);
			curpos = (c >> 3) | (b2 << 5) | (b3 << 13);
			pp = alloc_array(2);
			val_array_ptr(pp)[0] = curfile;
			val_array_ptr(pp)[1] = alloc_int(curpos);
			val_array_ptr(pos)[i++] = pp;
		}
	}
	return pos;
}

neko_module *neko_read_module( reader r, readp p, value loader ) {
	unsigned int i;
	unsigned int itmp;
	unsigned char t;
	unsigned short stmp;
	char *tmp = NULL;
	int entry;
	neko_module *m = (neko_module*)alloc(sizeof(neko_module));
	READ_LONG(itmp);
	if( itmp != 0x4F4B454E )
		ERROR();
	READ_LONG(m->nglobals);
	READ_LONG(m->nfields);
	READ_LONG(m->codesize);
	if( m->nglobals < 0 || m->nglobals > 0xFFFF || m->nfields < 0 || m->nfields > 0xFFFF || m->codesize < 0 || m->codesize > 0xFFFFF )
		ERROR();
	tmp = alloc_private(sizeof(char)*(((m->codesize+1)>MAXSIZE)?(m->codesize+1):MAXSIZE));
	m->jit = NULL;
	m->jit_gc = NULL;
	m->debuginf = val_null;
	m->globals = (value*)alloc(m->nglobals * sizeof(value));
	m->fields = (value*)alloc(sizeof(value*)*m->nfields);
#ifdef NEKO_PROF
	if( m->codesize >= PROF_SIZE )
		ERROR();
	m->code = (int_val*)alloc_private(sizeof(int_val)*(m->codesize+PROF_SIZE));
	memset(m->code+PROF_SIZE,0,m->codesize*sizeof(int_val));
#else
	m->code = (int_val*)alloc_private(sizeof(int_val)*(m->codesize+1));
#endif
	m->loader = loader;
	m->exports = alloc_object(NULL);
	alloc_field(m->exports,neko_id_module,alloc_abstract(neko_kind_module,m));
	// Init global table
	for(i=0;i<m->nglobals;i++) {
		READ(&t,1);
		switch( t ) {
		case 1:
			if( read_string(r,p,tmp) == -1 )
				ERROR();
			m->globals[i] = val_null;
			break;
		case 2:
			READ_LONG(itmp);
			if( (itmp & 0xFFFFFF) >= m->codesize )
				ERROR();
			m->globals[i] = alloc_module_function(m,(itmp&0xFFFFFF),(itmp >> 24));
			break;
		case 3:
			READ_SHORT(stmp);
			m->globals[i] = alloc_empty_string(stmp);
			READ(val_string(m->globals[i]),stmp);
			break;
		case 4:
			if( read_string(r,p,tmp) == -1 )
				ERROR();
			m->globals[i] = alloc_float( atof(tmp) );
			break;
		case 5:
			m->debuginf = read_debug_infos(r,p,tmp);
			if( m->debuginf == NULL || val_array_size(m->debuginf) != m->codesize )
				ERROR();
			m->globals[i] = val_null;
			break;
		default:
			ERROR();
			break;
		}
	}
	for(i=0;i<m->nfields;i++) {
		if( read_string(r,p,tmp) == -1 )
			ERROR();
		m->fields[i] = alloc_string(tmp);
	}
	i = 0;
	// Unpack opcodes
	while( i < m->codesize ) {
		READ(&t,1);
		tmp[i] = 1;
		switch( t & 3 ) {
		case 0:
			m->code[i++] = (t >> 2);
			break;
		case 1:
			m->code[i++] = (t >> 3);
			tmp[i] = 0;
			m->code[i++] = (t >> 2) & 1;
			break;
		case 2:
			m->code[i++] = (t >> 2);
			READ(&t,1);
			tmp[i] = 0;
			m->code[i++] = t;
			break;
		case 3:
			m->code[i++] = (t >> 2);
			READ_LONG(itmp);
			tmp[i] = 0;
			m->code[i++] = (int)itmp;
			break;
		}
	}
	tmp[i] = 1;
	m->code[i] = Last;
	entry = (int)m->code[1];
	// Check bytecode
	for(i=0;i<m->codesize;i++) {
		int c = (int)m->code[i];
		itmp = (unsigned int)m->code[i+1];
		if( c >= Last || tmp[i+1] == parameter_table[c] )
			ERROR();
		// Additional checks and optimizations
		switch( m->code[i] ) {
		case AccGlobal:
		case SetGlobal:
			if( itmp >= m->nglobals )
				ERROR();
			m->code[i+1] = (int_val)(m->globals + itmp);
			break;
		case Jump:
		case JumpIf:
		case JumpIfNot:
		case Trap:
			itmp += i;
			if( itmp > m->codesize || !tmp[itmp] )
				ERROR();
			m->code[i+1] = (int_val)(m->code + itmp);
			break;
		case AccInt:
			m->code[i+1] = (int_val)alloc_int((int)itmp);
			break;
		case AccIndex:
			m->code[i+1] += 2;
			break;
		case AccStack:
			m->code[i+1] += 2;
			itmp = (unsigned int)m->code[i+1];
		case SetStack:
			if( ((int)itmp) < 0 )
				ERROR();
			break;
		case Ret:
		case Pop:
		case AccEnv:
		case SetEnv:
			if( ((int)itmp) < 0 )
				ERROR();
			break;
		case AccBuiltin: {
			field f = (field)(int_val)itmp;
			if( f == id_loader )
				m->code[i+1] = (int_val)loader;
			else if( f == id_exports )
				m->code[i+1] = (int_val)m->exports;
			else
				m->code[i+1] = (int_val)get_builtin(m,f);
			}
			break;
		case Call:
		case ObjCall:
			if( itmp > CALL_MAX_ARGS )
				ERROR();
			break;
		case TailCall:
			if( (itmp&7) > CALL_MAX_ARGS )
				ERROR();
			break;
		case Apply:
			if( itmp == 0 || itmp >= CALL_MAX_ARGS )
				ERROR();
			break;
		case MakeEnv:
			if( itmp > 0xFF )
				failure("Too much big environment");
			break;
		case MakeArray:
			if( itmp > 0x10000 )
				failure("Too much big array");
			break;
		case JumpTable:
			if( itmp > 0xff || i + 1 + itmp * 2 >= m->codesize )
				ERROR();
			m->code[i+1] <<= 1;
			break;
		}
		if( !tmp[i+1] )
			i++;
	}
	// Check stack preservation
	{
		unsigned char *stmp = (unsigned char*)alloc_private(m->codesize+1);
		unsigned int prev = 0;
		memset(stmp,UNKNOWN,m->codesize+1);
		if( !neko_check_stack(m,stmp,0,0,0) )
			ERROR();
		for(i=0;i<m->nglobals;i++) {
			vfunction *f = (vfunction*)m->globals[i];
			if( val_type(f) == VAL_FUNCTION ) {
				itmp = (unsigned int)(int_val)f->addr;
				if( itmp >= m->codesize || !tmp[itmp] || itmp < prev )
					ERROR();
				if( !neko_check_stack(m,stmp,itmp,f->nargs,f->nargs) )
					ERROR();
				f->addr = m->code + itmp;
				prev = itmp;
			}
		}
	}
	// jit ?
	if( NEKO_VM()->run_jit )
		neko_module_jit(m);
	return m;
}

int neko_file_reader( readp p, void *buf, int size ) {
	int len = 0;
	while( size > 0 ) {
		int l = (int)fread(buf,1,size,(FILE*)p);
		if( l <= 0 )
			return len;
		size -= l;
		len += l;
		buf = (char*)buf+l;
	}
	return len;
}

int neko_string_reader( readp p, void *buf, int size ) {
	string_pos *sp = (string_pos*)p;
	int delta = (sp->len >= size)?size:sp->len;
	memcpy(buf,sp->p,delta);
	sp->p += delta;
	sp->len -= delta;
	return delta;
}

/* ************************************************************************ */
