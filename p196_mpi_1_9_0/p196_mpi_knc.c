      for ( ; i < last-127 ; i+=128) {
#if 0
        __m512i a = _mm512_loaddu(current, i);
        __m512i a_2 = _mm512_loaddu(current, i+64);
#else
        __m512i a = _mm512_load_epi32(current + i);
        __m512i a_2 = _mm512_load_epi32(current + i + 64);
#endif
#if 0
        __m512i bus = _mm512_loaddu(current, full_size - (i + 64)); 
        __m512i bus_2 = _mm512_loaddu(current, full_size - (i + 128)); 
        __m512i b_ = _mm512_bswap(bus);
        __m512i b_2_ = _mm512_bswap(bus_2);
#else
        __m512i b_ = _mm512_loaddu_bswap((unsigned char*)current, full_size - (i + 64)); 
        __m512i b_2_ = _mm512_loaddu_bswap((unsigned char*)current, full_size - (i + 128)); 
#endif
#ifndef PREFETCH_DISTANCE
      /* should be a multiple of 128 ; 0 to disable */
#define PREFETCH_DISTANCE 128
#endif
#ifndef PREFETCH_INST_LOAD
#define PREFETCH_INST_LOAD _mm_prefetch
#endif
#if PREFETCH_DISTANCE > 0
        PREFETCH_INST_LOAD(current+i+PREFETCH_DISTANCE, _MM_HINT_ET0);
        PREFETCH_INST_LOAD(current+i+PREFETCH_DISTANCE+64, _MM_HINT_ET0);
        PREFETCH_INST_LOAD(current+full_size-(i+PREFETCH_DISTANCE+64), _MM_HINT_ET0);
        PREFETCH_INST_LOAD(current+full_size-(i+PREFETCH_DISTANCE+128), _MM_HINT_ET0);
#endif
        __m512i vf6 = _mm512_set1_epi32(0xF6F6F6F6);
        __m512i zero = _mm512_setzero_epi32();
        __m512i c = _mm512_add_epi32(a, vf6);
        __m512i c_2 = _mm512_add_epi32(a_2, vf6);
        __mmask mmc = _mm512_int2mask((int)carry);
        __mmask mmc_2 = _mm512_int2mask(0);
        __mmask mmc2;
        __mmask mmc2_2;
        __mmask mmc3;
        __mmask mmc3_2;
        __m512i d = _mm512_adc_epi32(c, mmc, b_, &mmc2);
        __m512i d_2 = _mm512_adc_epi32(c_2, mmc_2, b_2_, &mmc2_2);
        int temp = _mm512_mask2int(mmc2);
        int temp_2 = _mm512_mask2int(mmc2_2);
        mmc3 = mmc2;
        mmc3_2 = mmc2_2;
        carry = 0;
        carry = (temp_2 >> 15) | carry;
        while ((temp_2 & 0x00007FFF) | temp)  {
          temp_2 = (temp_2 << 1) | (temp >> 15);
          temp = temp << 1;
          mmc = _mm512_int2mask(temp);
          mmc_2 = _mm512_int2mask(temp_2);
          d = _mm512_adc_epi32(d, mmc, zero, &mmc2);
          d_2 = _mm512_adc_epi32(d_2, mmc_2, zero, &mmc2_2);
          temp = _mm512_mask2int(mmc2);
          temp_2 = _mm512_mask2int(mmc2_2);
          mmc3 = _mm512_kor(mmc2, mmc3);
          mmc3_2 = _mm512_kor(mmc2_2, mmc3_2);
          carry = (temp_2 >> 15) | carry;
        }
        d = _mm512_mask_sub_epi32(d, _mm512_knot(mmc3), d, _mm512_set1_epi32(0xF6000000));
        d_2 = _mm512_mask_sub_epi32(d_2, _mm512_knot(mmc3_2), d_2, _mm512_set1_epi32(0xF6000000));
        __m512i e1 = _mm512_xor_epi32(b_, d);
        __m512i e1_2 = _mm512_xor_epi32(b_2_, d_2);
        __m512i e2 = _mm512_xor_epi32(e1, c);
        __m512i e2_2 = _mm512_xor_epi32(e1_2, c_2);
        __m512i e3 = _mm512_xor_epi32(e2, _mm512_set1_epi32(0xFFFFFFFF));
        __m512i e3_2 = _mm512_xor_epi32(e2_2, _mm512_set1_epi32(0xFFFFFFFF));
        __m512i e = _mm512_and_epi32(e3, _mm512_set1_epi32(0x01010100));
        __m512i e_2 = _mm512_and_epi32(e3_2, _mm512_set1_epi32(0x01010100));
        __m512i f1 = _mm512_srlv_epi32(e, _mm512_set1_epi32(8));
        __m512i f1_2 = _mm512_srlv_epi32(e_2, _mm512_set1_epi32(8));
        __m512i f2 = _mm512_mullo_epi32(f1, _mm512_set1_epi32(246));
        __m512i f2_2 = _mm512_mullo_epi32(f1_2, _mm512_set1_epi32(246));
        __m512i f = _mm512_sub_epi32(d, f2);
        __m512i f_2 = _mm512_sub_epi32(d_2, f2_2);
        /*     _mm512_stored(next + i, f, _MM_DOWNC_NONE, _MM_SUBSET32_16, 0); */
#if 0
        _mm512_storedu(f, next, i);
        _mm512_storedu(f_2, next, i+64);
#else
        _mm512_store_epi32(next + i, f);
        _mm512_store_epi32(next + i + 64, f_2);
#endif
#ifdef EVICT_ST
        _mm_clevict(next + i,  _MM_HINT_T0);
        _mm_clevict(next + i + 64,  _MM_HINT_T0);
        _mm_clevict(next + i,  _MM_HINT_T1);
        _mm_clevict(next + i + 64,  _MM_HINT_T1);
#endif
#ifndef PREFETCH_DISTANCE_ST
      /* should be a multiple of 128 ; 0 to disable */
#define PREFETCH_DISTANCE_ST 256
#endif
#ifndef PREFETCH_INST_STORE
#define PREFETCH_INST_STORE _mm_prefetch
#endif
#if PREFETCH_DISTANCE_ST > 0
        PREFETCH_INST_STORE(next+i+PREFETCH_DISTANCE, _MM_HINT_ET0);
        PREFETCH_INST_STORE(next+i+PREFETCH_DISTANCE+64, _MM_HINT_ET0);
#endif
      }
