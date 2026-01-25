    for ( ; i < last-32 ; i+= 32) {
      const uint8x8_t pe =      vcreate_u8(0x0001020304050607llu);
      const uint8x8_t p246 =    vcreate_u8(0x00F6F6F6F6F6F6F6llu);
      const uint8x8_t p9 =      vcreate_u8(0x0964646464646464llu);
      const uint8x8_t p10 =     vcreate_u8(0x0a00000000000000llu); //base?
      uint8x8_t r, rb, rc, rd;
      uint8x8_t mask, temp, maskb, tempb, maskc, tempc, maskd, tempd;
      uint8x8_t o1  = vld1_u8((const uint8_t *)(current + i     ));
      uint8x8_t o2  = vld1_u8((const uint8_t *)(current+full_size-(i+8 )));
      uint8x8_t o1b = vld1_u8((const uint8_t *)(current + i + 8 ));
      uint8x8_t o2b = vld1_u8((const uint8_t *)(current+full_size-(i+16)));
      uint8x8_t o1c = vld1_u8((const uint8_t *)(current + i + 16));
      uint8x8_t o2c = vld1_u8((const uint8_t *)(current+full_size-(i+24)));
      uint8x8_t o1d = vld1_u8((const uint8_t *)(current + i + 24));
      uint8x8_t o2d = vld1_u8((const uint8_t *)(current+full_size-(i+32)));
      uint8x8_t o2p, o2pb, o2pc, o2pd;
      uint8x8_t pc, pcb, pcc, pcd;
      uint8x8_t tmp1;
#ifndef PREFETCH_DISTANCE
#define PREFETCH_DISTANCE 256
#endif
      char* padr = current + i + PREFETCH_DISTANCE;
      char* padr2 = current + full_size - (i+PREFETCH_DISTANCE);

      __builtin_prefetch(padr);
      __builtin_prefetch(padr2);

      o2p =  vtbl1_u8(o2,  pe);
      o2pb = vtbl1_u8(o2b, pe);
      o2pc = vtbl1_u8(o2c, pe);
      o2pd = vtbl1_u8(o2d, pe);

      pc = vset_lane_u8(carry, vcreate_u8(0), 0);

      r  = vreinterpret_u8_u64(vadd_u64(vreinterpret_u64_u8(o1 ), vreinterpret_u64_u8(p246)));
      rb = vreinterpret_u8_u64(vadd_u64(vreinterpret_u64_u8(o1b), vreinterpret_u64_u8(p246)));
      rc = vreinterpret_u8_u64(vadd_u64(vreinterpret_u64_u8(o1c), vreinterpret_u64_u8(p246)));
      rd = vreinterpret_u8_u64(vadd_u64(vreinterpret_u64_u8(o1d), vreinterpret_u64_u8(p246)));

      r  = vreinterpret_u8_u64(vadd_u64(vreinterpret_u64_u8(o2p ), vreinterpret_u64_u8(r )));
      rb = vreinterpret_u8_u64(vadd_u64(vreinterpret_u64_u8(o2pb), vreinterpret_u64_u8(rb)));
      rc = vreinterpret_u8_u64(vadd_u64(vreinterpret_u64_u8(o2pc), vreinterpret_u64_u8(rc)));
      rd = vreinterpret_u8_u64(vadd_u64(vreinterpret_u64_u8(o2pd), vreinterpret_u64_u8(rd)));

      r  = vreinterpret_u8_u64(vadd_u64(vreinterpret_u64_u8(pc), vreinterpret_u64_u8(r)));

      /* */
      mask = vcge_u8(r, p246);
      r = vsub_u8(r, vand_u8(mask, p246));
      mask = vcgt_u8(r, p9);
      pc = vsub_u8(vcreate_u8(0), mask); /* fixme: vneg? */
      temp = vand_u8(mask, p10); // base?
      r = vsub_u8(r, temp);

      /* need to propagate pc[,b,c] */
      pcb = vreinterpret_u8_u64(vshr_n_u64(vreinterpret_u64_u8(pc), 56));
      rb = vreinterpret_u8_u64(vadd_u64(vreinterpret_u64_u8(pcb), vreinterpret_u64_u8(rb)));

      maskb = vcge_u8(rb, p246);
      rb = vsub_u8(rb, vand_u8(maskb, p246));
      maskb = vcgt_u8(rb, p9);
      pcb = vsub_u8(vcreate_u8(0), maskb);
      tempb = vand_u8(maskb, p10); // base?
      rb = vsub_u8(rb, tempb);

      /* need to propagate pc[,b,c] */
      pcc = vreinterpret_u8_u64(vshr_n_u64(vreinterpret_u64_u8(pcb), 56));
      rc = vreinterpret_u8_u64(vadd_u64(vreinterpret_u64_u8(pcc), vreinterpret_u64_u8(rc)));

      maskc = vcge_u8(rc, p246);
      rc = vsub_u8(rc, vand_u8(maskc, p246));
      maskc = vcgt_u8(rc, p9);
      pcc = vsub_u8(vcreate_u8(0), maskc);
      tempc = vand_u8(maskc, p10); // base?
      rc = vsub_u8(rc, tempc);

      /* need to propagate pc[,b,c] */
      pcd = vreinterpret_u8_u64(vshr_n_u64(vreinterpret_u64_u8(pcc), 56));
      rd = vreinterpret_u8_u64(vadd_u64(vreinterpret_u64_u8(pcd), vreinterpret_u64_u8(rd)));

      maskd = vcge_u8(rd, p246);
      rd = vsub_u8(rd, vand_u8(maskd, p246));
      maskd = vcgt_u8(rd, p9);
      pcd = vsub_u8(vcreate_u8(0), maskd);
      tempd = vand_u8(maskd, p10); // base?
      rd = vsub_u8(rd, tempd);

      carry = vget_lane_u8(pcd, 7);

      vst1_u8(next+i+ 0 , r );
      vst1_u8(next+i+ 8 , rb);
      vst1_u8(next+i+ 16, rc);
      vst1_u8(next+i+ 24, rd);
    }
