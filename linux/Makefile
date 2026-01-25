CC=mpicc
GCC=gcc
CFLAGS=-O3 -mavx2 -DALIGN_AVX2 -DPREFETCH_DISTANCE=256 -DSTREAMING_STORES -DPREFETCH_LOAD_TYPE=_MM_HINT_T0 -DAVX2 -D_GNU_SOURCE

# For MPI build, include only MPI source dir to avoid picking up stub mpi.h
INCLUDES_MPI=-I../p196_mpi_1_9_0

# For Seed build, include standalone dir
INCLUDES_SEED=-I../p196_standalone

SRC_MPI=../p196_mpi_1_9_0/p196_mpi.c ../p196_mpi_1_9_0/isf.c ../p196_mpi_1_9_0/mydump.c
TARGET_MPI=p196_mpi

SRC_SEED=../p196_standalone/make_seed.c ../p196_standalone/isf.c
TARGET_SEED=make_seed

all: $(TARGET_MPI) $(TARGET_SEED)

$(TARGET_MPI):
	$(CC) $(CFLAGS) $(INCLUDES_MPI) -o $(TARGET_MPI) $(SRC_MPI)

$(TARGET_SEED):
	$(GCC) -O3 $(INCLUDES_SEED) -o $(TARGET_SEED) $(SRC_SEED)

clean:
	rm -f $(TARGET_MPI) $(TARGET_SEED)
