/* Stub for LLVM's __cpu_model intrinsic used by AVX auto-vectorization.
 * All bits zero = no special CPU features detected, causing the generated
 * code to fall back to scalar paths. The PS5 (Zen 2) will still run fine;
 * SIMD just won't be auto-selected at runtime. */
struct __cpu_model_t {
    unsigned int __cpu_vendor;
    unsigned int __cpu_type;
    unsigned int __cpu_subtype;
    unsigned int __cpu_features[1];
};

__attribute__((weak))
struct __cpu_model_t __cpu_model = { 0, 0, 0, { 0 } };

__attribute__((weak))
unsigned int __cpu_features2[3] = { 0, 0, 0 };
