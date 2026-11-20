/*
 * openlog.so: LD_PRELOAD shim for the Tizen chroot (glibc 2.24, armv7 softfp) that
 * appends every path a process opens, stats, executes or dlopens to
 * /tmp/openlog.txt as "<pid> <call> <path>". Used once to find the part of the
 * Tizen rootfs our Android build really needs (tools/tzlite).
 *
 * Freestanding like tools/gps/gpsbridge.c: no Tizen headers, libc calls declared.
 */
typedef unsigned int size_t;
typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_arg(v, t) __builtin_va_arg(v, t)
#define va_end(v) __builtin_va_end(v)
#define RTLD_NEXT ((void *)-1l)
#define O_WRONLY 01
#define O_CREAT 0100
#define O_APPEND 02000
#define O_CLOEXEC 02000000

void *dlsym(void *, const char *);
long write(int, const void *, size_t);
int getpid(void);
int snprintf(char *, size_t, const char *, ...);

static int logfd = -1;
static int (*real_open)(const char *, int, ...);

static void note(const char *call, const char *path)
{
	char b[400];
	int n;
	if (!path)
		return;
	if (!real_open)
		real_open = dlsym(RTLD_NEXT, "open");
	if (logfd < 0 && real_open)
		logfd = real_open("/tmp/openlog.txt", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
	if (logfd < 0)
		return;
	n = snprintf(b, sizeof(b), "%d %s %s\n", getpid(), call, path);
	if (n > 0)
		write(logfd, b, n < (int)sizeof(b) ? n : (int)sizeof(b) - 1);
}

#define NEXT(name) static void *f; if (!f) f = dlsym(RTLD_NEXT, name)

int open(const char *p, int fl, ...)
{
	va_list a; int m; NEXT("open");
	va_start(a, fl); m = va_arg(a, int); va_end(a);
	note("open", p);
	return ((int (*)(const char *, int, int))f)(p, fl, m);
}
int open64(const char *p, int fl, ...)
{
	va_list a; int m; NEXT("open64");
	va_start(a, fl); m = va_arg(a, int); va_end(a);
	note("open", p);
	return ((int (*)(const char *, int, int))f)(p, fl, m);
}
int openat(int d, const char *p, int fl, ...)
{
	va_list a; int m; NEXT("openat");
	va_start(a, fl); m = va_arg(a, int); va_end(a);
	note("openat", p);
	return ((int (*)(int, const char *, int, int))f)(d, p, fl, m);
}
int openat64(int d, const char *p, int fl, ...)
{
	va_list a; int m; NEXT("openat64");
	va_start(a, fl); m = va_arg(a, int); va_end(a);
	note("openat", p);
	return ((int (*)(int, const char *, int, int))f)(d, p, fl, m);
}
void *fopen(const char *p, const char *mo)
{
	NEXT("fopen"); note("fopen", p);
	return ((void *(*)(const char *, const char *))f)(p, mo);
}
void *fopen64(const char *p, const char *mo)
{
	NEXT("fopen64"); note("fopen", p);
	return ((void *(*)(const char *, const char *))f)(p, mo);
}
void *opendir(const char *p)
{
	NEXT("opendir"); note("opendir", p);
	return ((void *(*)(const char *))f)(p);
}
int __xstat(int v, const char *p, void *st)
{
	NEXT("__xstat"); note("stat", p);
	return ((int (*)(int, const char *, void *))f)(v, p, st);
}
int __xstat64(int v, const char *p, void *st)
{
	NEXT("__xstat64"); note("stat", p);
	return ((int (*)(int, const char *, void *))f)(v, p, st);
}
int __lxstat(int v, const char *p, void *st)
{
	NEXT("__lxstat"); note("stat", p);
	return ((int (*)(int, const char *, void *))f)(v, p, st);
}
int __lxstat64(int v, const char *p, void *st)
{
	NEXT("__lxstat64"); note("stat", p);
	return ((int (*)(int, const char *, void *))f)(v, p, st);
}
int access(const char *p, int m)
{
	NEXT("access"); note("access", p);
	return ((int (*)(const char *, int))f)(p, m);
}
long readlink(const char *p, char *b, size_t n)
{
	NEXT("readlink"); note("readlink", p);
	return ((long (*)(const char *, char *, size_t))f)(p, b, n);
}
void *dlopen(const char *p, int fl)
{
	NEXT("dlopen"); note("dlopen", p);
	return ((void *(*)(const char *, int))f)(p, fl);
}
int execve(const char *p, char *const a[], char *const e[])
{
	NEXT("execve"); note("execve", p);
	return ((int (*)(const char *, char *const[], char *const[]))f)(p, a, e);
}
int execvp(const char *p, char *const a[])
{
	NEXT("execvp"); note("execvp", p);
	return ((int (*)(const char *, char *const[]))f)(p, a);
}
