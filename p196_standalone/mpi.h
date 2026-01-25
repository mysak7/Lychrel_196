#ifndef MPI_STUB_H
#define MPI_STUB_H

#define MPI_COMM_WORLD 0
#define MPI_CHAR 1
#define MPI_INT 2
#define MPI_LONG 3
#define MPI_LOR 4
#define MPI_SUCCESS 0
#define MPI_MAX_PROCESSOR_NAME 256
#define MPI_STATUS_IGNORE 0

#if defined(_MSC_VER)
typedef __int64 ssize_t;
#endif

typedef int MPI_Comm;
typedef int MPI_Datatype;
typedef int MPI_Op;
typedef int MPI_Request;

typedef struct {
    int MPI_SOURCE;
    int MPI_TAG;
    int MPI_ERROR;
} MPI_Status;

#ifdef __cplusplus
extern "C" {
#endif

int MPI_Init(int *argc, char ***argv);
int MPI_Comm_size(MPI_Comm comm, int *size);
int MPI_Comm_rank(MPI_Comm comm, int *rank);
int MPI_Finalize(void);
int MPI_Isend(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *request);
int MPI_Irecv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Request *request);
int MPI_Waitall(int count, MPI_Request *array_of_requests, MPI_Status *array_of_statuses);
int MPI_Allreduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm);
int MPI_Bcast(void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm);
int MPI_Get_processor_name(char *name, int *resultlen);
double MPI_Wtime(void);
int MPI_Abort(MPI_Comm comm, int errorcode);
int MPI_Alltoallv(const void *sendbuf, const int *sendcnts, const int *sdispls, MPI_Datatype sendtype, void *recvbuf, const int *recvcnts, const int *rdispls, MPI_Datatype recvtype, MPI_Comm comm);
int MPI_Gatherv(const void *sendbuf, int sendcnt, MPI_Datatype sendtype, void *recvbuf, const int *recvcnts, const int *displs, MPI_Datatype recvtype, int root, MPI_Comm comm);

#ifdef __cplusplus
}
#endif

#endif
