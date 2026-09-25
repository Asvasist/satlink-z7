/**
 * @file libc_min.c
 * @brief The few libc symbols the compiler may emit calls to. The HKC images link without a C
 *        library to stay small and fully under our control.
 *
 * @implements SRS-HKC-001
 */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0U; i < n; ++i)
    {
        d[i] = s[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s)
    {
        for (size_t i = 0U; i < n; ++i)
        {
            d[i] = s[i];
        }
    }
    else
    {
        for (size_t i = n; i > 0U; --i)
        {
            d[i - 1U] = s[i - 1U];
        }
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0U; i < n; ++i)
    {
        d[i] = (uint8_t)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (size_t i = 0U; i < n; ++i)
    {
        if (x[i] != y[i])
        {
            return (x[i] < y[i]) ? -1 : 1;
        }
    }
    return 0;
}
