The program p196_mpi is intended to be used in the pursuit of Lychrel numbers.
See <http://www.p196.org/> for more details. In particular, a lot of terms
are explained in [1].

This isn't a particularly clean code, and there is a lot of way it could
be improved.

This code is not meant to start the search from a seed number, you need
a pre-existing dump as a starting point, either in "Istvan-Standard Formatting"
[2] or as a 'raw dump'. Under Linux, you can try the code in [3] to generate
dump files from a seed.

This code requires a working MPI library, and some knowledge on how to use it.
Good starting points for learning about MPI are [4] and [5]. Rational of the
code is explained inside the code.

The MPI library & support binaries should be built to use a good compiler for
the platform. The code was tested with Intel ICC on x86_64 and IBM XLC on
POWER7.

For instance, on a Linux/x86_64 system a good command-line is:

$ mpicc -DPREFETCH_DISTANCE=256 -DSSE4 -DSTREAMING_STORES -O3 -ftz -msse4.1 p196_mpi.c isf.c mydump.c -o p196_mpi

If your processor doesn't support 'SSE4', you can also use 'SSSE3' or even
just 'SSE3' (see at the end of this document for various flags that can be
used). Try them all to see which gives the best speed, as supporting new
instructions doesn't mean they are fast. If you use GCC rather than ICC, try:

$ mpicc -DPREFETCH_DISTANCE=256 -DSSE4 -DSTREAMING_STORES -O3 -mssse3 -msse4 p196_mpi.c isf.c mydump.c -o p196_mpi

While on a Linux/POWER7 system a good command-line is:

$ mpicc -DPWR7 -O3 -qsimd -qaltivec -qarch=pwr7 -qtune=pwr7  p196_mpi.c isf.c mydump.c -o p196_mpi

or for GCC 4.4 user (might be faster...):

$ mpicc -DPWR7 -O3 -maltivec -mabi=altivec p196_mpi.c isf.c mydump.c -o p196_mpi

Then you need to run the code on your machine / cluster. This is very much
system-dependant. In particular, the cluster may be using a batch system or
a resource manager, constraining how to start the code.

On a Linux/x86_64 system using the SLURM resource manager, this:

$ srun --cpu_bind=core --mem_bind=local -N 10 -n 80 time ../p196_mpi -i dump.196.439682132 -d 0 -m 0 -M 0 -D 1000000

Will start 80 processes on 10 nodes (8 processes per node), each bound to a core
and using the local processor's memory only (in this example each node is a
dual-socket Nehalem NUMA system). The start file is from iteration 439682132 for
seed 196, and a dump file will be generated every time the number of digits is a
multiple of 1 million.

On a large SMP POWER7:

$ mpirun -n 64 ./p196_mpi -i dump.196.439682132 -d 0 -m 0 -M 0 -D 1000000

Will launch 64 copies on the local machine, to solve the same problem as above.

Remarks:
* When running on a cluster, the code might be very sensitive to network
  latency. The preprocessor symbol "TIMER_COMMS" when defined will cause
  the display of informations on how much time in spent in communications.
* For the code to be efficient, at least several tens of thousands of digits
  should be computed by each process, it's probably better to go to hundreds
  of thousands. The code is only scalable to a point. Again, monitoring
  communications helps.
* Every second the code display a statistics line such as:
0: 140557.834278: 632321405 ; 45264994 / 297 (322.038215 / 296.075062 iter/s) (8.127223e+10 / 7.749349e+10 d/s) (261736026)
  Where '0:' is the process number (only the first process should display
  information). The second number (140557.834278) is the number of seconds
  since the beginning of the run, the third (632321405) the last iteration
  done. The fourth is the number of iteration since start, the fifth since
  the last display. The next two are the average speeds in iterations per
  second since startup and the last display. The next two are the average
  speed in digits per seconds since startup and the last display. Finally
  the last number is the current number of digits, here 261736026.

The code was only ever thoroughly tested under GNU/Linux. Windows support
is experimental and is still missing some features such as reading its own
dump file format and generating ISF file.

The computation kernel itself might be subject to improvements. Again, 
patches & comments are welcome.

Romain Dolbeau, <romain@dolbeau.org>

[1] <http://www.p196.org/definitions.html>
[2] <http://www.p196.org/verification.html>
[3] <http://www.p196.org/ben-mirror/vp_sv.c>
[4] <http://en.wikipedia.org/wiki/Message_Passing_Interface>
[5] <http://www.open-mpi.org/>

##### SSE support
Three main variants of SSE support, SSE4, SSSE3 and SSE3. It may be worthwhile
to try SSSE3 or SSE3 even on SSE4-supporting processor to check speed. In
theory, all of them should be fast enough to saturate the memory bandwidth
of a modern multi-core processor.

If your system supports AVX, then you should compile with "-mavx", to take
advantage of the VEX.128 instructions (three operands instead of two).

Other flags of interest:
* PREFETCH_DISTANCE: distance in bytes to prefetch at each iteration of the
  loop. Default to 256, a good value in practice. Set to 0 to disable
  prefetching completely. Should usually be enabled. Should be a set to a
  multiple of 64.

* PREFETCH_LOAD_TYPE: type of prefetch to use. Should usually be set to
  _MM_HINT_T0, the default (relevant only if PREFETCH_DISTANCE > 0).

* CACHE_LINE_SIZE: size of the cache line. Default to 64, the proper value
  for most modern x86_64 processors. Don't touch it. (relevant only if
  PREFETCH_DISTANCE > 0).

* STREAMING_STORES: toggle to use (or not) non-temporal stores. This should
  almost always be defined.

* USE_LDDQU: toggle to replace MOVDQU by LDDQU, which according to Intel's
  documentation is pretty much the same instruction. Try and see if it improves
  or degrades performance for you.
