#ifndef PROT_H
#define PROT_H

#include <inttypes.h>
#include <stddef.h>
#include <sys/types.h>
#include <stdbool.h>



#define MAX_DATA_SIZE 63979


ssize_t	readn(int fd, void *vptr, size_t n);
ssize_t	writen(int fd, const void *vptr, size_t n);
uint64_t rand_uint64(void);



typedef struct __attribute__((__packed__)) {
    uint8_t pack_id;
    uint64_t session_id;
    uint8_t prot_id;
    uint64_t len;
} CONN;

typedef struct __attribute__((__packed__)) {
    uint8_t pack_id;
    uint64_t session_id;
} CONACC;

typedef struct __attribute__((__packed__)) {
    uint8_t pack_id;
    uint64_t session_id;
} CONRJT;

typedef struct __attribute__((__packed__)) {
    uint8_t pack_id;
    uint64_t session_id;
    uint64_t pack_nr;
    uint32_t len;
    char data[MAX_DATA_SIZE];
} DATA;

typedef struct __attribute__((__packed__)) {
    uint8_t pack_id;
    uint64_t session_id;
    uint64_t pack_nr;
} ACC;

typedef struct __attribute__((__packed__)) {
    uint8_t pack_id;
    uint64_t session_id;
    uint64_t pack_nr;
} RJT;

typedef struct __attribute__((__packed__)) {
    uint8_t pack_id;
    uint64_t session_id;
} RCVD;


void reset_params(uint64_t * cur_session,uint8_t *cur_prot,uint64_t *next_nr, uint64_t *msg_i, bool *has_client, int *last_send, int *ret_count, int socket_fd, struct timeval nolimit);

#endif // PROT_H
