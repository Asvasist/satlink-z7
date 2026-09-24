/**
 * @file libc_min.c
 * @brief The few C library functions the compiler may call by itself in freestanding code
 *        (structure copies, zero-initialisation). The firmware is linked with -nostdlib.
 *
 * The loops go through volatile pointers so the compiler cannot recognise them and turn them
 * back into calls to the function it is compiling.
 *
 * @implements SRS-HKC-004
 */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dest, const void *src, size_t count);
void *memset(void *dest, int value, size_t count);
int memcmp(const void *a, const void *b, size_t count);

void *memcpy(void *dest, const void *src, size_t count)
{
    volatile uint8_t *d = (volatile uint8_t *)dest;
    const volatile uint8_t *s = (const volatile uint8_t *)src;

    for (size_t i = 0U; i < count; ++i)
    {
        d[i] = s[i];
    }

    return dest;
}

void *memset(void *dest, int value, size_t count)
{
    volatile uint8_t *d = (volatile uint8_t *)dest;

    for (size_t i = 0U; i < count; ++i)
    {
        d[i] = (uint8_t)value;
    }

    return dest;
}

int memcmp(const void *a, const void *b, size_t count)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    int result = 0;

    for (size_t i = 0U; (i < count) && (result == 0); ++i)
    {
        result = (int)pa[i] - (int)pb[i];
    }

    return result;
}
