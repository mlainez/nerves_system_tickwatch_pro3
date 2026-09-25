/* Link-only stubs. The watch resolves these symbols from Bionic at runtime. */
typedef unsigned long size_t;
typedef unsigned long pthread_t;
void *dlopen(const char *p, int f) { (void)p; (void)f; return 0; }
void *dlsym(void *h, const char *s) { (void)h; (void)s; return 0; }
const char *dlerror(void) { return 0; }
int pthread_create(pthread_t *t, const void *a, void *(*f)(void *), void *p)
{ (void)t; (void)a; (void)f; (void)p; return -1; }
void *malloc(size_t n) { (void)n; return 0; }
void free(void *p) { (void)p; }
int snprintf(char *b, size_t n, const char *f, ...) { (void)b; (void)n; (void)f; return -1; }
long write(int f, const void *b, unsigned long n) { (void)f; (void)b; return (long)n; }
unsigned int sleep(unsigned int n) { return n; }

