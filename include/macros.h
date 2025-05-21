#ifndef MACROS_H
#define MACROS_H

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define CLAMP(low, high, x)                                                    \
    ((x) > (high) ? (high) : ((x) < (low) ? (low) : (x)))

#define ROUND_UP(x, align) (((x) + (align) - 1) & (-(align)))
#define ROUND_UP_PTR(x, align)                                                 \
    ((void*)((((u32)(x)) + (align) - 1) & (~((align) - 1))))

#define ROUND_DOWN(x, align) ((x) & (-(align)))
#define ROUND_DOWN_PTR(x, align) ((void*)(((u32)(x)) & (~((align) - 1))))

#define ARRAY_SIZE(x) (sizeof((x)) / sizeof((x)[0]))

#define ALIGN(x) __attribute__((aligned(x)))

#define DECL_SECTION(x) __declspec(section x)
#define DECL_WEAK __declspec(weak)


#define ASSERT(exp)                                                                                                    \
    do {                                                                                                               \
        if (!(exp)) {                                                                                                  \
            OSReport(__FILE__ ":%d: Assert `" #exp "` failed.\n", __LINE__);                                           \
            OSPanic(__FILE__, __LINE__, "Assert `" #exp "` failed.");                                                  \
        }                                                                                                              \
    } while (0)
#define ASSERT_MSG(exp, ...)                                                                                           \
    do {                                                                                                               \
        if (!(exp)) {                                                                                                  \
            OSReport(__FILE__ ":%d: Assert `" #exp "` failed: " __VA_ARGS__ "\n", __LINE__);                           \
            OSPanic(__FILE__, __LINE__, "Assert `" #exp "` failed: " __VA_ARGS__);                                     \
        }                                                                                                              \
    } while (0)


#if __cplusplus < 201103L && !defined(__clang__)
#define override
#define noexcept throw()
#else 
#define override \
    _Pragma("clang diagnostic push") \
    _Pragma("clang diagnostic ignored \"-Wc++11-extensions\"") \
    override \
    _Pragma("clang diagnostic pop")
#endif
#define nullptr NULL

#ifdef __MWERKS__

#define DECLTYPE(x) __decltype__(x)
#define MEMCLR(x) __memclr((x), sizeof(*(x)))
#define ARRAY_AT_ADDRESS(addr) [] : (addr)
#define AT_ADDRESS(addr) : (addr)
#define PPC_ASM asm

// For VSCode
#else

#define DECLTYPE(x) __decltype(x)


// 'Zero Size Array' hack to avoid incomplete type like `u8 foo[];`. 
// the real fix is to make the AT_ADDRESS macro work in clang.
// todo: use extern like that one person on dsicord todl me to. 
#define ARRAY_AT_ADDRESS(addr) [0]
#define AT_ADDRESS(addr)
// this macro was created with the following regex:
// search `asm \{([^{}]*\n?)*\}` replace `PPC_ASM ($1)`
#define PPC_ASM(...)
#define asm
// todo: pick another name for this macro so we can still use __attribute__ without codewarrior
#define __attribute__(x)
#define __declspec(x)

// CWCC builtins
double __fabs(double);

// powerpc sync instruction
#if !__has_builtin(__sync_synchronize)
#warning your language server doesn't have __sync_synchronize support, big L
#define __sync(x)
#else
#define __sync(x) __sync_synchronize(x)
#endif

// powerpc cntlzw instruction
#if !__has_builtin(__builtin_clz)
#warning your language server doesn't have __builtin_clz support, big L
#define __cntlzw(x)
#else
#define __cntlzw(x) __builtin_clz(x)
#endif

#if !__has_builtin(__builtin_memset)
#warning your language server doesn't have __builtin_memset support, big L
#define MEMCLR(x) 
#else
#define MEMCLR(x) __builtin_memset((x), 0, sizeof(*(x)));
#endif

#endif

#endif
