  const vector unsigned char p2 = vec_lvsl(LVSL_CAST_INT full_size, LVSL_CAST_PTR current);
  const vector unsigned char pe = (vector unsigned char){15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0}; /* mirror shuffling */
  const vector unsigned char p2e = vec_perm(p2, p2, pe);
  const vector signed char pbm1 = (vector signed char){9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9};
  const vector signed char pb = (vector signed char){10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10};
  const vector signed char pz = (vector signed char){0}; /* vector zero */
#ifdef USE_SHIFTS
  /* shift by bytes use bits 121-124, multiply by 8 the required value */
  const vector unsigned char pu1 = (vector unsigned char){8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8};
  const vector unsigned char pu15 = (vector unsigned char){120,120,120,120,120,120,120,120,120,120,120,120,120,120,120,120};
#else
  const vector unsigned char pslo15 = (vector unsigned char){15,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16};
  const vector unsigned char psro1 = (vector unsigned char){16,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14};
#endif
  vector signed char o2t = vec_ld(full_size - first, (signed char*)current); /* second input preload */
  for ( ; i < last - 63 ; i+=64) {
    vector signed char o1, o2, o1b, o2b;
    vector signed char o1c, o2c, o1d, o2d;
    vector signed char o1x, o1xb, o1xc,o1xd;
    vector signed char o2x, o2xb, o2xc,o2xd;
    vector signed char pc, pcb;
    vector signed char pcc, pcd;
    vector signed char r, rb;
    vector signed char rc, rd;
    vector bool char mask, maskb;
    vector bool char maskc, maskd;
    vector signed char temp, tempb;
    vector signed char tempc, tempd;
    vector signed char c1to2;
    vector signed char c2to3;
    vector signed char c3to4;
    int cc;
    int i2;

    o1  = vec_ld(i   , (signed char*)current); /* first input */
    o1b = vec_ld(i+16, (signed char*)current);
    o1c = vec_ld(i+32, (signed char*)current);
    o1d = vec_ld(i+48, (signed char*)current);

    o2  = vec_ld(full_size - (i+16), (signed char*)current); /* second input */
    o2b = vec_ld(full_size - (i+32), (signed char*)current);
    o2c = vec_ld(full_size - (i+48), (signed char*)current);
    o2d = vec_ld(full_size - (i+64), (signed char*)current);

    o2x  = o2t;
    o2xb = o2 ;
    o2xc = o2b;
    o2xd = o2c;
    o2t  = o2d;

    o2  = vec_perm(o2 , o2x , p2e);
    o2b = vec_perm(o2b, o2xb, p2e);
    o2c = vec_perm(o2c, o2xc, p2e);
    o2d = vec_perm(o2d, o2xd, p2e);

    pc  = pz;
    pcb = pz;
    pcc = pz;
    pcd = pz;

    pc = vec_insert(carry, pc, 0);
    carry = 0;
    r  = vec_add(o1 , o2); /* do the addition w/o the carry */
    rb = vec_add(o1b, o2b);
    rc = vec_add(o1c, o2c);
    rd = vec_add(o1d, o2d);

    /*       cc = _mm_testz_si128(pc,pc); */
    do {
      /* first copy */
      r  = vec_add(r , pc); /* add carry */
      rb = vec_add(rb, pcb);
      rc = vec_add(rc, pcc);
      rd = vec_add(rd, pcd);

      mask  = vec_cmpgt(r , pbm1); /* create a mask by comparing with base-1 */
      maskb = vec_cmpgt(rb, pbm1);
      maskc = vec_cmpgt(rc, pbm1);
      maskd = vec_cmpgt(rd, pbm1);

      pc  = vec_sub(pz, (vector signed char)mask); /* turn mask into carry */
      pcb = vec_sub(pz, (vector signed char)maskb);
      pcc = vec_sub(pz, (vector signed char)maskc);
      pcd = vec_sub(pz, (vector signed char)maskd);

      temp  = vec_and(pb, mask); /* mask base so that we can remove it where it overflows */
      tempb = vec_and(pb, maskb);
      tempc = vec_and(pb, maskc);
      tempd = vec_and(pb, maskd);

      r  = vec_sub(r , temp); /* remove overflow */
      rb = vec_sub(rb, tempb);
      rc = vec_sub(rc, tempc);
      rd = vec_sub(rd, tempd);

#ifdef USE_SHIFTS
      c1to2 = vec_slo(pc , pu15); /* extract rightmost carry for propagation (not 4) */
      c2to3 = vec_slo(pcb, pu15);
      c3to4 = vec_slo(pcc, pu15);
#else
      c1to2 = vec_perm(pc , pz, pslo15);
      c2to3 = vec_perm(pcb, pz, pslo15);
      c3to4 = vec_perm(pcc, pz, pslo15);
#endif
      carry += vec_extract(pcd, 15); /* extract leftmost carry (4 only) */

#ifdef USE_SHIFTS
      pc  = vec_sro(pc , pu1); /* shift carry into proper position */
      pcb = vec_sro(pcb, pu1);
      pcc = vec_sro(pcc, pu1);
      pcd = vec_sro(pcd, pu1);
#else
      pc  = vec_perm(pc , pz, psro1);
      pcb = vec_perm(pcb, pz, psro1);
      pcc = vec_perm(pcc, pz, psro1);
      pcd = vec_perm(pcd, pz, psro1);
#endif

      pcb = vec_add(pcb, c1to2); /* add 1 to 2 carry (not 1); */
      pcc = vec_add(pcc, c2to3);
      pcd = vec_add(pcd, c3to4);

      cc = vec_all_eq(pc,pz); /* check whether there's at least one non-zero carry */
      cc = cc && vec_all_eq(pcb,pz);
      cc = cc && vec_all_eq(pcc,pz);
      cc = cc && vec_all_eq(pcd,pz);
    } while (!cc);
    vec_stl(r , i, (signed char*)next);
    vec_stl(rb, i+16, (signed char*)next);
    vec_stl(rc, i+32, (signed char*)next);
    vec_stl(rd, i+48, (signed char*)next);
  }
