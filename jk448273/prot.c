#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "prot.h"

ssize_t readn(int fd, void *vptr, size_t n) {
    ssize_t nleft, nread;
    char *ptr;

    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) < 0)
            return nread;
        else if (nread == 0)
            break;

        nleft -= nread;
        ptr += nread;
    }
    return n - nleft; 
}

ssize_t writen(int fd, const void *vptr, size_t n){
    ssize_t nleft, nwritten;
    const char *ptr;

    ptr = vptr;          
    nleft = n;
    while (nleft > 0) {
        if ((nwritten = write(fd, ptr, nleft)) <= 0)
            return nwritten; 

        nleft -= nwritten;
        ptr += nwritten;
    }
    return n;
}
uint64_t rand_uint64(void) {
  uint64_t r = 0;
  for (int i=0; i<64; i++) {
    r = r*2 + rand()%2;
  }
  return r;
}

void reset_params(uint64_t * cur_session,uint8_t *cur_prot,uint64_t *next_nr, uint64_t *msg_i, bool *has_client, int *last_send, int *ret_count, int socket_fd, struct timeval nolimit){
    *cur_session=-1;
    *cur_prot=-1;
    *next_nr = 1;
    *msg_i=0;
    *has_client=false;
    *last_send=0;
    *ret_count=0;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &nolimit, sizeof nolimit);
}
