    for ( ; i < last-63 ; i+=64) {
      __m128i o1, o2, o1b, o2b;
      __m128i o1c, o2c, o1d, o2d;
      __m128i pc, pcb, pcc, pcd;
      __m128i r, rb, rc, rd;
      __m128i mask, maskb, maskc, maskd;
      __m128i temp, tempb, tempc, tempd;
      __m128i c1to2, c2to3, c3to4;
      int cc;
      /** broadcast base */
      const __m128i pb = _mm_set1_epi8(base);
      /** zero, obviously */
      const __m128i pz = _mm_setzero_si128();
      /** shuffling vector for the mirror operation */
#if defined(SSE4) || defined(SSSE3)
      const __m128i pe = _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
#endif
      /** The magic number. 246 + 9 = 255, so 246+10 = 256 -> overflow.
          this allow to use a 64 bits addition to propagate the carry 7
          places automatically inside each 64 bits word */
      const __m128i p246 = _mm_set_epi8(0,246,246,246,246,246,246,246,0,246,246,246,246,246,246,246);
      /** constant used to check if the upper byte of each 64 bits halves has a carry ;
          the 100 are just placeholder (anything above 10 would do) */
      const __m128i p9 = _mm_set_epi8(9,100,100,100,100,100,100,100,9,100,100,100,100,100,100,100);
      /* load regular */
      o1  = _mm_load_si128((__m128i*)(current+i+ 0));
      o1b = _mm_load_si128((__m128i*)(current+i+16));
      o1c = _mm_load_si128((__m128i*)(current+i+32));
      o1d = _mm_load_si128((__m128i*)(current+i+48));
      /* load mirrored. unaligned load are *very* convenient :-) */
#ifdef USE_LDDQU
      o2  = _mm_lddqu_si128((__m128i*)(current+full_size-(i+16)));
      o2b = _mm_lddqu_si128((__m128i*)(current+full_size-(i+32)));
      o2c = _mm_lddqu_si128((__m128i*)(current+full_size-(i+48)));
      o2d = _mm_lddqu_si128((__m128i*)(current+full_size-(i+64)));
#else
      o2  = _mm_loadu_si128((__m128i*)(current+full_size-(i+16)));
      o2b = _mm_loadu_si128((__m128i*)(current+full_size-(i+32)));
      o2c = _mm_loadu_si128((__m128i*)(current+full_size-(i+48)));
      o2d = _mm_loadu_si128((__m128i*)(current+full_size-(i+64)));
#endif

      /* prefetch ; doesn't seem to hurt, needs more testing */
      /* actually, hurts on small size (100-200 KB), but improves on large size (128+ MB),
         unsurprisingly.
         should likely be disabled on large cluster (high number of processes -> small
         size in each process), but enabled on single-node runs (few processes -> large
         size in each process). */ 
#ifndef PREFETCH_DISTANCE
      /* should be a multiple of 64 ; 0 to disable */
#define PREFETCH_DISTANCE 256
#endif
#ifndef PREFETCH_LOAD_TYPE
#define PREFETCH_LOAD_TYPE _MM_HINT_T0
#endif
#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif
#if PREFETCH_DISTANCE > 0
      _mm_prefetch(current+i+PREFETCH_DISTANCE, PREFETCH_LOAD_TYPE);
#if CACHE_LINE_SIZE == 32
      _mm_prefetch(current+i+PREFETCH_DISTANCE+32, PREFETCH_LOAD_TYPE);
#endif
      _mm_prefetch(current+full_size-(i+PREFETCH_DISTANCE+64), PREFETCH_LOAD_TYPE);
#if CACHE_LINE_SIZE == 32
      _mm_prefetch(current+full_size-(i+PREFETCH_DISTANCE+32), PREFETCH_LOAD_TYPE);
#endif
#endif

      /* mirror the 16-bytes we have loaded */
#if defined(SSE4) || defined(SSSE3)
      o2  = _mm_shuffle_epi8(o2 ,pe);
      o2b = _mm_shuffle_epi8(o2b,pe);
      o2c = _mm_shuffle_epi8(o2c,pe);
      o2d = _mm_shuffle_epi8(o2d,pe);
#else
      /* emulates the pshufb used for byte-swapping by an actual x86 byteswap */
#define SSE_BSWAP(reg)                                                  \
      do {                                                              \
        ALIGN16 unsigned long long shuf0[2], shuf1;       \
        _mm_store_si128((__m128i*)shuf0, reg);                          \
        shuf1 = _bswap64(shuf0[0]);                                     \
        shuf0[0] = _bswap64(shuf0[1]);                                  \
        shuf0[1] = shuf1;                                               \
        reg = _mm_load_si128((__m128i*)shuf0);                          \
      } while(0)
      SSE_BSWAP(o2);
      SSE_BSWAP(o2b);
      SSE_BSWAP(o2c);
      SSE_BSWAP(o2d);
#endif

      /* least significant carry is from the previous iteration,
         all the other are zero */
      pc = _mm_cvtsi32_si128((int)carry);
      pcb = pz;
      pcc = pz;
      pcd = pz;
      /* carry was consumed */
      carry = 0;

      /* ** step 1 ** */
      /* add the magic number */
      o1  = _mm_add_epi8(o1 ,p246);
      o1b = _mm_add_epi8(o1b,p246);
      o1c = _mm_add_epi8(o1c,p246);
      o1d = _mm_add_epi8(o1d,p246);
      /* do the 64 bits adds ; 2 carries in each 128 bits registers
         (one in each 64 bits halves) */
      r  = _mm_add_epi64(o1 , o2 );
      rb = _mm_add_epi64(o1b, o2b);
      rc = _mm_add_epi64(o1c, o2c);
      rd = _mm_add_epi64(o1d, o2d);

      /* add the previous iteration carry ; still 2 carries in each 128 bits registers */
      r  = _mm_add_epi64(r , pc );
      rb = _mm_add_epi64(rb, pcb);
      rc = _mm_add_epi64(rc, pcc);
      rd = _mm_add_epi64(rd, pcd);

      /* Here we compare the signed 8 bits to zero.
         anything lower than zero has the most significant
         bit at one, and therefore is over 246 in non-signed term,
         so we need to remove the extra 246 we added before.
         everything above zero has overflowed, so the 246 has been
         propagated to the next byte as a carry - no need to remove it.
      */
      /* This build the mask. */
      mask  = _mm_cmplt_epi8(r , pz);
      maskb = _mm_cmplt_epi8(rb, pz);
      maskc = _mm_cmplt_epi8(rc, pz);
      maskd = _mm_cmplt_epi8(rd, pz);
      /* this remove the extra 246 */
      r  = _mm_sub_epi8(r , _mm_and_si128(mask , p246));
      rb = _mm_sub_epi8(rb, _mm_and_si128(maskb, p246));
      rc = _mm_sub_epi8(rc, _mm_and_si128(maskc, p246));
      rd = _mm_sub_epi8(rd, _mm_and_si128(maskd, p246));
      /* check for the extra 2 carries */
      mask  = _mm_cmpgt_epi8(r , p9);
      maskb = _mm_cmpgt_epi8(rb, p9);
      maskc = _mm_cmpgt_epi8(rc, p9);
      maskd = _mm_cmpgt_epi8(rd, p9);
      /* turns the all-1 mask (i.e. '-1') into actual 1 */
      pc  = _mm_sub_epi8(pz, mask );
      pcb = _mm_sub_epi8(pz, maskb);
      pcc = _mm_sub_epi8(pz, maskc);
      pcd = _mm_sub_epi8(pz, maskd);
      /* keep only where we need to remove 10 (the base) ? */
      temp  = _mm_and_si128(pb, mask ); 
      tempb = _mm_and_si128(pb, maskb);
      tempc = _mm_and_si128(pb, maskc); 
      tempd = _mm_and_si128(pb, maskd);
      /* remove the base */
      r  = _mm_sub_epi8(r , temp );
      rb = _mm_sub_epi8(rb, tempb);
      rc = _mm_sub_epi8(rc, tempc);
      rd = _mm_sub_epi8(rd, tempd);
      /* carry propagation ;
         this one recover the carry from one register to the next
         or to the next iteration */
      c1to2 = _mm_srli_si128(pc , 15);
      c2to3 = _mm_srli_si128(pcb, 15);
      c3to4 = _mm_srli_si128(pcc, 15);
#ifdef SSE4
      carry += _mm_extract_epi8(pcd, 15);
#else
      { // emulates PEXTRB
        __m128i pextrb0 = _mm_srli_si128(pcd, 15);
        carry += _mm_cvtsi128_si32(pextrb0);
      }
#endif
      /* this one recover the carry from one half to the next */
      pc  = _mm_slli_si128(pc , 1);
      pcb = _mm_slli_si128(pcb, 1);
      pcc = _mm_slli_si128(pcc, 1);
      pcd = _mm_slli_si128(pcd, 1);
      /* carry fusion from both halves */
      pcb = _mm_add_epi8(pcb, c1to2);
      pcc = _mm_add_epi8(pcc, c2to3);
      pcd = _mm_add_epi8(pcd, c3to4);

      /* ** step 2 ** */
      /* we have to redo everything but the actual addition for carry propagation
         inside the 4 128 bits registers.
         This will go on as long as a carry propagate inside,
         it's usually only one step. */
      do {
        r  = _mm_add_epi64(r ,p246);
        rb = _mm_add_epi64(rb,p246);
        rc = _mm_add_epi64(rc,p246);
        rd = _mm_add_epi64(rd,p246);

        r  = _mm_add_epi64(r , pc );
        rb = _mm_add_epi64(rb, pcb);
        rc = _mm_add_epi64(rc, pcc);
        rd = _mm_add_epi64(rd, pcd);

        mask  = _mm_cmplt_epi8(r , pz);
        maskb = _mm_cmplt_epi8(rb, pz);
        maskc = _mm_cmplt_epi8(rc, pz);
        maskd = _mm_cmplt_epi8(rd, pz);
      
        r  = _mm_sub_epi8(r , _mm_and_si128(mask , p246));
        rb = _mm_sub_epi8(rb, _mm_and_si128(maskb, p246));
        rc = _mm_sub_epi8(rc, _mm_and_si128(maskc, p246));
        rd = _mm_sub_epi8(rd, _mm_and_si128(maskd, p246));

        mask  = _mm_cmpgt_epi8(r , p9);
        maskb = _mm_cmpgt_epi8(rb, p9);
        maskc = _mm_cmpgt_epi8(rc, p9);
        maskd = _mm_cmpgt_epi8(rd, p9);
      
        pc  = _mm_sub_epi8(pz, mask );
        pcb = _mm_sub_epi8(pz, maskb);
        pcc = _mm_sub_epi8(pz, maskc);
        pcd = _mm_sub_epi8(pz, maskd);

        temp  = _mm_and_si128(pb, mask ); 
        tempb = _mm_and_si128(pb, maskb);
        tempc = _mm_and_si128(pb, maskc); 
        tempd = _mm_and_si128(pb, maskd);

        r  = _mm_sub_epi8(r , temp );
        rb = _mm_sub_epi8(rb, tempb);
        rc = _mm_sub_epi8(rc, tempc);
        rd = _mm_sub_epi8(rd, tempd);

        c1to2 = _mm_srli_si128(pc, 15);
        c2to3 = _mm_srli_si128(pcb, 15);
        c3to4 = _mm_srli_si128(pcc, 15);
#ifdef SSE4
        carry += _mm_extract_epi8(pcd, 15);
#else
        { // emulates PEXTRB
          __m128i pextrb0 = _mm_srli_si128(pcd, 15);
          carry += _mm_cvtsi128_si32(pextrb0);
        }
#endif
      
        pc  = _mm_slli_si128(pc , 1);
        pcb = _mm_slli_si128(pcb, 1);
        pcc = _mm_slli_si128(pcc, 1);
        pcd = _mm_slli_si128(pcd, 1);

        pcb = _mm_add_epi8(pcb, c1to2);
        pcc = _mm_add_epi8(pcc, c2to3);
        pcd = _mm_add_epi8(pcd, c3to4);

#ifdef SSE4
        cc = _mm_testz_si128(pc,pc);
        cc = cc && _mm_testz_si128(pcb,pcb);
        cc = cc && _mm_testz_si128(pcc,pcc);
        cc = cc && _mm_testz_si128(pcd,pcd);
#else
        { // emulates PTEST
#if defined(_WIN32) && defined(_MSC_VER)
          /* for some reason, the original linux 64 bits
             version below doesn't work in VS2010.
             It seems that doing comparison on 64 bits
             integer doesn't work ?!?
             Using a 32 bits workaround seems OK ... */
          __m128i ptest0 = _mm_or_si128(pc, pcb);
          __m128i ptest1 = _mm_or_si128(pcc, pcd);
          __m128i ptest2 = _mm_or_si128(ptest0, ptest1);
          int ptestA = _mm_cvtsi128_si32(ptest2);
          int ptestB = _mm_cvtsi128_si32(_mm_srli_si128(ptest2, 4));
          int ptestC = _mm_cvtsi128_si32(_mm_srli_si128(ptest2, 8));
          int ptestD = _mm_cvtsi128_si32(_mm_srli_si128(ptest2, 12));
          cc = !ptestA && !ptestB && !ptestC && !ptestD;
#else
          cc =
            !(_mm_cvtsi128_si64(pc)) &&
            !(_mm_cvtsi128_si64(_mm_srli_si128(pc, 8)));
          cc = cc &&
            !(_mm_cvtsi128_si64(pcb)) &&
            !(_mm_cvtsi128_si64(_mm_srli_si128(pcb, 8)));
          cc = cc &&
            !(_mm_cvtsi128_si64(pcc)) &&
            !(_mm_cvtsi128_si64(_mm_srli_si128(pcc, 8)));
          cc = cc &&
            !(_mm_cvtsi128_si64(pcd)) &&
            !(_mm_cvtsi128_si64(_mm_srli_si128(pcd, 8)));
#endif
        }   
#endif
      } while (!cc);
      /* store the results where needed */
#ifdef STREAMING_STORES
      _mm_stream_si128((__m128i*)(next+i+ 0), r );
      _mm_stream_si128((__m128i*)(next+i+16), rb);
      _mm_stream_si128((__m128i*)(next+i+32), rc);
      _mm_stream_si128((__m128i*)(next+i+48), rd);
#else
      _mm_store_si128((__m128i*)(next+i+ 0), r );
      _mm_store_si128((__m128i*)(next+i+16), rb);
      _mm_store_si128((__m128i*)(next+i+32), rc);
      _mm_store_si128((__m128i*)(next+i+48), rd);
#endif
    }
