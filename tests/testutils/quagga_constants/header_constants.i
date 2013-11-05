%module header_constants
%{
#ifdef __ASSERT_FUNCTION
#undef __ASSERT_FUNCTION
#endif

#ifdef HAVE_INET_PTON
#undef HAVE_INET_PTON
#endif

/* zebra.h includes route_types.h and config.h
 * so we don't need them up here. */
#include "zebra.h"
#include "zclient.h"
%}

%include "config.h"
%include "route_types.h"
%include "zebra.h"
%include "zclient.h"
