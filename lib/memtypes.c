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


DEFINE_MGROUP(LIB, "libzebra")
DEFINE_MTYPE(LIB, TMP,			"Temporary memory")
DEFINE_MTYPE(LIB, STRVEC,			"String vector")
DEFINE_MTYPE(LIB, VECTOR,			"Vector")
DEFINE_MTYPE(LIB, VECTOR_INDEX,		"Vector index")
DEFINE_MTYPE(LIB, LINK_LIST,		"Link List")
DEFINE_MTYPE(LIB, LINK_NODE,		"Link Node")
DEFINE_MTYPE(LIB, THREAD,			"Thread")
DEFINE_MTYPE(LIB, THREAD_MASTER,		"Thread master")
DEFINE_MTYPE(LIB, THREAD_STATS,		"Thread stats")
DEFINE_MTYPE(LIB, VTY,			"VTY")
DEFINE_MTYPE(LIB, VTY_OUT_BUF,		"VTY output buffer")
DEFINE_MTYPE(LIB, VTY_HIST,		"VTY history")
DEFINE_MTYPE(LIB, IF,			"Interface")
DEFINE_MTYPE(LIB, CONNECTED,		"Connected")
DEFINE_MTYPE(LIB, CONNECTED_LABEL,		"Connected interface label")
DEFINE_MTYPE(LIB, BUFFER,			"Buffer")
DEFINE_MTYPE(LIB, BUFFER_DATA,		"Buffer data")
DEFINE_MTYPE(LIB, STREAM,			"Stream")
DEFINE_MTYPE(LIB, STREAM_DATA,		"Stream data")
DEFINE_MTYPE(LIB, STREAM_FIFO,		"Stream FIFO")
DEFINE_MTYPE(LIB, PREFIX,			"Prefix")
DEFINE_MTYPE(LIB, PREFIX_IPV4,		"Prefix IPv4")
DEFINE_MTYPE(LIB, PREFIX_IPV6,		"Prefix IPv6")
DEFINE_MTYPE(LIB, HASH,			"Hash")
DEFINE_MTYPE(LIB, HASH_BACKET,		"Hash Bucket")
DEFINE_MTYPE(LIB, HASH_INDEX,		"Hash Index")
DEFINE_MTYPE(LIB, ROUTE_TABLE,		"Route table")
DEFINE_MTYPE(LIB, ROUTE_NODE,		"Route node")
DEFINE_MTYPE(LIB, DISTRIBUTE,		"Distribute list")
DEFINE_MTYPE(LIB, DISTRIBUTE_IFNAME,	"Dist-list ifname")
DEFINE_MTYPE(LIB, ACCESS_LIST,		"Access List")
DEFINE_MTYPE(LIB, ACCESS_LIST_STR,		"Access List Str")
DEFINE_MTYPE(LIB, ACCESS_FILTER,		"Access Filter")
DEFINE_MTYPE(LIB, PREFIX_LIST,		"Prefix List")
DEFINE_MTYPE(LIB, PREFIX_LIST_ENTRY,	"Prefix List Entry")
DEFINE_MTYPE(LIB, PREFIX_LIST_STR,		"Prefix List Str")
DEFINE_MTYPE(LIB, ROUTE_MAP,		"Route map")
DEFINE_MTYPE(LIB, ROUTE_MAP_NAME,		"Route map name")
DEFINE_MTYPE(LIB, ROUTE_MAP_INDEX,		"Route map index")
DEFINE_MTYPE(LIB, ROUTE_MAP_RULE,		"Route map rule")
DEFINE_MTYPE(LIB, ROUTE_MAP_RULE_STR,	"Route map rule str")
DEFINE_MTYPE(LIB, ROUTE_MAP_COMPILED,	"Route map compiled")
DEFINE_MTYPE(LIB, CMD_TOKENS,		"Command desc")
DEFINE_MTYPE(LIB, KEY,			"Key")
DEFINE_MTYPE(LIB, KEYCHAIN,		"Key chain")
DEFINE_MTYPE(LIB, IF_RMAP,			"Interface route map")
DEFINE_MTYPE(LIB, IF_RMAP_NAME,		"I.f. route map name")
DEFINE_MTYPE(LIB, SOCKUNION,		"Socket union")
DEFINE_MTYPE(LIB, PRIVS,			"Privilege information")
DEFINE_MTYPE(LIB, ZLOG,			"Logging")
DEFINE_MTYPE(LIB, ZCLIENT,			"Zclient")
DEFINE_MTYPE(LIB, WORK_QUEUE,		"Work queue")
DEFINE_MTYPE(LIB, WORK_QUEUE_ITEM,		"Work queue item")
DEFINE_MTYPE(LIB, WORK_QUEUE_NAME,		"Work queue name string")
DEFINE_MTYPE(LIB, PQUEUE,			"Priority queue")
DEFINE_MTYPE(LIB, PQUEUE_DATA,		"Priority queue data")
DEFINE_MTYPE(LIB, HOST, "host configuration")
DEFINE_MTYPE(LIB, VRF,			"VRF")
DEFINE_MTYPE(LIB, VRF_NAME,		"VRF name")
DEFINE_MTYPE(LIB, VRF_BITMAP,		"VRF bit-map")



DEFINE_MGROUP(BABELD, "babeld")
DEFINE_MTYPE(BABELD, BABEL,		"Babel structure")
DEFINE_MTYPE(BABELD, BABEL_IF,		"Babel interface")

