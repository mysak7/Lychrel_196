#include "mpi.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <malloc.h>

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    *memptr = _aligned_malloc(size, alignment);
    if (*memptr == NULL) {
        return 12; // ENOMEM
    }
    return 0;
}

int MPI_Init(int *argc, char ***argv) {
    (void)argc; (void)argv;
    return MPI_SUCCESS;
}

int MPI_Comm_size(MPI_Comm comm, int *size) {
    (void)comm;
    *size = 1;
    return MPI_SUCCESS;
}

int MPI_Comm_rank(MPI_Comm comm, int *rank) {
    (void)comm;
    *rank = 0;
    return MPI_SUCCESS;
}

int MPI_Finalize(void) {
    return MPI_SUCCESS;
}

int MPI_Isend(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *request) {
    (void)buf; (void)count; (void)datatype; (void)dest; (void)tag; (void)comm; (void)request;
    return MPI_SUCCESS;
}

int MPI_Irecv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Request *request) {
    (void)buf; (void)count; (void)datatype; (void)source; (void)tag; (void)comm; (void)request;
    return MPI_SUCCESS;
}

int MPI_Waitall(int count, MPI_Request *array_of_requests, MPI_Status *array_of_statuses) {
    (void)count; (void)array_of_requests; (void)array_of_statuses;
    return MPI_SUCCESS;
}

int MPI_Allreduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    (void)op; (void)comm;
    size_t size = 0;
    if (datatype == MPI_CHAR) size = 1;
    else if (datatype == MPI_INT) size = sizeof(int);
    else if (datatype == MPI_LONG) size = sizeof(long);
    
    if (sendbuf != recvbuf) {
         memcpy(recvbuf, sendbuf, count * size);
    }
    return MPI_SUCCESS;
}

int MPI_Bcast(void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm) {
    (void)buffer; (void)count; (void)datatype; (void)root; (void)comm;
    return MPI_SUCCESS;
}

int MPI_Get_processor_name(char *name, int *resultlen) {
    strcpy(name, "LocalSingleNode");
    *resultlen = strlen(name);
    return MPI_SUCCESS;
}

double MPI_Wtime(void) {
    return (double)GetTickCount64() / 1000.0;
}

int MPI_Abort(MPI_Comm comm, int errorcode) {
    (void)comm;
    fprintf(stderr, "MPI_Abort called with error %d\n", errorcode);
    exit(errorcode);
    return MPI_SUCCESS;
}

int MPI_Alltoallv(const void *sendbuf, const int *sendcnts, const int *sdispls, MPI_Datatype sendtype, void *recvbuf, const int *recvcnts, const int *rdispls, MPI_Datatype recvtype, MPI_Comm comm) {
    (void)sendtype; (void)recvtype; (void)comm; (void)recvcnts;
    // For size=1, just copy if buffers are different.
    if (sendbuf != recvbuf) {
        memcpy((char*)recvbuf + rdispls[0], (char*)sendbuf + sdispls[0], sendcnts[0]);
    }
    return MPI_SUCCESS;
}

int MPI_Gatherv(const void *sendbuf, int sendcnt, MPI_Datatype sendtype, void *recvbuf, const int *recvcnts, const int *displs, MPI_Datatype recvtype, int root, MPI_Comm comm) {
     (void)sendtype; (void)recvcnts; (void)recvtype; (void)root; (void)comm;
     if (sendbuf != recvbuf) {
          memcpy((char*)recvbuf + displs[0], sendbuf, sendcnt);
     }
     return MPI_SUCCESS;
}
