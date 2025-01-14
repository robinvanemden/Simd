/*
* Simd Library (http://ermig1979.github.io/Simd).
*
* Copyright (c) 2011-2022 Yermalayeu Ihar.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#include "Simd/SimdMemory.h"
#include "Simd/SimdStore.h"
#include "Simd/SimdBase.h"
#include "Simd/SimdSse2.h"
#include "Simd/SimdAvx1.h"
#include "Simd/SimdAvx2.h"
#include "Simd/SimdAvx512bw.h"
#include "Simd/SimdSynet.h"
#include "Simd/SimdExtract.h"
#include "Simd/SimdExp.h"
#include "Simd/SimdPow.h"

namespace Simd
{
#if defined(SIMD_AVX512BW_ENABLE) && defined(SIMD_SYNET_ENABLE)     
    namespace Avx512bw
    {
        template<bool mask, bool nofma> SIMD_INLINE void SynetAdd8iNchwF(const uint8_t* a, const uint8_t* b, __m512 scale[3], __m512 shift[3], __m128i upper, uint8_t* c, size_t offset, __mmask16 tail = -1)
        {
            __m512 _a = Fmadd<nofma>(_mm512_cvtepi32_ps(_mm512_cvtepu8_epi32((Load<false, mask>(a + offset, tail)))), scale[0], shift[0]);
            __m512 _b = Fmadd<nofma>(_mm512_cvtepi32_ps(_mm512_cvtepu8_epi32((Load<false, mask>(b + offset, tail)))), scale[1], shift[1]);
            __m512i c32 = _mm512_cvtps_epi32(Fmadd<nofma>(_mm512_add_ps(_a, _b), scale[2], shift[2]));
            __m512i c8 = _mm512_permutexvar_epi32(K32_PERMUTE_FOR_TWO_UNPACK, _mm512_packus_epi16(_mm512_packs_epi32(c32, K_ZERO), K_ZERO));
            Store<false, mask>(c + offset, _mm_min_epu8(_mm512_extracti32x4_epi32(c8, 0), upper), tail);
        }

        template <bool nofma> void SynetAdd8iNchw(const uint8_t* aData, const float* aScale, const float* aShift, const uint8_t* bData, const float* bScale, const float* bShift,
            uint8_t* cData, const float* cScale, const float* cShift, size_t batch, size_t channels, size_t spatial, int upper)
        {
            size_t spatialF = AlignLo(spatial, F);
            __mmask16 tailF = TailMask16(spatial - spatialF);
            __m128i _upper = _mm_set1_epi8(upper);
            __m512 scale[3], shift[3];
            for (size_t b = 0; b < batch; ++b)
            {
                for (size_t c = 0; c < channels; ++c)
                {
                    scale[0] = _mm512_set1_ps(aScale[c]);
                    shift[0] = _mm512_set1_ps(aShift[c]);
                    scale[1] = _mm512_set1_ps(bScale[c]);
                    shift[1] = _mm512_set1_ps(bShift[c]);
                    scale[2] = _mm512_set1_ps(cScale[c]);
                    shift[2] = _mm512_set1_ps(cShift[c]);
                    size_t s = 0;
                    for (; s < spatialF; s += F)
                        SynetAdd8iNchwF<false, nofma>(aData, bData, scale, shift, _upper, cData, s);
                    if (s < spatial)
                        SynetAdd8iNchwF<true, nofma>(aData, bData, scale, shift, _upper, cData, s, tailF);
                    aData += spatial;
                    bData += spatial;
                    cData += spatial;
                }
            }
        }

        template <bool align, bool mask, bool nofma> SIMD_INLINE void SynetAdd8iNhwcF(const uint8_t* a, const float* aScale, const float* aShift,
            const uint8_t* b, const float* bScale, const float* bShift, const float* cScale, const float* cShift, __m128i upper, uint8_t* c, size_t offset, __mmask16 tail = -1)
        {
            __m512 _a = _mm512_cvtepi32_ps(_mm512_cvtepu8_epi32((Load<false, mask>(a + offset, tail))));
            __m512 _aScale = Load<align, mask>(aScale + offset, tail);
            __m512 _aShift = Load<align, mask>(aShift + offset, tail);
            _a = Fmadd<nofma>(_a, _aScale, _aShift);
            __m512 _b = _mm512_cvtepi32_ps(_mm512_cvtepu8_epi32((Load<false, mask>(b + offset, tail))));
            __m512 _bScale = Load<align, mask>(bScale + offset, tail);
            __m512 _bShift = Load<align, mask>(bShift + offset, tail);
            _b = Fmadd<nofma>(_b, _bScale, _bShift);
            __m512 _cScale = Load<align, mask>(cScale + offset, tail);
            __m512 _cShift = Load<align, mask>(cShift + offset, tail);
            __m512i c32 = _mm512_cvtps_epi32(Fmadd<nofma>(_mm512_add_ps(_a, _b), _cScale, _cShift));
            __m512i c8 = _mm512_permutexvar_epi32(K32_PERMUTE_FOR_TWO_UNPACK, _mm512_packus_epi16(_mm512_packs_epi32(c32, K_ZERO), K_ZERO));
            Store<false, mask>(c + offset, _mm_min_epu8(_mm512_extracti32x4_epi32(c8, 0), upper), tail);
        }

        template <bool align, bool nofma> void SynetAdd8iNhwc(const uint8_t* aData, const float* aScale, const float* aShift, const uint8_t* bData, const float* bScale, const float* bShift,
            uint8_t* cData, const float* cScale, const float* cShift, size_t batch, size_t channels, size_t spatial, int upper)
        {
            if (align)
                assert(Aligned(aScale) && Aligned(aShift) && Aligned(bScale) && Aligned(bShift) && Aligned(cScale) && Aligned(cShift));

            size_t channelsF = AlignLo(channels, F);
            __mmask16 tailF = TailMask16(channels - channelsF);
            __m128i _upper = _mm_set1_epi8(upper);
            for (size_t b = 0; b < batch; ++b)
            {
                for (size_t s = 0; s < spatial; ++s)
                {
                    size_t c = 0;
                    for (; c < channelsF; c += F)
                        SynetAdd8iNhwcF<align, false, nofma>(aData, aScale, aShift, bData, bScale, bShift, cScale, cShift, _upper, cData, c);
                    if (c < channels)
                        SynetAdd8iNhwcF<align, true, nofma>(aData, aScale, aShift, bData, bScale, bShift, cScale, cShift, _upper, cData, c, tailF);
                    aData += channels;
                    bData += channels;
                    cData += channels;
                }
            }
        }

        template <bool nofma> SIMD_INLINE void SynetAdd8iNhwc(const uint8_t* aData, const float* aScale, const float* aShift, const uint8_t* bData, const float* bScale, const float* bShift,
            uint8_t* cData, const float* cScale, const float* cShift, size_t batch, size_t channels, size_t spatial, int upper)
        {
            if (Aligned(aScale) && Aligned(aShift) && Aligned(bScale) && Aligned(bShift) && Aligned(cScale) && Aligned(cShift))
                SynetAdd8iNhwc<true, nofma>(aData, aScale, aShift, bData, bScale, bShift, cData, cScale, cShift, batch, channels, spatial, upper);
            else
                SynetAdd8iNhwc<false, nofma>(aData, aScale, aShift, bData, bScale, bShift, cData, cScale, cShift, batch, channels, spatial, upper);
        }

        void SynetAdd8i(const uint8_t* aData, const float* aScale, const float* aShift, const uint8_t* bData, const float* bScale, const float* bShift,
            uint8_t* cData, const float* cScale, const float* cShift, size_t batch, size_t channels, size_t spatial, SimdTensorFormatType format, SimdSynetCompatibilityType compatibility)
        {
            int upper = Base::Narrowed(compatibility) ? Base::U8_NARROWED_MAX : Base::U8_PRECISE_MAX;
            bool nofma = Base::FmaAvoid(compatibility);
            if (format == SimdTensorFormatNchw && spatial > HF)
            {
                if (nofma)
                    SynetAdd8iNchw<true>(aData, aScale, aShift, bData, bScale, bShift, cData, cScale, cShift, batch, channels, spatial, upper);
                else
                    SynetAdd8iNchw<false>(aData, aScale, aShift, bData, bScale, bShift, cData, cScale, cShift, batch, channels, spatial, upper);
            }
            else if (format == SimdTensorFormatNhwc && channels > HF)
            {
                if (nofma)
                    SynetAdd8iNhwc<true>(aData, aScale, aShift, bData, bScale, bShift, cData, cScale, cShift, batch, channels, spatial, upper);
                else
                    SynetAdd8iNhwc<false>(aData, aScale, aShift, bData, bScale, bShift, cData, cScale, cShift, batch, channels, spatial, upper);
            }
            else
                Avx2::SynetAdd8i(aData, aScale, aShift, bData, bScale, bShift, cData, cScale, cShift, batch, channels, spatial, format, compatibility);
        }

        //-----------------------------------------------------------------------------------------

        template <bool align, bool mask> SIMD_INLINE void SynetAddBias(const __m512& bias, float* dst, __mmask16 tail = -1)
        {
            Store<align, mask>(dst, _mm512_add_ps((Load<align, mask>(dst, tail)), bias), tail);
        }

        template <bool align, bool mask> SIMD_INLINE void SynetAddBias(const float* bias, float* dst, __mmask16 tail = -1)
        {
            __m512 _bias = Load<align, mask>(bias, tail);
            __m512 _dst = Load<align, mask>(dst, tail);
            Store<align, mask>(dst, _mm512_add_ps(_dst, _bias), tail);
        }

        template <bool align> void SynetAddBiasNchw(const float* bias, size_t channels, size_t spatial, float* dst)
        {
            if (align)
                assert(Aligned(spatial, F) && Aligned(dst));

            size_t aligned = AlignLo(spatial, QF);
            size_t partial = AlignLo(spatial, F);
            __mmask16 tail = TailMask16(spatial - partial);
            for (size_t c = 0; c < channels; ++c)
            {
                size_t s = 0;
                __m512 _bias = _mm512_set1_ps(bias[c]);
                for (; s < aligned; s += QF)
                {
                    SynetAddBias<align, false>(_bias, dst + s + F * 0);
                    SynetAddBias<align, false>(_bias, dst + s + F * 1);
                    SynetAddBias<align, false>(_bias, dst + s + F * 2);
                    SynetAddBias<align, false>(_bias, dst + s + F * 3);
                }
                for (; s < partial; s += F)
                    SynetAddBias<align, false>(_bias, dst + s);
                if (s < spatial)
                    SynetAddBias<align, true>(_bias, dst + s, tail);
                dst += spatial;
            }
        }

        SIMD_INLINE void SynetAddBiasNchw(const float* bias, size_t channels, size_t spatial, float* dst)
        {
            if (Aligned(spatial, F) && Aligned(dst))
                SynetAddBiasNchw<true>(bias, channels, spatial, dst);
            else
                SynetAddBiasNchw<false>(bias, channels, spatial, dst);
        }

        template <bool align> void SynetAddBiasNhwc(const float* bias, size_t channels, size_t spatial, float* dst)
        {
            if (align)
                assert(Aligned(channels, F) && Aligned(bias) && Aligned(dst));

            size_t aligned = AlignLo(channels, QF);
            size_t partial = AlignLo(channels, F);
            __mmask16 tail = TailMask16(channels - partial);
            for (size_t s = 0; s < spatial; ++s)
            {
                size_t c = 0;
                for (; c < aligned; c += QF)
                {
                    SynetAddBias<align, false>(bias + c + F * 0, dst + c + F * 0);
                    SynetAddBias<align, false>(bias + c + F * 1, dst + c + F * 1);
                    SynetAddBias<align, false>(bias + c + F * 2, dst + c + F * 2);
                    SynetAddBias<align, false>(bias + c + F * 3, dst + c + F * 3);
                }
                for (; c < partial; c += F)
                    SynetAddBias<align, false>(bias + c, dst + c);
                if (c < channels)
                    SynetAddBias<align, true>(bias + c, dst + c, tail);
                dst += channels;
            }
        }

        SIMD_INLINE void SynetAddBiasNhwc(const float* bias, size_t channels, size_t spatial, float* dst)
        {
            if (Aligned(bias) && Aligned(channels, F) && Aligned(dst))
                SynetAddBiasNhwc<true>(bias, channels, spatial, dst);
            else
                SynetAddBiasNhwc<false>(bias, channels, spatial, dst);
        }

        template <bool align> void SynetAddBiasNchw16c(const float* bias, size_t channels, size_t spatial, float* dst)
        {
            if (align)
                assert(Aligned(dst));

            size_t spatial4 = AlignLo(spatial, 4);
            for (size_t c = 0; c < channels; c += F)
            {
                __m512 _bias = Load<false>(bias + c);
                size_t s = 0;
                for (; s < spatial4; s += 4, dst += 4 * F)
                {
                    SynetAddBias<align, false>(_bias, dst + 0 * F);
                    SynetAddBias<align, false>(_bias, dst + 1 * F);
                    SynetAddBias<align, false>(_bias, dst + 2 * F);
                    SynetAddBias<align, false>(_bias, dst + 3 * F);
                }
                for (; s < spatial; ++s, dst += F)
                    SynetAddBias<align, false>(_bias, dst);
            }
        }

        SIMD_INLINE void SynetAddBiasNchw16c(const float* bias, size_t channels, size_t spatial, float* dst)
        {
            if (Aligned(dst))
                SynetAddBiasNchw16c<true>(bias, channels, spatial, dst);
            else
                SynetAddBiasNchw16c<false>(bias, channels, spatial, dst);
        }

        void SynetAddBias(const float* bias, size_t channels, size_t spatial, float* dst, SimdTensorFormatType format)
        {
            if (Base::NchwCompatible(channels, spatial, format))
                SynetAddBiasNchw(bias, channels, spatial, dst);
            else if (Base::NhwcCompatible(channels, spatial, format))
                SynetAddBiasNhwc(bias, channels, spatial, dst);
            else if (format == SimdTensorFormatNchw4c)
                Sse2::SynetAddBias(bias, channels, spatial, dst, format);
            else if (format == SimdTensorFormatNchw8c)
                Avx::SynetAddBias(bias, channels, spatial, dst, format);
            else if (format == SimdTensorFormatNchw16c)
                SynetAddBiasNchw16c(bias, channels, spatial, dst);
            else
                Base::SynetAddBias(bias, channels, spatial, dst, format);
        }

        //-----------------------------------------------------------------------------------------

        template <SimdSynetEltwiseOperationType type> __m512 SynetEltwiseLayerForward(__m512 src0, __m512 src1);

        template <> SIMD_INLINE __m512 SynetEltwiseLayerForward<SimdSynetEltwiseOperationProduct>(__m512 src0, __m512 src1)
        {
            return _mm512_mul_ps(src0, src1);
        }

        template <> SIMD_INLINE __m512 SynetEltwiseLayerForward<SimdSynetEltwiseOperationMax>(__m512 src0, __m512 src1)
        {
            return _mm512_max_ps(src0, src1);
        }

        template <> SIMD_INLINE __m512 SynetEltwiseLayerForward<SimdSynetEltwiseOperationMin>(__m512 src0, __m512 src1)
        {
            return _mm512_min_ps(src0, src1);
        }

        template <SimdSynetEltwiseOperationType type, bool align, bool mask > SIMD_INLINE void SynetEltwiseLayerForward(const float* src0, const float* src1, float* dst, size_t offset, __mmask16 tail = -1)
        {
            Store<align, mask>(dst + offset, SynetEltwiseLayerForward<type>((Load<align, mask>(src0 + offset, tail)), (Load<align, mask>(src1 + offset, tail))), tail);
        }

        template <SimdSynetEltwiseOperationType type, bool align> void SynetEltwiseLayerForward(float const* const* src, size_t count, size_t size, float* dst)
        {
            size_t aligned = AlignLo(size, QF);
            size_t partial = AlignLo(size, F);
            __mmask16 tail = __mmask16(-1) >> (F + partial - size);
            const float* src0 = src[0];
            const float* src1 = src[1];
            size_t j = 0;
            for (; j < aligned; j += QF)
            {
                SynetEltwiseLayerForward<type, align, false>(src0, src1, dst, j + F * 0);
                SynetEltwiseLayerForward<type, align, false>(src0, src1, dst, j + F * 1);
                SynetEltwiseLayerForward<type, align, false>(src0, src1, dst, j + F * 2);
                SynetEltwiseLayerForward<type, align, false>(src0, src1, dst, j + F * 3);
            }
            for (; j < partial; j += F)
                SynetEltwiseLayerForward<type, align, false>(src0, src1, dst, j);
            if (j < size)
                SynetEltwiseLayerForward<type, align, true>(src0, src1, dst, j, tail);
            for (size_t i = 2; i < count; ++i)
            {
                const float* srci = src[i];
                for (j = 0; j < aligned; j += QF)
                {
                    SynetEltwiseLayerForward<type, align, false>(dst, srci, dst, j + F * 0);
                    SynetEltwiseLayerForward<type, align, false>(dst, srci, dst, j + F * 1);
                    SynetEltwiseLayerForward<type, align, false>(dst, srci, dst, j + F * 2);
                    SynetEltwiseLayerForward<type, align, false>(dst, srci, dst, j + F * 3);
                }
                for (; j < partial; j += F)
                    SynetEltwiseLayerForward<type, align, false>(dst, srci, dst, j);
                if (j < size)
                    SynetEltwiseLayerForward<type, align, true>(dst, srci, dst, j, tail);
            }
        }

        template <bool align, bool mask> void SynetEltwiseLayerForwardSum(const float* src0, const __m512& weight0, const float* src1, const __m512& weight1, float* dst, size_t offset, __mmask16 tail = -1)
        {
            Store<align, mask>(dst + offset, _mm512_fmadd_ps((Load<align, mask>(src0 + offset, tail)), weight0, _mm512_mul_ps((Load<align, mask>(src1 + offset, tail)), weight1)), tail);
        }

        template <bool align, bool mask> void SynetEltwiseLayerForwardSum(const float* src, const __m512& weight, float* dst, size_t offset, __mmask16 tail = -1)
        {
            Store<align, mask>(dst + offset, _mm512_fmadd_ps((Load<align, mask>(src + offset, tail)), weight, (Load<align, mask>(dst + offset, tail))), tail);
        }

        template <bool align> void SynetEltwiseLayerForwardSum(float const* const* src, const float* weight, size_t count, size_t size, float* dst)
        {
            size_t aligned = AlignLo(size, QF);
            size_t partial = AlignLo(size, F);
            __mmask16 tail = __mmask16(-1) >> (F + partial - size);
            const float* src0 = src[0];
            const float* src1 = src[1];
            __m512 weight0 = _mm512_set1_ps(weight[0]);
            __m512 weight1 = _mm512_set1_ps(weight[1]);
            size_t j = 0;
            for (; j < aligned; j += QF)
            {
                SynetEltwiseLayerForwardSum<align, false>(src0, weight0, src1, weight1, dst, j + F * 0);
                SynetEltwiseLayerForwardSum<align, false>(src0, weight0, src1, weight1, dst, j + F * 1);
                SynetEltwiseLayerForwardSum<align, false>(src0, weight0, src1, weight1, dst, j + F * 2);
                SynetEltwiseLayerForwardSum<align, false>(src0, weight0, src1, weight1, dst, j + F * 3);
            }
            for (; j < partial; j += F)
                SynetEltwiseLayerForwardSum<align, false>(src0, weight0, src1, weight1, dst, j);
            if (j < size)
                SynetEltwiseLayerForwardSum<align, true>(src0, weight0, src1, weight1, dst, j, tail);
            for (size_t i = 2; i < count; ++i)
            {
                const float* srci = src[i];
                __m512 weighti = _mm512_set1_ps(weight[i]);
                for (j = 0; j < aligned; j += QF)
                {
                    SynetEltwiseLayerForwardSum<align, false>(srci, weighti, dst, j + F * 0);
                    SynetEltwiseLayerForwardSum<align, false>(srci, weighti, dst, j + F * 1);
                    SynetEltwiseLayerForwardSum<align, false>(srci, weighti, dst, j + F * 2);
                    SynetEltwiseLayerForwardSum<align, false>(srci, weighti, dst, j + F * 3);
                }
                for (; j < partial; j += F)
                    SynetEltwiseLayerForwardSum<align, false>(srci, weighti, dst, j);
                if (j < size)
                    SynetEltwiseLayerForwardSum<align, true>(srci, weighti, dst, j, tail);
            }
        }

        template <bool align> void SynetEltwiseLayerForward(float const* const* src, const float* weight, size_t count, size_t size, SimdSynetEltwiseOperationType type, float* dst)
        {
            switch (type)
            {
            case SimdSynetEltwiseOperationProduct:
                SynetEltwiseLayerForward<SimdSynetEltwiseOperationProduct, align>(src, count, size, dst);
                break;
            case SimdSynetEltwiseOperationSum:
                SynetEltwiseLayerForwardSum<align>(src, weight, count, size, dst);
                break;
            case SimdSynetEltwiseOperationMax:
                SynetEltwiseLayerForward<SimdSynetEltwiseOperationMax, align>(src, count, size, dst);
                break;
            case SimdSynetEltwiseOperationMin:
                SynetEltwiseLayerForward<SimdSynetEltwiseOperationMin, align>(src, count, size, dst);
                break;
            default:
                assert(0);
            }
        }

        void SynetEltwiseLayerForward(float const* const* src, const float* weight, size_t count, size_t size, SimdSynetEltwiseOperationType type, float* dst)
        {
            assert(count >= 2);
            bool aligned = Aligned(dst) && Aligned(src[0]) && Aligned(src[1]);
            for (size_t i = 2; i < count; ++i)
                aligned = aligned && Aligned(src[i]);
            if (aligned)
                SynetEltwiseLayerForward<true>(src, weight, count, size, type, dst);
            else
                SynetEltwiseLayerForward<false>(src, weight, count, size, type, dst);
        }

        //-----------------------------------------------------------------------------------------

        static SIMD_INLINE void Save4Sums(const __m512i& sum0, const __m512i sum1, const __m512i& sum2, const __m512i& sum3, int32_t* dst)
        {
            __m512i sum02 = _mm512_add_epi32(_mm512_unpacklo_epi32(sum0, sum2), _mm512_unpackhi_epi32(sum0, sum2));
            __m512i sum13 = _mm512_add_epi32(_mm512_unpacklo_epi32(sum1, sum3), _mm512_unpackhi_epi32(sum1, sum3));
            __m512i sum512 = _mm512_add_epi32(_mm512_unpacklo_epi32(sum02, sum13), _mm512_unpackhi_epi32(sum02, sum13));
            _mm_storeu_si128((__m128i*)dst, _mm_add_epi32(_mm_add_epi32(_mm512_extracti32x4_epi32(sum512, 0), _mm512_extracti32x4_epi32(sum512, 1)),
                _mm_add_epi32(_mm512_extracti32x4_epi32(sum512, 2), _mm512_extracti32x4_epi32(sum512, 3))));
        }

        template<bool overflow> static void SynetInnerProduct8i1x1(size_t K, const uint8_t* S, size_t lds, const int8_t* W, size_t ldw, int32_t* D, size_t ldd)
        {
            size_t KA = AlignLo(K, A);
            const uint8_t* S0 = S + 0 * lds;
            const int8_t* W0 = W + 0 * ldw;
            __m512i d00 = _mm512_setzero_si512();
            __m512i s0, w0;
            for (size_t k = 0; k < KA; k += A)
            {
                s0 = _mm512_loadu_si512((__m512i*)(S0 + k));
                w0 = _mm512_loadu_si512((__m512i*)(W0 + k));
                Madd4<overflow>(d00, s0, w0);
            }
            if (KA < K)
            {
                __mmask64 tail = TailMask64(K - KA);
                s0 = Load<false, true>(S0 + KA, tail);
                w0 = Load<false, true>(W0 + KA, tail);
                Madd4<overflow>(d00, s0, w0);
            }
            D[0] = ExtractSum<uint32_t>(d00);
        }

        template<bool overflow> static void SynetInnerProduct8i1x4(size_t K, const uint8_t* S, size_t lds, const int8_t* W, size_t ldw, int32_t* D, size_t ldd)
        {
            size_t KA = AlignLo(K, A);
            const uint8_t* S0 = S + 0 * lds;
            const int8_t* W0 = W + 0 * ldw;
            const int8_t* W1 = W + 1 * ldw;
            const int8_t* W2 = W + 2 * ldw;
            const int8_t* W3 = W + 3 * ldw;
            __m512i d00 = _mm512_setzero_si512();
            __m512i d01 = _mm512_setzero_si512();
            __m512i d02 = _mm512_setzero_si512();
            __m512i d03 = _mm512_setzero_si512();
            __m512i s0, w0;
            for (size_t k = 0; k < KA; k += A)
            {
                s0 = _mm512_loadu_si512((__m512i*)(S0 + k));
                w0 = _mm512_loadu_si512((__m512i*)(W0 + k));
                Madd4<overflow>(d00, s0, w0);
                w0 = _mm512_loadu_si512((__m512i*)(W1 + k));
                Madd4<overflow>(d01, s0, w0);
                w0 = _mm512_loadu_si512((__m512i*)(W2 + k));
                Madd4<overflow>(d02, s0, w0);
                w0 = _mm512_loadu_si512((__m512i*)(W3 + k));
                Madd4<overflow>(d03, s0, w0);
            }
            if (KA < K)
            {
                __mmask64 tail = TailMask64(K - KA);
                s0 = Load<false, true>(S0 + KA, tail);
                w0 = Load<false, true>(W0 + KA, tail);
                Madd4<overflow>(d00, s0, w0);
                w0 = Load<false, true>(W1 + KA, tail);
                Madd4<overflow>(d01, s0, w0);
                w0 = Load<false, true>(W2 + KA, tail);
                Madd4<overflow>(d02, s0, w0);
                w0 = Load<false, true>(W3 + KA, tail);
                Madd4<overflow>(d03, s0, w0);
            }
            Save4Sums(d00, d01, d02, d03, D);
        }

        template<bool overflow> static void SynetInnerProduct8i2x1(size_t K, const uint8_t* S, size_t lds, const int8_t* W, size_t ldw, int32_t* D, size_t ldd)
        {
            size_t KA = AlignLo(K, A);
            const uint8_t* S0 = S + 0 * lds;
            const uint8_t* S1 = S + 1 * lds;
            const int8_t* W0 = W + 0 * ldw;
            __m512i d00 = _mm512_setzero_si512();
            __m512i d10 = _mm512_setzero_si512();
            __m512i s0, s1, w0;
            for (size_t k = 0; k < KA; k += A)
            {
                s0 = _mm512_loadu_si512((__m512i*)(S0 + k));
                s1 = _mm512_loadu_si512((__m512i*)(S1 + k));
                w0 = _mm512_loadu_si512((__m512i*)(W0 + k));
                Madd4<overflow>(d00, s0, w0);
                Madd4<overflow>(d10, s1, w0);
            }
            if (KA < K)
            {
                __mmask64 tail = TailMask64(K - KA);
                s0 = Load<false, true>(S0 + KA, tail);
                s1 = Load<false, true>(S1 + KA, tail);
                w0 = Load<false, true>(W0 + KA, tail);
                Madd4<overflow>(d00, s0, w0);
                Madd4<overflow>(d10, s1, w0);
            }
            D[0 * ldd] = ExtractSum<uint32_t>(d00);
            D[1 * ldd] = ExtractSum<uint32_t>(d10);
        }

        template<bool overflow> static void SynetInnerProduct8i2x4(size_t K, const uint8_t* S, size_t lds, const int8_t* W, size_t ldw, int32_t* D, size_t ldd)
        {
            size_t KA = AlignLo(K, A);
            const uint8_t* S0 = S + 0 * lds;
            const uint8_t* S1 = S + 1 * lds;
            const int8_t* W0 = W + 0 * ldw;
            const int8_t* W1 = W + 1 * ldw;
            const int8_t* W2 = W + 2 * ldw;
            const int8_t* W3 = W + 3 * ldw;
            __m512i d00 = _mm512_setzero_si512();
            __m512i d01 = _mm512_setzero_si512();
            __m512i d02 = _mm512_setzero_si512();
            __m512i d03 = _mm512_setzero_si512();
            __m512i d10 = _mm512_setzero_si512();
            __m512i d11 = _mm512_setzero_si512();
            __m512i d12 = _mm512_setzero_si512();
            __m512i d13 = _mm512_setzero_si512();
            __m512i s0, s1, w0;
            for (size_t k = 0; k < KA; k += A)
            {
                s0 = _mm512_loadu_si512((__m512i*)(S0 + k));
                s1 = _mm512_loadu_si512((__m512i*)(S1 + k));
                w0 = _mm512_loadu_si512((__m512i*)(W0 + k));
                Madd4<overflow>(d00, s0, w0);
                Madd4<overflow>(d10, s1, w0);
                w0 = _mm512_loadu_si512((__m512i*)(W1 + k));
                Madd4<overflow>(d01, s0, w0);
                Madd4<overflow>(d11, s1, w0);
                w0 = _mm512_loadu_si512((__m512i*)(W2 + k));
                Madd4<overflow>(d02, s0, w0);
                Madd4<overflow>(d12, s1, w0);
                w0 = _mm512_loadu_si512((__m512i*)(W3 + k));
                Madd4<overflow>(d03, s0, w0);
                Madd4<overflow>(d13, s1, w0);
            }
            if (KA < K)
            {
                __mmask64 tail = TailMask64(K - KA);
                s0 = Load<false, true>(S0 + KA, tail);
                s1 = Load<false, true>(S1 + KA, tail);
                w0 = Load<false, true>(W0 + KA, tail);
                Madd4<overflow>(d00, s0, w0);
                Madd4<overflow>(d10, s1, w0);
                w0 = Load<false, true>(W1 + KA, tail);
                Madd4<overflow>(d01, s0, w0);
                Madd4<overflow>(d11, s1, w0);
                w0 = Load<false, true>(W2 + KA, tail);
                Madd4<overflow>(d02, s0, w0);
                Madd4<overflow>(d12, s1, w0);
                w0 = Load<false, true>(W3 + KA, tail);
                Madd4<overflow>(d03, s0, w0);
                Madd4<overflow>(d13, s1, w0);
            }
            Save4Sums(d00, d01, d02, d03, D + 0 * ldd);
            Save4Sums(d10, d11, d12, d13, D + 1 * ldd);
        }

        template<bool overflow> static void SynetInnerProduct8i4x1(size_t K, const uint8_t* S, size_t lds, const int8_t* W, size_t ldw, int32_t* D, size_t ldd)
        {
            size_t KA = AlignLo(K, A);
            const uint8_t* S0 = S + 0 * lds;
            const uint8_t* S1 = S + 1 * lds;
            const uint8_t* S2 = S + 2 * lds;
            const uint8_t* S3 = S + 3 * lds;
            const int8_t* W0 = W + 0 * ldw;
            __m512i d00 = _mm512_setzero_si512();
            __m512i d10 = _mm512_setzero_si512();
            __m512i d20 = _mm512_setzero_si512();
            __m512i d30 = _mm512_setzero_si512();
            __m512i s0, s1, s2, s3, w0;
            for (size_t k = 0; k < KA; k += A)
            {
                s0 = _mm512_loadu_si512((__m512i*)(S0 + k));
                s1 = _mm512_loadu_si512((__m512i*)(S1 + k));
                s2 = _mm512_loadu_si512((__m512i*)(S2 + k));
                s3 = _mm512_loadu_si512((__m512i*)(S3 + k));
                w0 = _mm512_loadu_si512((__m512i*)(W0 + k));
                Madd4<overflow>(d00, s0, w0);
                Madd4<overflow>(d10, s1, w0);
                Madd4<overflow>(d20, s2, w0);
                Madd4<overflow>(d30, s3, w0);
            }
            if (KA < K)
            {
                __mmask64 tail = TailMask64(K - KA);
                s0 = Load<false, true>(S0 + KA, tail);
                s1 = Load<false, true>(S1 + KA, tail);
                s2 = Load<false, true>(S2 + KA, tail);
                s3 = Load<false, true>(S3 + KA, tail);
                w0 = Load<false, true>(W0 + KA, tail);
                Madd4<overflow>(d00, s0, w0);
                Madd4<overflow>(d10, s1, w0);
                Madd4<overflow>(d20, s2, w0);
                Madd4<overflow>(d30, s3, w0);
            }
            D[0 * ldd] = ExtractSum<uint32_t>(d00);
            D[1 * ldd] = ExtractSum<uint32_t>(d10);
            D[2 * ldd] = ExtractSum<uint32_t>(d20);
            D[3 * ldd] = ExtractSum<uint32_t>(d30);
        }

        template<bool overflow> static void SynetInnerProduct8i4x4(size_t K, const uint8_t* S, size_t lds, const int8_t* W, size_t ldw, int32_t* D, size_t ldd)
        {
            size_t KA = AlignLo(K, A);
            const uint8_t* S0 = S + 0 * lds;
            const uint8_t* S1 = S + 1 * lds;
            const uint8_t* S2 = S + 2 * lds;
            const uint8_t* S3 = S + 3 * lds;
            const int8_t* W0 = W + 0 * ldw;
            const int8_t* W1 = W + 1 * ldw;
            const int8_t* W2 = W + 2 * ldw;
            const int8_t* W3 = W + 3 * ldw;
            __m512i d00 = _mm512_setzero_si512();
            __m512i d01 = _mm512_setzero_si512();
            __m512i d02 = _mm512_setzero_si512();
            __m512i d03 = _mm512_setzero_si512();
            __m512i d10 = _mm512_setzero_si512();
            __m512i d11 = _mm512_setzero_si512();
            __m512i d12 = _mm512_setzero_si512();
            __m512i d13 = _mm512_setzero_si512();
            __m512i d20 = _mm512_setzero_si512();
            __m512i d21 = _mm512_setzero_si512();
            __m512i d22 = _mm512_setzero_si512();
            __m512i d23 = _mm512_setzero_si512();
            __m512i d30 = _mm512_setzero_si512();
            __m512i d31 = _mm512_setzero_si512();
            __m512i d32 = _mm512_setzero_si512();
            __m512i d33 = _mm512_setzero_si512();
            __m512i s0, s1, s2, s3, w0;
            for (size_t k = 0; k < KA; k += A)
            {
                s0 = _mm512_loadu_si512((__m512i*)(S0 + k));
                s1 = _mm512_loadu_si512((__m512i*)(S1 + k));
                s2 = _mm512_loadu_si512((__m512i*)(S2 + k));
                s3 = _mm512_loadu_si512((__m512i*)(S3 + k));
                w0 = _mm512_loadu_si512((__m512i*)(W0 + k));
                Madd4<overflow>(d00, s0, w0);
                Madd4<overflow>(d10, s1, w0);
                Madd4<overflow>(d20, s2, w0);
                Madd4<overflow>(d30, s3, w0);
                w0 = _mm512_loadu_si512((__m512i*)(W1 + k));
                Madd4<overflow>(d01, s0, w0);
                Madd4<overflow>(d11, s1, w0);
                Madd4<overflow>(d21, s2, w0);
                Madd4<overflow>(d31, s3, w0);
                w0 = _mm512_loadu_si512((__m512i*)(W2 + k));
                Madd4<overflow>(d02, s0, w0);
                Madd4<overflow>(d12, s1, w0);
                Madd4<overflow>(d22, s2, w0);
                Madd4<overflow>(d32, s3, w0);
                w0 = _mm512_loadu_si512((__m512i*)(W3 + k));
                Madd4<overflow>(d03, s0, w0);
                Madd4<overflow>(d13, s1, w0);
                Madd4<overflow>(d23, s2, w0);
                Madd4<overflow>(d33, s3, w0);
            }
            if (KA < K)
            {
                __mmask64 tail = TailMask64(K - KA);
                s0 = Load<false, true>(S0 + KA, tail);
                s1 = Load<false, true>(S1 + KA, tail);
                s2 = Load<false, true>(S2 + KA, tail);
                s3 = Load<false, true>(S3 + KA, tail);
                w0 = Load<false, true>(W0 + KA, tail);
                Madd4<overflow>(d00, s0, w0);
                Madd4<overflow>(d10, s1, w0);
                Madd4<overflow>(d20, s2, w0);
                Madd4<overflow>(d30, s3, w0);
                w0 = Load<false, true>(W1 + KA, tail);
                Madd4<overflow>(d01, s0, w0);
                Madd4<overflow>(d11, s1, w0);
                Madd4<overflow>(d21, s2, w0);
                Madd4<overflow>(d31, s3, w0);
                w0 = Load<false, true>(W2 + KA, tail);
                Madd4<overflow>(d02, s0, w0);
                Madd4<overflow>(d12, s1, w0);
                Madd4<overflow>(d22, s2, w0);
                Madd4<overflow>(d32, s3, w0);
                w0 = Load<false, true>(W3 + KA, tail);
                Madd4<overflow>(d03, s0, w0);
                Madd4<overflow>(d13, s1, w0);
                Madd4<overflow>(d23, s2, w0);
                Madd4<overflow>(d33, s3, w0);
            }
            Save4Sums(d00, d01, d02, d03, D + 0 * ldd);
            Save4Sums(d10, d11, d12, d13, D + 1 * ldd);
            Save4Sums(d20, d21, d22, d23, D + 2 * ldd);
            Save4Sums(d30, d31, d32, d33, D + 3 * ldd);
        }

        template<bool overflow> void SynetInnerProduct8i(size_t M, size_t N, size_t K, const uint8_t* src, const int8_t* weight, int32_t* dst)
        {
            size_t M2 = AlignLoAny(M, 2);
            size_t M4 = AlignLoAny(M, 4);
            size_t N4 = AlignLoAny(N, 4);
            size_t i = 0;
            for (; i < M4; i += 4)
            {
                size_t j = 0;
                for (; j < N4; j += 4)
                    SynetInnerProduct8i4x4<overflow>(K, src, K, weight + j * K, K, dst + j, N);
                for (; j < N; j += 1)
                    SynetInnerProduct8i4x1<overflow>(K, src, K, weight + j * K, K, dst + j, N);
                src += K * 4;
                dst += N * 4;
            }
            for (; i < M2; i += 2)
            {
                size_t j = 0;
                for (; j < N4; j += 4)
                    SynetInnerProduct8i2x4<overflow>(K, src, K, weight + j * K, K, dst + j, N);
                for (; j < N; j += 1)
                    SynetInnerProduct8i2x1<overflow>(K, src, K, weight + j * K, K, dst + j, N);
                src += K * 2;
                dst += N * 2;
            }
            for (; i < M; i += 1)
            {
                size_t j = 0;
                for (; j < N4; j += 4)
                    SynetInnerProduct8i1x4<overflow>(K, src, K, weight + j * K, K, dst + j, N);
                for (; j < N; j += 1)
                    SynetInnerProduct8i1x1<overflow>(K, src, K, weight + j * K, K, dst + j, N);
                src += K;
                dst += N;
            }
        }

        void SynetInnerProduct8i(size_t M, size_t N, size_t K, const uint8_t* src, const int8_t* weight, int32_t* dst, SimdSynetCompatibilityType compatibility)
        {
            if (Base::Precise(compatibility))
                SynetInnerProduct8i<false>(M, N, K, src, weight, dst);
            else
                SynetInnerProduct8i<true>(M, N, K, src, weight, dst);
        }

        //-----------------------------------------------------------------------------------------

        void SynetInnerProductLayerForward1(const float* S0, const float* W, const float* B, size_t K, float* D)
        {
            size_t K16 = K & (~15);
            size_t K64 = K & (~63);
            const float* W0 = W + 0 * K;
            __m512 d00, d01, d02, d03;
            __m512 s0, s1, s2, s3, w0, w1, w2, w3;
            size_t k = 0;
            d00 = _mm512_setzero_ps();
            if (K64)
            {
                d01 = _mm512_setzero_ps();
                d02 = _mm512_setzero_ps();
                d03 = _mm512_setzero_ps();
                for (; k < K64; k += 64)
                {
                    s0 = _mm512_loadu_ps(S0 + k + 0 * F);
                    s1 = _mm512_loadu_ps(S0 + k + 1 * F);
                    w0 = _mm512_loadu_ps(W0 + k + 0 * F);
                    w1 = _mm512_loadu_ps(W0 + k + 1 * F);
                    d00 = _mm512_fmadd_ps(s0, w0, d00);
                    d01 = _mm512_fmadd_ps(s1, w1, d01);
                    s2 = _mm512_loadu_ps(S0 + k + 2 * F);
                    s3 = _mm512_loadu_ps(S0 + k + 3 * F);
                    w2 = _mm512_loadu_ps(W0 + k + 2 * F);
                    w3 = _mm512_loadu_ps(W0 + k + 3 * F);
                    d02 = _mm512_fmadd_ps(s2, w2, d02);
                    d03 = _mm512_fmadd_ps(s3, w3, d03);
                }
                d00 = _mm512_add_ps(_mm512_add_ps(d00, d01), _mm512_add_ps(d02, d03));
            }
            for (; k < K16; k += 16)
            {
                s0 = _mm512_loadu_ps(S0 + k);
                w0 = _mm512_loadu_ps(W0 + k);
                d00 = _mm512_fmadd_ps(s0, w0, d00);
            }
            if (k < K)
            {
                __mmask16 tail = __mmask16(-1) >> (16 + k - K);
                s0 = _mm512_maskz_loadu_ps(tail, S0 + k);
                w0 = _mm512_maskz_loadu_ps(tail, W0 + k);
                d00 = _mm512_fmadd_ps(s0, w0, d00);
            }
            D[0] = ExtractSum(d00) + B[0];
        }

        void SynetInnerProductLayerForward4(const float* S0, const float* W, const float* B, size_t K, float* D)
        {
            size_t K16 = K & (~15);
            size_t K32 = K & (~31);
            const float* W0 = W + 0 * K;
            const float* W1 = W + 1 * K;
            const float* W2 = W + 2 * K;
            const float* W3 = W + 3 * K;
            __m512 d00, d01, d10, d11, d20, d21, d30, d31;
            __m512 s0, s1, w0, w1;
            size_t k = 0;
            d00 = _mm512_setzero_ps();
            d10 = _mm512_setzero_ps();
            d20 = _mm512_setzero_ps();
            d30 = _mm512_setzero_ps();
            if (K32)
            {
                d01 = _mm512_setzero_ps();
                d11 = _mm512_setzero_ps();
                d21 = _mm512_setzero_ps();
                d31 = _mm512_setzero_ps();
                for (; k < K32; k += 32)
                {
                    s0 = _mm512_loadu_ps(S0 + k + 0 * F);
                    s1 = _mm512_loadu_ps(S0 + k + 1 * F);
                    w0 = _mm512_loadu_ps(W0 + k + 0 * F);
                    w1 = _mm512_loadu_ps(W0 + k + 1 * F);
                    d00 = _mm512_fmadd_ps(s0, w0, d00);
                    d01 = _mm512_fmadd_ps(s1, w1, d01);
                    w0 = _mm512_loadu_ps(W1 + k + 0 * F);
                    w1 = _mm512_loadu_ps(W1 + k + 1 * F);
                    d10 = _mm512_fmadd_ps(s0, w0, d10);
                    d11 = _mm512_fmadd_ps(s1, w1, d11);
                    w0 = _mm512_loadu_ps(W2 + k + 0 * F);
                    w1 = _mm512_loadu_ps(W2 + k + 1 * F);
                    d20 = _mm512_fmadd_ps(s0, w0, d20);
                    d21 = _mm512_fmadd_ps(s1, w1, d21);
                    w0 = _mm512_loadu_ps(W3 + k + 0 * F);
                    w1 = _mm512_loadu_ps(W3 + k + 1 * F);
                    d30 = _mm512_fmadd_ps(s0, w0, d30);
                    d31 = _mm512_fmadd_ps(s1, w1, d31);
                }
                d00 = _mm512_add_ps(d00, d01);
                d10 = _mm512_add_ps(d10, d11);
                d20 = _mm512_add_ps(d20, d21);
                d30 = _mm512_add_ps(d30, d31);
            }
            for (; k < K16; k += 16)
            {
                s0 = _mm512_loadu_ps(S0 + k + 0 * F);
                w0 = _mm512_loadu_ps(W0 + k + 0 * F);
                d00 = _mm512_fmadd_ps(s0, w0, d00);
                w0 = _mm512_loadu_ps(W1 + k + 0 * F);
                d10 = _mm512_fmadd_ps(s0, w0, d10);
                w0 = _mm512_loadu_ps(W2 + k + 0 * F);
                d20 = _mm512_fmadd_ps(s0, w0, d20);
                w0 = _mm512_loadu_ps(W3 + k + 0 * F);
                d30 = _mm512_fmadd_ps(s0, w0, d30);
            }
            if (k < K)
            {
                __mmask16 tail = __mmask16(-1) >> (16 + k - K);
                s0 = _mm512_maskz_loadu_ps(tail, S0 + k);
                w0 = _mm512_maskz_loadu_ps(tail, W0 + k);
                d00 = _mm512_fmadd_ps(s0, w0, d00);
                w0 = _mm512_maskz_loadu_ps(tail, W1 + k);
                d10 = _mm512_fmadd_ps(s0, w0, d10);
                w0 = _mm512_maskz_loadu_ps(tail, W2 + k);
                d20 = _mm512_fmadd_ps(s0, w0, d20);
                w0 = _mm512_maskz_loadu_ps(tail, W3 + k);
                d30 = _mm512_fmadd_ps(s0, w0, d30);
            }
            _mm_storeu_ps(D, _mm_add_ps(Extract4Sums(d00, d10, d20, d30), _mm_loadu_ps(B)));
        }

        void SynetInnerProductLayerForward(const float* src, const float* weight, const float* bias, size_t count, size_t size, float* dst)
        {
            float _bias[4] = { 0, 0, 0, 0 };
            size_t count4 = AlignLo(count, 4);
            size_t i = 0;
            for (; i < count4; i += 4)
                SynetInnerProductLayerForward4(src, weight + i * size, (bias ? bias + i : _bias), size, dst + i);
            for (; i < count; ++i)
                SynetInnerProductLayerForward1(src, weight + i * size, (bias ? bias + i : _bias), size, dst + i);
        }

        //-----------------------------------------------------------------------------------------

        SIMD_INLINE __m512 NoseSquareSum(const float* src)
        {
            __m512 s0 = _mm512_maskz_loadu_ps(0xFFFC, src - 2);
            __m512 s1 = _mm512_maskz_loadu_ps(0xFFFE, src - 1);
            __m512 s2 = _mm512_loadu_ps(src);
            __m512 s3 = _mm512_loadu_ps(src + 1);
            __m512 s4 = _mm512_loadu_ps(src + 2);
            return _mm512_add_ps(_mm512_fmadd_ps(s0, s0, _mm512_mul_ps(s1, s1)), _mm512_fmadd_ps(s2, s2, _mm512_fmadd_ps(s3, s3, _mm512_mul_ps(s4, s4))));
        }

        SIMD_INLINE __m512 BodySquareSum(const float* src)
        {
            __m512 s0 = _mm512_loadu_ps(src - 2);
            __m512 s1 = _mm512_loadu_ps(src - 1);
            __m512 s2 = _mm512_loadu_ps(src);
            __m512 s3 = _mm512_loadu_ps(src + 1);
            __m512 s4 = _mm512_loadu_ps(src + 2);
            return _mm512_add_ps(_mm512_fmadd_ps(s0, s0, _mm512_mul_ps(s1, s1)), _mm512_fmadd_ps(s2, s2, _mm512_fmadd_ps(s3, s3, _mm512_mul_ps(s4, s4))));
        }

        SIMD_INLINE __m512 TailSquareSum(const float* src)
        {
            __m512 s0 = _mm512_loadu_ps(src - 2);
            __m512 s1 = _mm512_loadu_ps(src - 1);
            __m512 s2 = _mm512_loadu_ps(src);
            __m512 s3 = _mm512_maskz_loadu_ps(0x7FFF, src + 1);
            __m512 s4 = _mm512_maskz_loadu_ps(0x3FFF, src + 2);
            return _mm512_add_ps(_mm512_fmadd_ps(s0, s0, _mm512_mul_ps(s1, s1)), _mm512_fmadd_ps(s2, s2, _mm512_fmadd_ps(s3, s3, _mm512_mul_ps(s4, s4))));
        }

        template<bool align> void SynetLrnLayerCrossChannelsNchw(const float* src, size_t half, size_t channels, size_t spatial, const float* k, float* dst)
        {
            __m512 k0 = _mm512_set1_ps(k[0]);
            __m512 k1 = _mm512_set1_ps(k[1]);
            __m512 k2 = _mm512_set1_ps(k[2]);
            Pow pow;
            Array32f sum(spatial, true), zero(spatial, true);
            size_t aligned = AlignLo(spatial, F);
            __mmask16 tail = TailMask16(spatial - aligned);
            for (size_t c = 0; c < half; ++c)
            {
                const float* pos = src + c * spatial;
                size_t s = 0;
                for (; s < aligned; s += F)
                {
                    __m512 _pos = Load<align>(pos + s);
                    Store<true>(sum.data + s, _mm512_fmadd_ps(_pos, _pos, Load<true>(sum.data + s)));
                }
                if (s < spatial)
                {
                    __m512 _pos = Load<align, true>(pos + s, tail);
                    __m512 _sum = Load<true, true>(sum.data + s, tail);
                    Store<true, true>(sum.data + s, _mm512_fmadd_ps(_pos, _pos, _sum), tail);
                }
            }
            for (size_t c = 0; c < channels; ++c)
            {
                const float* pos = (c < channels - half) ? src + half * spatial : zero.data;
                const float* neg = (c > half) ? src - (half + 1) * spatial : zero.data;
                size_t s = 0;
                for (; s < aligned; s += F)
                {
                    __m512 _pos = Load<align>(pos + s);
                    __m512 _neg = Load<align>(neg + s);
                    __m512 _sum = Load<true>(sum.data + s);
                    _sum = _mm512_fmadd_ps(_pos, _pos, _mm512_fnmadd_ps(_neg, _neg, _sum));
                    __m512 _src = Load<align>(src + s);
                    Store<true>(sum.data + s, _sum);
                    Store<align>(dst + s, _mm512_mul_ps(_src, pow(_mm512_fmadd_ps(k1, _sum, k0), k2)));
                }
                if (s < spatial)
                {
                    __m512 _pos = Load<align, true>(pos + s, tail);
                    __m512 _neg = Load<align, true>(neg + s, tail);
                    __m512 _sum = Load<true, true>(sum.data + s, tail);
                    _sum = _mm512_fmadd_ps(_pos, _pos, _mm512_fnmadd_ps(_neg, _neg, _sum));
                    __m512 _src = Load<align, true>(src + s, tail);
                    Store<true, true>(sum.data + s, _sum, tail);
                    Store<align, true>(dst + s, _mm512_mul_ps(_src, pow(_mm512_fmadd_ps(k1, _sum, k0), k2)), tail);
                }
                src += spatial;
                dst += spatial;
            }
        }

        SIMD_INLINE void SynetLrnLayerCrossChannelsNchw(const float* src, size_t half, size_t channels, size_t spatial, const float* k, float* dst)
        {
            if (Aligned(src) && Aligned(dst) && Aligned(spatial, F))
                SynetLrnLayerCrossChannelsNchw<true>(src, half, channels, spatial, k, dst);
            else
                SynetLrnLayerCrossChannelsNchw<false>(src, half, channels, spatial, k, dst);
        }

        template<bool align> void SynetLrnLayerCrossChannelsNhwc2h(const float* src, size_t half, size_t channels, size_t spatial, const float* k, float* dst)
        {
            __m512 k0 = _mm512_set1_ps(k[0]);
            __m512 k1 = _mm512_set1_ps(k[1]);
            __m512 k2 = _mm512_set1_ps(k[2]);
            Pow pow;
            size_t aligned = AlignLo(channels - half, F);
            for (size_t s = 0; s < spatial; ++s)
            {
                Store<align>(dst + 0, _mm512_mul_ps(Load<align>(src + 0), pow(_mm512_add_ps(k0, _mm512_mul_ps(k1, NoseSquareSum(src + 0))), k2)));
                for (size_t c = F; c < aligned; c += F)
                    Store<align>(dst + c, _mm512_mul_ps(Load<align>(src + c), pow(_mm512_add_ps(k0, _mm512_mul_ps(k1, BodySquareSum(src + c))), k2)));
                if (aligned != channels - half)
                {
                    size_t c = channels - half - F;
                    Store<false>(dst + c, _mm512_mul_ps(Load<false>(src + c), pow(_mm512_add_ps(k0, _mm512_mul_ps(k1, BodySquareSum(src + c))), k2)));
                }
                size_t c = channels - F;
                Store<false>(dst + c, _mm512_mul_ps(Load<false>(src + c), pow(_mm512_add_ps(k0, _mm512_mul_ps(k1, TailSquareSum(src + c))), k2)));
                src += channels;
                dst += channels;
            }
        }

        SIMD_INLINE void SynetLrnLayerCrossChannelsNhwc(const float* src, size_t half, size_t channels, size_t spatial, const float* k, float* dst)
        {
            if (half == 2 && channels >= F + half)
            {
                if (Aligned(src) && Aligned(dst) && Aligned(channels, F))
                    SynetLrnLayerCrossChannelsNhwc2h<true>(src, half, channels, spatial, k, dst);
                else
                    SynetLrnLayerCrossChannelsNhwc2h<false>(src, half, channels, spatial, k, dst);
            }
            else
                SynetLrnLayerCrossChannels(src, half, channels, spatial, k, dst, SimdTensorFormatNhwc);
        }

        void SynetLrnLayerCrossChannels(const float* src, size_t half, size_t channels, size_t spatial, const float* k, float* dst, SimdTensorFormatType format)
        {
            if (format == SimdTensorFormatNchw)
                SynetLrnLayerCrossChannelsNchw(src, half, channels, spatial, k, dst);
            else if (format == SimdTensorFormatNhwc)
                SynetLrnLayerCrossChannelsNhwc(src, half, channels, spatial, k, dst);
            else
                Base::SynetLrnLayerCrossChannels(src, half, channels, spatial, k, dst, format);
        }

        //-----------------------------------------------------------------------------------------

        void SynetShuffleLayerForward(const float* src0, const float* src1, size_t channels0, size_t channels1, size_t spatial, float* dst0, float* dst1, SimdTensorFormatType format, int type)
        {
            if (format == SimdTensorFormatNchw)
                Base::SynetShuffleLayerForward(src0, src1, channels0, channels1, spatial, dst0, dst1, format, type);
            else if (format == SimdTensorFormatNhwc)
            {
                size_t channels = (channels0 + channels1) / 2;
                size_t channels0DF = AlignLo(channels0, DF);
                __mmask16 tail00 = TailMask16(channels0 - channels0DF);
                __mmask16 tail0F = TailMask16(channels0 - channels0DF - F);
                size_t channels0t = (channels0 - channels0DF) / 2;
                __mmask16 tail0 = TailMask16(channels0t);
                size_t channels1DF = AlignLo(channels1, DF);
                __mmask16 tail10 = TailMask16(channels1 - channels1DF);
                __mmask16 tail1F = TailMask16(channels1 - channels1DF - F);
                size_t channels1t = (channels1 - channels1DF) / 2;
                __mmask16 tail1 = TailMask16(channels1t);
                if (type == 0)
                {
                    for (size_t s = 0; s < spatial; ++s)
                    {
                        size_t cd = 0, cs0 = 0, cs1 = 0;
                        for (; cs0 < channels0DF; cs0 += DF, cd += F)
                        {
                            __m512 s0 = _mm512_loadu_ps(src0 + cs0 + 0);
                            __m512 s1 = _mm512_loadu_ps(src0 + cs0 + F);
                            _mm512_storeu_ps(dst0 + cd, Deinterleave<0>(s0, s1));
                            _mm512_storeu_ps(dst1 + cd, Deinterleave<1>(s0, s1));
                        }
                        if (channels0DF < channels0)
                        {
                            __m512 s0 = _mm512_maskz_loadu_ps(tail00, src0 + cs0 + 0);
                            __m512 s1 = _mm512_maskz_loadu_ps(tail0F, src0 + cs0 + F);
                            _mm512_mask_storeu_ps(dst0 + cd, tail0, Deinterleave<0>(s0, s1));
                            _mm512_mask_storeu_ps(dst1 + cd, tail0, Deinterleave<1>(s0, s1));
                            cd += channels0t;
                        }
                        for (; cs1 < channels1DF; cs1 += DF, cd += F)
                        {
                            __m512 s0 = _mm512_loadu_ps(src1 + cs1 + 0);
                            __m512 s1 = _mm512_loadu_ps(src1 + cs1 + F);
                            _mm512_storeu_ps(dst0 + cd, Deinterleave<0>(s0, s1));
                            _mm512_storeu_ps(dst1 + cd, Deinterleave<1>(s0, s1));
                        }
                        if (channels1DF < channels1)
                        {
                            __m512 s0 = _mm512_maskz_loadu_ps(tail10, src1 + cs1 + 0);
                            __m512 s1 = _mm512_maskz_loadu_ps(tail1F, src1 + cs1 + F);
                            _mm512_mask_storeu_ps(dst0 + cd, tail1, Deinterleave<0>(s0, s1));
                            _mm512_mask_storeu_ps(dst1 + cd, tail1, Deinterleave<1>(s0, s1));
                            cd += channels1t;
                        }
                        src0 += channels0;
                        src1 += channels1;
                        dst0 += channels;
                        dst1 += channels;
                    }
                }
                else if (type == 1)
                {
                    for (size_t s = 0; s < spatial; ++s)
                    {
                        size_t cs = 0, cd0 = 0, cd1 = 0;
                        for (; cd0 < channels0DF; cd0 += DF, cs += F)
                        {
                            __m512 s0 = _mm512_loadu_ps(src0 + cs);
                            __m512 s1 = _mm512_loadu_ps(src1 + cs);
                            _mm512_storeu_ps(dst0 + cd0 + 0, Interleave<0>(s0, s1));
                            _mm512_storeu_ps(dst0 + cd0 + F, Interleave<1>(s0, s1));
                        }
                        if (channels0DF < channels0)
                        {
                            __m512 s0 = _mm512_maskz_loadu_ps(tail0, src0 + cs);
                            __m512 s1 = _mm512_maskz_loadu_ps(tail0, src1 + cs);
                            _mm512_mask_storeu_ps(dst0 + cd0 + 0, tail00, Interleave<0>(s0, s1));
                            _mm512_mask_storeu_ps(dst0 + cd0 + F, tail0F, Interleave<1>(s0, s1));
                            cs += channels0t;
                        }
                        for (; cd1 < channels1DF; cd1 += DF, cs += F)
                        {
                            __m512 s0 = _mm512_loadu_ps(src0 + cs);
                            __m512 s1 = _mm512_loadu_ps(src1 + cs);
                            _mm512_storeu_ps(dst1 + cd1 + 0, Interleave<0>(s0, s1));
                            _mm512_storeu_ps(dst1 + cd1 + F, Interleave<1>(s0, s1));
                        }
                        if (channels1DF < channels1)
                        {
                            __m512 s0 = _mm512_maskz_loadu_ps(tail1, src0 + cs);
                            __m512 s1 = _mm512_maskz_loadu_ps(tail1, src1 + cs);
                            _mm512_mask_storeu_ps(dst1 + cd1 + 0, tail10, Interleave<0>(s0, s1));
                            _mm512_mask_storeu_ps(dst1 + cd1 + F, tail1F, Interleave<1>(s0, s1));
                            cs += channels1t;
                        }
                        src0 += channels;
                        src1 += channels;
                        dst0 += channels0;
                        dst1 += channels1;
                    }
                }
                else
                    assert(0);
            }
            else
                assert(0);
        }

        //-----------------------------------------------------------------------------------------

        void SynetSoftmaxLayerForward21(const float* src, size_t outer, float* dst)
        {
            Exp exp;
            size_t aligned = Simd::AlignLo(outer, F), tail = outer - aligned;
            for (size_t o = 0; o < aligned; o += F)
            {
                __m512 s0 = _mm512_loadu_ps(src + 0);
                __m512 s1 = _mm512_loadu_ps(src + F);
                __m512 ss0 = _mm512_shuffle_ps(s0, s1, 0x88);
                __m512 ss1 = _mm512_shuffle_ps(s0, s1, 0xDD);
                __m512 max = _mm512_max_ps(ss0, ss1);
                __m512 exp0 = exp.Exponent(_mm512_sub_ps(ss0, max));
                __m512 exp1 = exp.Exponent(_mm512_sub_ps(ss1, max));
                __m512 sum = _mm512_add_ps(exp0, exp1);
                __m512 d0 = _mm512_div_ps(exp0, sum);
                __m512 d1 = _mm512_div_ps(exp1, sum);
                _mm512_storeu_ps(dst + 0, _mm512_unpacklo_ps(d0, d1));
                _mm512_storeu_ps(dst + F, _mm512_unpackhi_ps(d0, d1));
                src += DF;
                dst += DF;
            }
            if (tail)
            {
                __mmask16 mask0 = TailMask16(tail * 2 - 0 * F);
                __mmask16 mask1 = TailMask16(tail * 2 - 1 * F);
                __m512 s0 = _mm512_maskz_loadu_ps(mask0, src + 0 * F);
                __m512 s1 = _mm512_maskz_loadu_ps(mask1, src + 1 * F);
                __m512 ss0 = _mm512_shuffle_ps(s0, s1, 0x88);
                __m512 ss1 = _mm512_shuffle_ps(s0, s1, 0xDD);
                __m512 max = _mm512_max_ps(ss0, ss1);
                __m512 exp0 = exp.Exponent(_mm512_sub_ps(ss0, max));
                __m512 exp1 = exp.Exponent(_mm512_sub_ps(ss1, max));
                __m512 sum = _mm512_add_ps(exp0, exp1);
                __m512 d0 = _mm512_div_ps(exp0, sum);
                __m512 d1 = _mm512_div_ps(exp1, sum);
                _mm512_mask_storeu_ps(dst + 0 * F, mask0, _mm512_unpacklo_ps(d0, d1));
                _mm512_mask_storeu_ps(dst + 1 * F, mask1, _mm512_unpackhi_ps(d0, d1));
            }
        }

        SIMD_INLINE void SynetSoftmaxLayerForward31(const Exp& exp, __m512 buf[3])
        {
            __m512 max = _mm512_max_ps(buf[0], _mm512_max_ps(buf[1], buf[2]));
            buf[0] = exp.Exponent(_mm512_sub_ps(buf[0], max));
            buf[1] = exp.Exponent(_mm512_sub_ps(buf[1], max));
            buf[2] = exp.Exponent(_mm512_sub_ps(buf[2], max));
            __m512 sum = _mm512_add_ps(buf[0], _mm512_add_ps(buf[1], buf[2]));
            buf[0] = _mm512_div_ps(buf[0], sum);
            buf[1] = _mm512_div_ps(buf[1], sum);
            buf[2] = _mm512_div_ps(buf[2], sum);
        }

        void SynetSoftmaxLayerForward31(const float* src, size_t outer, float* dst)
        {
            static const __m512i idx = _mm512_setr_epi32(0x00, 0x03, 0x06, 0x09, 0x0C, 0x0F, 0x12, 0x15, 0x18, 0x1B, 0x1E, 0x21, 0x24, 0x27, 0x2A, 0x2D);
            Exp exp;
            __m512 buf[3];
            size_t aligned = Simd::AlignLo(outer, F), tail = outer - aligned;
            for (size_t o = 0; o < aligned; o += F)
            {
                buf[0] = _mm512_i32gather_ps(idx, src + 0, 4);
                buf[1] = _mm512_i32gather_ps(idx, src + 1, 4);
                buf[2] = _mm512_i32gather_ps(idx, src + 2, 4);
                SynetSoftmaxLayerForward31(exp, buf);
                _mm512_i32scatter_ps(dst + 0, idx, buf[0], 4);
                _mm512_i32scatter_ps(dst + 1, idx, buf[1], 4);
                _mm512_i32scatter_ps(dst + 2, idx, buf[2], 4);
                src += 3 * F;
                dst += 3 * F;
            }
            if (tail)
            {
                __mmask16 mask = TailMask16(tail);
                buf[0] = _mm512_mask_i32gather_ps(_mm512_setzero_ps(), mask, idx, src + 0, 4);
                buf[1] = _mm512_mask_i32gather_ps(_mm512_setzero_ps(), mask, idx, src + 1, 4);
                buf[2] = _mm512_mask_i32gather_ps(_mm512_setzero_ps(), mask, idx, src + 2, 4);
                SynetSoftmaxLayerForward31(exp, buf);
                _mm512_mask_i32scatter_ps(dst + 0, mask, idx, buf[0], 4);
                _mm512_mask_i32scatter_ps(dst + 1, mask, idx, buf[1], 4);
                _mm512_mask_i32scatter_ps(dst + 2, mask, idx, buf[2], 4);
            }
        }

        void SynetSoftmaxLayerForward(const float* src, size_t outer, size_t count, size_t inner, float* dst)
        {
            if (count == 2 && inner == 1)
                SynetSoftmaxLayerForward21(src, outer, dst);
            else if (count == 3 && inner == 1)
                SynetSoftmaxLayerForward31(src, outer, dst);
            else
            {
                Exp exp;
                size_t aligned = Simd::AlignLo(inner, F);
                __mmask16 tail = TailMask16(inner - aligned);
                Array32f tmp(inner * 2);
                const float* s;
                float* max = tmp.data, * sum = tmp.data + inner, * d;
                for (size_t o = 0; o < outer; ++o)
                {
                    memcpy(max, src, inner * sizeof(float));
                    s = src + inner;
                    for (size_t c = 1; c < count; ++c)
                    {
                        size_t i = 0;
                        for (; i < aligned; i += F)
                            _mm512_storeu_ps(max + i, _mm512_max_ps(_mm512_loadu_ps(s + i), _mm512_loadu_ps(max + i)));
                        if (i < inner)
                            _mm512_mask_storeu_ps(max + i, tail, _mm512_max_ps(_mm512_maskz_loadu_ps(tail, s + i), _mm512_maskz_loadu_ps(tail, max + i)));
                        s += inner;
                    }

                    s = src;
                    d = dst;
                    memset(sum, 0, inner * sizeof(float));
                    for (size_t c = 0; c < count; ++c)
                    {
                        size_t i = 0;
                        for (; i < aligned; i += F)
                        {
                            __m512 _d = exp.Exponent(_mm512_sub_ps(_mm512_loadu_ps(s + i), _mm512_loadu_ps(max + i)));
                            _mm512_storeu_ps(d + i, _d);
                            _mm512_storeu_ps(sum + i, _mm512_add_ps(_d, _mm512_loadu_ps(sum + i)));
                        }
                        if (i < inner)
                        {
                            __m512 _d = exp.Exponent(_mm512_sub_ps(_mm512_maskz_loadu_ps(tail, s + i), _mm512_maskz_loadu_ps(tail, max + i)));
                            _mm512_mask_storeu_ps(d + i, tail, _d);
                            _mm512_mask_storeu_ps(sum + i, tail, _mm512_add_ps(_d, _mm512_maskz_loadu_ps(tail, sum + i)));
                        }
                        s += inner;
                        d += inner;
                    }

                    d = dst;
                    for (size_t c = 0; c < count; ++c)
                    {
                        size_t i = 0;
                        for (; i < aligned; i += F)
                            _mm512_storeu_ps(d + i, _mm512_div_ps(_mm512_loadu_ps(d + i), _mm512_loadu_ps(sum + i)));
                        if (i < inner)
                            _mm512_mask_storeu_ps(d + i, tail, _mm512_div_ps(_mm512_maskz_loadu_ps(tail, d + i), _mm512_maskz_loadu_ps(tail, sum + i)));
                        d += inner;
                    }
                    src += count * inner;
                    dst += count * inner;
                }
            }
        }

        //-----------------------------------------------------------------------------------------

        template<SimdSynetUnaryOperation32fType type> __m512 SynetUnaryOperation32f(__m512 value);

        template<> SIMD_INLINE __m512 SynetUnaryOperation32f<SimdSynetUnaryOperation32fAbs>(__m512 value)
        {
            return AndNot(_mm512_set1_ps(-0.0f), value);
        }

        template<> SIMD_INLINE __m512 SynetUnaryOperation32f<SimdSynetUnaryOperation32fExp>(__m512 value)
        {
            return Exponent(value);
        }

        template<> SIMD_INLINE __m512 SynetUnaryOperation32f<SimdSynetUnaryOperation32fLog>(__m512 value)
        {
            return Logarithm(value);
        }

        template<> SIMD_INLINE __m512 SynetUnaryOperation32f<SimdSynetUnaryOperation32fNeg>(__m512 value)
        {
            return _mm512_sub_ps(_mm512_setzero_ps(), value);
        }

        template<> SIMD_INLINE __m512 SynetUnaryOperation32f<SimdSynetUnaryOperation32fRsqrt>(__m512 value)
        {
            return _mm512_rsqrt14_ps(value);
        }

        template<> SIMD_INLINE __m512 SynetUnaryOperation32f<SimdSynetUnaryOperation32fSqrt>(__m512 value)
        {
            return _mm512_sqrt_ps(value);
        }

        template<> SIMD_INLINE __m512 SynetUnaryOperation32f<SimdSynetUnaryOperation32fTanh>(__m512 value)
        {
            return Tanh(value);
        }

        template<> SIMD_INLINE __m512 SynetUnaryOperation32f<SimdSynetUnaryOperation32fZero>(__m512 value)
        {
            return _mm512_setzero_ps();
        }

        template<SimdSynetUnaryOperation32fType type, bool align> void SynetUnaryOperation32fLayerForward(const float* src, size_t size, float* dst)
        {
            size_t sizeF = AlignLo(size, F);
            size_t sizeQF = AlignLo(size, QF);
            size_t i = 0;
            for (; i < sizeQF; i += QF)
            {
                Store<align>(dst + i + 0 * F, SynetUnaryOperation32f<type>(Load<align>(src + i + 0 * F)));
                Store<align>(dst + i + 1 * F, SynetUnaryOperation32f<type>(Load<align>(src + i + 1 * F)));
                Store<align>(dst + i + 2 * F, SynetUnaryOperation32f<type>(Load<align>(src + i + 2 * F)));
                Store<align>(dst + i + 3 * F, SynetUnaryOperation32f<type>(Load<align>(src + i + 3 * F)));
            }
            for (; i < sizeF; i += F)
                Store<align>(dst + i, SynetUnaryOperation32f<type>(Load<align>(src + i)));
            if (i < size)
            {
                __mmask16 tail = TailMask16(size - sizeF);
                Store<align, true>(dst + i, SynetUnaryOperation32f<type>(Load<align, true>(src + i, tail)), tail);
            }
        }

        template<bool align> void SynetUnaryOperation32fLayerForward(const float* src, size_t size, SimdSynetUnaryOperation32fType type, float* dst)
        {
            switch (type)
            {
            case SimdSynetUnaryOperation32fAbs: SynetUnaryOperation32fLayerForward<SimdSynetUnaryOperation32fAbs, align>(src, size, dst); break;
            case SimdSynetUnaryOperation32fExp: SynetUnaryOperation32fLayerForward<SimdSynetUnaryOperation32fExp, align>(src, size, dst); break;
            case SimdSynetUnaryOperation32fLog: SynetUnaryOperation32fLayerForward<SimdSynetUnaryOperation32fLog, align>(src, size, dst); break;
            case SimdSynetUnaryOperation32fNeg: SynetUnaryOperation32fLayerForward<SimdSynetUnaryOperation32fNeg, align>(src, size, dst); break;
            case SimdSynetUnaryOperation32fRsqrt: SynetUnaryOperation32fLayerForward<SimdSynetUnaryOperation32fRsqrt, align>(src, size, dst); break;
            case SimdSynetUnaryOperation32fSqrt: SynetUnaryOperation32fLayerForward<SimdSynetUnaryOperation32fSqrt, align>(src, size, dst); break;
            case SimdSynetUnaryOperation32fTanh: SynetUnaryOperation32fLayerForward<SimdSynetUnaryOperation32fTanh, align>(src, size, dst); break;
            case SimdSynetUnaryOperation32fZero: SynetUnaryOperation32fLayerForward<SimdSynetUnaryOperation32fZero, align>(src, size, dst); break;
            default:
                assert(0);
            }
        }

        void SynetUnaryOperation32fLayerForward(const float* src, size_t size, SimdSynetUnaryOperation32fType type, float* dst)
        {
            if (Aligned(src) && Aligned(dst))
                SynetUnaryOperation32fLayerForward<true>(src, size, type, dst);
            else
                SynetUnaryOperation32fLayerForward<false>(src, size, type, dst);
        }
    }
#endif
}
