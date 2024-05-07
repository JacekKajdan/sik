#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <endian.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdbool.h>

#include "prot.h"
#include "err.h"
#include "common.h"
#include "protconst.h"

#define QUEUE_LENGTH  5
#define SOCK_TIMEOUT  4

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr,"ERROR: Wrong number of arguments\n");
        exit(1);
    }

    uint16_t port = read_port(argv[2]);

    signal(SIGPIPE, SIG_IGN);

    
    //TCP
    if(strcmp(argv[1],"tcp")==0){
        
        int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0) {
            fprintf(stderr,"ERROR: socket\n");
            exit(1);
        }

        struct sockaddr_in server_address;
        server_address.sin_family = AF_INET;
        server_address.sin_addr.s_addr = htonl(INADDR_ANY);
        server_address.sin_port = htons(port);


        if (bind(socket_fd, (struct sockaddr *) &server_address, (socklen_t) sizeof server_address) < 0) {
            fprintf(stderr,"ERROR: bind\n");
            exit(1);
        }
        

        if (listen(socket_fd, QUEUE_LENGTH) < 0) {
            fprintf(stderr,"ERROR: listen\n");
            exit(1);
        }

        
        socklen_t lenght = (socklen_t) sizeof server_address;
        if (getsockname(socket_fd, (struct sockaddr *) &server_address, &lenght) < 0) {
            fprintf(stderr,"ERROR: getsockname\n");
            exit(1);
        }
        

        for (;;) {
            struct sockaddr_in client_address;
            int client_fd = accept(socket_fd, (struct sockaddr *) &client_address,
                                &((socklen_t){sizeof(client_address)}));
            if (client_fd < 0) {
                fprintf(stderr,"ERROR: accept\n");
                close(socket_fd);
                exit(1);
            }


            //Limit czasu na odbior wiadomosci
            struct timeval to = {.tv_sec = MAX_WAIT, .tv_usec = 0};
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);

            ssize_t read_length;
            ssize_t written_length;

            //Odbior pakietu CONN
            static char buffer[sizeof(DATA)];
            memset(buffer, 0, sizeof(buffer));

            read_length = readn(client_fd, buffer, sizeof(CONN));

            if(read_length<0){
                fprintf(stderr,"ERROR: Problem while reading from the client\n");
                close(client_fd);
                continue;
            }

            if(buffer[0]!=1){
                fprintf(stderr,"ERROR: Wrong packet\n");
                close(client_fd);
                continue;
            }

            CONN conn;
            memcpy(&conn,buffer,sizeof(CONN));
            
            CONACC conacc;
            conacc.pack_id=2;
            conacc.session_id=conn.session_id;
            written_length = writen(client_fd, &conacc, sizeof conacc);
            if(written_length<0){
                fprintf(stderr,"ERROR: Problem while writing to the client\n");
                close(client_fd);
                continue;
            }


            //Odbieranie pakietow DATA

            uint64_t next_nr = 1;
            uint64_t msg_len = be64toh(conn.len);
            char *msg = (char*)malloc(sizeof(char)*msg_len);
            memset(msg, 0, msg_len);
            uint64_t msg_i=0;
            bool success=true;

            while(msg_i<msg_len) {
                memset(buffer, 0, sizeof(buffer));
                read_length = readn(client_fd, buffer, 21); //21 - wielkosc pakietu DATA bez pola na dane
                if(read_length<21){
                    fprintf(stderr,"ERROR: Problem while reading from the client\n");
                    success=false;
                    break;
                }
                if(buffer[0]!=4){
                    fprintf(stderr,"ERROR: Wrong packet\n");
                    success=false;
                    break;
                }


                DATA data;
                memcpy(&data,buffer,21);
                uint64_t nr = be64toh(data.pack_nr);
                uint32_t len = ntohl(data.len);
                if(nr!= next_nr || data.session_id != conn.session_id){

                    RJT rjt;
                    rjt.pack_nr=data.pack_nr;
                    rjt.pack_id=6;
                    rjt.session_id=conn.session_id;
                    written_length = writen(client_fd, &rjt, sizeof rjt);
                    if(written_length<0){
                        fprintf(stderr,"ERROR: Problem while writing to the client\n");
                    }
                    success=false;
                    break;
                }
                memset(buffer, 0, sizeof(buffer));
                read_length = readn(client_fd, buffer, len);
                if(read_length<0){
                    fprintf(stderr,"ERROR: Problem while reading from the client\n");
                    success=false;
                    break;
                }

                memcpy(msg+msg_i,buffer,len);
                msg_i+=len;
                ++next_nr;
            }
            if(success){
                RCVD rcvd;
                rcvd.pack_id=7;
                rcvd.session_id=conn.session_id;
                written_length = writen(client_fd, &rcvd, sizeof rcvd);
                if(written_length<0){
                    fprintf(stderr,"ERROR: Problem while writing to the client\n");
                }
                printf("%s",msg);
            }
            
            free(msg);
            close(client_fd);
        }
    }



    //UDP
    if(strcmp(argv[1],"udp")==0){
        
        int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) {
            fprintf(stderr,"ERROR: socket\n");
            exit(1);
        }

        struct timeval limit = {.tv_sec = MAX_WAIT, .tv_usec = 0};
        struct timeval nolimit = {.tv_sec = 0, .tv_usec = 0};
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &nolimit, sizeof nolimit);


        struct sockaddr_in server_address;
        server_address.sin_family = AF_INET;
        server_address.sin_addr.s_addr = htonl(INADDR_ANY);
        server_address.sin_port = htons(port);


        if (bind(socket_fd, (struct sockaddr *) &server_address, (socklen_t) sizeof(server_address)) < 0) {
            fprintf(stderr,"ERROR: bind\n");
            exit(1);
        }



        //Dane aktualnie obsługowanego klienta
        ssize_t received_length;
        bool has_client=false;

        uint64_t cur_session;
        cur_session=-1;
        uint8_t cur_prot=-1;
        uint64_t next_nr = 1;

        uint64_t msg_len;
        msg_len=0;
        char *msg = (char*)malloc(sizeof(char)*(msg_len));;
        uint64_t msg_i=0;
        int last_send = 0;
        int ret_count=0;

        for(;;) {
            // Otrzymywanie pakietu
            static char buffer[sizeof(DATA)];
            memset(buffer, 0, sizeof(buffer));

            int flags = 0;
            struct sockaddr_in client_address;
            socklen_t address_length = (socklen_t) sizeof(client_address);

            received_length = recvfrom(socket_fd, buffer, sizeof(DATA), flags,
                                (struct sockaddr *) &client_address, &address_length);

            if(received_length<0 && errno!=EAGAIN){
                fprintf(stderr,"ERROR: Problem while reading from a client\n");
                reset_params(&cur_session,&cur_prot,&next_nr,&msg_i,&has_client,&last_send,&ret_count,socket_fd,nolimit);
                continue;
            }
            else if(received_length<0){
                //Brak odpowiedzi
                ++ret_count;
                if(ret_count>MAX_RETRANSMITS){
                    reset_params(&cur_session,&cur_prot,&next_nr,&msg_i,&has_client,&last_send,&ret_count,socket_fd,nolimit);
                    continue;
                }
                if(last_send==2){
                    CONACC conacc;
                    conacc.pack_id=2;
                    conacc.session_id=cur_session;
                    int send_flags = 0;
                    ssize_t sent_length = sendto(socket_fd, &conacc, sizeof(conacc), send_flags,
                                                (struct sockaddr *) &client_address, address_length);
                    if(sent_length<0){
                        fprintf(stderr,"ERROR: Problem while writing to the client\n");
                        reset_params(&cur_session,&cur_prot,&next_nr,&msg_i,&has_client,&last_send,&ret_count,socket_fd,nolimit);
                        continue;
                    }
                }
                else{
                    ACC acc;
                    acc.pack_id=5;
                    acc.session_id=cur_session;
                    acc.pack_nr=last_send;
                    int send_flags = 0;
                    ssize_t sent_length = sendto(socket_fd, &acc, sizeof(acc), send_flags,
                                                (struct sockaddr *) &client_address, address_length);
                    if(sent_length<0){
                        fprintf(stderr,"ERROR: Problem while writing to the client\n");
                        reset_params(&cur_session,&cur_prot,&next_nr,&msg_i,&has_client,&last_send,&ret_count,socket_fd,nolimit);
                        continue;
                    }
                }

                continue;
            }

            //Dane o aktualnym pakiecie
            u_int8_t id = buffer[0];
            uint64_t session;
            memcpy(&session,buffer+sizeof(u_int8_t),sizeof(u_int64_t));
        
            u_int8_t prot = -1;

            if(id==1){
                prot = buffer[9];
            }

            if(id==1 && !has_client){
                //Nowy klient
                memcpy(&cur_session,&session,sizeof(session));
                has_client=true;
                cur_prot=prot;
                msg_len=0;
                memcpy(&msg_len,buffer+10,sizeof(u_int64_t));
                
                msg_len = be64toh(msg_len);
                setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof limit);
                free(msg);
                
                msg = (char*)malloc(sizeof(char)*(msg_len));

                msg_i=0;

                CONACC conacc;
                conacc.pack_id=2;
                conacc.session_id=cur_session;
                int send_flags = 0;
                ssize_t sent_length = sendto(socket_fd, &conacc, sizeof(conacc), send_flags,
                                            (struct sockaddr *) &client_address, address_length);
                if(sent_length<0){
                    fprintf(stderr,"ERROR: Problem while writing to the client\n");
                    reset_params(&cur_session,&cur_prot,&next_nr,&msg_i,&has_client,&last_send,&ret_count,socket_fd,nolimit);
                    continue;
                }
                last_send=2;
                
            }
            else if(has_client && cur_session!=session){
                //Nieobslugiwany klient
                CONRJT conrjt;
                conrjt.pack_id=3;
                conrjt.session_id=cur_session;
                int send_flags = 0;
                ssize_t sent_length = sendto(socket_fd, &conrjt, sizeof(conrjt), send_flags,
                                            (struct sockaddr *) &client_address, address_length);
                if(sent_length<0){
                    fprintf(stderr,"ERROR: Problem while writing to the client\n");
                    reset_params(&cur_session,&cur_prot,&next_nr,&msg_i,&has_client,&last_send,&ret_count,socket_fd,nolimit);
                    continue;
                }
            }
            else if(has_client && cur_session==session){
                //Obsluga aktualnego klienta
                if(id==1){ //Stary conn
                    continue;
                }
                else if (id!=4){ //zly pakiet od dobrego klienta
                    reset_params(&cur_session,&cur_prot,&next_nr,&msg_i,&has_client,&last_send,&ret_count,socket_fd,nolimit);
                    continue;
                }

                uint64_t nr;
                memcpy(&nr,buffer+9,sizeof(u_int64_t));
                nr = be64toh(nr);

                uint32_t len;
                memcpy(&len,buffer+17,sizeof(u_int32_t));
                len=ntohl(len);
                if(nr > next_nr){
                    RJT rjt;
                    rjt.pack_id=6;
                    rjt.session_id=cur_session;
                    rjt.pack_nr=htobe64(nr);
                    int send_flags = 0;
                    ssize_t sent_length = sendto(socket_fd, &rjt, sizeof(rjt), send_flags,
                                                (struct sockaddr *) &client_address, address_length);
                    if(sent_length<0){
                        fprintf(stderr,"ERROR: Problem while writing to the client\n");
                    }
                    reset_params(&cur_session,&cur_prot,&next_nr,&msg_i,&has_client,&last_send,&ret_count,socket_fd,nolimit);
                    continue;
                }
                else if(nr < next_nr){
                    continue;
                }

                //Pakiet poprawny

                if(cur_prot==3){//retransmisja
                    ACC acc;
                    acc.pack_id=5;
                    acc.session_id=cur_session;
                    acc.pack_nr=htobe64(nr);
                    int send_flags = 0;
                    ssize_t sent_length = sendto(socket_fd, &acc, sizeof(acc), send_flags,
                                                (struct sockaddr *) &client_address, address_length);
                    if(sent_length<0){
                        fprintf(stderr,"ERROR: Problem while writing to the client\n");
                        reset_params(&cur_session,&cur_prot,&next_nr,&msg_i,&has_client,&last_send,&ret_count,socket_fd,nolimit);
                        continue;
                    }
                    last_send=htobe64(nr);
                }

                ret_count=0;
                memcpy(msg+msg_i,buffer+21,len);
                
                msg_i+=len;
                ++next_nr;
                if(msg_i>=msg_len){
                    printf("%s\n",msg);
                    RCVD rcvd;
                    rcvd.pack_id=7;
                    rcvd.session_id=cur_session;
                    int send_flags = 0;
                    ssize_t sent_length = sendto(socket_fd, &rcvd, sizeof(rcvd), send_flags,
                                                (struct sockaddr *) &client_address, address_length);
                    if(sent_length<0){
                        fprintf(stderr,"ERROR: Problem while writing to the client\n");
                    }
                    reset_params(&cur_session,&cur_prot,&next_nr,&msg_i,&has_client,&last_send,&ret_count,socket_fd,nolimit);
                }
            }
            
            else{
                fprintf(stderr,"ERROR: Wrong Packet\n");
                reset_params(&cur_session,&cur_prot,&next_nr,&msg_i,&has_client,&last_send,&ret_count,socket_fd,nolimit);
            }
        }

        close(socket_fd);
    }



    
    return 0;
}
