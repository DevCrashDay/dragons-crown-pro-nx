#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* newlib has no pread: the seek+read pair must not interleave with another thread's. */
static pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;

ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
    pthread_mutex_lock(&io_lock);
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur == (off_t)-1) { pthread_mutex_unlock(&io_lock); return -1; }
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) { pthread_mutex_unlock(&io_lock); return -1; }
    ssize_t r = read(fd, buf, count);
    int saved = errno;
    lseek(fd, cur, SEEK_SET);
    pthread_mutex_unlock(&io_lock);
    errno = saved;
    return r;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
    pthread_mutex_lock(&io_lock);
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur == (off_t)-1) { pthread_mutex_unlock(&io_lock); return -1; }
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) { pthread_mutex_unlock(&io_lock); return -1; }
    ssize_t w = write(fd, buf, count);
    int saved = errno;
    lseek(fd, cur, SEEK_SET);
    pthread_mutex_unlock(&io_lock);
    errno = saved;
    return w;
}

int dirfd(DIR *dirp) {
    (void)dirp;
    errno = ENOSYS;
    return -1;
}

int fstatat(int dirfd_val, const char *pathname, struct stat *statbuf, int flags) {
    (void)dirfd_val;
    (void)flags;
    return stat(pathname, statbuf);
}

/* newlib has no nftw. Enough of it for save-directory removal: depth-first, no symlink policy. */
static int nftw_walk(const char *path, int (*fn)(const char *, const struct stat *, int, struct FTW *),
                     int flags, int level) {
    struct stat st;
    struct FTW info = {0, level};
    if (stat(path, &st)) return fn(path, &st, FTW_NS, &info);
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) return fn(path, &st, FTW_DNR, &info);
        struct dirent *entry;
        char child[1024];
        while ((entry = readdir(dir))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            if (strlen(path) + strlen(entry->d_name) + 2 > sizeof child) continue;
            snprintf(child, sizeof child, "%s/%s", path, entry->d_name);
            if (nftw_walk(child, fn, flags, level + 1)) { closedir(dir); return -1; }
        }
        closedir(dir);
        return fn(path, &st, (flags & FTW_DEPTH) ? FTW_DP : FTW_D, &info);
    }
    return fn(path, &st, FTW_F, &info);
}

int nftw(const char *path, int (*fn)(const char *, const struct stat *, int, struct FTW *),
         int nopenfd, int flags) {
    (void)nopenfd;
    return nftw_walk(path, fn, flags, 0);
}

int pipe(int pipefd[2]) {
    (void)pipefd;
    errno = ENOSYS;
    return -1;
}

int pipe2(int pipefd[2], int flags) {
    (void)pipefd; (void)flags;
    errno = ENOSYS;
    return -1;
}

int execvp(const char *file, char *const argv[]) {
    (void)file; (void)argv;
    errno = ENOSYS;
    return -1;
}

pid_t waitpid(pid_t pid, int *status, int options) {
    (void)pid; (void)status; (void)options;
    errno = ECHILD;
    return -1;
}

int pause(void) {
    struct timespec ts = {1, 0};
    nanosleep(&ts, NULL);
    errno = EINTR;
    return -1;
}

int pthread_setname_np(pthread_t thread, const char *name) {
    (void)thread; (void)name;
    return 0;
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset) {
    (void)how; (void)set; (void)oldset;
    return 0;
}

int timespec_get(struct timespec *ts, int base) {
    if (!ts || base != TIME_UTC) return 0;
    return clock_gettime(CLOCK_REALTIME, ts) == 0 ? base : 0;
}
