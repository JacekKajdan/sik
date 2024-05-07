#include <endian.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>

#include "prot.h"
#include "err.h"
#include "common.h"
#include "protconst.h"

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr,"ERROR: Wrong number of arguments\n");
        exit(1);
    }

    char const *host = argv[2];
    uint16_t port = read_port(argv[3]);
    struct sockaddr_in server_address = get_server_address(host, port);


    //Wczytywanie standardowego wejscia
    char *input = (char *)malloc(1024 * sizeof(char));
    int index = 0;
    char c;
    while ((c = getchar()) != EOF) {
        input[index++] = c;
        if (index % 1024 == 0) {
            input = (char *)realloc(input, (index + 1024) * sizeof(char));
        }
    }
    input[index] = '\0';

    
    //TCP
    if(strcmp(argv[1],"tcp")==0){

        ssize_t read_length;
        ssize_t write_length;


        int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0) {
            fprintf(stderr,"ERROR: socket\n");
            exit(1);
        }


        if (connect(socket_fd, (struct sockaddr *) &server_address,
                    (socklen_t) sizeof(server_address)) < 0) {
            fprintf(stderr,"ERROR: connect\n");
            exit(1);
        }

        struct timeval to = {.tv_sec = MAX_WAIT, .tv_usec = 0};
            setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);


        CONN conn;
        conn.prot_id=1;
        conn.pack_id=1;
        conn.session_id=rand_uint64();
        conn.len=htobe64(index+1);

        write_length = writen(socket_fd, &conn, sizeof (conn));
        if(write_length<0){
            fprintf(stderr,"ERROR: Problem while writing to the server\n");
            close(socket_fd);
            exit(1);
        }
        static char buffer[sizeof(DATA)];
        memset(buffer, 0, sizeof(buffer));
        read_length = readn(socket_fd, buffer, sizeof (CONACC));
        if(read_length<0){
            fprintf(stderr,"ERROR: Problem while reading from the server\n");
            close(socket_fd);
            exit(1);
        }
        if(buffer[0]!=2){
            fprintf(stderr,"ERROR: Wrong packet\n");
            close(socket_fd);
            exit(1);
        }
        CONACC resp;
        memcpy(&resp,buffer,sizeof(CONACC));
        
        
        int index_sent = 0;
        u_int64_t pack_nr=1;
        DATA data;
        data.pack_id=4;
        data.session_id=conn.session_id;
        

        
        while(index_sent<index+1){
            u_int32_t len_to_send = MAX_DATA_SIZE;
            if(index+1-index_sent<MAX_DATA_SIZE){
                len_to_send = index+1-index_sent;
            }
            data.pack_nr=htobe64(pack_nr);

            ++pack_nr;
            data.len=htobe32(len_to_send);
            memcpy(data.data,input+index_sent,len_to_send);
            index_sent+=len_to_send;
            write_length = writen(socket_fd,&data,sizeof data);
            if(write_length<0){
                fprintf(stderr,"ERROR: Problem while writing to the server\n");
                close(socket_fd);
                exit(1);
            }
        }
        memset(buffer, 0, sizeof(buffer));
        read_length = readn(socket_fd, buffer, 9);
        if(read_length<0){
            fprintf(stderr,"ERROR: Problem while reading from a server\n");
            close(socket_fd);
            exit(1);
        }
        uint64_t s;
        memcpy(&s,buffer+1,sizeof(uint64_t));
        if((buffer[0]!=6 && buffer[0]!=7) || s!=conn.session_id){
            fprintf(stderr,"ERROR: Wrong packet\n");
            close(socket_fd);
            exit(1);
        }
        if(buffer[0]==6){ //RJT
            memset(buffer, 0, sizeof(buffer));
            read_length = readn(socket_fd, buffer, sizeof(uint64_t));
            if(read_length<0){
                fprintf(stderr,"ERROR: Problem while reading from a server\n");
                close(socket_fd);
                exit(1);
            }

            uint64_t nr;
            memcpy(&nr,buffer,sizeof(uint64_t));
            nr=be64toh(nr);
            if(nr>=pack_nr){
                fprintf(stderr,"ERROR: Wrong packet\n");
            }
            else{
                fprintf(stderr,"ERROR: Packet #%ld rejected\n",nr);
            }
            close(socket_fd);
            exit(1);
        }
        close(socket_fd);
        
    }


    if(strcmp(argv[1],"udp")==0){

        int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) {
            fprintf(stderr,"ERROR: socket\n");
            exit(1);
        }
        struct timeval to = {.tv_sec = MAX_WAIT, .tv_usec = 0};
            setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
        
        int send_flags = 0;
        socklen_t address_length = (socklen_t) sizeof(server_address);

        CONN conn;
        
        conn.prot_id=2;
        conn.pack_id=1;
        conn.session_id=rand_uint64();
        conn.len=htobe64(index+1);
        ssize_t sent_length = sendto(socket_fd, &conn, sizeof(conn), send_flags,
                                    (struct sockaddr *) &server_address, address_length);
        if(sent_length<0){
            fprintf(stderr,"ERROR: Problem while writing to the server\n");
            close(socket_fd);
            exit(1);
        }

        static char buffer[sizeof(DATA)];
        memset(buffer, 0, sizeof(buffer)); // Clean the buffer.
        int receive_flags = 0;
        struct sockaddr_in receive_address;
        address_length = (socklen_t) sizeof(receive_address);

        //Pobranie typu pakietu
        ssize_t received_length = recvfrom(socket_fd, buffer, sizeof(CONACC), receive_flags,
                                        (struct sockaddr *) &receive_address, &address_length);
        if ((long unsigned int)received_length < sizeof(CONACC)) {
            if(received_length<0) fprintf(stderr,"ERROR: Problem while reading from a server\n");
            else fprintf(stderr,"ERROR: Incomplite packet\n");
            close(socket_fd);
            exit(1);
        }

        u_int8_t id = buffer[0];

        uint64_t s;
        memcpy(&s,buffer+1,sizeof(u_int64_t));
        if(s!=conn.session_id){
            fprintf(stderr,"ERROR: Wrong packet.\n");
            close(socket_fd);
            exit(1);
        }
        
        if(id==2){
            //Polaczenie zaakceptowane
     
            int index_sent = 0;
            u_int64_t pack_nr=1;
            DATA data;
            data.pack_id=4;
            data.session_id=conn.session_id;

            
            while(index_sent<index+1){
                u_int32_t len_to_send = MAX_DATA_SIZE;
                if(index+1-index_sent<MAX_DATA_SIZE){
                    len_to_send = index+1-index_sent;
                }
                data.pack_nr=htobe64(pack_nr);
                ++pack_nr;
                data.len=htobe32(len_to_send);

                memcpy(data.data,input+index_sent,len_to_send);
                index_sent+=len_to_send;

                int send_flags = 0;
                socklen_t address_length = (socklen_t) sizeof(server_address);
                ssize_t sent_length = sendto(socket_fd, &data, sizeof(data), send_flags,
                                            (struct sockaddr *) &server_address, address_length);
                if(sent_length<0){
                    fprintf(stderr,"ERROR: Problem while writing to the server\n");
                    close(socket_fd);
                    exit(1);
                }
            }
            memset(buffer, 0, sizeof(buffer));
            receive_flags = 0;
            received_length = recvfrom(socket_fd, buffer, sizeof(RJT), receive_flags,
                                            (struct sockaddr *) &receive_address, &address_length);
            if (received_length <= 0) {
                fprintf(stderr,"ERROR: Problem while reading from a server\n");
                close(socket_fd);
                exit(1);
            }
            if((id==7 && (long unsigned int)received_length<sizeof(RCVD)) || (id==6 && (long unsigned int)received_length<sizeof(RCVD))){
                fprintf(stderr,"ERROR: Inclomplite packet\n");
                close(socket_fd);
                exit(1);
            }
            id=buffer[0];
            memset(&s, 0, sizeof(uint64_t));
            memcpy(&s,buffer+1,sizeof(uint64_t));
            
            if(s!=conn.session_id || (id!=6 && id!=7)){
                fprintf(stderr,"ERROR: Wrong packet.\n");
                close(socket_fd);
                exit(1);
            }
            if(id==6){
                uint64_t nr;
                memcpy(&nr,buffer+9,sizeof(uint64_t));
                nr=be64toh(nr);
                if(nr>=pack_nr){
                    fprintf(stderr,"ERROR: Wrong packet.\n");
                }
                else{
                    fprintf(stderr,"ERROR: Packet #%ld rejected\n",nr);
                }
                close(socket_fd);
                exit(1);
            }
            
        }


        close(socket_fd);

    }


    if(strcmp(argv[1],"udpr")==0){

        
        int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) {
            fprintf(stderr,"ERROR: socket\n");
            exit(1);
        }

        struct timeval limit = {.tv_sec = MAX_WAIT, .tv_usec = 0};
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof limit);

        int send_flags = 0;
        socklen_t address_length = (socklen_t) sizeof(server_address);


        CONN conn;
        conn.prot_id=3;
        conn.pack_id=1;
        conn.session_id=rand_uint64();
        conn.len=htobe64(index+1);

        static char buffer[sizeof(DATA)];
        struct sockaddr_in receive_address;
        address_length = (socklen_t) sizeof(receive_address);
        ssize_t received_length;
        ssize_t sent_length;
        int receive_flags;
        int rep_count=0;

        do{
            sent_length = sendto(socket_fd, &conn, sizeof(conn), send_flags,
                                        (struct sockaddr *) &server_address, address_length);
            if(sent_length<0){
                fprintf(stderr,"ERROR: Problem while writing to the server\n");
                close(socket_fd);
                exit(1);
            }
            
            memset(buffer, 0, sizeof(buffer));
            receive_flags = 0;
            
            received_length = recvfrom(socket_fd, buffer, sizeof(buffer), receive_flags,
                                            (struct sockaddr *) &receive_address, &address_length);
            if (received_length < 0 && errno!=EAGAIN) {
                fprintf(stderr,"ERROR: Problem while reading from a server\n");
                close(socket_fd);
                exit(1);
            }
            else if(received_length<0){
                ++rep_count;
            }
            else{
                uint64_t s;
                memcpy(&s,buffer+1,sizeof(uint64_t));
                if((buffer[0]==2 || buffer[0]==3) && s==conn.session_id){
                    break;
                }
                fprintf(stderr,"ERROR: Wrong packet\n");
                close(socket_fd);
                exit(1);
            }
            
        }while(rep_count<MAX_RETRANSMITS);


        u_int8_t id = buffer[0];
        if(id==2){
            //ACCEPTED
            int index_sent = 0;
            u_int64_t pack_nr=1;
            DATA data;
            data.pack_id=4;
            data.session_id=conn.session_id;

            rep_count=0;
            while(index_sent<index+1){
                u_int32_t len_to_send = MAX_DATA_SIZE;
                if(index+1-index_sent<MAX_DATA_SIZE){
                    len_to_send = index+1-index_sent;
                }
                data.pack_nr=htobe64(pack_nr);
                ++pack_nr;
                data.len=htobe32(len_to_send);
                memcpy(data.data,input+index_sent,len_to_send);
                index_sent+=len_to_send;

                int send_flags = 0;
                socklen_t address_length = (socklen_t) sizeof(server_address);
                do{
                    ssize_t sent_length = sendto(socket_fd, &data, sizeof(data), send_flags,
                                                (struct sockaddr *) &server_address, address_length);
                    if(sent_length<0){
                        fprintf(stderr,"ERROR: Problem while writing to the server\n");
                        close(socket_fd);
                        exit(1);
                    }

                    memset(buffer, 0, sizeof(buffer));
                    receive_flags = 0;
                    received_length = recvfrom(socket_fd, buffer, sizeof(buffer), receive_flags,
                                                    (struct sockaddr *) &receive_address, &address_length);

                    if (received_length < 0 && errno!=EAGAIN) {
                        fprintf(stderr,"ERROR: Problem while reading from from server\n");
                        close(socket_fd);
                        exit(1);
                    }
                    else if(received_length<0){
                        ++rep_count;
                    }
                    else{
                        
                        uint64_t s;
                        memcpy(&s,buffer+1,sizeof(uint64_t));

                        if(buffer[0]==2 && s==conn.session_id)continue;//Stary conn

                        uint64_t sent_nr;
                        memcpy(&sent_nr,buffer+9,sizeof(uint64_t));
                        sent_nr=be64toh(sent_nr);
                        if(buffer[0]==5 && s==conn.session_id && sent_nr<=pack_nr-1){
                            if(sent_nr==pack_nr-1)break;
                            else continue;
                            
                        }
                        fprintf(stderr,"ERROR: Wrong packet from the server\n");
                        close(socket_fd);
                        exit(1);
                    }
                    
                }while(rep_count<MAX_RETRANSMITS);
                

            }
            memset(buffer, 0, sizeof(buffer));
            receive_flags = 0;
            received_length = recvfrom(socket_fd, buffer, sizeof(buffer), receive_flags,
                                            (struct sockaddr *) &receive_address, &address_length);
            if (received_length < 0) {
                fprintf(stderr,"ERROR: Problem while reading from the server\n");
                close(socket_fd);
                exit(1);
            }
            
        }
        else{
            fprintf(stderr,"ERROR: Connection rejected\n");
            close(socket_fd);
            exit(1);
        }

        close(socket_fd);

    }
    return 0;
}