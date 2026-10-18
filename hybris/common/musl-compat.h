/*
 * Compatibilidade da libhybris com a musl (Alpine): tipos e funções glibc que
 * os hooks usam. Incluído no topo de hooks.c, hooks_shm.c e properties.c.
 */
#ifndef HYBRIS_MUSL_COMPAT_H
#define HYBRIS_MUSL_COMPAT_H
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <features.h>
#ifndef __GLIBC__

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <dirent.h>

/* Na musl off_t já é de 64 bits; os nomes *64 sumiram na 1.2.4 */
typedef off_t off64_t;
typedef fpos_t fpos64_t;
#define fopen64 fopen
#define freopen64 freopen
#define fgetpos64 fgetpos
#define fsetpos64 fsetpos
#define ftello64 ftello
#define fseeko64 fseeko
#define mmap64 mmap
#define lseek64 lseek
#define pread64 pread
#define pwrite64 pwrite
#define ftruncate64 ftruncate

/* fpos_t da musl: o offset fica em __lldata */
#define HYBRIS_FPOS_GET(f) ((f).__lldata)
#define HYBRIS_FPOS_SET(f, v) do { memset(&(f), 0, sizeof(f)); (f).__lldata = (v); } while (0)

#ifndef __THROW
#define __THROW
#endif

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(exp) ({ \
	long _rc; \
	do { _rc = (long)(exp); } while (_rc == -1L && errno == EINTR); \
	_rc; })
#endif

static inline wchar_t *wmempcpy(wchar_t *d, const wchar_t *s, size_t n)
{
	return wmemcpy(d, s, n) + n;
}

static inline void *pvalloc(size_t n)
{
	size_t pg = (size_t)sysconf(_SC_PAGESIZE);

	return valloc((n + pg - 1) & ~(pg - 1));
}

/* mallinfo não existe na musl; mesma disposição da glibc/bionic (10 ints) */
struct mallinfo {
	int arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost;
};
static inline struct mallinfo mallinfo(void)
{
	struct mallinfo mi;

	memset(&mi, 0, sizeof(mi));
	return mi;
}

/* rwlock "kind" (preferência de leitor/escritor): a musl não tem; aceita e ignora */
static inline int pthread_rwlockattr_setkind_np(pthread_rwlockattr_t *a, int k)
{
	(void)a; (void)k;
	return 0;
}
static inline int pthread_rwlockattr_getkind_np(const pthread_rwlockattr_t *a, int *k)
{
	(void)a;
	*k = 0;
	return 0;
}

/* pthread_cond_clockwait: aproxima com o relógio do condattr (REALTIME por padrão) */
static inline int pthread_cond_clockwait(pthread_cond_t *c, pthread_mutex_t *m,
					 clockid_t clk, const struct timespec *abst)
{
	struct timespec now, rt, out = *abst;

	if (clk != CLOCK_REALTIME) {
		clock_gettime(clk, &now);
		clock_gettime(CLOCK_REALTIME, &rt);
		out.tv_sec = rt.tv_sec + (abst->tv_sec - now.tv_sec);
		out.tv_nsec = rt.tv_nsec + (abst->tv_nsec - now.tv_nsec);
		while (out.tv_nsec < 0) { out.tv_nsec += 1000000000L; out.tv_sec--; }
		while (out.tv_nsec >= 1000000000L) { out.tv_nsec -= 1000000000L; out.tv_sec++; }
	}
	return pthread_cond_timedwait(c, m, &out);
}

/* stackaddr isolado (obsoleto): guarda via setstack mantendo o tamanho atual */
static inline int pthread_attr_setstackaddr(pthread_attr_t *a, void *addr)
{
	size_t sz = 0;

	pthread_attr_getstacksize(a, &sz);
	return pthread_attr_setstack(a, (char *)addr - sz, sz);
}
static inline int pthread_attr_getstackaddr(const pthread_attr_t *a, void **addr)
{
	void *base;
	size_t sz;
	int ret = pthread_attr_getstack(a, &base, &sz);

	if (!ret)
		*addr = (char *)base + sz;
	return ret;
}

static inline int scandirat(int dirfd, const char *dirp, struct dirent ***nl,
			    int (*filter)(const struct dirent *),
			    int (*compar)(const struct dirent **, const struct dirent **))
{
	(void)dirfd; (void)dirp; (void)nl; (void)filter; (void)compar;
	errno = ENOSYS;
	return -1;
}

#else
#define HYBRIS_FPOS_GET(f) ((f).__pos)
#define HYBRIS_FPOS_SET(f, v) do { (f).__pos = (v); memset(&(f).__state, 0, sizeof(mbstate_t)); } while (0)
#endif /* __GLIBC__ */
#endif

/* __v*printf_chk são da glibc (fortify); na musl usa as funções comuns */
#ifdef __GLIBC__
#define HYBRIS_VSPRINTF_CHK(s, flag, slen, fmt, ap) __vsprintf_chk(s, flag, slen, fmt, ap)
#define HYBRIS_VSNPRINTF_CHK(s, max, flag, slen, fmt, ap) __vsnprintf_chk(s, max, flag, slen, fmt, ap)
#else
#define HYBRIS_VSPRINTF_CHK(s, flag, slen, fmt, ap) vsprintf(s, fmt, ap)
#define HYBRIS_VSNPRINTF_CHK(s, max, flag, slen, fmt, ap) vsnprintf(s, max, fmt, ap)
#endif

/*
 * time_t: o bionic de 32 bits usa time_t/long de 32 bits; a musl de 32 bits usa
 * time_t de 64 bits. Converte os timespec que os hooks repassam à libc do host.
 */
#if !defined(__GLIBC__) && !defined(__LP64__)
#include <stdint.h>
typedef struct { int32_t tv_sec; int32_t tv_nsec; } hybris_bionic_timespec;
#define HYBRIS_TS(p) ((p) ? &(struct timespec){ 	((const hybris_bionic_timespec *)(p))->tv_sec, 	((const hybris_bionic_timespec *)(p))->tv_nsec } : (const struct timespec *)0)
#define HYBRIS_TS_SEC(p) (((const hybris_bionic_timespec *)(p))->tv_sec)
#define HYBRIS_TS_NSEC(p) (((const hybris_bionic_timespec *)(p))->tv_nsec)
#define HYBRIS_TIME32 1
#else
#define HYBRIS_TS(p) (p)
#define HYBRIS_TS_SEC(p) ((p)->tv_sec)
#define HYBRIS_TS_NSEC(p) ((p)->tv_nsec)
#endif
