/*
 *  AArch64 specific helpers
 *
 *  Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "cpu.h"
#include "gdbstub/helpers.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/bitops.h"
#include "internals.h"
#include "qemu/crc32c.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "qemu/int128.h"
#include "qemu/atomic128.h"
#include "fpu/softfloat.h"
#include <zlib.h> /* For crc32 */

/* C2.4.7 Multiply and divide */
/* special cases for 0 and LLONG_MIN are mandated by the standard */
uint64_t HELPER(udiv64)(uint64_t num, uint64_t den)
{
    if (den == 0) {
        return 0;
    }
    return num / den;
}

int64_t HELPER(sdiv64)(int64_t num, int64_t den)
{
    if (den == 0) {
        return 0;
    }
    if (num == LLONG_MIN && den == -1) {
        return LLONG_MIN;
    }
    return num / den;
}

uint64_t HELPER(rbit64)(uint64_t x)
{
    return revbit64(x);
}

void HELPER(msr_i_spsel)(CPUARMState *env, uint32_t imm)
{
    update_spsel(env, imm);
}

static void daif_check (CPUARMState *env, uint32_t op,
                       uint32_t imm, uintptr_t ra)
{
    /* DAIF update to PSTATE. This is OK from EL0 only if UMA is set.  */
    if (arm_current_el(env) == 0 && !(arm_sctlr(env, 0) & SCTLR_UMA)) {
        raise_exception_ra(env, EXCP_UDEF,
                           syn_aa64_sysregtrap(0, extract32(op, 0, 3),
                                               extract32(op, 3, 3), 4,
                                               imm, 0x1f, 0),
                          exception_target_el(env), ra);
    }
}

void HELPER(msr_i_daifset)(CPUARMState *env, uint32_t imm)
{
    daif_check(env, 0x1e, imm, GETPC());
    env->daif |= (imm << 6) & PSTATE_DAIF;
    arm_rebuild_hflags(env);
}

void HELPER(msr_i_daifclear)(CPUARMState *env, uint32_t imm)
{
    daif_check(env, 0x1f, imm, GETPC());
    env->daif &= ~((imm << 6) & PSTATE_DAIF);
    arm_rebuild_hflags(env);
}

/* Convert a softfloat float_relation_ (as returned by
 * the float*_compare functions) to the correct ARM
 * NZCV flag state.
 */
static inline uint32_t float_rel_to_flags(int res)
{
    uint64_t flags;
    switch (res) {
    case float_relation_equal:
        flags = PSTATE_Z | PSTATE_C;
        break;
    case float_relation_less:
        flags = PSTATE_N;
        break;
    case float_relation_greater:
        flags = PSTATE_C;
        break;
    case float_relation_unordered:
    default:
        flags = PSTATE_C | PSTATE_V;
        break;
    }
    return flags;
}

uint64_t HELPER(vfp_cmph_a64)(uint32_t x, uint32_t y, void *fp_status)
{
    return float_rel_to_flags(float16_compare_quiet(x, y, fp_status));
}

uint64_t HELPER(vfp_cmpeh_a64)(uint32_t x, uint32_t y, void *fp_status)
{
    return float_rel_to_flags(float16_compare(x, y, fp_status));
}

uint64_t HELPER(vfp_cmps_a64)(float32 x, float32 y, void *fp_status)
{
    return float_rel_to_flags(float32_compare_quiet(x, y, fp_status));
}

uint64_t HELPER(vfp_cmpes_a64)(float32 x, float32 y, void *fp_status)
{
    return float_rel_to_flags(float32_compare(x, y, fp_status));
}

uint64_t HELPER(vfp_cmpd_a64)(float64 x, float64 y, void *fp_status)
{
    return float_rel_to_flags(float64_compare_quiet(x, y, fp_status));
}

uint64_t HELPER(vfp_cmped_a64)(float64 x, float64 y, void *fp_status)
{
    return float_rel_to_flags(float64_compare(x, y, fp_status));
}

float32 HELPER(vfp_mulxs)(float32 a, float32 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float32_squash_input_denormal(a, fpst);
    b = float32_squash_input_denormal(b, fpst);

    if ((float32_is_zero(a) && float32_is_infinity(b)) ||
        (float32_is_infinity(a) && float32_is_zero(b))) {
        /* 2.0 with the sign bit set to sign(A) XOR sign(B) */
        return make_float32((1U << 30) |
                            ((float32_val(a) ^ float32_val(b)) & (1U << 31)));
    }
    return float32_mul(a, b, fpst);
}

float64 HELPER(vfp_mulxd)(float64 a, float64 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float64_squash_input_denormal(a, fpst);
    b = float64_squash_input_denormal(b, fpst);

    if ((float64_is_zero(a) && float64_is_infinity(b)) ||
        (float64_is_infinity(a) && float64_is_zero(b))) {
        /* 2.0 with the sign bit set to sign(A) XOR sign(B) */
        return make_float64((1ULL << 62) |
                            ((float64_val(a) ^ float64_val(b)) & (1ULL << 63)));
    }
    return float64_mul(a, b, fpst);
}

/* 64bit/double versions of the neon float compare functions */
uint64_t HELPER(neon_ceq_f64)(float64 a, float64 b, void *fpstp)
{
    float_status *fpst = fpstp;
    return -float64_eq_quiet(a, b, fpst);
}

uint64_t HELPER(neon_cge_f64)(float64 a, float64 b, void *fpstp)
{
    float_status *fpst = fpstp;
    return -float64_le(b, a, fpst);
}

uint64_t HELPER(neon_cgt_f64)(float64 a, float64 b, void *fpstp)
{
    float_status *fpst = fpstp;
    return -float64_lt(b, a, fpst);
}

/* Reciprocal step and sqrt step. Note that unlike the A32/T32
 * versions, these do a fully fused multiply-add or
 * multiply-add-and-halve.
 */

uint32_t HELPER(recpsf_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float16_squash_input_denormal(a, fpst);
    b = float16_squash_input_denormal(b, fpst);

    a = float16_chs(a);
    if ((float16_is_infinity(a) && float16_is_zero(b)) ||
        (float16_is_infinity(b) && float16_is_zero(a))) {
        return float16_two;
    }
    return float16_muladd(a, b, float16_two, 0, fpst);
}

float32 HELPER(recpsf_f32)(float32 a, float32 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float32_squash_input_denormal(a, fpst);
    b = float32_squash_input_denormal(b, fpst);

    a = float32_chs(a);
    if ((float32_is_infinity(a) && float32_is_zero(b)) ||
        (float32_is_infinity(b) && float32_is_zero(a))) {
        return float32_two;
    }
    return float32_muladd(a, b, float32_two, 0, fpst);
}

float64 HELPER(recpsf_f64)(float64 a, float64 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float64_squash_input_denormal(a, fpst);
    b = float64_squash_input_denormal(b, fpst);

    a = float64_chs(a);
    if ((float64_is_infinity(a) && float64_is_zero(b)) ||
        (float64_is_infinity(b) && float64_is_zero(a))) {
        return float64_two;
    }
    return float64_muladd(a, b, float64_two, 0, fpst);
}

uint32_t HELPER(rsqrtsf_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float16_squash_input_denormal(a, fpst);
    b = float16_squash_input_denormal(b, fpst);

    a = float16_chs(a);
    if ((float16_is_infinity(a) && float16_is_zero(b)) ||
        (float16_is_infinity(b) && float16_is_zero(a))) {
        return float16_one_point_five;
    }
    return float16_muladd(a, b, float16_three, float_muladd_halve_result, fpst);
}

float32 HELPER(rsqrtsf_f32)(float32 a, float32 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float32_squash_input_denormal(a, fpst);
    b = float32_squash_input_denormal(b, fpst);

    a = float32_chs(a);
    if ((float32_is_infinity(a) && float32_is_zero(b)) ||
        (float32_is_infinity(b) && float32_is_zero(a))) {
        return float32_one_point_five;
    }
    return float32_muladd(a, b, float32_three, float_muladd_halve_result, fpst);
}

float64 HELPER(rsqrtsf_f64)(float64 a, float64 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float64_squash_input_denormal(a, fpst);
    b = float64_squash_input_denormal(b, fpst);

    a = float64_chs(a);
    if ((float64_is_infinity(a) && float64_is_zero(b)) ||
        (float64_is_infinity(b) && float64_is_zero(a))) {
        return float64_one_point_five;
    }
    return float64_muladd(a, b, float64_three, float_muladd_halve_result, fpst);
}

/* Pairwise long add: add pairs of adjacent elements into
 * double-width elements in the result (eg _s8 is an 8x8->16 op)
 */
uint64_t HELPER(neon_addlp_s8)(uint64_t a)
{
    uint64_t nsignmask = 0x0080008000800080ULL;
    uint64_t wsignmask = 0x8000800080008000ULL;
    uint64_t elementmask = 0x00ff00ff00ff00ffULL;
    uint64_t tmp1, tmp2;
    uint64_t res, signres;

    /* Extract odd elements, sign extend each to a 16 bit field */
    tmp1 = a & elementmask;
    tmp1 ^= nsignmask;
    tmp1 |= wsignmask;
    tmp1 = (tmp1 - nsignmask) ^ wsignmask;
    /* Ditto for the even elements */
    tmp2 = (a >> 8) & elementmask;
    tmp2 ^= nsignmask;
    tmp2 |= wsignmask;
    tmp2 = (tmp2 - nsignmask) ^ wsignmask;

    /* calculate the result by summing bits 0..14, 16..22, etc,
     * and then adjusting the sign bits 15, 23, etc manually.
     * This ensures the addition can't overflow the 16 bit field.
     */
    signres = (tmp1 ^ tmp2) & wsignmask;
    res = (tmp1 & ~wsignmask) + (tmp2 & ~wsignmask);
    res ^= signres;

    return res;
}

uint64_t HELPER(neon_addlp_u8)(uint64_t a)
{
    uint64_t tmp;

    tmp = a & 0x00ff00ff00ff00ffULL;
    tmp += (a >> 8) & 0x00ff00ff00ff00ffULL;
    return tmp;
}

uint64_t HELPER(neon_addlp_s16)(uint64_t a)
{
    int32_t reslo, reshi;

    reslo = (int32_t)(int16_t)a + (int32_t)(int16_t)(a >> 16);
    reshi = (int32_t)(int16_t)(a >> 32) + (int32_t)(int16_t)(a >> 48);

    return (uint32_t)reslo | (((uint64_t)reshi) << 32);
}

uint64_t HELPER(neon_addlp_u16)(uint64_t a)
{
    uint64_t tmp;

    tmp = a & 0x0000ffff0000ffffULL;
    tmp += (a >> 16) & 0x0000ffff0000ffffULL;
    return tmp;
}

/* Floating-point reciprocal exponent - see FPRecpX in ARM ARM */
uint32_t HELPER(frecpx_f16)(uint32_t a, void *fpstp)
{
    float_status *fpst = fpstp;
    uint16_t val16, sbit;
    int16_t exp;

    if (float16_is_any_nan(a)) {
        float16 nan = a;
        if (float16_is_signaling_nan(a, fpst)) {
            float_raise(float_flag_invalid, fpst);
            if (!fpst->default_nan_mode) {
                nan = float16_silence_nan(a, fpst);
            }
        }
        if (fpst->default_nan_mode) {
            nan = float16_default_nan(fpst);
        }
        return nan;
    }

    a = float16_squash_input_denormal(a, fpst);

    val16 = float16_val(a);
    sbit = 0x8000 & val16;
    exp = extract32(val16, 10, 5);

    if (exp == 0) {
        return make_float16(deposit32(sbit, 10, 5, 0x1e));
    } else {
        return make_float16(deposit32(sbit, 10, 5, ~exp));
    }
}

float32 HELPER(frecpx_f32)(float32 a, void *fpstp)
{
    float_status *fpst = fpstp;
    uint32_t val32, sbit;
    int32_t exp;

    if (float32_is_any_nan(a)) {
        float32 nan = a;
        if (float32_is_signaling_nan(a, fpst)) {
            float_raise(float_flag_invalid, fpst);
            if (!fpst->default_nan_mode) {
                nan = float32_silence_nan(a, fpst);
            }
        }
        if (fpst->default_nan_mode) {
            nan = float32_default_nan(fpst);
        }
        return nan;
    }

    a = float32_squash_input_denormal(a, fpst);

    val32 = float32_val(a);
    sbit = 0x80000000ULL & val32;
    exp = extract32(val32, 23, 8);

    if (exp == 0) {
        return make_float32(sbit | (0xfe << 23));
    } else {
        return make_float32(sbit | (~exp & 0xff) << 23);
    }
}

float64 HELPER(frecpx_f64)(float64 a, void *fpstp)
{
    float_status *fpst = fpstp;
    uint64_t val64, sbit;
    int64_t exp;

    if (float64_is_any_nan(a)) {
        float64 nan = a;
        if (float64_is_signaling_nan(a, fpst)) {
            float_raise(float_flag_invalid, fpst);
            if (!fpst->default_nan_mode) {
                nan = float64_silence_nan(a, fpst);
            }
        }
        if (fpst->default_nan_mode) {
            nan = float64_default_nan(fpst);
        }
        return nan;
    }

    a = float64_squash_input_denormal(a, fpst);

    val64 = float64_val(a);
    sbit = 0x8000000000000000ULL & val64;
    exp = extract64(float64_val(a), 52, 11);

    if (exp == 0) {
        return make_float64(sbit | (0x7feULL << 52));
    } else {
        return make_float64(sbit | (~exp & 0x7ffULL) << 52);
    }
}

float32 HELPER(fcvtx_f64_to_f32)(float64 a, CPUARMState *env)
{
    /* Von Neumann rounding is implemented by using round-to-zero
     * and then setting the LSB of the result if Inexact was raised.
     */
    float32 r;
    float_status *fpst = &env->vfp.fp_status;
    float_status tstat = *fpst;
    int exflags;

    set_float_rounding_mode(float_round_to_zero, &tstat);
    set_float_exception_flags(0, &tstat);
    r = float64_to_float32(a, &tstat);
    exflags = get_float_exception_flags(&tstat);
    if (exflags & float_flag_inexact) {
        r = make_float32(float32_val(r) | 1);
    }
    exflags |= get_float_exception_flags(fpst);
    set_float_exception_flags(exflags, fpst);
    return r;
}

/* 64-bit versions of the CRC helpers. Note that although the operation
 * (and the prototypes of crc32c() and crc32() mean that only the bottom
 * 32 bits of the accumulator and result are used, we pass and return
 * uint64_t for convenience of the generated code. Unlike the 32-bit
 * instruction set versions, val may genuinely have 64 bits of data in it.
 * The upper bytes of val (above the number specified by 'bytes') must have
 * been zeroed out by the caller.
 */
uint64_t HELPER(crc32_64)(uint64_t acc, uint64_t val, uint32_t bytes)
{
    uint8_t buf[8];

    stq_le_p(buf, val);

    /* zlib crc32 converts the accumulator and output to one's complement.  */
    return crc32(acc ^ 0xffffffff, buf, bytes) ^ 0xffffffff;
}

uint64_t HELPER(crc32c_64)(uint64_t acc, uint64_t val, uint32_t bytes)
{
    uint8_t buf[8];

    stq_le_p(buf, val);

    /* Linux crc32c converts the output to one's complement.  */
    return crc32c(acc, buf, bytes) ^ 0xffffffff;
}

/*Bad owner or permissions on /root/.ssh/config
fatal: Could not read from remote repository.

Please make sure you have the correct access rights
and the repository exists.
 * AdvSIMD half-precision
 */

#define ADVSIMD_HELPER(name, suffix) HELPER(glue(glue(advsimd_, name), suffix))

#define ADVSIMD_HALFOP(name) \
uint32_t ADVSIMD_HELPER(name, h)(uint32_t a, uint32_t b, void *fpstp) \
{ \
    float_status *fpst = fpstp; \
    return float16_ ## name(a, b, fpst);    \
}

ADVSIMD_HALFOP(add)
ADVSIMD_HALFOP(sub)
ADVSIMD_HALFOP(mul)
ADVSIMD_HALFOP(div)
ADVSIMD_HALFOP(min)
ADVSIMD_HALFOP(max)
ADVSIMD_HALFOP(minnum)
ADVSIMD_HALFOP(maxnum)

#define ADVSIMD_TWOHALFOP(name)                                         \
uint32_t ADVSIMD_HELPER(name, 2h)(uint32_t two_a, uint32_t two_b, void *fpstp) \
{ \
    float16  a1, a2, b1, b2;                        \
    uint32_t r1, r2;                                \
    float_status *fpst = fpstp;                     \
    a1 = extract32(two_a, 0, 16);                   \
    a2 = extract32(two_a, 16, 16);                  \
    b1 = extract32(two_b, 0, 16);                   \
    b2 = extract32(two_b, 16, 16);                  \
    r1 = float16_ ## name(a1, b1, fpst);            \
    r2 = float16_ ## name(a2, b2, fpst);            \
    return deposit32(r1, 16, 16, r2);               \
}

ADVSIMD_TWOHALFOP(add)
ADVSIMD_TWOHALFOP(sub)
ADVSIMD_TWOHALFOP(mul)
ADVSIMD_TWOHALFOP(div)
ADVSIMD_TWOHALFOP(min)
ADVSIMD_TWOHALFOP(max)
ADVSIMD_TWOHALFOP(minnum)
ADVSIMD_TWOHALFOP(maxnum)

/* Data processing - scalar floating-point and advanced SIMD */
static float16 float16_mulx(float16 a, float16 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float16_squash_input_denormal(a, fpst);
    b = float16_squash_input_denormal(b, fpst);

    if ((float16_is_zero(a) && float16_is_infinity(b)) ||
        (float16_is_infinity(a) && float16_is_zero(b))) {
        /* 2.0 with the sign bit set to sign(A) XOR sign(B) */
        return make_float16((1U << 14) |
                            ((float16_val(a) ^ float16_val(b)) & (1U << 15)));
    }
    return float16_mul(a, b, fpst);
}

ADVSIMD_HALFOP(mulx)
ADVSIMD_TWOHALFOP(mulx)

/* fused multiply-accumulate */
uint32_t HELPER(advsimd_muladdh)(uint32_t a, uint32_t b, uint32_t c,
                                 void *fpstp)
{
    float_status *fpst = fpstp;
    return float16_muladd(a, b, c, 0, fpst);
}

uint32_t HELPER(advsimd_muladd2h)(uint32_t two_a, uint32_t two_b,
                                  uint32_t two_c, void *fpstp)
{
    float_status *fpst = fpstp;
    float16  a1, a2, b1, b2, c1, c2;
    uint32_t r1, r2;
    a1 = extract32(two_a, 0, 16);
    a2 = extract32(two_a, 16, 16);
    b1 = extract32(two_b, 0, 16);
    b2 = extract32(two_b, 16, 16);
    c1 = extract32(two_c, 0, 16);
    c2 = extract32(two_c, 16, 16);
    r1 = float16_muladd(a1, b1, c1, 0, fpst);
    r2 = float16_muladd(a2, b2, c2, 0, fpst);
    return deposit32(r1, 16, 16, r2);
}

/*
 * Floating point comparisons produce an integer result. Softfloat
 * routines return float_relation types which we convert to the 0/-1
 * Neon requires.
 */

#define ADVSIMD_CMPRES(test) (test) ? 0xffff : 0

uint32_t HELPER(advsimd_ceq_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    int compare = float16_compare_quiet(a, b, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_equal);
}

uint32_t HELPER(advsimd_cge_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    int compare = float16_compare(a, b, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_greater ||
                          compare == float_relation_equal);
}

uint32_t HELPER(advsimd_cgt_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    int compare = float16_compare(a, b, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_greater);
}

uint32_t HELPER(advsimd_acge_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    float16 f0 = float16_abs(a);
    float16 f1 = float16_abs(b);
    int compare = float16_compare(f0, f1, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_greater ||
                          compare == float_relation_equal);
}

uint32_t HELPER(advsimd_acgt_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    float16 f0 = float16_abs(a);
    float16 f1 = float16_abs(b);
    int compare = float16_compare(f0, f1, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_greater);
}

/* round to integral */
uint32_t HELPER(advsimd_rinth_exact)(uint32_t x, void *fp_status)
{
    return float16_round_to_int(x, fp_status);
}

uint32_t HELPER(advsimd_rinth)(uint32_t x, void *fp_status)
{
    int old_flags = get_float_exception_flags(fp_status), new_flags;
    float16 ret;

    ret = float16_round_to_int(x, fp_status);

    /* Suppress any inexact exceptions the conversion produced */
    if (!(old_flags & float_flag_inexact)) {
        new_flags = get_float_exception_flags(fp_status);
        set_float_exception_flags(new_flags & ~float_flag_inexact, fp_status);
    }

    return ret;
}

/*
 * Half-precision floating point conversion functions
 *
 * There are a multitude of conversion functions with various
 * different rounding modes. This is dealt with by the calling code
 * setting the mode appropriately before calling the helper.
 */

uint32_t HELPER(advsimd_f16tosinth)(uint32_t a, void *fpstp)
{
    float_status *fpst = fpstp;

    /* Invalid if we are passed a NaN */
    if (float16_is_any_nan(a)) {
        float_raise(float_flag_invalid, fpst);
        return 0;
    }
    return float16_to_int16(a, fpst);
}

uint32_t HELPER(advsimd_f16touinth)(uint32_t a, void *fpstp)
{
    float_status *fpst = fpstp;

    /* Invalid if we are passed a NaN */
    if (float16_is_any_nan(a)) {
        float_raise(float_flag_invalid, fpst);
        return 0;
    }
    return float16_to_uint16(a, fpst);
}

static int el_from_spsr(uint32_t spsr)
{
    /* Return the exception level that this SPSR is requesting a return to,
     * or -1 if it is invalid (an illegal return)
     */
    if (spsr & PSTATE_nRW) {
        switch (spsr & CPSR_M) {
        case ARM_CPU_MODE_USR:
            return 0;
        case ARM_CPU_MODE_HYP:
            return 2;
        case ARM_CPU_MODE_FIQ:
        case ARM_CPU_MODE_IRQ:
        case ARM_CPU_MODE_SVC:
        case ARM_CPU_MODE_ABT:
        case ARM_CPU_MODE_UND:
        case ARM_CPU_MODE_SYS:
            return 1;
        case ARM_CPU_MODE_MON:
            /* Returning to Mon from AArch64 is never possible,
             * so this is an illegal return.
             */
        default:
            return -1;
        }
    } else {
        if (extract32(spsr, 1, 1)) {
            /* Return with reserved M[1] bit set */
            return -1;
        }
        if (extract32(spsr, 0, 4) == 1) {
            /* return to EL0 with M[0] bit set */
            return -1;
        }
        return extract32(spsr, 2, 2);
    }
}

static void cpsr_write_from_spsr_elx(CPUARMState *env,
                                     uint32_t val)
{
    uint32_t mask;

    /* Save SPSR_ELx.SS into PSTATE. */
    env->pstate = (env->pstate & ~PSTATE_SS) | (val & PSTATE_SS);
    val &= ~PSTATE_SS;

    /* Move DIT to the correct location for CPSR */
    if (val & PSTATE_DIT) {
        val &= ~PSTATE_DIT;
        val |= CPSR_DIT;
    }

    mask = aarch32_cpsr_valid_mask(env->features, \
        &env_archcpu(env)->isar);
    cpsr_write(env, val, mask, CPSRWriteRaw);
}

void HELPER(exception_return)(CPUARMState *env, uint64_t new_pc)
{
    int cur_el = arm_current_el(env);
    unsigned int spsr_idx = aarch64_banked_spsr_index(cur_el);
    uint32_t spsr = env->banked_spsr[spsr_idx];
    int new_el;
    bool return_to_aa64 = (spsr & PSTATE_nRW) == 0;

    aarch64_save_sp(env, cur_el);

    arm_clear_exclusive(env);

    /* We must squash the PSTATE.SS bit to zero unless both of the
     * following hold:
     *  1. debug exceptions are currently disabled
     *  2. singlestep will be active in the EL we return to
     * We check 1 here and 2 after we've done the pstate/cpsr write() to
     * transition to the EL we're going to.
     */
    if (arm_generate_debug_exceptions(env)) {
        spsr &= ~PSTATE_SS;
    }

    /*
     * FEAT_RME forbids return from EL3 with an invalid security state.
     * We don't need an explicit check for FEAT_RME here because we enforce
     * in scr_write() that you can't set the NSE bit without it.
     */
    if (cur_el == 3 && (env->cp15.scr_el3 & (SCR_NS | SCR_NSE)) == SCR_NSE) {
        goto illegal_return;
    }

    new_el = el_from_spsr(spsr);
    if (new_el == -1) {
        goto illegal_return;
    }
    if (new_el > cur_el || (new_el == 2 && !arm_is_el2_enabled(env))) {
        /* Disallow return to an EL which is unimplemented or higher
         * than the current one.
         */
        goto illegal_return;
    }

    if (new_el != 0 && arm_el_is_aa64(env, new_el) != return_to_aa64) {
        /* Return to an EL which is configured for a different register width */
        goto illegal_return;
    }

    if (new_el == 1 && (arm_hcr_el2_eff(env) & HCR_TGE)) {
        goto illegal_return;
    }

    bql_lock();
    arm_call_pre_el_change_hook(env_archcpu(env));
    bql_unlock();

    if (!return_to_aa64) {
        env->aarch64 = false;
        /* We do a raw CPSR write because aarch64_sync_64_to_32()
         * will sort the register banks out for us, and we've already
         * caught all the bad-mode cases in el_from_spsr().
         */
        cpsr_write_from_spsr_elx(env, spsr);
        if (!arm_singlestep_active(env)) {
            env->pstate &= ~PSTATE_SS;
        }
        aarch64_sync_64_to_32(env);

        if (spsr & CPSR_T) {
            env->regs[15] = new_pc & ~0x1;
        } else {
            env->regs[15] = new_pc & ~0x3;
        }
        helper_rebuild_hflags_a32(env, new_el);
        qemu_log_mask(CPU_LOG_INT, "Exception return from AArch64 EL%d to "
                      "AArch32 EL%d PC 0x%" PRIx32 "\n",
                      cur_el, new_el, env->regs[15]);
    } else {
        int tbii;

        env->aarch64 = true;
        spsr &= aarch64_pstate_valid_mask(&env_archcpu(env)->isar);
        pstate_write(env, spsr);
        if (!arm_singlestep_active(env)) {
            env->pstate &= ~PSTATE_SS;
        }
        aarch64_restore_sp(env, new_el);
        helper_rebuild_hflags_a64(env, new_el);

        /*
         * Apply TBI to the exception return address.  We had to delay this
         * until after we selected the new EL, so that we could select the
         * correct TBI+TBID bits.  This is made easier by waiting until after
         * the hflags rebuild, since we can pull the composite TBII field
         * from there.
         */
        tbii = EX_TBFLAG_A64(env->hflags, TBII);
        if ((tbii >> extract64(new_pc, 55, 1)) & 1) {
            /* TBI is enabled. */
            int core_mmu_idx = arm_env_mmu_index(env);
            if (regime_has_2_ranges(core_to_aa64_mmu_idx(core_mmu_idx))) {
                new_pc = sextract64(new_pc, 0, 56);
            } else {
                new_pc = extract64(new_pc, 0, 56);
            }
        }
        env->pc = new_pc;

        qemu_log_mask(CPU_LOG_INT, "Exception return from AArch64 EL%d to "
                      "AArch64 EL%d PC 0x%" PRIx64 "\n",
                      cur_el, new_el, env->pc);
    }

    /*
     * Note that cur_el can never be 0.  If new_el is 0, then
     * el0_a64 is return_to_aa64, else el0_a64 is ignored.
     */
    aarch64_sve_change_el(env, cur_el, new_el, return_to_aa64);

    bql_lock();
    arm_call_el_change_hook(env_archcpu(env));
    bql_unlock();

    return;

illegal_return:
    /* Illegal return events of various kinds have architecturally
     * mandated behaviour:
     * restore NZCV and DAIF from SPSR_ELx
     * set PSTATE.IL
     * restore PC from ELR_ELx
     * no change to exception level, execution state or stack pointer
     */
    env->pstate |= PSTATE_IL;
    env->pc = new_pc;
    spsr &= PSTATE_NZCV | PSTATE_DAIF;
    spsr |= pstate_read(env) & ~(PSTATE_NZCV | PSTATE_DAIF);
    pstate_write(env, spsr);
    if (!arm_singlestep_active(env)) {
        env->pstate &= ~PSTATE_SS;
    }
    helper_rebuild_hflags_a64(env, cur_el);
    qemu_log_mask(LOG_GUEST_ERROR, "Illegal exception return at EL%d: "
                  "resuming execution at 0x%" PRIx64 "\n", cur_el, env->pc);
}

/*
 * Square Root and Reciprocal square root
 */

uint32_t HELPER(sqrt_f16)(uint32_t a, void *fpstp)
{
    float_status *s = fpstp;

    return float16_sqrt(a, s);
}

void HELPER(dc_zva)(CPUARMState *env, uint64_t vaddr_in)
{
    /*
     * Implement DC ZVA, which zeroes a fixed-length block of memory.
     * Note that we do not implement the (architecturally mandated)
     * alignment fault for attempts to use this on Device memory
     * (which matches the usual QEMU behaviour of not implementing either
     * alignment faults or any memory attribute handling).
     */
    int blocklen = 4 << env_archcpu(env)->dcz_blocksize;
    uint64_t vaddr = vaddr_in & ~(blocklen - 1);
    int mmu_idx = arm_env_mmu_index(env);
    void *mem;

    /*
     * Trapless lookup.  In addition to actual invalid page, may
     * return NULL for I/O, watchpoints, clean pages, etc.
     */
    mem = tlb_vaddr_to_host(env, vaddr, MMU_DATA_STORE, mmu_idx);

#ifndef CONFIG_USER_ONLY
    if (unlikely(!mem)) {
        uintptr_t ra = GETPC();

        /*
         * Trap if accessing an invalid page.  DC_ZVA requires that we supply
         * the original pointer for an invalid page.  But watchpoints require
         * that we probe the actual space.  So do both.
         */
        (void) probe_write(env, vaddr_in, 1, mmu_idx, ra);
        mem = probe_write(env, vaddr, blocklen, mmu_idx, ra);

        if (unlikely(!mem)) {
            /*
             * The only remaining reason for mem == NULL is I/O.
             * Just do a series of byte writes as the architecture demands.
             */
            for (int i = 0; i < blocklen; i++) {
                cpu_stb_mmuidx_ra(env, vaddr + i, 0, mmu_idx, ra);
            }
            return;
        }
    }
#endif

    memset(mem, 0, blocklen);
}

void HELPER(unaligned_access)(CPUARMState *env, uint64_t addr,
                              uint32_t access_type, uint32_t mmu_idx)
{
    arm_cpu_do_unaligned_access(env_cpu(env), addr, access_type,
                                mmu_idx, GETPC());
}

/* Memory operations (memset, memmove, memcpy) */

/*
 * Return true if the CPY* and SET* insns can execute; compare
 * pseudocode CheckMOPSEnabled(), though we refactor it a little.
 */
static bool mops_enabled(CPUARMState *env)
{
    int el = arm_current_el(env);

    if (el < 2 &&
        (arm_hcr_el2_eff(env) & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE) &&
        !(arm_hcrx_el2_eff(env) & HCRX_MSCEN)) {
        return false;
    }

    if (el == 0) {
        if (!el_is_in_host(env, 0)) {
            return env->cp15.sctlr_el[1] & SCTLR_MSCEN;
        } else {
            return env->cp15.sctlr_el[2] & SCTLR_MSCEN;
        }
    }
    return true;
}

static void check_mops_enabled(CPUARMState *env, uintptr_t ra)
{
    if (!mops_enabled(env)) {
        raise_exception_ra(env, EXCP_UDEF, syn_uncategorized(),
                           exception_target_el(env), ra);
    }
}

/*
 * Return the target exception level for an exception due
 * to mismatched arguments in a FEAT_MOPS copy or set.
 * Compare pseudocode MismatchedCpySetTargetEL()
 */
static int mops_mismatch_exception_target_el(CPUARMState *env)
{
    int el = arm_current_el(env);

    if (el > 1) {
        return el;
    }
    if (el == 0 && (arm_hcr_el2_eff(env) & HCR_TGE)) {
        return 2;
    }
    if (el == 1 && (arm_hcrx_el2_eff(env) & HCRX_MCE2)) {
        return 2;
    }
    return 1;
}

/*
 * Check whether an M or E instruction was executed with a CF value
 * indicating the wrong option for this implementation.
 * Assumes we are always Option A.
 */
static void check_mops_wrong_option(CPUARMState *env, uint32_t syndrome,
                                    uintptr_t ra)
{
    if (env->CF != 0) {
        syndrome |= 1 << 17; /* Set the wrong-option bit */
        raise_exception_ra(env, EXCP_UDEF, syndrome,
                           mops_mismatch_exception_target_el(env), ra);
    }
}

/*
 * Return the maximum number of bytes we can transfer starting at addr
 * without crossing a page boundary.
 */
static uint64_t page_limit(uint64_t addr)
{
    return TARGET_PAGE_ALIGN(addr + 1) - addr;
}

/*
 * Return the number of bytes we can copy starting from addr and working
 * backwards without crossing a page boundary.
 */
static uint64_t page_limit_rev(uint64_t addr)
{
    return (addr & ~TARGET_PAGE_MASK) + 1;
}

/*
 * Perform part of a memory set on an area of guest memory starting at
 * toaddr (a dirty address) and extending for setsize bytes.
 *
 * Returns the number of bytes actually set, which might be less than
 * setsize; the caller should loop until the whole set has been done.
 * The caller should ensure that the guest registers are correct
 * for the possibility that the first byte of the set encounters
 * an exception or watchpoint. We guarantee not to take any faults
 * for bytes other than the first.
 */
static uint64_t set_step(CPUARMState *env, uint64_t toaddr,
                         uint64_t setsize, uint32_t data, int memidx,
                         uint32_t *mtedesc, uintptr_t ra)
{
    void *mem;

    setsize = MIN(setsize, page_limit(toaddr));
    if (*mtedesc) {
        uint64_t mtesize = mte_mops_probe(env, toaddr, setsize, *mtedesc);
        if (mtesize == 0) {
            /* Trap, or not. All CPU state is up to date */
            mte_check_fail(env, *mtedesc, toaddr, ra);
            /* Continue, with no further MTE checks required */
            *mtedesc = 0;
        } else {
            /* Advance to the end, or to the tag mismatch */
            setsize = MIN(setsize, mtesize);
        }
    }

    toaddr = useronly_clean_ptr(toaddr);
    /*
     * Trapless lookup: returns NULL for invalid page, I/O,
     * watchpoints, clean pages, etc.
     */
    mem = tlb_vaddr_to_host(env, toaddr, MMU_DATA_STORE, memidx);

#ifndef CONFIG_USER_ONLY
    if (unlikely(!mem)) {
        /*
         * Slow-path: just do one byte write. This will handle the
         * watchpoint, invalid page, etc handling correctly.
         * For clean code pages, the next iteration will see
         * the page dirty and will use the fast path.
         */
        cpu_stb_mmuidx_ra(env, toaddr, data, memidx, ra);
        return 1;
    }
#endif
    /* Easy case: just memset the host memory */
    memset(mem, data, setsize);
    return setsize;
}

/*
 * Similar, but setting tags. The architecture requires us to do this
 * in 16-byte chunks. SETP accesses are not tag checked; they set
 * the tags.
 */
static uint64_t set_step_tags(CPUARMState *env, uint64_t toaddr,
                              uint64_t setsize, uint32_t data, int memidx,
                              uint32_t *mtedesc, uintptr_t ra)
{
    void *mem;
    uint64_t cleanaddr;

    setsize = MIN(setsize, page_limit(toaddr));

    cleanaddr = useronly_clean_ptr(toaddr);
    /*
     * Trapless lookup: returns NULL for invalid page, I/O,
     * watchpoints, clean pages, etc.
     */
    mem = tlb_vaddr_to_host(env, cleanaddr, MMU_DATA_STORE, memidx);

#ifndef CONFIG_USER_ONLY
    if (unlikely(!mem)) {
        /*
         * Slow-path: just do one write. This will handle the
         * watchpoint, invalid page, etc handling correctly.
         * The architecture requires that we do 16 bytes at a time,
         * and we know both ptr and size are 16 byte aligned.
         * For clean code pages, the next iteration will see
         * the page dirty and will use the fast path.
         */
        uint64_t repldata = data * 0x0101010101010101ULL;
        MemOpIdx oi16 = make_memop_idx(MO_TE | MO_128, memidx);
        cpu_st16_mmu(env, toaddr, int128_make128(repldata, repldata), oi16, ra);
        mte_mops_set_tags(env, toaddr, 16, *mtedesc);
        return 16;
    }
#endif
    /* Easy case: just memset the host memory */
    memset(mem, data, setsize);
    mte_mops_set_tags(env, toaddr, setsize, *mtedesc);
    return setsize;
}

typedef uint64_t StepFn(CPUARMState *env, uint64_t toaddr,
                        uint64_t setsize, uint32_t data,
                        int memidx, uint32_t *mtedesc, uintptr_t ra);

/* Extract register numbers from a MOPS exception syndrome value */
static int mops_destreg(uint32_t syndrome)
{
    return extract32(syndrome, 10, 5);
}

static int mops_srcreg(uint32_t syndrome)
{
    return extract32(syndrome, 5, 5);
}

static int mops_sizereg(uint32_t syndrome)
{
    return extract32(syndrome, 0, 5);
}

/*
 * Return true if TCMA and TBI bits mean we need to do MTE checks.
 * We only need to do this once per MOPS insn, not for every page.
 */
static bool mte_checks_needed(uint64_t ptr, uint32_t desc)
{
    int bit55 = extract64(ptr, 55, 1);

    /*
     * Note that tbi_check() returns true for "access checked" but
     * tcma_check() returns true for "access unchecked".
     */
    if (!tbi_check(desc, bit55)) {
        return false;
    }
    return !tcma_check(desc, bit55, allocation_tag_from_addr(ptr));
}

/* Take an exception if the SETG addr/size are not granule aligned */
static void check_setg_alignment(CPUARMState *env, uint64_t ptr, uint64_t size,
                                 uint32_t memidx, uintptr_t ra)
{
    if ((size != 0 && !QEMU_IS_ALIGNED(ptr, TAG_GRANULE)) ||
        !QEMU_IS_ALIGNED(size, TAG_GRANULE)) {
        arm_cpu_do_unaligned_access(env_cpu(env), ptr, MMU_DATA_STORE,
                                    memidx, ra);

    }
}

static uint64_t arm_reg_or_xzr(CPUARMState *env, int reg)
{
    /*
     * Runtime equivalent of cpu_reg() -- return the CPU register value,
     * for contexts when index 31 means XZR (not SP).
     */
    return reg == 31 ? 0 : env->xregs[reg];
}

/*
 * For the Memory Set operation, our implementation chooses
 * always to use "option A", where we update Xd to the final
 * address in the SETP insn, and set Xn to be -(bytes remaining).
 * On SETM and SETE insns we only need update Xn.
 *
 * @env: CPU
 * @syndrome: syndrome value for mismatch exceptions
 * (also contains the register numbers we need to use)
 * @mtedesc: MTE descriptor word
 * @stepfn: function which does a single part of the set operation
 * @is_setg: true if this is the tag-setting SETG variant
 */
static void do_setp(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc,
                    StepFn *stepfn, bool is_setg, uintptr_t ra)
{
    /* Prologue: we choose to do up to the next page boundary */
    int rd = mops_destreg(syndrome);
    int rs = mops_srcreg(syndrome);
    int rn = mops_sizereg(syndrome);
    uint8_t data = arm_reg_or_xzr(env, rs);
    uint32_t memidx = FIELD_EX32(mtedesc, MTEDESC, MIDX);
    uint64_t toaddr = env->xregs[rd];
    uint64_t setsize = env->xregs[rn];
    uint64_t stagesetsize, step;

    check_mops_enabled(env, ra);

    if (setsize > INT64_MAX) {
        setsize = INT64_MAX;
        if (is_setg) {
            setsize &= ~0xf;
        }
    }

    if (unlikely(is_setg)) {
        check_setg_alignment(env, toaddr, setsize, memidx, ra);
    } else if (!mte_checks_needed(toaddr, mtedesc)) {
        mtedesc = 0;
    }

    stagesetsize = MIN(setsize, page_limit(toaddr));
    while (stagesetsize) {
        env->xregs[rd] = toaddr;
        env->xregs[rn] = setsize;
        step = stepfn(env, toaddr, stagesetsize, data, memidx, &mtedesc, ra);
        toaddr += step;
        setsize -= step;
        stagesetsize -= step;
    }
    /* Insn completed, so update registers to the Option A format */
    env->xregs[rd] = toaddr + setsize;
    env->xregs[rn] = -setsize;

    /* Set NZCV = 0000 to indicate we are an Option A implementation */
    env->NF = 0;
    env->ZF = 1; /* our env->ZF encoding is inverted */
    env->CF = 0;
    env->VF = 0;
    return;
}

void HELPER(setp)(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc)
{
    do_setp(env, syndrome, mtedesc, set_step, false, GETPC());
}

void HELPER(setgp)(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc)
{
    do_setp(env, syndrome, mtedesc, set_step_tags, true, GETPC());
}

static void do_setm(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc,
                    StepFn *stepfn, bool is_setg, uintptr_t ra)
{
    /* Main: we choose to do all the full-page chunks */
    CPUState *cs = env_cpu(env);
    int rd = mops_destreg(syndrome);
    int rs = mops_srcreg(syndrome);
    int rn = mops_sizereg(syndrome);
    uint8_t data = arm_reg_or_xzr(env, rs);
    uint64_t toaddr = env->xregs[rd] + env->xregs[rn];
    uint64_t setsize = -env->xregs[rn];
    uint32_t memidx = FIELD_EX32(mtedesc, MTEDESC, MIDX);
    uint64_t step, stagesetsize;

    check_mops_enabled(env, ra);

    /*
     * We're allowed to NOP out "no data to copy" before the consistency
     * checks; we choose to do so.
     */
    if (env->xregs[rn] == 0) {
        return;
    }

    check_mops_wrong_option(env, syndrome, ra);

    /*
     * Our implementation will work fine even if we have an unaligned
     * destination address, and because we update Xn every time around
     * the loop below and the return value from stepfn() may be less
     * than requested, we might find toaddr is unaligned. So we don't
     * have an IMPDEF check for alignment here.
     */

    if (unlikely(is_setg)) {
        check_setg_alignment(env, toaddr, setsize, memidx, ra);
    } else if (!mte_checks_needed(toaddr, mtedesc)) {
        mtedesc = 0;
    }

    /* Do the actual memset: we leave the last partial page to SETE */
    stagesetsize = setsize & TARGET_PAGE_MASK;
    while (stagesetsize > 0) {
        step = stepfn(env, toaddr, setsize, data, memidx, &mtedesc, ra);
        toaddr += step;
        setsize -= step;
        stagesetsize -= step;
        env->xregs[rn] = -setsize;
        if (stagesetsize > 0 && unlikely(cpu_loop_exit_requested(cs))) {
            cpu_loop_exit_restore(cs, ra);
        }
    }
}

void HELPER(setm)(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc)
{
    do_setm(env, syndrome, mtedesc, set_step, false, GETPC());
}

void HELPER(setgm)(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc)
{
    do_setm(env, syndrome, mtedesc, set_step_tags, true, GETPC());
}

static void do_sete(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc,
                    StepFn *stepfn, bool is_setg, uintptr_t ra)
{
    /* Epilogue: do the last partial page */
    int rd = mops_destreg(syndrome);
    int rs = mops_srcreg(syndrome);
    int rn = mops_sizereg(syndrome);
    uint8_t data = arm_reg_or_xzr(env, rs);
    uint64_t toaddr = env->xregs[rd] + env->xregs[rn];
    uint64_t setsize = -env->xregs[rn];
    uint32_t memidx = FIELD_EX32(mtedesc, MTEDESC, MIDX);
    uint64_t step;

    check_mops_enabled(env, ra);

    /*
     * We're allowed to NOP out "no data to copy" before the consistency
     * checks; we choose to do so.
     */
    if (setsize == 0) {
        return;
    }

    check_mops_wrong_option(env, syndrome, ra);

    /*
     * Our implementation has no address alignment requirements, but
     * we do want to enforce the "less than a page" size requirement,
     * so we don't need to have the "check for interrupts" here.
     */
    if (setsize >= TARGET_PAGE_SIZE) {
        raise_exception_ra(env, EXCP_UDEF, syndrome,
                           mops_mismatch_exception_target_el(env), ra);
    }

    if (unlikely(is_setg)) {
        check_setg_alignment(env, toaddr, setsize, memidx, ra);
    } else if (!mte_checks_needed(toaddr, mtedesc)) {
        mtedesc = 0;
    }

    /* Do the actual memset */
    while (setsize > 0) {
        step = stepfn(env, toaddr, setsize, data, memidx, &mtedesc, ra);
        toaddr += step;
        setsize -= step;
        env->xregs[rn] = -setsize;
    }
}

void HELPER(sete)(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc)
{
    do_sete(env, syndrome, mtedesc, set_step, false, GETPC());
}

void HELPER(setge)(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc)
{
    do_sete(env, syndrome, mtedesc, set_step_tags, true, GETPC());
}

/*
 * Perform part of a memory copy from the guest memory at fromaddr
 * and extending for copysize bytes, to the guest memory at
 * toaddr. Both addresses are dirty.
 *
 * Returns the number of bytes actually set, which might be less than
 * copysize; the caller should loop until the whole copy has been done.
 * The caller should ensure that the guest registers are correct
 * for the possibility that the first byte of the copy encounters
 * an exception or watchpoint. We guarantee not to take any faults
 * for bytes other than the first.
 */
static uint64_t copy_step(CPUARMState *env, uint64_t toaddr, uint64_t fromaddr,
                          uint64_t copysize, int wmemidx, int rmemidx,
                          uint32_t *wdesc, uint32_t *rdesc, uintptr_t ra)
{
    void *rmem;
    void *wmem;

    /* Don't cross a page boundary on either source or destination */
    copysize = MIN(copysize, page_limit(toaddr));
    copysize = MIN(copysize, page_limit(fromaddr));
    /*
     * Handle MTE tag checks: either handle the tag mismatch for byte 0,
     * or else copy up to but not including the byte with the mismatch.
     */
    if (*rdesc) {
        uint64_t mtesize = mte_mops_probe(env, fromaddr, copysize, *rdesc);
        if (mtesize == 0) {
            mte_check_fail(env, *rdesc, fromaddr, ra);
            *rdesc = 0;
        } else {
            copysize = MIN(copysize, mtesize);
        }
    }
    if (*wdesc) {
        uint64_t mtesize = mte_mops_probe(env, toaddr, copysize, *wdesc);
        if (mtesize == 0) {
            mte_check_fail(env, *wdesc, toaddr, ra);
            *wdesc = 0;
        } else {
            copysize = MIN(copysize, mtesize);
        }
    }

    toaddr = useronly_clean_ptr(toaddr);
    fromaddr = useronly_clean_ptr(fromaddr);
    /* Trapless lookup of whether we can get a host memory pointer */
    wmem = tlb_vaddr_to_host(env, toaddr, MMU_DATA_STORE, wmemidx);
    rmem = tlb_vaddr_to_host(env, fromaddr, MMU_DATA_LOAD, rmemidx);

#ifndef CONFIG_USER_ONLY
    /*
     * If we don't have host memory for both source and dest then just
     * do a single byte copy. This will handle watchpoints, invalid pages,
     * etc correctly. For clean code pages, the next iteration will see
     * the page dirty and will use the fast path.
     */
    if (unlikely(!rmem || !wmem)) {
        uint8_t byte;
        if (rmem) {
            byte = *(uint8_t *)rmem;
        } else {
            byte = cpu_ldub_mmuidx_ra(env, fromaddr, rmemidx, ra);
        }
        if (wmem) {
            *(uint8_t *)wmem = byte;
        } else {
            cpu_stb_mmuidx_ra(env, toaddr, byte, wmemidx, ra);
        }
        return 1;
    }
#endif
    /* Easy case: just memmove the host memory */
    memmove(wmem, rmem, copysize);
    return copysize;
}

/*
 * Do part of a backwards memory copy. Here toaddr and fromaddr point
 * to the *last* byte to be copied.
 */
static uint64_t copy_step_rev(CPUARMState *env, uint64_t toaddr,
                              uint64_t fromaddr,
                              uint64_t copysize, int wmemidx, int rmemidx,
                              uint32_t *wdesc, uint32_t *rdesc, uintptr_t ra)
{
    void *rmem;
    void *wmem;

    /* Don't cross a page boundary on either source or destination */
    copysize = MIN(copysize, page_limit_rev(toaddr));
    copysize = MIN(copysize, page_limit_rev(fromaddr));

    /*
     * Handle MTE tag checks: either handle the tag mismatch for byte 0,
     * or else copy up to but not including the byte with the mismatch.
     */
    if (*rdesc) {
        uint64_t mtesize = mte_mops_probe_rev(env, fromaddr, copysize, *rdesc);
        if (mtesize == 0) {
            mte_check_fail(env, *rdesc, fromaddr, ra);
            *rdesc = 0;
        } else {
            copysize = MIN(copysize, mtesize);
        }
    }
    if (*wdesc) {
        uint64_t mtesize = mte_mops_probe_rev(env, toaddr, copysize, *wdesc);
        if (mtesize == 0) {
            mte_check_fail(env, *wdesc, toaddr, ra);
            *wdesc = 0;
        } else {
            copysize = MIN(copysize, mtesize);
        }
    }

    toaddr = useronly_clean_ptr(toaddr);
    fromaddr = useronly_clean_ptr(fromaddr);
    /* Trapless lookup of whether we can get a host memory pointer */
    wmem = tlb_vaddr_to_host(env, toaddr, MMU_DATA_STORE, wmemidx);
    rmem = tlb_vaddr_to_host(env, fromaddr, MMU_DATA_LOAD, rmemidx);

#ifndef CONFIG_USER_ONLY
    /*
     * If we don't have host memory for both source and dest then just
     * do a single byte copy. This will handle watchpoints, invalid pages,
     * etc correctly. For clean code pages, the next iteration will see
     * the page dirty and will use the fast path.
     */
    if (unlikely(!rmem || !wmem)) {
        uint8_t byte;
        if (rmem) {
            byte = *(uint8_t *)rmem;
        } else {
            byte = cpu_ldub_mmuidx_ra(env, fromaddr, rmemidx, ra);
        }
        if (wmem) {
            *(uint8_t *)wmem = byte;
        } else {
            cpu_stb_mmuidx_ra(env, toaddr, byte, wmemidx, ra);
        }
        return 1;
    }
#endif
    /*
     * Easy case: just memmove the host memory. Note that wmem and
     * rmem here point to the *last* byte to copy.
     */
    memmove(wmem - (copysize - 1), rmem - (copysize - 1), copysize);
    return copysize;
}

/*
 * for the Memory Copy operation, our implementation chooses always
 * to use "option A", where we update Xd and Xs to the final addresses
 * in the CPYP insn, and then in CPYM and CPYE only need to update Xn.
 *
 * @env: CPU
 * @syndrome: syndrome value for mismatch exceptions
 * (also contains the register numbers we need to use)
 * @wdesc: MTE descriptor for the writes (destination)
 * @rdesc: MTE descriptor for the reads (source)
 * @move: true if this is CPY (memmove), false for CPYF (memcpy forwards)
 */
static void do_cpyp(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                    uint32_t rdesc, uint32_t move, uintptr_t ra)
{
    int rd = mops_destreg(syndrome);
    int rs = mops_srcreg(syndrome);
    int rn = mops_sizereg(syndrome);
    uint32_t rmemidx = FIELD_EX32(rdesc, MTEDESC, MIDX);
    uint32_t wmemidx = FIELD_EX32(wdesc, MTEDESC, MIDX);
    bool forwards = true;
    uint64_t toaddr = env->xregs[rd];
    uint64_t fromaddr = env->xregs[rs];
    uint64_t copysize = env->xregs[rn];
    uint64_t stagecopysize, step;

    check_mops_enabled(env, ra);


    if (move) {
        /*
         * Copy backwards if necessary. The direction for a non-overlapping
         * copy is IMPDEF; we choose forwards.
         */
        if (copysize > 0x007FFFFFFFFFFFFFULL) {
            copysize = 0x007FFFFFFFFFFFFFULL;
        }
        uint64_t fs = extract64(fromaddr, 0, 56);
        uint64_t ts = extract64(toaddr, 0, 56);
        uint64_t fe = extract64(fromaddr + copysize, 0, 56);

        if (fs < ts && fe > ts) {
            forwards = false;
        }
    } else {
        if (copysize > INT64_MAX) {
            copysize = INT64_MAX;
        }
    }

    if (!mte_checks_needed(fromaddr, rdesc)) {
        rdesc = 0;
    }
    if (!mte_checks_needed(toaddr, wdesc)) {
        wdesc = 0;
    }

    if (forwards) {
        stagecopysize = MIN(copysize, page_limit(toaddr));
        stagecopysize = MIN(stagecopysize, page_limit(fromaddr));
        while (stagecopysize) {
            env->xregs[rd] = toaddr;
            env->xregs[rs] = fromaddr;
            env->xregs[rn] = copysize;
            step = copy_step(env, toaddr, fromaddr, stagecopysize,
                             wmemidx, rmemidx, &wdesc, &rdesc, ra);
            toaddr += step;
            fromaddr += step;
            copysize -= step;
            stagecopysize -= step;
        }
        /* Insn completed, so update registers to the Option A format */
        env->xregs[rd] = toaddr + copysize;
        env->xregs[rs] = fromaddr + copysize;
        env->xregs[rn] = -copysize;
    } else {
        /*
         * In a reverse copy the to and from addrs in Xs and Xd are the start
         * of the range, but it's more convenient for us to work with pointers
         * to the last byte being copied.
         */
        toaddr += copysize - 1;
        fromaddr += copysize - 1;
        stagecopysize = MIN(copysize, page_limit_rev(toaddr));
        stagecopysize = MIN(stagecopysize, page_limit_rev(fromaddr));
        while (stagecopysize) {
            env->xregs[rn] = copysize;
            step = copy_step_rev(env, toaddr, fromaddr, stagecopysize,
                                 wmemidx, rmemidx, &wdesc, &rdesc, ra);
            copysize -= step;
            stagecopysize -= step;
            toaddr -= step;
            fromaddr -= step;
        }
        /*
         * Insn completed, so update registers to the Option A format.
         * For a reverse copy this is no different to the CPYP input format.
         */
        env->xregs[rn] = copysize;
    }

    /* Set NZCV = 0000 to indicate we are an Option A implementation */
    env->NF = 0;
    env->ZF = 1; /* our env->ZF encoding is inverted */
    env->CF = 0;
    env->VF = 0;
    return;
}

void HELPER(cpyp)(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                  uint32_t rdesc)
{
    do_cpyp(env, syndrome, wdesc, rdesc, true, GETPC());
}

void HELPER(cpyfp)(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                   uint32_t rdesc)
{
    do_cpyp(env, syndrome, wdesc, rdesc, false, GETPC());
}

static void do_cpym(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                    uint32_t rdesc, uint32_t move, uintptr_t ra)
{
    /* Main: we choose to copy until less than a page remaining */
    CPUState *cs = env_cpu(env);
    int rd = mops_destreg(syndrome);
    int rs = mops_srcreg(syndrome);
    int rn = mops_sizereg(syndrome);
    uint32_t rmemidx = FIELD_EX32(rdesc, MTEDESC, MIDX);
    uint32_t wmemidx = FIELD_EX32(wdesc, MTEDESC, MIDX);
    bool forwards = true;
    uint64_t toaddr, fromaddr, copysize, step;

    check_mops_enabled(env, ra);

    /* We choose to NOP out "no data to copy" before consistency checks */
    if (env->xregs[rn] == 0) {
        return;
    }

    check_mops_wrong_option(env, syndrome, ra);

    if (move) {
        forwards = (int64_t)env->xregs[rn] < 0;
    }

    if (forwards) {
        toaddr = env->xregs[rd] + env->xregs[rn];
        fromaddr = env->xregs[rs] + env->xregs[rn];
        copysize = -env->xregs[rn];
    } else {
        copysize = env->xregs[rn];
        /* This toaddr and fromaddr point to the *last* byte to copy */
        toaddr = env->xregs[rd] + copysize - 1;
        fromaddr = env->xregs[rs] + copysize - 1;
    }

    if (!mte_checks_needed(fromaddr, rdesc)) {
        rdesc = 0;
    }
    if (!mte_checks_needed(toaddr, wdesc)) {
        wdesc = 0;
    }

    /* Our implementation has no particular parameter requirements for CPYM */

    /* Do the actual memmove */
    if (forwards) {
        while (copysize >= TARGET_PAGE_SIZE) {
            step = copy_step(env, toaddr, fromaddr, copysize,
                             wmemidx, rmemidx, &wdesc, &rdesc, ra);
            toaddr += step;
            fromaddr += step;
            copysize -= step;
            env->xregs[rn] = -copysize;
            if (copysize >= TARGET_PAGE_SIZE &&
                unlikely(cpu_loop_exit_requested(cs))) {
                cpu_loop_exit_restore(cs, ra);
            }
        }
    } else {
        while (copysize >= TARGET_PAGE_SIZE) {
            step = copy_step_rev(env, toaddr, fromaddr, copysize,
                                 wmemidx, rmemidx, &wdesc, &rdesc, ra);
            toaddr -= step;
            fromaddr -= step;
            copysize -= step;
            env->xregs[rn] = copysize;
            if (copysize >= TARGET_PAGE_SIZE &&
                unlikely(cpu_loop_exit_requested(cs))) {
                cpu_loop_exit_restore(cs, ra);
            }
        }
    }
}

void HELPER(cpym)(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                  uint32_t rdesc)
{
    do_cpym(env, syndrome, wdesc, rdesc, true, GETPC());
}

void HELPER(cpyfm)(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                   uint32_t rdesc)
{
    do_cpym(env, syndrome, wdesc, rdesc, false, GETPC());
}

static void do_cpye(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                    uint32_t rdesc, uint32_t move, uintptr_t ra)
{
    /* Epilogue: do the last partial page */
    int rd = mops_destreg(syndrome);
    int rs = mops_srcreg(syndrome);
    int rn = mops_sizereg(syndrome);
    uint32_t rmemidx = FIELD_EX32(rdesc, MTEDESC, MIDX);
    uint32_t wmemidx = FIELD_EX32(wdesc, MTEDESC, MIDX);
    bool forwards = true;
    uint64_t toaddr, fromaddr, copysize, step;

    check_mops_enabled(env, ra);

    /* We choose to NOP out "no data to copy" before consistency checks */
    if (env->xregs[rn] == 0) {
        return;
    }

    check_mops_wrong_option(env, syndrome, ra);

    if (move) {
        forwards = (int64_t)env->xregs[rn] < 0;
    }

    if (forwards) {
        toaddr = env->xregs[rd] + env->xregs[rn];
        fromaddr = env->xregs[rs] + env->xregs[rn];
        copysize = -env->xregs[rn];
    } else {
        copysize = env->xregs[rn];
        /* This toaddr and fromaddr point to the *last* byte to copy */
        toaddr = env->xregs[rd] + copysize - 1;
        fromaddr = env->xregs[rs] + copysize - 1;
    }

    if (!mte_checks_needed(fromaddr, rdesc)) {
        rdesc = 0;
    }
    if (!mte_checks_needed(toaddr, wdesc)) {
        wdesc = 0;
    }

    /* Check the size; we don't want to have do a check-for-interrupts */
    if (copysize >= TARGET_PAGE_SIZE) {
        raise_exception_ra(env, EXCP_UDEF, syndrome,
                           mops_mismatch_exception_target_el(env), ra);
    }

    /* Do the actual memmove */
    if (forwards) {
        while (copysize > 0) {
            step = copy_step(env, toaddr, fromaddr, copysize,
                             wmemidx, rmemidx, &wdesc, &rdesc, ra);
            toaddr += step;
            fromaddr += step;
            copysize -= step;
            env->xregs[rn] = -copysize;
        }
    } else {
        while (copysize > 0) {
            step = copy_step_rev(env, toaddr, fromaddr, copysize,
                                 wmemidx, rmemidx, &wdesc, &rdesc, ra);
            toaddr -= step;
            fromaddr -= step;
            copysize -= step;
            env->xregs[rn] = copysize;
        }
    }
}

void HELPER(cpye)(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                  uint32_t rdesc)
{
    do_cpye(env, syndrome, wdesc, rdesc, true, GETPC());
}

void HELPER(cpyfe)(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                   uint32_t rdesc)
{
    do_cpye(env, syndrome, wdesc, rdesc, false, GETPC());
}

//#ifdef TARGET_CRYPTO_CAP
typedef enum capPermFlags {
    READ = 1,
    WRITE = 2,
    EXEC = 4,
    TRANS = 8,
} capPermFlagsType;

static uint64_t pac_cell_shuffle(uint64_t i)
{
    uint64_t o = 0;

    o |= extract64(i, 52, 4);
    o |= extract64(i, 24, 4) << 4;
    o |= extract64(i, 44, 4) << 8;
    o |= extract64(i,  0, 4) << 12;

    o |= extract64(i, 28, 4) << 16;
    o |= extract64(i, 48, 4) << 20;
    o |= extract64(i,  4, 4) << 24;
    o |= extract64(i, 40, 4) << 28;

    o |= extract64(i, 32, 4) << 32;
    o |= extract64(i, 12, 4) << 36;
    o |= extract64(i, 56, 4) << 40;
    o |= extract64(i, 20, 4) << 44;

    o |= extract64(i,  8, 4) << 48;
    o |= extract64(i, 36, 4) << 52;
    o |= extract64(i, 16, 4) << 56;
    o |= extract64(i, 60, 4) << 60;

    return o;
}

static uint64_t pac_cell_inv_shuffle(uint64_t i)
{
    uint64_t o = 0;

    o |= extract64(i, 12, 4);
    o |= extract64(i, 24, 4) << 4;
    o |= extract64(i, 48, 4) << 8;
    o |= extract64(i, 36, 4) << 12;

    o |= extract64(i, 56, 4) << 16;
    o |= extract64(i, 44, 4) << 20;
    o |= extract64(i,  4, 4) << 24;
    o |= extract64(i, 16, 4) << 28;

    o |= i & MAKE_64BIT_MASK(32, 4);
    o |= extract64(i, 52, 4) << 36;
    o |= extract64(i, 28, 4) << 40;
    o |= extract64(i,  8, 4) << 44;

    o |= extract64(i, 20, 4) << 48;
    o |= extract64(i,  0, 4) << 52;
    o |= extract64(i, 40, 4) << 56;
    o |= i & MAKE_64BIT_MASK(60, 4);

    return o;
}

static uint64_t pac_sub(uint64_t i)
{
    static const uint8_t sub1[16] = {
        0xa, 0xd, 0xe, 0x6, 0xf, 0x7, 0x3, 0x5,
        0x9, 0x8, 0x0, 0xc, 0xb, 0x1, 0x2, 0x4,
    };
    uint64_t o = 0;
    int b;

    for (b = 0; b < 64; b += 4) {
        o |= (uint64_t)sub1[(i >> b) & 0xf] << b;
    }
    return o;
}

static uint64_t pac_inv_sub(uint64_t i)
{
    static const uint8_t inv_sub[16] = {
        0x5, 0xe, 0xd, 0x8, 0xa, 0xb, 0x1, 0x9,
        0x2, 0x6, 0xf, 0x0, 0x4, 0xc, 0x7, 0x3,
    };
    uint64_t o = 0;
    int b;

    for (b = 0; b < 64; b += 4) {
        o |= (uint64_t)inv_sub[(i >> b) & 0xf] << b;
    }
    return o;
}

static int rot_cell(int cell, int n)
{
    /* 4-bit rotate left by n.  */
    cell |= cell << 4;
    return extract32(cell, 4 - n, 4);
}

static uint64_t pac_mult(uint64_t i)
{
    uint64_t o = 0;
    int b;

    for (b = 0; b < 4 * 4; b += 4) {
        int i0, i4, i8, ic, t0, t1, t2, t3;

        i0 = extract64(i, b, 4);
        i4 = extract64(i, b + 4 * 4, 4);
        i8 = extract64(i, b + 8 * 4, 4);
        ic = extract64(i, b + 12 * 4, 4);

        t0 = rot_cell(i8, 1) ^ rot_cell(i4, 2) ^ rot_cell(i0, 1);
        t1 = rot_cell(ic, 1) ^ rot_cell(i4, 1) ^ rot_cell(i0, 2);
        t2 = rot_cell(ic, 2) ^ rot_cell(i8, 1) ^ rot_cell(i0, 1);
        t3 = rot_cell(ic, 1) ^ rot_cell(i8, 2) ^ rot_cell(i4, 1);

        o |= (uint64_t)t3 << b;
        o |= (uint64_t)t2 << (b + 4 * 4);
        o |= (uint64_t)t1 << (b + 8 * 4);
        o |= (uint64_t)t0 << (b + 12 * 4);
    }
    return o;
}

static uint64_t tweak_cell_rot(uint64_t cell)
{
    return (cell >> 1) | (((cell ^ (cell >> 1)) & 1) << 3);
}

static uint64_t tweak_shuffle(uint64_t i)
{
    uint64_t o = 0;

    o |= extract64(i, 16, 4) << 0;
    o |= extract64(i, 20, 4) << 4;
    o |= tweak_cell_rot(extract64(i, 24, 4)) << 8;
    o |= extract64(i, 28, 4) << 12;

    o |= tweak_cell_rot(extract64(i, 44, 4)) << 16;
    o |= extract64(i,  8, 4) << 20;
    o |= extract64(i, 12, 4) << 24;
    o |= tweak_cell_rot(extract64(i, 32, 4)) << 28;

    o |= extract64(i, 48, 4) << 32;
    o |= extract64(i, 52, 4) << 36;
    o |= extract64(i, 56, 4) << 40;
    o |= tweak_cell_rot(extract64(i, 60, 4)) << 44;

    o |= tweak_cell_rot(extract64(i,  0, 4)) << 48;
    o |= extract64(i,  4, 4) << 52;
    o |= tweak_cell_rot(extract64(i, 40, 4)) << 56;
    o |= tweak_cell_rot(extract64(i, 36, 4)) << 60;

    return o;
}

static uint64_t tweak_cell_inv_rot(uint64_t cell)
{
    return ((cell << 1) & 0xf) | ((cell & 1) ^ (cell >> 3));
}

static uint64_t tweak_inv_shuffle(uint64_t i)
{
    uint64_t o = 0;

    o |= tweak_cell_inv_rot(extract64(i, 48, 4));
    o |= extract64(i, 52, 4) << 4;
    o |= extract64(i, 20, 4) << 8;
    o |= extract64(i, 24, 4) << 12;

    o |= extract64(i,  0, 4) << 16;
    o |= extract64(i,  4, 4) << 20;
    o |= tweak_cell_inv_rot(extract64(i,  8, 4)) << 24;
    o |= extract64(i, 12, 4) << 28;

    o |= tweak_cell_inv_rot(extract64(i, 28, 4)) << 32;
    o |= tweak_cell_inv_rot(extract64(i, 60, 4)) << 36;
    o |= tweak_cell_inv_rot(extract64(i, 56, 4)) << 40;
    o |= tweak_cell_inv_rot(extract64(i, 16, 4)) << 44;

    o |= extract64(i, 32, 4) << 48;
    o |= extract64(i, 36, 4) << 52;
    o |= extract64(i, 40, 4) << 56;
    o |= tweak_cell_inv_rot(extract64(i, 44, 4)) << 60;

    return o;
}


static uint64_t qarma_block_encrypt(uint64_t plaintext, CCKey key)
{
    static const uint64_t RC[5] = {
        0x0000000000000000ull,
        0x13198A2E03707344ull,
        0xA4093822299F31D0ull,
        0x082EFA98EC4E6C89ull,
        0x452821E638D01377ull,
    };
    int iterations = 2;
    uint64_t key0 = key.hi, key1 = key.lo;
    uint64_t state, roundkey;
    int i;

    state = plaintext ^ key0;

    for (i = 0; i <= iterations; ++i) {
        roundkey = key1;
        state ^= roundkey;
        state ^= RC[i];
        if (i > 0) {
            state = pac_cell_shuffle(state);
            state = pac_mult(state);
        }
        state = pac_sub(state);
    }

    state ^= key1;
    state = pac_cell_shuffle(state);
    state = pac_mult(state);
    state = pac_sub(state);
    state = pac_cell_shuffle(state);
    state = pac_mult(state);
    state ^= key0;

    return state;
}

static uint64_t qarma_mac(uint64_t data1, uint64_t data2,  uint64_t data3,  uint32_t data4, CCKey key)
{
    uint64_t mac = 0;  // Initialization vector
    
    // First block
    mac = qarma_block_encrypt(data1 ^ mac, key);
    
    // Second block
    mac = qarma_block_encrypt(data2 ^ mac, key);

    // Third block
    mac = qarma_block_encrypt(data3 ^ mac, key);
    
    // Fourth block (padded)
    uint64_t last_block = ((uint64_t)data4 << 32) | 0x80000000;  // Padding
    mac = qarma_block_encrypt(last_block ^ mac, key);
    
    return mac;
}


static uint64_t qarma_mac64(uint64_t data1, uint64_t data2,  uint64_t data3,  uint64_t data4, CCKey key)
{
    uint64_t mac = 0;  // Initialization vector
    
    // First block
    mac = qarma_block_encrypt(data1 ^ mac, key);
    
    // Second block
    mac = qarma_block_encrypt(data2 ^ mac, key);

    // Third block
    mac = qarma_block_encrypt(data3 ^ mac, key);
    
    // Fourth block
    mac = qarma_block_encrypt(data4 ^ mac, key);
    
    return mac;
}


static uint64_t computeMAC(uint64_t tcr, uint64_t perms_base, uint64_t PT, uint32_t size, CCKey mkey){
    return qarma_mac(tcr, perms_base, PT, size, mkey);
}

static bool is_perms_violation(uint64_t perms, capPermFlagsType flag){
    uint64_t val=(uint64_t)flag;
    val&=perms;
    return (val==0);
}

static bool is_bounds_violation(uint64_t base, uint32_t offset, uint32_t size){
    return (offset > size);
}

static bool is_MAC_violation(uint64_t tcr, uint64_t perms_base, uint32_t size, uint64_t PT, uint64_t MAC, CCKey mkey){
    //TODO: Remove later
    if (MAC==0)
        return false;
    uint64_t refMAC=computeMAC(tcr, perms_base, PT, size, mkey);
    return (MAC!=refMAC);
}

static bool is_privileged_mode(CPUARMState *env){
    int current_el = arm_current_el(env);
    return (current_el > 0);
}

static bool is_cross_domain(CPUARMState *env, uint64_t PT){
    // ns: non-secure mode, s: secure mode
    // TTBR0: base register 0 (typically user space)
    // TTBR1: base register 1 (@should be for kernel space) we're using TTBR0_EL1, adjust if needed
   
    uint64_t ttbr0 = env->cp15.ttbr0_ns;  // ns for non-secure mode s for secure mod we're using TTBR0_EL1, adjust if needed
    //uint64_t ttbr = env->cp15.ttbr1_ns;  // ns for non-secure mode s for secure mod we're using TTBR0_EL1, adjust if needed
    
    if (ttbr0 != 0 && ttbr0!=PT)
        return true;
    return false;
}

static bool is_host_domain(CPUARMState *env, uint64_t PT){
    // ns: non-secure mode, s: secure mode
    // TTBR0: base register 0 (typically user space)
    // TTBR1: base register 1 (@should be for kernel space) we're using TTBR0_EL1, adjust if needed
    
    uint64_t ttbr0 = env->cp15.ttbr0_ns;  // ns for non-secure mode s for secure mod we're using TTBR0_EL1, adjust if needed
    //uint64_t ttbr = env->cp15.ttbr1_ns;  // ns for non-secure mode s for secure mod we're using TTBR0_EL1, adjust if needed
  
    if (PT == 0 || (PT!=0 && ttbr0 != 0 && ttbr0==PT))
        return true;
    return false;
}

void HELPER(updtcr)(CPUARMState *env)
{
    // Check if the current exception level is appropriate (e.g., EL1 or higher)
    if (!is_privileged_mode(env)) {
        // Raise an exception if the privilege level is insufficient
        raise_exception(env, EXCP_UDEF, syn_uncategorized(), exception_target_el(env));
        return;
    }
}

void HELPER(updckeys)(CPUARMState *env)
{
    // Check if the current exception level is appropriate (e.g., EL1 or higher)
    if (!is_privileged_mode(env)) {
        // Raise an exception if the privilege level is insufficient
        raise_exception(env, EXCP_UDEF, syn_uncategorized(), exception_target_el(env));
        return;
    }
}


void HELPER(csigncl)(CPUARMState *env, uint64_t target_pid, uint64_t host_pid, uint64_t pc, uint64_t MAC)    
{
    // Check if the current exception level is appropriate (e.g., EL1 or higher)
    if (!is_privileged_mode(env)) {
        // Raise an exception if the privilege level is insufficient
        raise_exception(env, EXCP_UDEF, syn_uncategorized(), exception_target_el(env));
        return;
    }
    CCKey key=env->mkey;
    env->pclc.FIELD[3]=qarma_mac64(target_pid, host_pid, pc, 0, key);
    return;
}

// void HELPER(csign)(CPUARMState *env, uint64_t crs_idx, uint64_t perms_base, uint32_t size, uint64_t PT, uint64_t MAC)    
// {
//     // Check if the current exception level is appropriate (e.g., EL1 or higher)
//     if (!is_privileged_mode(env)) {
//         // Raise an exception if the privilege level is insufficient
//         raise_exception(env, EXCP_UDEF, syn_uncategorized(), exception_target_el(env));
//         return;
//     }

//     //creating a capability from metadata on the fly
//     //important this should be done before switching PT as the host PT cannot be lost
//     CCKey mkey=env->mkey;
//     uint64_t tcr = env->tcr;
//     uint64_t ptcr = env->ptcr;
//     uint64_t MACval=0;

//     //assuming TCR is updated for the call and resigning a capability
//     if (!is_host_domain(env, PT)){
//         if (is_MAC_violation(ptcr, perms_base, size, PT, MAC, mkey)){
//             int syn = syn_data_abort_no_iss(arm_current_el(env) != 0, 0, 0, 0, 0, 1, 0x11);
//             raise_exception_ra(env, EXCP_DATA_ABORT, syn,
//                             exception_target_el(env), GETPC());
//             return;
//         }
//         MACval=computeMAC(tcr, perms_base, PT, size, mkey);
//         env->ccregs[crs_idx].MAC=MACval;
        
//     }//creation of a capability for the first time
//     else{
//         uint64_t ttbr0 = env->cp15.ttbr0_ns;
//         //uint64_t ttbr = env->cp15.ttbr1_ns;
//         env->ccregs[crs_idx].PT=ttbr0;
//         MACval=computeMAC(tcr, perms_base, ttbr0, size, mkey);
//         env->ccregs[crs_idx].MAC=MACval;
//     } 
//     return;
// }

void HELPER(csign)(CPUARMState *env, uint64_t crs_idx, uint64_t perms_base, uint32_t size, uint64_t PT, uint64_t MAC)    
{
    // Check if the current exception level is appropriate (e.g., EL1 or higher)
    if (!is_privileged_mode(env)) {
        // Raise an exception if the privilege level is insufficient
        raise_exception(env, EXCP_UDEF, syn_uncategorized(), exception_target_el(env));
        return;
    }

    //creating a capability from metadata on the fly
    //important this should be done before switching PT as the host PT cannot be lost
    CCKey mkey=env->mkey;
    uint64_t tcr = env->tcr;
    uint64_t ptcr = env->ptcr;
    uint64_t MACval=0;

    //assuming TCR is updated for the call and resigning a capability
    if (!is_host_domain(env, PT)){
        if (is_MAC_violation(ptcr, perms_base, size, PT, MAC, mkey)){
            int syn = syn_data_abort_no_iss(arm_current_el(env) != 0, 0, 0, 0, 0, 1, 0x11);
            raise_exception_ra(env, EXCP_DATA_ABORT, syn,
                            exception_target_el(env), GETPC());
            return;
        }
        MACval=computeMAC(tcr, perms_base, PT, size, mkey);
        env->ccregs[crs_idx].MAC=MACval;
        
    }//creation of a capability for the first time
    else{
        uint64_t ttbr0 = env->cp15.ttbr0_ns;
        //uint64_t ttbr = env->cp15.ttbr1_ns;
        env->ccregs[crs_idx].PT=ttbr0;
        MACval=computeMAC(tcr, perms_base, ttbr0, size, mkey);
        env->ccregs[crs_idx].MAC=MACval;
    } 
    return;
}


void HELPER(ccreate)(CPUARMState *env, uint64_t crd_idx, uint64_t perms_base, uint64_t offset_size)    
{
    CCKey mkey=env->mkey;
    uint64_t PT=env->cp15.ttbr0_ns;
    //uint64_t tcr = env->tcr;
    uint64_t tcr = 0;
    //uint64_t tcr = env->cp15.ttbr0_ns;
    uint32_t offset= (uint32_t)offset_size;
    uint32_t size= (uint32_t)(offset_size>>32);
    uint64_t MACval=computeMAC(tcr, perms_base, PT, size, mkey);
    env->ccregs[crd_idx].perms_base=perms_base;
    env->ccregs[crd_idx].offset=offset;
    env->ccregs[crd_idx].size=size;
    env->ccregs[crd_idx].PT=PT;
    env->ccregs[crd_idx].MAC=MACval;
    return;
}

void HELPER(csetbase)(CPUARMState *env, uint64_t crd, uint64_t crs, uint64_t rs)    
{
    //uint64_t tcr = env->tcr;
    uint64_t tcr = 0;
    //uint64_t tcr = env->cp15.ttbr0_ns; 
    uint64_t crs_perms = env->ccregs[crs].perms_base;
    crs_perms >>= 48;
    crs_perms <<= 48;
    
    uint64_t crs_base=env->ccregs[crs].perms_base & 0x0000FFFFFFFFFFFF;
    uint64_t new_base = env->xregs[rs] & 0x0000FFFFFFFFFFFF; 
    if (new_base>((uint64_t)env->ccregs[crs].size+crs_base)
        || new_base<crs_base 
        || is_MAC_violation(tcr, env->ccregs[crs].perms_base, env->ccregs[crs].size, env->ccregs[crs].PT, env->ccregs[crs].MAC, env->mkey)){
        int syn = syn_data_abort_no_iss(arm_current_el(env) != 0, 0, 0, 0, 0, 1, 0x11);
        raise_exception_ra(env, EXCP_DATA_ABORT, syn, exception_target_el(env), GETPC());
    }
    else{
        env->ccregs[crd].perms_base=(crs_perms|new_base);
        env->ccregs[crd].offset=env->ccregs[crs].offset;
        env->ccregs[crd].size=env->ccregs[crs].size;
        env->ccregs[crd].PT=env->ccregs[crs].PT;
        uint64_t newMAC=computeMAC(tcr, env->ccregs[crd].perms_base, env->ccregs[crd].PT, env->ccregs[crd].size, env->mkey);
        env->ccregs[crd].MAC=newMAC;
    }
    return;
}

void HELPER(csetperms)(CPUARMState *env, uint64_t crd, uint64_t crs, uint64_t rs) 
{
    //uint64_t tcr = env->tcr;
    uint64_t tcr = 0;
    //uint64_t tcr = env->cp15.ttbr0_ns; 
    uint64_t crs_perms = env->ccregs[crs].perms_base;
    crs_perms >>= 48;
    crs_perms <<= 48;
    
    uint64_t crs_base=env->ccregs[crs].perms_base & 0x0000FFFFFFFFFFFF;
    uint64_t new_perms = env->xregs[rs] & 0xFFFF000000000000; 
    new_perms>>=48;
    if ((new_perms|crs_perms)>crs_perms
        || is_MAC_violation(tcr, env->ccregs[crs].perms_base, env->ccregs[crs].size, env->ccregs[crs].PT, env->ccregs[crs].MAC, env->mkey)){
        int syn = syn_data_abort_no_iss(arm_current_el(env) != 0, 0, 0, 0, 0, 1, 0x11);
        raise_exception_ra(env, EXCP_DATA_ABORT, syn, exception_target_el(env), GETPC());
    }
    else{
        env->ccregs[crd].perms_base=(new_perms|crs_base);
        env->ccregs[crd].offset=env->ccregs[crs].offset;
        env->ccregs[crd].size=env->ccregs[crs].size;
        env->ccregs[crd].PT=env->ccregs[crs].PT;
        uint64_t newMAC=computeMAC(tcr, env->ccregs[crd].perms_base, env->ccregs[crd].PT, env->ccregs[crd].size, env->mkey);
        env->ccregs[crd].MAC=newMAC;
    }
    return;
}

void HELPER(csetsize)(CPUARMState *env, uint64_t crd, uint64_t crs, uint64_t rs)   
{
    //uint64_t tcr = env->tcr;
    uint64_t tcr = 0;
    //uint64_t tcr = env->cp15.ttbr0_ns; 
    uint32_t crs_size=env->ccregs[crs].size;
    uint32_t new_size=(uint32_t)env->xregs[rs]; 
    if (new_size>crs_size
        || is_MAC_violation(tcr, env->ccregs[crs].perms_base, env->ccregs[crs].size, env->ccregs[crs].PT, env->ccregs[crs].MAC, env->mkey)){
        int syn = syn_data_abort_no_iss(arm_current_el(env) != 0, 0, 0, 0, 0, 1, 0x11);
        raise_exception_ra(env, EXCP_DATA_ABORT, syn, exception_target_el(env), GETPC());
    }
    else{
        env->ccregs[crd].perms_base=env->ccregs[crs].perms_base;
        env->ccregs[crd].offset=env->ccregs[crs].offset;
        env->ccregs[crd].size=new_size;
        env->ccregs[crd].PT=env->ccregs[crs].PT;
        uint64_t newMAC=computeMAC(tcr, env->ccregs[crd].perms_base, env->ccregs[crd].PT, env->ccregs[crd].size, env->mkey);
        env->ccregs[crd].MAC=newMAC;
    }
     return;
}
void HELPER(stc)(CPUARMState *env, uint64_t cr_idx, uint64_t perms_base, uint32_t size, uint64_t PT)
{
    CCKey mkey=env->mkey;
    //uint64_t tcr = env->tcr;
    uint64_t tcr = 0;
    //uint64_t tcr = env->cp15.ttbr0_ns;
    uint64_t MACval=computeMAC(tcr, perms_base, PT, size, mkey);
    env->ccregs[cr_idx].MAC=MACval;
    return;
}
void HELPER(ldc)(CPUARMState *env, uint64_t perms_base, uint32_t size, uint64_t PT, uint64_t MAC)
{
    CCKey mkey=env->mkey;
    //uint64_t tcr = env->tcr;
    uint64_t tcr = 0;
    //uint64_t tcr = env->cp15.ttbr0_ns;
    uint64_t refMAC=computeMAC(tcr, perms_base, PT, size, mkey);
    if (refMAC!=MAC){
        int syn = syn_data_abort_no_iss(arm_current_el(env) != 0, 0, 0, 0, 0, 0, 0x11);
        raise_exception_ra(env, EXCP_DATA_ABORT, syn, exception_target_el(env), GETPC());
    }
    return;
}

void HELPER(cstg)(CPUARMState *env, uint64_t size_idx, uint64_t r_idx, uint64_t perms_base, uint32_t offset, uint32_t size, uint64_t PT, uint64_t MAC, uint64_t curr_pc)
{
    uint64_t perms = (perms_base >> 48);  
    uint64_t base = (perms_base & 0x0000FFFFFFFFFFFF);  
    uint64_t addr= base+(uint64_t)offset;
    //uint64_t tcr = env->tcr;
    uint64_t tcr = 0;
    //uint64_t tcr = env->cp15.ttbr0_ns;
    uint64_t svalue=env->xregs[r_idx];
    CCKey mkey=env->mkey;
    //intra-domain access
    if (!is_cross_domain(env, PT)){
        switch (size_idx){
            case 0:
                cpu_stq_data(env, addr, svalue);
                break;
            case 1:
                cpu_stl_data(env, addr, svalue);
                break;
            case 2:
                cpu_stw_data(env, addr, svalue);
                break;
            case 3:
                cpu_stb_data(env, addr, svalue);
                break;
            default:
                cpu_stq_data(env, addr, svalue);
        }
        return;   
    }//cross-domain access
    else{    
        //check permissions and bounds
        if (
            //is_MAC_violation(tcr, perms_base, size, PT, MAC, mkey) ||
            is_perms_violation(perms, WRITE) ||
            is_bounds_violation(base, offset, size)) {
            int syn = syn_data_abort_no_iss(arm_current_el(env) != 0, 0, 0, 0, 0, 1, 0x11);
            raise_exception_ra(env, EXCP_DATA_ABORT, syn,
                            exception_target_el(env), GETPC());
            
        }

        //env->cc_access_flag=true;
        //these values are set to help page walker to identify cross-domain memory accesses 
        //env->pc=curr_pc;
        env->cc_access_pc=env->pc;
        //env->cc_access_pc=curr_pc;
        env->cc_access_ttbr0=env->cp15.ttbr0_ns;
        env->cc_ttbr0=PT;
        switch (size_idx){
            case 0:
                cpu_stq_data(env, addr, svalue);
                break;
            case 1:
                cpu_stl_data(env, addr, svalue);
                break;
            case 2:
                cpu_stw_data(env, addr, svalue);
                break;
            case 3:
                cpu_stb_data(env, addr, svalue);
                break;
            default:
                cpu_stq_data(env, addr, svalue);
        }
        //env->cc_access_flag=false;
        return; 
    }
    return;
}

void HELPER(cldg)(CPUARMState *env, uint64_t size_idx, uint64_t r_idx, uint64_t perms_base, uint32_t offset, uint32_t size, uint64_t PT, uint64_t MAC, uint64_t curr_pc)
{

    //tlb_flush(env_cpu(env));
    //arm_rebuild_hflags(env);
    uint64_t perms = (perms_base >> 48);  
    uint64_t base = (perms_base & 0x0000FFFFFFFFFFFF);  
    uint64_t addr= base+(uint64_t)offset;
    //uint64_t tcr = env->tcr;
    uint64_t tcr = 0;
    //uint64_t tcr = env->cp15.ttbr0_ns;
    uint64_t lvalue=0;
    CCKey mkey=env->mkey;
    //intra-domain access
    env->cc_tcrel1=env->cp15.tcr_el[1];
    if (!is_cross_domain(env, PT)){
    //if (false){
        switch (size_idx){
            case 0:
                lvalue = cpu_ldq_data(env, addr);
                break;
            case 1:
                lvalue = cpu_ldl_data(env, addr);
                break;
            case 2:
                lvalue = cpu_lduw_data(env, addr);
                break;
            case 3:
                lvalue = cpu_ldub_data(env, addr);
                break;
            default:
                lvalue = cpu_ldq_data(env, addr);
        }
        env->xregs[r_idx] = lvalue;
        return;   
    }//cross-domain access
    else{    
       //check permissions and bounds
        if (
            //is_MAC_violation(tcr, perms_base, size, PT, MAC, mkey) ||
            is_perms_violation(perms, READ) || 
            is_bounds_violation(base, offset, size)) {
            int syn = syn_data_abort_no_iss(arm_current_el(env) != 0, 0, 0, 0, 0, 0, 0x11);
            raise_exception_ra(env, EXCP_DATA_ABORT, syn,
                            exception_target_el(env), GETPC());
            return;
        }
        //env->pc=curr_pc;
        env->cc_access_pc=env->pc;
        //env->cc_access_pc=curr_pc;
        //env->pc=curr_pc;
        env->cc_access_ttbr0=env->cp15.ttbr0_ns;
        env->cc_ttbr0=PT;
        //E0PD0, bit [55] FEAT_E0PD:  Unprivileged access to any address translated by TTBR0_EL1 will not generate a fault by this mechanism.
        //HD, bit [40] FEAT_HAFDBS: Stage 1 hardware management of dirty state disabled.
        //HA, bit [39] FEAT_HAFDBS: Stage 1 Access flag update disabled.
        //EPD0, bit [7] Perform translation table walks using TTBR0_EL1.
        uint64_t tcr_el1=env->cp15.tcr_el[1];
        tcr_el1&=0b1111111101111111111111100111111111111111111111111111111101111111;
        env->cp15.tcr_el[1]=tcr_el1;

        //uint64_t asid=PT;
        //asid>>=48;
        //asid<<=48;
        //addr|=asid;
        

        // tlb_flush(env_cpu(env));
        // arm_rebuild_hflags(env);
        
        switch (size_idx){
            case 0:
                lvalue = cpu_ldq_data(env, addr);
                break;
            case 1:
                lvalue = cpu_ldl_data(env, addr);
                break;
            case 2:
                lvalue = cpu_lduw_data(env, addr);
                break;
            case 3:
                lvalue = cpu_ldub_data_crca(env, addr);
                //lvalue = cpu_ldub_data(env, addr);
                //lvalue = cpu_ldub_data_cc(env, addr);
                break;
            default:
                lvalue = cpu_ldq_data(env, addr);
        }
        env->xregs[r_idx] = lvalue;
    }
    env->cp15.tcr_el[1]=env->cc_tcrel1;
    return;
}

void HELPER(cldc)(CPUARMState *env, uint64_t crd_idx, uint64_t perms_base, uint32_t offset, uint32_t size, uint64_t PT, uint64_t MAC)
{
    uint64_t perms = (perms_base >> 48);  
    uint64_t base = (perms_base & 0x0000FFFFFFFFFFFF);  
    uint64_t addr= base+(uint64_t)offset;
    //uint64_t tcr = env->tcr;
    uint64_t tcr = 0;
    //uint64_t tcr = env->cp15.ttbr0_ns;
    uint64_t dest_perms_base=0;
    uint32_t dest_offset=0;
    uint32_t dest_size=0;
    uint64_t dest_PT=0;
    uint64_t dest_MAC=0;
    
    uint64_t val32=0;
    CCKey mkey=env->mkey;
    //intra-domain access
    if (!is_cross_domain(env, PT)){
        
        //perms_base
        dest_perms_base = cpu_ldq_data(env, addr);
        env->ccregs[crd_idx].perms_base = dest_perms_base;
        addr+=8;
        
        //offset
        dest_offset=cpu_ldl_data(env, addr);
        env->ccregs[crd_idx].offset = dest_offset;
        addr+=4;
        
        //size
        dest_size=cpu_ldl_data(env, addr);
        env->ccregs[crd_idx].size = dest_size;
        addr+=4;

        //PT
        dest_PT = cpu_ldq_data(env, addr);
        env->ccregs[crd_idx].PT = dest_PT;
        addr+=8;

        //MAC
        dest_MAC = cpu_ldq_data(env, addr);
        env->ccregs[crd_idx].MAC = dest_MAC;
        
        return;   
    }//cross-domain access
    else{    
        //check permissions and bounds
        if (
            //is_MAC_violation(tcr, perms_base, size, PT, MAC, mkey) ||
            is_perms_violation(perms, READ) || 
            is_bounds_violation(base, offset, size)) {
            int syn = syn_data_abort_no_iss(arm_current_el(env) != 0, 0, 0, 0, 0, 0, 0x11);
            raise_exception_ra(env, EXCP_DATA_ABORT, syn,
                            exception_target_el(env), GETPC());
            return;
        }

        //perms_base
        dest_perms_base = cpu_ldq_data(env, addr);
        env->ccregs[crd_idx].perms_base = dest_perms_base;
        addr+=8;

        //offset
        dest_offset=cpu_ldl_data(env, addr);
        env->ccregs[crd_idx].offset = dest_offset;
        addr+=4;

        //size
        dest_size=cpu_ldl_data(env, addr);
        env->ccregs[crd_idx].size = dest_size;
        addr+=4;

        //PT
        dest_PT = cpu_ldq_data(env, addr);
        env->ccregs[crd_idx].PT = dest_PT;
        addr+=8;

        //MAC
        uint64_t dest_perms = (dest_perms_base >> 48); 
        //if the capability being loaded has transitive closure feature, update its MAC for the new call context (TCR) 
        if (!is_perms_violation(dest_perms, TRANS))
            dest_MAC=computeMAC(tcr, perms_base, PT, size, mkey);
        else
            dest_MAC = cpu_ldq_data(env, addr);
        env->ccregs[crd_idx].MAC = dest_MAC;
    }
    return;
}

void HELPER(ccall)(CPUARMState *env)
{
    CPUARMState* state=env;
    arm_rebuild_hflags(env);
    return;
}
    
void HELPER(cret)(CPUARMState *env)
{
    CPUARMState* state=env;
    return;
}

void HELPER(cjmp)(CPUARMState *env)
{
    CPUARMState* state=env;
    return;
}

void HELPER(dcall)(CPUARMState *env, uint64_t curr_pc)
{
    CPUARMState* state=env;
    
    //restore existing values to link capability register
    env->dclr.FIELD[0]=curr_pc+4; //save caller's return address
    //env->dclr.FIELD[1]=env->sp_el[0]; //save caller's SP
    env->dclr.FIELD[1]=env->xregs[31]; //save caller's SP
    env->dclr.FIELD[2]=env->cp15.ttbr0_ns; //save caller's TTBR_EL1
    env->dclr.FIELD[3]=env->cp15.ttbr1_ns; //save caller's TTBR1_EL1
    env->dclr.FIELD[4]=env->cp15.tpidr_el[1]; //save caller's TPIDR_EL1 (caller task_struct in a patched Linux)
    env->dclr.FIELD[5]=env->pstate; //save caller's existing pstate
    env->dclr.FIELD[6]=env->cp15.tpidr_el[0]; //save caller's TPIDR_EL0
    env->dclr.FIELD[7]=env->cp15.tpidrro_el[0]; //save caller's TPIDRRO_EL0
    
    //env->dclr.FIELD[8]=env->cp15.tcr_el[1]; //save caller's TCR_EL1
    //env->dclr.FIELD[9]=env->cp15.sctlr_el[1]; //save caller's SCTLR_EL1
    //env->dclr.FIELD[10]=env->cp15.mair_el[1]; //save caller's MAIR_EL1

    //update target values using capability target register (DCLC)
    env->pc=env->dclc.FIELD[0]; //set to callee PC
    //env->sp_el[0]=env->dclc.FIELD[1]; //set to callee SP
    env->xregs[31]=env->dclc.FIELD[1]; //set to callee SP
    env->cp15.ttbr0_ns=env->dclc.FIELD[2]; //set to callee TTBR0_EL1
    env->cp15.ttbr1_ns=env->dclc.FIELD[3]; //set to callee TTBR1_EL1
    env->cp15.tpidr_el[1]=env->dclc.FIELD[4]; //set to callee TPIDR_EL1 (callee task_struct in a patched Linux) 
    env->pstate=env->dclc.FIELD[5]; //set to callee pstate (SPSR_EL1)
    env->cp15.tpidr_el[0]=env->dclc.FIELD[6]; //set to callee TPIDR_EL0
    env->cp15.tpidrro_el[0]=env->dclc.FIELD[7]; //set to callee TPIDRRO_EL0
    
    //env->cp15.tcr_el[1]=env->dclc.FIELD[8]; //set to callee TCR_EL1
    //env->cp15.sctlr_el[1]=env->dclc.FIELD[9]; //set to callee SCTLR_EL1
    //env->cp15.mair_el[1]=env->dclc.FIELD[10]; //set to callee MAIR_EL1
    
    tlb_flush(env_cpu(env));
    tlb_flush_crca(env_cpu(env));
    arm_rebuild_hflags(env);

    return;
}
    
void HELPER(dret)(CPUARMState *env)
{
    CPUARMState* state=env;
    //update return values using capability link register (DCLR)
    env->pc=env->dclr.FIELD[0]; //restore caller's PC 
    //env->sp_el[0]=env->dclr.FIELD[1]; //restore caller's SP
    env->xregs[31]=env->dclr.FIELD[1]; //restore caller's SP
    env->cp15.ttbr0_ns=env->dclr.FIELD[2]; //restore caller's TTBR0_EL1
    env->cp15.ttbr1_ns=env->dclr.FIELD[3]; //restore caller's TTBR1_EL1
    //callee's task_struct for Linux patched for the swap of tpidr_el1-sp_el0 
    env->cp15.tpidr_el[1]=env->dclr.FIELD[4]; //restore caller's TPIDR_EL1 (caller task_struct in a patched Linux)
    env->pstate=env->dclr.FIELD[5]; //restore caller's pstate
    env->cp15.tpidr_el[0]=env->dclr.FIELD[6]; //restore TPIDR_EL0 
    env->cp15.tpidrro_el[0]=env->dclr.FIELD[7]; //restore TPIDRRO_EL0

    //env->cp15.tcr_el[1]=env->dclr.FIELD[8]; //restore to callee TCR_EL1
    //env->cp15.sctlr_el[1]=env->dclr.FIELD[9]; //restore to callee SCTLR_EL1
    //env->cp15.mair_el[1]=env->dclr.FIELD[10]; //restore to callee MAIR_EL1
    //env->xregs[31]=env->dclr.FIELD[10]; //restore caller's SP
    
    tlb_flush(env_cpu(env));
    tlb_flush_crca(env_cpu(env));
    arm_rebuild_hflags(env);

    return;
}

void HELPER(dgrant)(CPUARMState *env, uint64_t pc, uint64_t sp)
{
    CPUARMState* state=env;
    env->dclc.FIELD[0]=pc; //save target PC
    env->dclc.FIELD[1]=sp; //save target SP
    
    env->dclc.FIELD[2]=env->cp15.ttbr0_ns; //save caller's TTBR_EL1
    env->dclc.FIELD[3]=env->cp15.ttbr1_ns; //save caller's TTBR1_EL1
    env->dclc.FIELD[4]=env->cp15.tpidr_el[1]; //save caller's TPIDR_EL1 (caller task_struct in a patched Linux)
    env->dclc.FIELD[5]=env->pstate; //save caller's existing pstate
    env->dclc.FIELD[6]=env->cp15.tpidr_el[0]; //save caller's TPIDR_EL0
    env->dclc.FIELD[7]=env->cp15.tpidrro_el[0]; //save caller's TPIDRRO_EL0
   
    //env->dclc.FIELD[8]=env->cp15.tcr_el[1]; //save caller's TCR_EL1
    //env->dclc.FIELD[9]=env->cp15.sctlr_el[1]; //save caller's SCTLR_EL1
    //env->dclc.FIELD[10]=env->cp15.mair_el[1]; //save caller's MAIR_EL1

    return;
}
//#endif
