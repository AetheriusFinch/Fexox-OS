/* Fexox OS Project, EULA GPLv2
 * MuslTest: first program built against the real musl libc.
 * Output goes to stdout (= kernel log, open the SysLogs app to read it).
 * Exit code 0 = every check passed.                                     */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>

static int g_fail;
#define CHECK(cond, what) do { \
    if (cond) printf("[ ok ] %s\n", what); \
    else { printf("[FAIL] %s (errno=%d)\n", what, errno); g_fail++; } } while (0)

static int g_cnt;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static void *worker(void *arg)
{
    int fd = open("/", O_RDONLY);                                            
    for (int i = 0; i < 1000; i++) { pthread_mutex_lock(&g_mu); g_cnt++; pthread_mutex_unlock(&g_mu); }
    if (fd >= 0) close(fd);
    return (void *)(long)((long)arg * 2);
}

int main(int argc, char **argv, char **envp)
{
    printf("MuslTest: argc=%d argv[0]=%s envp=%p\n", argc, argv[0], (void *)envp);
    printf("printf: %d %u %x %s %c %5.2f %e\n", -42, 42u, 0xBEEFu, "str", 'c', 3.14159, 12345.678);
    CHECK(fabs(sqrt(2.0) - 1.41421356) < 1e-6, "SSE floating point (sqrt)");

                                  
    char *small = malloc(100);
    char *big   = malloc(4 * 1024 * 1024);
    CHECK(small && big, "malloc 100 B and 4 MiB");
    if (big) { memset(big, 0x5A, 4 * 1024 * 1024); CHECK(big[4 * 1024 * 1024 - 1] == 0x5A, "touch all of 4 MiB"); }
    free(small); free(big);

                                  
    char cwd[256];
    CHECK(getcwd(cwd, sizeof cwd) != NULL, "getcwd");
    printf("cwd = %s\n", cwd);

    FILE *f = fopen("/MUSLTEST.TXT", "w");
    CHECK(f != NULL, "fopen(w)");
    if (f) { fprintf(f, "hello from musl %d\n", 123); fclose(f); }
    struct stat st;
    CHECK(stat("/MUSLTEST.TXT", &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 10, "stat() Linux struct");
    f = fopen("/MUSLTEST.TXT", "r");
    char line[64] = {0};
    CHECK(f && fgets(line, sizeof line, f) && strncmp(line, "hello from musl", 15) == 0, "fopen(r)+fgets");
    if (f) fclose(f);
    CHECK(rename("/MUSLTEST.TXT", "/MUSLT2.TXT") == 0 && access("/MUSLT2.TXT", F_OK) == 0, "rename");
    CHECK(unlink("/MUSLT2.TXT") == 0, "unlink");

    DIR *d = opendir("/");
    int n = 0;
    if (d) { while (readdir(d)) n++; closedir(d); }
    CHECK(d != NULL && n > 0, "opendir/readdir /");

              
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    struct timespec nap = { 0, 20 * 1000 * 1000 };
    nanosleep(&nap, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    CHECK(ms >= 15, "nanosleep 20 ms");

                                                                           
    int p[2];
    CHECK(pipe(p) == 0, "pipe");
    pid_t pid = fork();
    if (pid == 0) {
        close(p[0]);
        double x = sqrt(81.0);                                                     
        char msg[32];
        snprintf(msg, sizeof msg, "child %.0f", x);
        write(p[1], msg, strlen(msg) + 1);
        _exit(7);
    }
    close(p[1]);
    char buf[32] = {0};
    ssize_t got = read(p[0], buf, sizeof buf);
    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    CHECK(pid > 0 && got > 0 && strcmp(buf, "child 9") == 0, "fork + pipe");
    CHECK(w == pid && WIFEXITED(status) && WEXITSTATUS(status) == 7, "waitpid exit status 7");

                                                                    
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    CHECK(sigaction(SIGUSR1, &sa, NULL) == 0 && raise(SIGUSR1) == 0, "SIGUSR1 ignored");
    pid = fork();
    if (pid == 0) { raise(SIGTERM); _exit(0); }                                       
    waitpid(pid, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM, "default SIGTERM kills child");

                                                                       
    pthread_t th[4]; void *ret; int okj = 1;
    int tfd = open("/", O_RDONLY);                                                            
    for (int i = 0; i < 4; i++) okj &= (pthread_create(&th[i], NULL, worker, (void *)(long)(i + 1)) == 0);
    for (int i = 0; i < 4; i++) { okj &= (pthread_join(th[i], &ret) == 0 && (long)ret == (i + 1) * 2); }
    CHECK(okj, "pthread_create/join x4");
    CHECK(g_cnt == 4000, "mutex + futex: 4 threads x 1000 increments");
    CHECK(tfd >= 0 && fcntl(tfd, F_GETFD) >= 0, "fd opened before threads still valid after them");
    if (tfd >= 0) close(tfd);

    printf("MuslTest: %s (%d failed)\n", g_fail ? "FAILED" : "ALL OK", g_fail);
    return g_fail ? 1 : 0;
}
