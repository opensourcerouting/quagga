/*
 * $Id: zassert.h,v 1.2 2004/12/03 18:01:04 ajs Exp $
 */

#ifndef _QUAGGA_ASSERT_H
#define _QUAGGA_ASSERT_H

extern void _zlog_assert_failed (const char *assertion, const char *file,
				 unsigned int line, const char *function)
				 __attribute__ ((noreturn));

#ifdef __GNUC__
#define UNLIKELY(EX)	__builtin_expect(!!(EX), 0)
#define LIKELY(EX)	__builtin_expect(!!(EX), 1)
#else
#define UNLIKELY(EX)	(EX)
#define LIKELY(EX)	(EX)
#endif

#define zassert(EX) ((void)(LIKELY(EX) ? 0 :				 \
			    _zlog_assert_failed(#EX, __FILE__, __LINE__, \
						__func__)))

#undef assert
#define assert(EX) zassert(EX)

#endif /* _QUAGGA_ASSERT_H */
