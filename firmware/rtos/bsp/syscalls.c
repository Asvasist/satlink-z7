/**
 * @file syscalls.c
 * @brief newlib system call stubs. The firmware never uses stdio or malloc; these exist so
 *        libm (errno) and libc link. _sbrk refuses, so an accidental malloc() fails loudly.
 *
 * @implements SRS-AMP-001
 */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>

#include "rtos/bsp.h"

void *_sbrk(ptrdiff_t increment);
int _write(int fd, const char *buf, int len);
int _read(int fd, char *buf, int len);
int _close(int fd);
int _lseek(int fd, int offset, int whence);
int _fstat(int fd, struct stat *st);
int _isatty(int fd);
void _exit(int status);
int _kill(int pid, int sig);
int _getpid(void);

void *_sbrk(ptrdiff_t increment)
{
    (void)increment;
    errno = ENOMEM;
    return (void *)-1;
}

int _write(int fd, const char *buf, int len)
{
    (void)fd;
    for (int i = 0; i < len; ++i)
    {
        bsp_uart_putc(buf[i]);
    }
    return len;
}

int _read(int fd, char *buf, int len)
{
    (void)fd;
    (void)buf;
    (void)len;
    return 0;
}

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _lseek(int fd, int offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    return 0;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

void _exit(int status)
{
    satlink_fault(BSP_FAULT_ASSERT, (uint32_t)status);
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

int _getpid(void)
{
    return 1;
}
