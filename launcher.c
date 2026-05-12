/*
 * launcher.c — ET_EXEC FSELF homescreen stub for Moonlight PS5.
 *
 * Uses SYS_sprx_load (594) = sceKernelLoadStartModule to load
 * moonlight-ps5.elf INTO THIS PROCESS. Running in-process gives moonlight
 * the homescreen app's user session, so sceVideoOutOpen works.
 *
 * moonlight's module_start() calls main(), which opens its own TCP
 * connection to HOST_IP:DIAG_PORT and redirects stdout/stderr there.
 *
 * Build: prospero-clang -c -fno-builtin -nostdlib launcher.c -o launcher.o
 *        prospero-lld --static -T eboot.x -o launcher.elf launcher.o
 *        make_fself.py --ptype fake ... launcher.elf launcher_eboot.bin
 */

/* ── Syscall ABI ──────────────────────────────────────────────────────────── */
static long
__syscall(long n, ...)
{
    long a1=0,a2=0,a3=0,a4=0,a5=0,a6=0;
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    a1=__builtin_va_arg(ap,long); a2=__builtin_va_arg(ap,long);
    a3=__builtin_va_arg(ap,long); a4=__builtin_va_arg(ap,long);
    a5=__builtin_va_arg(ap,long); a6=__builtin_va_arg(ap,long);
    __builtin_va_end(ap);
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    unsigned long ret; char err;
    __asm__ __volatile__("syscall"
        : "=a"(ret), "=@ccc"(err), "+r"(r10), "+r"(r8), "+r"(r9)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
    return err ? -(long)ret : (long)ret;
}

/* FreeBSD syscall numbers */
#define SYS_exit      1
#define SYS_sprx_load 594   /* sceKernelLoadStartModule */

static void klog(const char *s) {
    __syscall(0x259, 7L, (long)s, 0L);
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void) {
    klog("[MLPS] launcher v7: calling sprx_load\n");

    int status = 0;
    long rc = __syscall(SYS_sprx_load,
        (long)"/app0/moonlight-ps5.elf",
        0L, 0L, 0L, 0L,
        (long)&status);

    if (rc < 0)
        klog("[MLPS] sprx_load failed\n");
    else
        klog("[MLPS] sprx_load done\n");

    return 0;
}

void _start(void) {
    __syscall(SYS_exit, main());
    __builtin_trap();
}
