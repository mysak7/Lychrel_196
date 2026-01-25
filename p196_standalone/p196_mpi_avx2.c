    for ( ; i < last-127 ; i+=128) {
      __m256i o1, o2, o1b, o2b;
      __m256i pc, pcb, pc2, pc2b;
      __m256i r, rb;
      __m256i mask, maskb;
      __m256i temp, tempb;
      __m256i c1to2;
      __m256i o1c, o2c, o1d, o2d;
      __m256i pcc, pcd, pc2c, pc2d;
      __m256i rc, rd;
      __m256i maskc, maskd;
      __m256i tempc, tempd;
      __m256i c2to3, c3to4;
      int cc;
      /** broadcast base */
      const __m256i pb = _mm256_set1_epi8(base);
      /** zero, obviously */
      const __m256i pz = _mm256_setzero_si256();
      /** shuffling vector for the mirror operation ;
          unfortunately _mm256_shuffle_epi8 doesn't do inter-lane :-( */
      const __m256i pe = _mm256_set_epi8( 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,
                                          0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15);
      /** The magic number. 246 + 9 = 255, so 246+10 = 256 -> overflow.
          this allow to use a 64 bits addition to propagate the carry 7
          places automatically inside each 64 bits word */
      const __m256i p246 = _mm256_set_epi8(0,246,246,246,246,246,246,246,0,246,246,246,246,246,246,246,
                                           0,246,246,246,246,246,246,246,0,246,246,246,246,246,246,246);
      /** constant used to check if the upper byte of each 64 bits quarters has a carry ;
          the 100 are just placeholder (anything above 10 would do) */
      const __m256i p9 = _mm256_set_epi8(9,100,100,100,100,100,100,100,9,100,100,100,100,100,100,100,
                                         9,100,100,100,100,100,100,100,9,100,100,100,100,100,100,100);
      /* load regular */
#if defined(ALIGN_AVX2)
      o1  = _mm256_load_si256((__m256i*)(current+i+ 0));
      o1b = _mm256_load_si256((__m256i*)(current+i+32));
      o1c = _mm256_load_si256((__m256i*)(current+i+64));
      o1d = _mm256_load_si256((__m256i*)(current+i+96));
#else
      o1  = _mm256_loadu_si256((__m256i*)(current+i+ 0));
      o1b = _mm256_loadu_si256((__m256i*)(current+i+32));
      o1c = _mm256_loadu_si256((__m256i*)(current+i+64));
      o1d = _mm256_loadu_si256((__m256i*)(current+i+96));
#endif
      /* load mirrored. unaligned load are *very* convenient :-) */
      o2  = _mm256_loadu_si256((__m256i*)(current+full_size-(i+32)));
      o2b = _mm256_loadu_si256((__m256i*)(current+full_size-(i+64)));
      o2c = _mm256_loadu_si256((__m256i*)(current+full_size-(i+96)));
      o2d = _mm256_loadu_si256((__m256i*)(current+full_size-(i+128)));

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
#define PREFETCH_LOAD_TYPE _MM256_HINT_T0
#endif
#if PREFETCH_DISTANCE > 0
      _mm_prefetch(current+i+PREFETCH_DISTANCE, PREFETCH_LOAD_TYPE);
      _mm_prefetch(current+full_size-(i+PREFETCH_DISTANCE+64), PREFETCH_LOAD_TYPE);
#endif

      /* mirror the 32-bytes we have loaded */
      /* first the intra-lane shuffle */
      o2  = _mm256_shuffle_epi8(o2 ,pe);
      o2b = _mm256_shuffle_epi8(o2b,pe);
      o2c = _mm256_shuffle_epi8(o2c,pe);
      o2d = _mm256_shuffle_epi8(o2d,pe);
      /* then the lane shuffle */
      o2  = _mm256_permute2x128_si256(o2,o2,0x01);
      o2b = _mm256_permute2x128_si256(o2b,o2b,0x01);
      o2c = _mm256_permute2x128_si256(o2c,o2c,0x01);
      o2d = _mm256_permute2x128_si256(o2d,o2d,0x01);
      

      /* least significant carry is from the previous iteration,
         all the other are zero */
      pc = _mm256_inserti128_si256(pz, _mm_cvtsi32_si128((int)carry), 0);
      pcb = pz;
      pcc = pz;
      pcd = pz;
      /* carry was consumed */
      carry = 0;

      /* ** step 1 ** */
      /* add the magic number */
      o1  = _mm256_add_epi8(o1 ,p246);
      o1b = _mm256_add_epi8(o1b,p246);
      o1c = _mm256_add_epi8(o1c,p246);
      o1d = _mm256_add_epi8(o1d,p246);
      /* do the 64 bits adds ; 4 carries in each 128 bits registers
         (one in each 64 bits quarters) */
      r  = _mm256_add_epi64(o1 , o2 );
      rb = _mm256_add_epi64(o1b, o2b);
      rc = _mm256_add_epi64(o1c, o2c);
      rd = _mm256_add_epi64(o1d, o2d);

      /* add the previous iteration carry ; still 2 carries in each 128 bits registers */
      r  = _mm256_add_epi64(r , pc );
      rb = _mm256_add_epi64(rb, pcb);
      rc = _mm256_add_epi64(rc, pcc);
      rd = _mm256_add_epi64(rd, pcd);

      /* Here we compare the signed 8 bits to zero.
         anything lower than zero has the most significant
         bit at one, and therefore is over 246 in non-signed term,
         so we need to remove the extra 246 we added before.
         everything above zero has overflowed, so the 246 has been
         propagated to the next byte as a carry - no need to remove it.
      */
      /* This build the mask. */
      mask  = _mm256_cmpgt_epi8(pz, r);
      maskb = _mm256_cmpgt_epi8(pz, rb);
      maskc = _mm256_cmpgt_epi8(pz, rc);
      maskd = _mm256_cmpgt_epi8(pz, rd);
      /* this remove the extra 246 */
      r  = _mm256_sub_epi8(r , _mm256_and_si256(mask , p246));
      rb = _mm256_sub_epi8(rb, _mm256_and_si256(maskb, p246));
      rc = _mm256_sub_epi8(rc, _mm256_and_si256(maskc, p246));
      rd = _mm256_sub_epi8(rd, _mm256_and_si256(maskd, p246));
      /* check for the extra 2 carries */
      mask  = _mm256_cmpgt_epi8(r , p9);
      maskb = _mm256_cmpgt_epi8(rb, p9);
      maskc = _mm256_cmpgt_epi8(rc, p9);
      maskd = _mm256_cmpgt_epi8(rd, p9);
      /* turns the all-1 mask (i.e. '-1') into actual 1 */
      pc  = _mm256_sub_epi8(pz, mask );
      pcb = _mm256_sub_epi8(pz, maskb);
      pcc = _mm256_sub_epi8(pz, maskc);
      pcd = _mm256_sub_epi8(pz, maskd);
      /* keep only where we need to remove 10 (the base) ? */
      temp  = _mm256_and_si256(pb, mask ); 
      tempb = _mm256_and_si256(pb, maskb);
      tempc = _mm256_and_si256(pb, maskc); 
      tempd = _mm256_and_si256(pb, maskd);
      /* remove the base */
      r  = _mm256_sub_epi8(r , temp );
      rb = _mm256_sub_epi8(rb, tempb);
      rc = _mm256_sub_epi8(rc, tempc);
      rd = _mm256_sub_epi8(rd, tempd);

#define printv(p,v)                                                     \
      {                                                                 \
        ALIGN32 unsigned char temp[32];                                 \
        _mm256_store_si256((__m256i*)(temp), v);                        \
        int z;                                                          \
        printf("%8s:%8s = ",p,#v);                                          \
        for (z = 31 ; z >= 0 ; z--) printf("%d", (int)temp[z]);         \
        printf("\n");                                                   \
      }
      /* carry propagation ;
         this one recover the carry from one register to the next
         or to the next iteration */
      /* _mm256_srli_si256 doesn't work inter-lane... */
      c1to2 = _mm256_srli_si256(pc, 15);
      c1to2 = _mm256_permute2x128_si256(c1to2,pz,0x21);
      c2to3 = _mm256_srli_si256(pcb, 15);
      c2to3 = _mm256_permute2x128_si256(c2to3,pz,0x21);
      c3to4 = _mm256_srli_si256(pcc, 15);
      c3to4 = _mm256_permute2x128_si256(c3to4,pz,0x21);

#ifdef SOMEFUTUREINSTRUCTIONSET_MAYBE
      carry += _mm256_extract_epi8(pcd, 31);
#else
      // emulates non-existent 256bits VPEXTRB
      carry += _mm_extract_epi8(_mm256_extracti128_si256(pcd, 1), 15);
#endif
      /* this one recover the carry from one quarter to the next */
      /*  _mm256_slli_si256 doesn't work inter-lane, either */
      pc2  = _mm256_srli_si256(pc, 15);
      pc2  = _mm256_permute2x128_si256(pc2,pz,0x02);
      pc2b = _mm256_srli_si256(pcb, 15);
      pc2b = _mm256_permute2x128_si256(pc2b,pz,0x02);
      pc2c = _mm256_srli_si256(pcc, 15);
      pc2c = _mm256_permute2x128_si256(pc2c,pz,0x02);
      pc2d = _mm256_srli_si256(pcd, 15);
      pc2d = _mm256_permute2x128_si256(pc2d,pz,0x02);
      pc  = _mm256_slli_si256(pc , 1);
      pc  = _mm256_add_epi8(pc , pc2 );
      pcb = _mm256_slli_si256(pcb, 1);
      pcb = _mm256_add_epi8(pcb, pc2b);
      pcc = _mm256_slli_si256(pcc, 1);
      pcc = _mm256_add_epi8(pcc, pc2c);
      pcd = _mm256_slli_si256(pcd, 1);
      pcd = _mm256_add_epi8(pcd, pc2d);
      /* carry fusion from both halves */
      pcb = _mm256_add_epi8(pcb, c1to2);
      pcc = _mm256_add_epi8(pcc, c2to3);
      pcd = _mm256_add_epi8(pcd, c3to4);

      /* ** step 2 ** */
      /* we have to redo everything but the actual addition for carry propagation
         inside the 2 256 bits registers.
         This will go on as long as a carry propagate inside,
         it's usually only one step. */
      do {
        r  = _mm256_add_epi64(r ,p246);
        rb = _mm256_add_epi64(rb,p246);
        rc = _mm256_add_epi64(rc,p246);
        rd = _mm256_add_epi64(rd,p246);

        r  = _mm256_add_epi64(r , pc );
        rb = _mm256_add_epi64(rb, pcb);
        rc = _mm256_add_epi64(rc, pcc);
        rd = _mm256_add_epi64(rd, pcd);

        mask  = _mm256_cmpgt_epi8(pz, r );
        maskb = _mm256_cmpgt_epi8(pz, rb);
        maskc = _mm256_cmpgt_epi8(pz, rc);
        maskd = _mm256_cmpgt_epi8(pz, rd);
      
        r  = _mm256_sub_epi8(r , _mm256_and_si256(mask , p246));
        rb = _mm256_sub_epi8(rb, _mm256_and_si256(maskb, p246));
        rc = _mm256_sub_epi8(rc, _mm256_and_si256(maskc, p246));
        rd = _mm256_sub_epi8(rd, _mm256_and_si256(maskd, p246));

        mask  = _mm256_cmpgt_epi8(r , p9);
        maskb = _mm256_cmpgt_epi8(rb, p9);
        maskc = _mm256_cmpgt_epi8(rc, p9);
        maskd = _mm256_cmpgt_epi8(rd, p9);
      
        pc  = _mm256_sub_epi8(pz, mask );
        pcb = _mm256_sub_epi8(pz, maskb);
        pcc = _mm256_sub_epi8(pz, maskc);
        pcd = _mm256_sub_epi8(pz, maskd);

        temp  = _mm256_and_si256(pb, mask ); 
        tempb = _mm256_and_si256(pb, maskb);
        tempc = _mm256_and_si256(pb, maskc); 
        tempd = _mm256_and_si256(pb, maskd);

        r  = _mm256_sub_epi8(r , temp );
        rb = _mm256_sub_epi8(rb, tempb);
        rc = _mm256_sub_epi8(rc, tempc);
        rd = _mm256_sub_epi8(rd, tempd);

        c1to2 = _mm256_srli_si256(pc, 15);
        c1to2 = _mm256_permute2x128_si256(c1to2,pz,0x21);
        c2to3 = _mm256_srli_si256(pcb, 15);
        c2to3 = _mm256_permute2x128_si256(c2to3,pz,0x21);
        c3to4 = _mm256_srli_si256(pcc, 15);
        c3to4 = _mm256_permute2x128_si256(c3to4,pz,0x21);
        
#ifdef SOMEFUTUREINSTRUCTIONSET_MAYBE
        carry += _mm256_extract_epi8(pcd, 31);
#else
        // emulates non-existent 256bits VPEXTRB
        carry += _mm_extract_epi8(_mm256_extracti128_si256(pcd, 1), 15);
#endif
        
        pc2  = _mm256_srli_si256(pc, 15);
        pc2  = _mm256_permute2x128_si256(pc2,pz,0x02);
        pc2b = _mm256_srli_si256(pcb, 15);
        pc2b = _mm256_permute2x128_si256(pc2b,pz,0x02);
        pc2c = _mm256_srli_si256(pcc, 15);
        pc2c = _mm256_permute2x128_si256(pc2c,pz,0x02);
        pc2d = _mm256_srli_si256(pcd, 15);
        pc2d = _mm256_permute2x128_si256(pc2d,pz,0x02);
        pc  = _mm256_slli_si256(pc , 1);
        pc  = _mm256_add_epi8(pc , pc2 );
        pcb = _mm256_slli_si256(pcb, 1);
        pcb = _mm256_add_epi8(pcb, pc2b);
        pcc = _mm256_slli_si256(pcc, 1);
        pcc = _mm256_add_epi8(pcc, pc2c);
        pcd = _mm256_slli_si256(pcd, 1);
        pcd = _mm256_add_epi8(pcd, pc2d);
        
        pcb = _mm256_add_epi8(pcb, c1to2);
        pcc = _mm256_add_epi8(pcc, c2to3);
        pcd = _mm256_add_epi8(pcd, c3to4);
        
        cc = _mm256_testz_si256(pc,pc);
        cc = cc && _mm256_testz_si256(pcb,pcb);
        cc = cc && _mm256_testz_si256(pcc,pcc);
        cc = cc && _mm256_testz_si256(pcd,pcd);
      } while (!cc);
      /* store the results where needed */
#if defined(ALIGN_AVX2)
      _mm256_stream_si256((__m256i*)(next+i+ 0), r );
      _mm256_stream_si256((__m256i*)(next+i+32), rb);
      _mm256_stream_si256((__m256i*)(next+i+64), rc);
      _mm256_stream_si256((__m256i*)(next+i+96), rd);
#else
      _mm256_storeu_si256((__m256i*)(next+i+ 0), r );
      _mm256_storeu_si256((__m256i*)(next+i+32), rb);
      _mm256_storeu_si256((__m256i*)(next+i+64), rc);
      _mm256_storeu_si256((__m256i*)(next+i+96), rd);
#endif
    }
