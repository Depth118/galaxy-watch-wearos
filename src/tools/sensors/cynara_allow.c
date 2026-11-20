/*
 * cynara_allow.so: LD_PRELOAD shim for Tizen's sensord in the Wear OS chroot.
 *
 * sensord checks privileged sensors (pedometer, sleep, stress, HRM: the
 * http://tizen.org/privilege/healthinfo privilege) through Cynara. The chroot
 * runs no cynara daemon and the kernel has no SMACK, so every check fails and
 * clients get -EACCES. The only clients here are our own bridge tools, so
 * allow everything.
 *
 * Build: clang --target=armv7a-linux-gnueabi -mfloat-abi=soft -fPIC -shared
 *        -nostdlib -fuse-ld=lld -O2 -o cynara_allow.so cynara_allow.c
 * (strdup is resolved from the process's glibc at load time.)
 */
typedef struct cynara cynara;
typedef struct cynara_configuration cynara_configuration;
typedef unsigned int size_t;

extern char *strdup(const char *);

#define CYNARA_API_SUCCESS        0
#define CYNARA_API_ACCESS_ALLOWED 2

static int dummy;

int cynara_initialize(cynara **p, const cynara_configuration *conf)
{
    (void)conf;
    if (p) *p = (cynara *)&dummy;
    return CYNARA_API_SUCCESS;
}
int cynara_finish(cynara *p) { (void)p; return CYNARA_API_SUCCESS; }

int cynara_check(cynara *p, const char *client, const char *session,
                 const char *user, const char *privilege)
{
    (void)p; (void)client; (void)session; (void)user; (void)privilege;
    return CYNARA_API_ACCESS_ALLOWED;
}

int cynara_configuration_create(cynara_configuration **c)
{
    if (c) *c = (cynara_configuration *)&dummy;
    return CYNARA_API_SUCCESS;
}
int cynara_configuration_set_cache_size(cynara_configuration *c, size_t n)
{ (void)c; (void)n; return CYNARA_API_SUCCESS; }
void cynara_configuration_destroy(cynara_configuration *c) { (void)c; }

int cynara_creds_socket_get_client(int fd, int method, char **client)
{
    (void)fd; (void)method;
    *client = strdup("User");
    return *client ? CYNARA_API_SUCCESS : -1;
}
int cynara_creds_socket_get_user(int fd, int method, char **user)
{
    (void)fd; (void)method;
    *user = strdup("0");
    return *user ? CYNARA_API_SUCCESS : -1;
}
char *cynara_session_from_pid(int pid) { (void)pid; return strdup("r800"); }
