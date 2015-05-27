/*
 * Memory type definitions. This file is parsed by memtypes.awk to extract
 * MTYPE_ and memory_list_.. information in order to autogenerate 
 * memtypes.h.
 *
 * The script is sensitive to the format (though not whitespace), see
 * the top of memtypes.awk for more details.
 */

#include "zebra.h"
#include "memory.h"

DEFINE_MGROUP(BABELD, "babeld")
DEFINE_MTYPE(BABELD, BABEL,		"Babel structure")
DEFINE_MTYPE(BABELD, BABEL_IF,		"Babel interface")

