#pragma once

#define bit_SSE4_1  (1u << 19)
#define bit_SSE4_2  (1u << 20)
#define bit_MOVBE   (1u << 22)
#define bit_POPCNT  (1u << 23)
#define bit_OSXSAVE (1u << 27)
#define bit_AVX     (1u << 28)
#define bit_BMI     (1u << 3)
#define bit_LZCNT   (1u << 5)

static inline unsigned int __get_cpuid_max(unsigned int ext, unsigned int *sig) {
    (void)ext;
    if (sig) *sig = 0;
    return 0;
}

static inline int __get_cpuid(unsigned int leaf, unsigned int *eax, unsigned int *ebx,
                              unsigned int *ecx, unsigned int *edx) {
    (void)leaf;
    if (eax) *eax = 0;
    if (ebx) *ebx = 0;
    if (ecx) *ecx = 0;
    if (edx) *edx = 0;
    return 0;
}

static inline int __get_cpuid_count(unsigned int leaf, unsigned int subleaf, unsigned int *eax,
                                    unsigned int *ebx, unsigned int *ecx, unsigned int *edx) {
    (void)leaf;
    (void)subleaf;
    if (eax) *eax = 0;
    if (ebx) *ebx = 0;
    if (ecx) *ecx = 0;
    if (edx) *edx = 0;
    return 0;
}
