int doup = 1;
for ( ; i < last[0]-63 ; i+=64) {
  __m128i ho1, ho2, ho1b, ho2b;
  __m128i ho1c, ho2c, ho1d, ho2d;
  __m256i o1, o2, o1b, o2b;
  __m256i o1c, o2c, o1d, o2d;
  __m256i pc, pcb, pcc, pcd;
  __m256i r, rb, rc, rd;
  __m256i mask, maskb, maskc, maskd;
  __m256i temp, tempb, tempc, tempd;
  __m256i c1to2, c2to3, c3to4;
  int cc;
  doup = doup && (i1 < last[1]-63);
  /** broadcast base */
  const __m256i pb = _mm256_set1_epi8(base);
  /** zero, obviously */
  const __m256i pz = _mm256_setzero_si256();
  /** shuffling vector for the mirror operation */
  const __m256i pe = _mm256_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
                                     0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
  /** The magic number. 246 + 9 = 255, so 246+10 = 256 -> overflow.
      this allow to use a 64 bits addition to propagate the carry 7
      places automatically inside each 64 bits word */
  const __m256i p246 = _mm256_set_epi8(0,246,246,246,246,246,246,246,0,246,246,246,246,246,246,246,
                                       0,246,246,246,246,246,246,246,0,246,246,246,246,246,246,246);
  /** constant used to check if the upper byte of each 64 bits halves has a carry ;
      the 100 are just placeholder (anything above 10 would do) */
  const __m256i p9 = _mm256_set_epi8(9,100,100,100,100,100,100,100,9,100,100,100,100,100,100,100,
                                     9,100,100,100,100,100,100,100,9,100,100,100,100,100,100,100);
  /* load regular */
  /* first */
  ho1  = _mm_load_si128((__m128i*)(current+i+ 0));
  ho1b = _mm_load_si128((__m128i*)(current+i+16));
  ho1c = _mm_load_si128((__m128i*)(current+i+32));
  ho1d = _mm_load_si128((__m128i*)(current+i+48));
  /* second */
  if (doup) {
    o1  = _mm256_inserti128_si256(_mm256_castsi128_si256(ho1 ),_mm_load_si128((__m128i*)(current+i1+ 0)),1);
    o1b = _mm256_inserti128_si256(_mm256_castsi128_si256(ho1b),_mm_load_si128((__m128i*)(current+i1+16)),1);
    o1c = _mm256_inserti128_si256(_mm256_castsi128_si256(ho1c),_mm_load_si128((__m128i*)(current+i1+32)),1);
    o1d = _mm256_inserti128_si256(_mm256_castsi128_si256(ho1d),_mm_load_si128((__m128i*)(current+i1+48)),1);
  } else {
    o1  = _mm256_inserti128_si256(_mm256_castsi128_si256(ho1 ),_mm_setzero_si128(),1);
    o1b = _mm256_inserti128_si256(_mm256_castsi128_si256(ho1b),_mm_setzero_si128(),1);
    o1c = _mm256_inserti128_si256(_mm256_castsi128_si256(ho1c),_mm_setzero_si128(),1);
    o1d = _mm256_inserti128_si256(_mm256_castsi128_si256(ho1d),_mm_setzero_si128(),1);
  }
  /* load mirrored. unaligned load are *very* convenient :-) */
  /* first */
  ho2  = _mm_loadu_si128((__m128i*)(current+full_size-(i+16)));
  ho2b = _mm_loadu_si128((__m128i*)(current+full_size-(i+32)));
  ho2c = _mm_loadu_si128((__m128i*)(current+full_size-(i+48)));
  ho2d = _mm_loadu_si128((__m128i*)(current+full_size-(i+64)));
  /* second */
  if (doup) {
    o2  = _mm256_inserti128_si256(_mm256_castsi128_si256(ho2 ),_mm_loadu_si128((__m128i*)(current+full_size-(i1+16))),1);
    o2b = _mm256_inserti128_si256(_mm256_castsi128_si256(ho2b),_mm_loadu_si128((__m128i*)(current+full_size-(i1+32))),1);
    o2c = _mm256_inserti128_si256(_mm256_castsi128_si256(ho2c),_mm_loadu_si128((__m128i*)(current+full_size-(i1+48))),1);
    o2d = _mm256_inserti128_si256(_mm256_castsi128_si256(ho2d),_mm_loadu_si128((__m128i*)(current+full_size-(i1+64))),1);
  } else {
    o2  = _mm256_inserti128_si256(_mm256_castsi128_si256(ho2 ),_mm_setzero_si128(),1);
    o2b = _mm256_inserti128_si256(_mm256_castsi128_si256(ho2b),_mm_setzero_si128(),1);
    o2c = _mm256_inserti128_si256(_mm256_castsi128_si256(ho2c),_mm_setzero_si128(),1);
    o2d = _mm256_inserti128_si256(_mm256_castsi128_si256(ho2d),_mm_setzero_si128(),1);
  }

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
  _mm_prefetch(current+i1+PREFETCH_DISTANCE, PREFETCH_LOAD_TYPE);
#if CACHE_LINE_SIZE == 32
  _mm_prefetch(current+i+PREFETCH_DISTANCE+32, PREFETCH_LOAD_TYPE);
  _mm_prefetch(current+i1+PREFETCH_DISTANCE+32, PREFETCH_LOAD_TYPE);
#endif
  _mm_prefetch(current+full_size-(i+PREFETCH_DISTANCE+64), PREFETCH_LOAD_TYPE);
  _mm_prefetch(current+full_size-(i1+PREFETCH_DISTANCE+64), PREFETCH_LOAD_TYPE);
#if CACHE_LINE_SIZE == 32
  _mm_prefetch(current+full_size-(i+PREFETCH_DISTANCE+32), PREFETCH_LOAD_TYPE);
  _mm_prefetch(current+full_size-(i1+PREFETCH_DISTANCE+32), PREFETCH_LOAD_TYPE);
#endif
#endif

  /* mirror the 16-bytes we have loaded */
  o2  = _mm256_shuffle_epi8(o2 ,pe);
  o2b = _mm256_shuffle_epi8(o2b,pe);
  o2c = _mm256_shuffle_epi8(o2c,pe);
  o2d = _mm256_shuffle_epi8(o2d,pe);

  /* least significant carry is from the previous iteration,
     all the other are zero */
  pc = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_cvtsi32_si128((int)carry[0])), _mm_cvtsi32_si128((int)carry[1]), 1);
  pcb = pz;
  pcc = pz;
  pcd = pz;
  /* carry was consumed */
  carry[0] = 0;
  if (doup)
    carry[1] = 0;

  /* ** step 1 ** */
  /* add the magic number */
  o1  = _mm256_add_epi8(o1 ,p246);
  o1b = _mm256_add_epi8(o1b,p246);
  o1c = _mm256_add_epi8(o1c,p246);
  o1d = _mm256_add_epi8(o1d,p246);
  /* do the 64 bits adds ; 2 carries in each 128 bits registers
     (one in each 64 bits halves) */
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
#define _mm256_cmplt_epi8(a,b) _mm256_cmpgt_epi8(b,a)
  mask  = _mm256_cmplt_epi8(r , pz);
  maskb = _mm256_cmplt_epi8(rb, pz);
  maskc = _mm256_cmplt_epi8(rc, pz);
  maskd = _mm256_cmplt_epi8(rd, pz);
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
  /* carry propagation ;
     this one recover the carry from one register to the next
     or to the next iteration */
  c1to2 = _mm256_srli_si256(pc , 15);
  c2to3 = _mm256_srli_si256(pcb, 15);
  c3to4 = _mm256_srli_si256(pcc, 15);
  carry[0] += _mm_extract_epi8(_mm256_extracti128_si256(pcd,0), 15);
  carry[1] += _mm_extract_epi8(_mm256_extracti128_si256(pcd,1), 15);
  /* this one recover the carry from one half to the next */
  pc  = _mm256_slli_si256(pc , 1);
  pcb = _mm256_slli_si256(pcb, 1);
  pcc = _mm256_slli_si256(pcc, 1);
  pcd = _mm256_slli_si256(pcd, 1);
  /* carry fusion from both halves */
  pcb = _mm256_add_epi8(pcb, c1to2);
  pcc = _mm256_add_epi8(pcc, c2to3);
  pcd = _mm256_add_epi8(pcd, c3to4);

  /* ** step 2 ** */
  /* we have to redo everything but the actual addition for carry propagation
     inside the 4 128 bits registers.
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

    mask  = _mm256_cmplt_epi8(r , pz);
    maskb = _mm256_cmplt_epi8(rb, pz);
    maskc = _mm256_cmplt_epi8(rc, pz);
    maskd = _mm256_cmplt_epi8(rd, pz);
  
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
    c2to3 = _mm256_srli_si256(pcb, 15);
    c3to4 = _mm256_srli_si256(pcc, 15);
    carry[0] += _mm_extract_epi8(_mm256_extracti128_si256(pcd,0), 15);
    carry[1] += _mm_extract_epi8(_mm256_extracti128_si256(pcd,1), 15);
      
    pc  = _mm256_slli_si256(pc , 1);
    pcb = _mm256_slli_si256(pcb, 1);
    pcc = _mm256_slli_si256(pcc, 1);
    pcd = _mm256_slli_si256(pcd, 1);

    pcb = _mm256_add_epi8(pcb, c1to2);
    pcc = _mm256_add_epi8(pcc, c2to3);
    pcd = _mm256_add_epi8(pcd, c3to4);

    cc = _mm256_testz_si256(pc,pc);
    cc = cc && _mm256_testz_si256(pcb,pcb);
    cc = cc && _mm256_testz_si256(pcc,pcc);
    cc = cc && _mm256_testz_si256(pcd,pcd);
  } while (!cc);
  /* store the results where needed */
  _mm_stream_si128((__m128i*)(next+i+ 0), _mm256_extracti128_si256(r ,0));
  _mm_stream_si128((__m128i*)(next+i+16), _mm256_extracti128_si256(rb,0));
  _mm_stream_si128((__m128i*)(next+i+32), _mm256_extracti128_si256(rc,0));
  _mm_stream_si128((__m128i*)(next+i+48), _mm256_extracti128_si256(rd,0));
  if (doup) {
    _mm_stream_si128((__m128i*)(next+i1+ 0), _mm256_extracti128_si256(r ,1));
    _mm_stream_si128((__m128i*)(next+i1+16), _mm256_extracti128_si256(rb,1));
    _mm_stream_si128((__m128i*)(next+i1+32), _mm256_extracti128_si256(rc,1));
    _mm_stream_si128((__m128i*)(next+i1+48), _mm256_extracti128_si256(rd,1));
    i1 += 64;
  }
 }
