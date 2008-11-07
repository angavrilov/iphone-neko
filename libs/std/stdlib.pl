# Copyright (C) AT Software, 2008 
#
# This script generates a table of exports to ensure linkage.
#

print <<'__HEADER__';
/* THIS FILE IS GENERATED; DO NOT EDIT */

#include <neko.h>

#define PRIM_LIST \
__HEADER__


while (<>) {
    s/\(neko_sprintf,/(sprintf,/;
    next unless /DEFINE_PRIM\(\s*([a-z_0-9]+\s*,\s*\d+)\s*\)/;
    print "    PRIM($1) \\\n"
}


print <<'__FOOTER__';

/* Create a table of all exports to ensure that they are linked */

#define PRIM(name,arity) extern void *name##__##arity();
C_FUNCTION_BEGIN
PRIM_LIST
C_FUNCTION_END
#undef PRIM

#define PRIM(name,arity) name##__##arity,
static void *export_table[] = {
PRIM_LIST
    NULL
};

extern void std_main();

/* Call this function from the executable */

void *neko_stdlib_init() {
    std_main();
    return export_table;
}

__FOOTER__
