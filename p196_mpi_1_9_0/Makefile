SRCS=p196_mpi.c isf.c mydump.c
HDRS=isf.h  mydump.h
SRCSCB=p196_mpi.14BETA_CACHEBLOCKING.c isf.c mydump.c

MPIHOME=/soft/local/openmpi-1.5.4-icc12.0
MPIHOME=/usr/local/openmpi-1.6.5-icc14.0.1
MPIGCCHOME=/soft/local/openmpi-1.5.4-gcc
MPCHOME=/soft/local/MPC_2.3.1
MPIHOME=/opt/intel/impi/5.0.3.048/intel64

MPICC=$(MPIHOME)/bin/mpicc
MPIGCC=$(MPIGCCHOME)/bin/mpicc
MPC_CC=$(MPCHOME)/bin/mpc_cc
MPICC=mpiicc
#MPICFLAGS=-DPREFETCH_DISTANCE=256 -DSTREAMING_STORES -O3 -ftz -openmp -msse4.2
MPICFLAGS=-static-intel -DPREFETCH_DISTANCE=256 -DSTREAMING_STORES -DPREFETCH_LOAD_TYPE=_MM_HINT_T0 -O3 -ftz -openmp
MPIGCFLAGS=-DPREFETCH_DISTANCE=256 -DSTREAMING_STORES -DPREFETCH_LOAD_TYPE=_MM_HINT_T0 -O3 -fopenmp

BINARIES_ICC=p196_mpi_SSE3_icc p196_mpi_SSSE3_icc p196_mpi_SSE4_icc p196_mpi_SSE4ASAVX_icc p196_mpi_AVX2_icc
BINARIES_GCC=p196_mpi_SSE3_gcc p196_mpi_SSSE3_gcc p196_mpi_SSE4_gcc p196_mpi_SSE4ASAVX_gcc p196_mpi_AVX2_gcc

all: $(BINARIES_ICC) $(BINARIES_GCC)


icc: $(BINARIES_ICC)


gcc: $(BINARIES_GCC)

p196_mpi_TWINAVX2_icc: $(SRCS) $(HDRS) p196_mpi_twin_avx2.c
	$(MPICC) $(MPICFLAGS) -DTWIN -DAVX2 $(SRCS) -march=core-avx2 -o $@

p196_mpi_AVX2_icc: $(SRCS) $(HDRS) p196_mpi_avx2.c
	$(MPICC) $(MPICFLAGS) -DAVX2 -DALIGN_AVX2 $(SRCS) -march=core-avx2 -o $@

p196_mpi_SSE4ASAVX_icc: $(SRCS) $(HDRS)
	$(MPICC) $(MPICFLAGS) -DSSE4 $(SRCS) -mavx -o $@

p196_mpi_SSE4_icc: $(SRCS) $(HDRS)
	$(MPICC) $(MPICFLAGS) -DSSE4 $(SRCS) -msse3 -mssse3 -msse4.1 -o $@

p196_mpi_SSSE3_icc: $(SRCS) $(HDRS)
	$(MPICC) $(MPICFLAGS) -DSSSE3 $(SRCS) -msse3 -mssse3 -o $@

p196_mpi_SSE3_icc: $(SRCS) $(HDRS)
	$(MPICC) $(MPICFLAGS) -DSSE3 $(SRCS) -msse3 -o $@


p196_mpi_TWINAVX2_gcc: $(SRCS) $(HDRS) p196_mpi_twin_avx2.c
	$(MPICC) $(MPIGCFLAGS) -DTWIN -DAVX2 $(SRCS) -mavx2 -o $@

p196_mpi_AVX2_gcc: $(SRCS) $(HDRS) p196_mpi_avx2.c
	$(MPICC) $(MPIGCFLAGS) -DAVX2 $(SRCS) -mavx2 -o $@

p196_mpi_SSE4ASAVX_gcc: $(SRCS) $(HDRS)
	$(MPIGCC) $(MPIGCFLAGS) -DSSE4 $(SRCS) -mavx -o $@

p196_mpi_SSE4_gcc: $(SRCS) $(HDRS)
	$(MPIGCC) $(MPIGCFLAGS) -DSSE4 $(SRCS) -msse3 -mssse3 -msse4.1 -o $@

p196_mpi_SSSE3_gcc: $(SRCS) $(HDRS)
	$(MPIGCC) $(MPIGCFLAGS) -DSSSE3 $(SRCS) -msse3 -mssse3 -o $@

p196_mpi_SSE3_gcc: $(SRCS) $(HDRS)
	$(MPIGCC) $(MPIGCFLAGS) -DSSE3 $(SRCS) -msse3 -o $@


p196_mpi_ARMNEON_gcc: $(SRCS) $(HDRS)
	$(MPIGCC) $(MPIGCFLAGS) -DARMNEON $(SRCS) -mfloat-abi=softfp -mfpu=neon -mcpu=cortex-a9 -o $@

p196_mpi_ARMHFNEON_gcc: $(SRCS) $(HDRS)
	$(MPIGCC) $(MPIGCFLAGS) -DARMNEON $(SRCS) -mfloat-abi=hard -mfpu=neon -mcpu=cortex-a15 -o $@

p196_mpi_SSE4_mpc: $(SRCS) $(HDRS)
	$(MPC_CC) $(MPIGCFLAGS) -fmpc-privatize -DSSE4 $(SRCS) -msse3 -mssse3 -msse4.1 -o $@


clean:
	rm -f $(BINARIES_ICC) $(BINARIES_GCC) p196_mpi.bbl p196_mpi.aux p196_mpi.log p196_mpi.out p196_mpi.blg *~


algo_dot.pdf: algo.dot
	dot -Tpdf -o$@ $<

p196_mpi.bbl: p196_mpi.tex p196_mpi.bib p196_mpi.aux
	bibtex p196_mpi

p196_mpi.aux: p196_mpi.tex
	pdflatex $< && pdflatex $<

p196_mpi.pdf: p196_mpi.tex p196_mpi.bbl algo_dot.pdf # $(FIGS)
	pdflatex $< && pdflatex $<

see: p196_mpi.pdf
	xpdf $<
