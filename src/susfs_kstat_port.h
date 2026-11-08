/* SUSFS static kstat command for backslashxx/ksu_toolkit.
 * Include from toolkit.c after ksu_sys_reboot() is defined.
 * No libc functions are called (uses small_rt.h's __syscall).
 * ABI must match the running kernel's st_susfs_sus_kstat exactly.
 */
#ifndef KSU_TOOLKIT_SUSFS_KSTAT_PORT_H
#define KSU_TOOLKIT_SUSFS_KSTAT_PORT_H

#include <stdint.h>
#include <limits.h>
#include <sys/stat.h>

#define OZ_SUSFS_SUS_KSTAT 10050
#define SUSFS_MAX_LEN_PATHNAME 256
/* High bit identifies per-field hybrid mode; low 12 bits are overrides. */
#define SUSFS_KSTAT_HYBRID (1U << 16)
#define SUSFS_KSTAT_ALL_MASK 0x0fffU

struct st_susfs_sus_kstat {
    int is_statically;
    unsigned long target_ino;
    char target_pathname[SUSFS_MAX_LEN_PATHNAME];
    unsigned long spoofed_ino;
    unsigned long spoofed_dev;
    unsigned int spoofed_nlink;
    long long spoofed_size;
    long spoofed_atime_tv_sec;
    long spoofed_mtime_tv_sec;
    long spoofed_ctime_tv_sec;
    long spoofed_atime_tv_nsec;
    long spoofed_mtime_tv_nsec;
    long spoofed_ctime_tv_nsec;
    unsigned long spoofed_blksize;
    unsigned long long spoofed_blocks;
};

static int oz_streq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

/* No strtoull dependency. Reject empty, negative, trailing junk, overflow. */
static int susfs_kstat_parse_u64(const char *s, uint64_t *out)
{
    uint64_t v = 0;
    unsigned digit;
    if (*s == '+') ++s;
    if (!*s) return -1;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return -1;
        digit = (unsigned)(*s - '0');
        if (v > (UINT64_MAX - digit) / 10) return -1;
        v = v * 10 + digit;
    }
    *out = v;
    return 0;
}

static int susfs_kstat_parse_i64(const char *s, int64_t *out)
{
    int negative = 0;
    uint64_t v = 0, limit;
    unsigned digit;
    if (*s == '-' || *s == '+') {
        negative = (*s == '-');
        ++s;
    }
    if (!*s) return -1;
    limit = (uint64_t)INT64_MAX + (unsigned)negative;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return -1;
        digit = (unsigned)(*s - '0');
        if (v > (limit - digit) / 10) return -1;
        v = v * 10 + digit;
    }
    *out = negative ? (v == (uint64_t)INT64_MAX + 1
                        ? INT64_MIN : -(int64_t)v) : (int64_t)v;
    return 0;
}

static int susfs_kstat_get_stat(const char *path, struct stat *sb)
{
#if defined(SYS_newfstatat)
    return (int)__syscall(SYS_newfstatat, -100L, (long)path,
                          (long)sb, 0L);
#elif defined(SYS_fstatat64) && defined(__ANDROID__)
    /* Bionic's struct stat is stat64-layout on 32-bit Android. */
    return (int)__syscall(SYS_fstatat64, -100L, (long)path,
                          (long)sb, 0L);
#else
#error "Need newfstatat or Android fstatat64 for SUSFS kstat"
#endif
}

static int toolkit_add_sus_kstat_statically(char **argv)
{
    struct st_susfs_sus_kstat info = {0};
    struct stat sb = {0};
    uint64_t u;
    int64_t s;
    unsigned long path_len;
    uintptr_t marker = 0;
    const int hybrid = oz_streq(argv[1], "add_sus_kstat_hybrid") ||
                       oz_streq(argv[1], "--add-sus-kstat-hybrid");

    path_len = strlen(argv[2]);
    if (!path_len || path_len >= SUSFS_MAX_LEN_PATHNAME)
        return 1;
    if (susfs_kstat_get_stat(argv[2], &sb) < 0)
        return 1;

    info.is_statically = hybrid ? (int)SUSFS_KSTAT_HYBRID : 1;
    info.target_ino = (unsigned long)sb.st_ino;
    for (unsigned long i = 0; i < path_len; ++i)
        info.target_pathname[i] = argv[2][i];
    info.target_pathname[path_len] = '\0';

    info.spoofed_ino = (unsigned long)sb.st_ino;
    info.spoofed_dev = (unsigned long)sb.st_dev;
    info.spoofed_nlink = (unsigned int)sb.st_nlink;
    info.spoofed_size = (long long)sb.st_size;
    info.spoofed_atime_tv_sec = (long)sb.st_atim.tv_sec;
    info.spoofed_mtime_tv_sec = (long)sb.st_mtim.tv_sec;
    info.spoofed_ctime_tv_sec = (long)sb.st_ctim.tv_sec;
    info.spoofed_atime_tv_nsec = (long)sb.st_atim.tv_nsec;
    info.spoofed_mtime_tv_nsec = (long)sb.st_mtim.tv_nsec;
    info.spoofed_ctime_tv_nsec = (long)sb.st_ctim.tv_nsec;
    info.spoofed_blksize = (unsigned long)sb.st_blksize;
    info.spoofed_blocks = (unsigned long long)sb.st_blocks;

#define SUSFS_SET_U(idx, field, maxval) do {                           \
    if (!oz_streq(argv[idx], "default")) {                    \
        if (susfs_kstat_parse_u64(argv[idx], &u) ||                    \
            u > (uint64_t)(maxval)) return 1;                          \
        info.field = u;                                                \
        if (hybrid) info.is_statically |= (1U << ((idx) - 3));          \
    }                                                                  \
} while (0)
#define SUSFS_SET_S(idx, field, minval, maxval) do {                   \
    if (!oz_streq(argv[idx], "default")) {                    \
        if (susfs_kstat_parse_i64(argv[idx], &s) ||                    \
            s < (int64_t)(minval) || s > (int64_t)(maxval)) return 1;  \
        info.field = s;                                                \
        if (hybrid) info.is_statically |= (1U << ((idx) - 3));          \
    }                                                                  \
} while (0)

    SUSFS_SET_U(3,  spoofed_ino, ULONG_MAX);
    SUSFS_SET_U(4,  spoofed_dev, ULONG_MAX);
    SUSFS_SET_U(5,  spoofed_nlink, UINT_MAX);
    SUSFS_SET_U(6,  spoofed_size, LLONG_MAX);
    SUSFS_SET_S(7,  spoofed_atime_tv_sec, LONG_MIN, LONG_MAX);
    SUSFS_SET_U(8,  spoofed_atime_tv_nsec, 999999999ULL);
    SUSFS_SET_S(9,  spoofed_mtime_tv_sec, LONG_MIN, LONG_MAX);
    SUSFS_SET_U(10, spoofed_mtime_tv_nsec, 999999999ULL);
    SUSFS_SET_S(11, spoofed_ctime_tv_sec, LONG_MIN, LONG_MAX);
    SUSFS_SET_U(12, spoofed_ctime_tv_nsec, 999999999ULL);
    SUSFS_SET_U(13, spoofed_blocks, ULLONG_MAX);
    SUSFS_SET_U(14, spoofed_blksize, ULONG_MAX);
#undef SUSFS_SET_U
#undef SUSFS_SET_S

    /* Kernel receives user_info = (struct st_susfs_sus_kstat *)*arg.
     * Thus pass &info directly as reboot syscall's 4th argument.
     * Kernel ack (on success) writes &info to first 8 bytes of info.
     */
    ksu_sys_reboot(OZ_SUSFS_SUS_KSTAT, 0, (long)&info);
    __builtin_memcpy(&marker, &info, sizeof(marker));
    return (marker == (uintptr_t)&info) ? 0 : 1;
}
#endif /* KSU_TOOLKIT_SUSFS_KSTAT_PORT_H */
