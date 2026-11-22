/* Unit test for the 80-bit x87 (tbyte) <-> double helpers added to
 * vendor/sr/SR/llasm-support/llasm_float.c.
 *
 * Build and run (host):
 *   gcc -O2 -o build/lift-full/tbyte/test_tbyte \
 *       work/lift-full/tools/test_tbyte.c \
 *       vendor/sr/SR/llasm-support/llasm_float.c -lm
 *   build/lift-full/tbyte/test_tbyte
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* the helpers take a 32-bit guest address; keep the buffers in the low 4 GiB */
static uint8_t g_buf[32] __attribute__((aligned(16)));

/* minimal _cpu declaration, same layout as llasm_cpu.h */
typedef struct {
    uint32_t _eax, _ecx, _edx, _ebx, _esp, _ebp, _esi, _edi, _eflags;
    uint32_t _st_top, _st_sw_cond, _st_cw;
    double _st[8];
    uint64_t _reserved1;
    int64_t _st_result;
    void *stack_bottom, *stack_top;
} _cpu;

extern void x87_fld_tbyte(_cpu *cpu, uint32_t ptr);
extern void x87_fstp_tbyte(_cpu *cpu, uint32_t ptr);

static int failures;

static uint64_t double_bits(double d)
{
    uint64_t b;
    memcpy(&b, &d, sizeof(b));
    return b;
}

static double bits_double(uint64_t b)
{
    double d;
    memcpy(&d, &b, sizeof(d));
    return d;
}

/* round trip through the helper pair */
static void check(const char *name, double value)
{
    _cpu cpu;
    uint8_t *buf = g_buf;
    uint64_t gotbits;

    memset(&cpu, 0, sizeof(cpu));
    memcpy(buf, &value, 8);

    /* double -> tbyte -> double through the helpers */
    cpu._st[0] = value;
    cpu._st_top = 0;
    x87_fstp_tbyte(&cpu, (uint32_t)(uintptr_t)buf);
    if (cpu._st_top != 1) { printf("FAIL %-10s st_top=%u\n", name, cpu._st_top); failures++; return; }

    memset(cpu._st, 0, sizeof(cpu._st));
    cpu._st_top = 0;
    x87_fld_tbyte(&cpu, (uint32_t)(uintptr_t)buf);
    if (cpu._st_top != 7) { printf("FAIL %-10s st_top=%u\n", name, cpu._st_top); failures++; return; }

    gotbits = double_bits(cpu._st[7]);

    if (isnan(value))
    {
        if (!isnan(cpu._st[7])) { printf("FAIL %-10s nan lost (%016llx)\n", name, (unsigned long long)gotbits); failures++; }
        return;
    }
    if (gotbits != double_bits(value))
    {
        printf("FAIL %-10s in=%016llx out=%016llx\n", name,
               (unsigned long long)double_bits(value), (unsigned long long)gotbits);
        failures++;
    }
}

int main(void)
{
    static const uint8_t one80[10] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80, 0xff,0x3f }; /* 1.0 */
    static const uint8_t inf80[10] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80, 0xff,0x7f };
    _cpu cpu;
    unsigned long long i;

    check("zero", 0.0);
    check("-zero", -0.0);
    check("one", 1.0);
    check("minus1", -1.0);
    check("0.5", 0.5);
    check("3.5", 3.5);
    check("1e300", 1e300);
    check("1e-300", 1e-300);
    check("1e308", 1e308);
    check("1e-308", 1e-308);
    check("pi", 3.14159265358979323846);
    check("denormal", 5e-324);
    check("inf", INFINITY);
    check("nan", NAN);

    /* known extended constant: 1.0 -> bytes */
    memset(&cpu, 0, sizeof(cpu));
    {
        uint8_t *buf = g_buf;

        cpu._st[0] = 1.0;
        cpu._st_top = 0;
        x87_fstp_tbyte(&cpu, (uint32_t)(uintptr_t)buf);
        if (memcmp(buf, one80, 10) != 0)
        {
            printf("FAIL 1.0 extended bytes:");
            for (i = 0; i < 10; i++) printf(" %02x", buf[i]);
            printf("\n");
            failures++;
        }
    }

    /* infinity -> extended bytes */
    memset(&cpu, 0, sizeof(cpu));
    {
        uint8_t *buf = g_buf;

        cpu._st[0] = INFINITY;
        cpu._st_top = 0;
        x87_fstp_tbyte(&cpu, (uint32_t)(uintptr_t)buf);
        if (memcmp(buf, inf80, 10) != 0)
        {
            printf("FAIL inf extended bytes:");
            for (i = 0; i < 10; i++) printf(" %02x", buf[i]);
            printf("\n");
            failures++;
        }
    }

    /* load a known extended constant 1.0 */
    memset(&cpu, 0, sizeof(cpu));
    x87_fld_tbyte(&cpu, (uint32_t)(uintptr_t)one80);
    if (cpu._st[7] != 1.0) { printf("FAIL load 1.0 extended\n"); failures++; }

    /* pseudo-denormal (integer bit clear, exponent non-zero) must normalise */
    {
        uint8_t *pd = g_buf; /* 0x3fff, integer bit clear, mantissa 2^62 */

        pd[0]=0x00; pd[1]=0x00; pd[2]=0x00; pd[3]=0x00; pd[4]=0x00; pd[5]=0x00; pd[6]=0x00; pd[7]=0x40;
        pd[8]=0xff; pd[9]=0x3f;
        memset(&cpu, 0, sizeof(cpu));
        x87_fld_tbyte(&cpu, (uint32_t)(uintptr_t)pd);
        if (cpu._st[7] != 0.5) { printf("FAIL pseudo-denormal: %g\n", cpu._st[7]); failures++; }
    }

    printf(failures ? "FAIL: %d test(s)\n" : "PASS: tbyte helpers (%d failures)\n", failures);
    return failures ? 1 : 0;
}
