/*
 *	NMH's Simple C Compiler, 2011,2012,2014
 *	Symbol table management
 */

#include "defs.h"
#include "data.h"
#include "decl.h"
#include "cgen.h"

// Determine if the symbol s is in the global symbol table.
// Return its slot position or 0 if not found. Global symbols
// occupy the bottom section of the symbol table.
int findglob(char *s) {
	int	i;

	for (i=0; i<Globs; i++) {
		// It must not be a macro, not a struct member.
		// Match on the first char to speed up the strcmp()
		if (	Types[i] != TMACRO && Stcls[i] != CMEMBER &&
			*s == *Names[i] && !strcmp(s, Names[i])
		)
			return i;
	}
	return 0;
}

// Determine if the symbol s is in the local symbol table.
// Return its slot position or 0 if not found. Local symbols
// occupy the top section of the symbol table.
int findloc(char *s) {
	int	i;

	for (i=Locs; i<NSYMBOLS; i++) {
		// It must not be a struct member.
		// Match on the first char to speed up the strcmp()
		if (	Stcls[i] != CMEMBER &&
			*s == *Names[i] && !strcmp(s, Names[i])
		)
			return i;
	}
	return 0;
}

// Find the slot of a symbol, or return 0 if not found.
// Search the local symbols before the global symbols.
int findsym(char *s) {
	int	y;

	if ((y = findloc(s)) != 0) return y;
	return findglob(s);
}

// Determine if the symbol s is a global macro in the symbol table.
// Return its slot position or 0 if not found.
int findmac(char *s) {
	int	i;

	for (i=0; i<Globs; i++)
		if (	TMACRO == Types[i] &&
			*s == *Names[i] && !strcmp(s, Names[i])
		)
			return i;
	return 0;
}

// Determine if the symbol s is a structure definition.
// Return its slot position or 0 if not found.
// Search the local symbols before the global symbols.
int findstruct(char *s) {
	int	i;

	for (i=Locs; i<NSYMBOLS; i++)
		if (	TSTRUCT == Types[i] &&
			*s == *Names[i] && !strcmp(s, Names[i])
		)
			return i;
	for (i=0; i<Globs; i++)
		if (	TSTRUCT == Types[i] &&
			*s == *Names[i] && !strcmp(s, Names[i])
		)
			return i;
	return 0;
}

// XXX: Find a struct member, but why the y parameter?
int findmem(int y, char *s) {
	y++;
	while (	(y < Globs ||
		 (y >= Locs && y < NSYMBOLS)) &&
		CMEMBER ==  Stcls[y]
	) {
		if (*s == *Names[y] && !strcmp(s, Names[y]))
			return y;
		y++;
	}
	return 0;
}

// Get the position of a new global symbol slot, or die
// if we've hit the local section of the symbol table.
int newglob(void) {
	int	p;

	if ((p = Globs++) >= Locs)
		fatal("too many global symbols");
	return p;
}

// Get the position of a new local symbol slot, or die
// if we've hit the global section of the symbol table.
int newloc(void) {
	int	p;

	if ((p = --Locs) <= Globs)
		fatal("too many local symbols");
	return p;
}

#ifdef __SUBC__
 #define PTR_INT_CAST	(int)
#else
 #define PTR_INT_CAST	(int) (long)
#endif

// The namelist is essentially superfluous. It's
// just a fixed-size storage area to store strings.
// galloc(k, align) can be replaced with malloc(k).
// Both globname(s) and locname(s) can be replaced
// with strdup(s). There is no need for the align
// parameter, as the code runs when align=0 always.
char *galloc(int k, int align) {
	int	p, mask;

	k += align * sizeof(int);
	if (Nbot + k >= Ntop)
		fatal("out of space for symbol names");
	p = Nbot;
	Nbot += k;
	mask = sizeof(int)-1;
	if (align)
		while (PTR_INT_CAST &Nlist[p] & mask)
			p++;
	return &Nlist[p];
}

// Add the string s to the global namelist by allocating
// space in the namelist and copying the string. Return
// a pointer to the copied name.
char *globname(char *s) {
	char	*p;
	
	p = galloc(strlen(s)+1, 0);
	strcpy(p, s);
	return p;
}

// Add the string s to the local namelist by allocating
// space in the namelist and copying the string. Return
// a pointer to the copied name.
// Die if there is no room left in the namelist.
char *locname(char *s) {
	int	p, k;

	k = strlen(s) + 1;
	if (Nbot + k >= Ntop)
		fatal("out of space for symbol names");
	Ntop -= k;
	p = Ntop;
	strcpy(&Nlist[p], s);
	return &Nlist[p];
}

// Generate the assembly output for a global symbol.
//   prim: Primitive type of the symbol
//   type: Meta type of the symbol
//   size: Number of elements in the symbol
//   val:  Initial value of the symbol
//   scls: Storage class of the symbol
//   init: If true, symbol has an initial value
static void defglob(char *name, int prim, int type, int size, int val,
			int scls, int init)
{
	int	st;

	// Nothing to do for constants or functions
	if (TCONSTANT == type || TFUNCTION == type) return;

	// Switch to the data section
	gendata();

	// st is true if the symbol is static
	st = scls == CSTATIC;

	// Generate a .globl directive if the symbol is public
	if (CPUBLIC == scls) genpublic(name);

	// Do nothing if the symbol is an initialised array.
	// Someone else (who?XXX) will generate the array.
	if (init && TARRAY == type)
		return;

	// Generate a name label if not an array, struct or union
	if (TARRAY != type && !(prim & STCMASK)) genname(name);

	// If a struct/union, generate bss storage with the object's size.
        // Deal with the situation when it's an array of struct/unions.
	if (prim & STCMASK) {
		if (TARRAY == type)
			genbss(gsym(name), objsize(prim, TARRAY, size), st);
		else
			genbss(gsym(name), objsize(prim, TVARIABLE, size), st);
	}
	else if (PCHAR == prim) {
		// If a char array, generate bss storage with the size.
                // Otherwise generate a byte and then force alignment afterwards
		if (TARRAY == type)
			genbss(gsym(name), size, st);
		else {
			gendefb(val);
			genalign(1);
		}
	}
	else if (PINT == prim) {
		// If a char array, generate bss storage with the size.
                // Otherwise generate a word which needs no further alignment
		if (TARRAY == type)
			genbss(gsym(name), size*INTSIZE, st);
		else
			gendefw(val);
	}
	else {
		// If a pointer array, generate bss storage with the size.
                // Otherwise generate a pointer which needs no further alignment
		if (TARRAY == type)
			genbss(gsym(name), size*PTRSIZE, st);
		else
			gendefp(val);
	}
}

// Redeclare a symbol to have a new class.
// Return the new class of the symbol.
// Here is what's permitted (apart from when oldcls == newcls):
//  
//    CEXTERN -> CPUBLIC
//    CPUBLIC -> CEXTERN, stays as CPUBLIC
//    CSPROTO -> CSTATIC
//    CSTATIC -> CSPROTO, stays as CSTATIC
//
int redeclare(char *name, int oldcls, int newcls) {
	switch (oldcls) {

	// Cannot redeclare an extern symbol as static
	case CEXTERN:
		if (newcls != CPUBLIC && newcls != CEXTERN)
			error("extern symbol redeclared static: %s", name);
		return newcls;

	case CPUBLIC:
		if (CEXTERN == newcls)
			return CPUBLIC;
		if (newcls != CPUBLIC) {
			error("extern symbol redeclared static: %s", name);
			return CPUBLIC;
		}
		break;
	case CSPROTO:
		if (newcls != CSTATIC && newcls != CSPROTO)
			error("static symbol redeclared extern: %s", name);
		return newcls;
	case CSTATIC:
		if (CSPROTO == newcls)
			return CSTATIC;
		if (newcls != CSTATIC) {
			error("static symbol redeclared extern: %s", name);
			return CSTATIC;
		}
		break;
	}
	error("redefined symbol: %s", name);
	return newcls;
}

// Add a global symbol to the symbol table.
//   name: Name of the symbol
//   prim: Primitive type of the symbol
//   type: Meta type of the symbol
//   scls: Storage class of the symbol
//   size: Number of elements in the symbol
//   val:  Initial value of the symbol
//   mtext: Text of a macro, or a function signature (list of parameters)
//   init: If true, symbol has an initial value
// Return the slot number in the symbol table
int addglob(char *name, int prim, int type, int scls, int size, int val,
		char *mtext, int init)
{
	int	y;

	// If this is already in the symbol table
	if ((y = findglob(name)) != 0) {

		// Redeclare the symbol with a new storage class.
		// If a function, keep the old function signature.
		scls = redeclare(name, Stcls[y], scls);
		if (TFUNCTION == Types[y])
			mtext = Mtext[y];
	}

	// Doesn't exist, so get a new global symbol slot
	// and copy the symbol name into the symbol table
	if (0 == y) {
 		y = newglob();
		Names[y] = globname(name);
	}

	// Check that, if this is a redefinition of a function or macro,
	// that the redefinition has the same type as before
	else if (TFUNCTION == Types[y] || TMACRO == Types[y]) {
		if (Prims[y] != prim || Types[y] != type)
			error("redefinition does not match prior type: %s",
				name);
	}

	// If this is a public of static declaration, generate
	// the storage in the assembly output now
	if (CPUBLIC == scls || CSTATIC == scls)
		defglob(name, prim, type, size, val, scls, init);

	// Copy the values into the symbol table
	// and return the slot
	Prims[y] = prim;
	Types[y] = type;
	Stcls[y] = scls;
	Sizes[y] = size;
	Vals[y] = val;
	Mtext[y] = mtext;
	return y;
}

// Generate the assembly output for a local static symbol.
// Note: local auto symbols are allocated on the stack.
//   prim: Primitive type of the symbol
//   type: Meta type of the symbol
//   size: Number of elements in the symbol
//   val:  Initial value of the symbol
//   init: If true, symbol has an initial value
static void defloc(int prim, int type, int size, int val, int init) {

	// Switch to the data section
	gendata();

	// Generate a label with the value if not an array or struct/union
	// Use the val value to make the label unique. XXX: more details.
	if (type != TARRAY && !(prim &STCMASK)) genlab(val);

	// If a struct/union, generate bss storage with the object's size.
	// Deal with the situation when it's an array of struct/unions.
	if (prim & STCMASK) {
		if (TARRAY == type)
			genbss(labname(val), objsize(prim, TARRAY, size), 1);
		else
			genbss(labname(val), objsize(prim, TVARIABLE, size),1);
	}

	else if (PCHAR == prim) {
		// If a char array, generate bss storage with the size.
		// Otherwise generate a byte and then force alignment afterwards
		if (TARRAY == type)
			genbss(labname(val), size, 1);
		else {
			gendefb(init);
			genalign(1);
		}
	}
	else if (PINT == prim) {
		// If a char array, generate bss storage with the size.
		// Otherwise generate a word which needs no further alignment
		if (TARRAY == type)
			genbss(labname(val), size*INTSIZE, 1);
		else
			gendefw(init);
	}
	else {
		// If a pointer array, generate bss storage with the size.
		// Otherwise generate a pointer which needs no further alignment
		if (TARRAY == type)
			genbss(labname(val), size*PTRSIZE, 1);
		else
			gendefp(init);
	}
}

// Add a local symbol to the symbol table.
//   name: Name of the symbol
//   prim: Primitive type of the symbol
//   type: Meta type of the symbol
//   scls: Storage class of the symbol
//   size: Number of elements in the symbol
//   val:  Initial value of the symbol
//   init: If true, symbol has an initial value
// Return the slot number in the symbol table
int addloc(char *name, int prim, int type, int scls, int size, int val,
		int init)
{
	int	y;

	// Ensure the local hasn't already been defined
	if (findloc(name))
		error("redefinition of: %s", name);

	// Get a new local slot in the symbol table
 	y = newloc();

	// If this is a static declaration, generate the
	// storage in the assembly output now

	if (CLSTATC == scls) defloc(prim, type, size, val, init);

	// Copy the values into the symbol table
	// and return the slot
	Names[y] = locname(name);
	Prims[y] = prim;
	Types[y] = type;
	Stcls[y] = scls;
	Sizes[y] = size;
	Vals[y] = val;
	return y;
}

// Clear the namelist
void clrlocs(void) {
	Ntop = POOLSIZE;
	Locs = NSYMBOLS;
}

// Given a primitive type, return the size of this type.
// If type is TARRAY, multiply the returned value by size.
// Return -1 if the type has no size: function, constant, macro.
int objsize(int prim, int type, int size) {
	int	k = 0, sp;

	// Keep only the struct/union bits in sp
	sp = prim & STCMASK;
	if (PINT == prim)
		k = INTSIZE;
	else if (PCHAR == prim)
		k = CHARSIZE;
	else if (INTPTR == prim || CHARPTR == prim || VOIDPTR == prim)
		k = PTRSIZE;
	else if (INTPP == prim || CHARPP == prim || VOIDPP == prim)
		k = PTRSIZE;
	else if (STCPTR == sp || STCPP == sp)
		k = PTRSIZE;
	else if (UNIPTR == sp || UNIPP == sp)
		k = PTRSIZE;
	else if (PSTRUCT == sp || PUNION == sp)
		k = Sizes[prim & ~STCMASK];
	else if (FUNPTR == prim)
		k = PTRSIZE;
	if (TFUNCTION == type || TCONSTANT == type || TMACRO == type)
		return -1;
	if (TARRAY == type)
		k *= size;
	return k;
}

// Given a type value (which includes a struct/union
// bitmask), return a string decribing this type.
static char *typename(int p) {
	switch (p & STCMASK) {
	case PSTRUCT:	return "STRUCT";
	case STCPTR:	return "STCT*";
	case STCPP:	return "STCT**";
	case PUNION:	return "UNION";
	case UNIPTR:	return "UNIO*";
	case UNIPP:	return "UNIO**";
	}
	return	PINT    == p? "INT":
		PCHAR   == p? "CHAR":
		INTPTR  == p? "INT*":
		CHARPTR == p? "CHAR*":
		VOIDPTR == p? "VOID*":
		FUNPTR  == p? "FUN*":
		INTPP   == p? "INT**":
		CHARPP  == p? "CHAR**":
		VOIDPP  == p? "VOID**":
		PVOID   == p? "VOID": "n/a";
}

// Dump part of the symbol table to stdout.
// Print the entries with indexes from ... to.
// Print out a header that includes the title and sub-title.
void dumpsyms(char *title, char *sub, int from, int to) {
	int	i;
	char	*p;

	printf("\n===== %s%s =====\n", title, sub);
	printf(	"PRIM    TYPE  STCLS   SIZE  VALUE  NAME [MVAL]/(SIG)\n"
		"------  ----  -----  -----  -----  -----------------\n");
	for (i = from; i < to; i++) {
		printf("%-6s  %s  %s  %5d  %5d  %s",
			typename(Prims[i]),
			TVARIABLE == Types[i]? "VAR ":
				TARRAY == Types[i]? "ARRY":
				TFUNCTION == Types[i]? "FUN ":
				TCONSTANT == Types[i]? "CNST":
				TMACRO == Types[i]? "MAC ":
				TSTRUCT == Types[i]? "STCT": "n/a",
			CPUBLIC == Stcls[i]? "PUBLC":
				CEXTERN == Stcls[i]? "EXTRN":
				CSTATIC == Stcls[i]? "STATC":
				CSPROTO == Stcls[i]? "STATP":
				CLSTATC == Stcls[i]? "LSTAT":
				CAUTO   == Stcls[i]? "AUTO ":
				CMEMBER == Stcls[i]? "MEMBR": "n/a  ",
			Sizes[i],
			Vals[i],
			Names[i]);
		if (TMACRO == Types[i])
			printf(" [\"%s\"]", Mtext[i]);
		if (TFUNCTION == Types[i]) {
			printf(" (");
			for (p = Mtext[i]; *p; p++) {
				printf("%s", typename(*p));
				if (p[1]) printf(", ");
			}
			putchar(')');
		}
		putchar('\n');
	}
}