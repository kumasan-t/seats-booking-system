#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <fcntl.h>
#include <argp.h> 
#include <time.h>
#include <signal.h>


#define BACKLOG 10
#define BUFFER 1024
#define RES_DIM 20


struct server_data {
	unsigned int key_length;
	unsigned int rows;
	unsigned int clmn;
	char * matrix;
};
struct reservation { 				//entry of a collection of reservations
	unsigned int s_num;			//number of seats reserved for a single reservation
	char * reservation_code;		//unique code generated for a single reservation
	struct seat * seats; 			//array of reserved seats;	
};
struct seat {
	unsigned int row;
	unsigned int col;
};

struct server_data info;
struct reservation * res_list;
unsigned int keep_going = 1;
//void methods
void matrix_init();
void check_res_status();
void print_matrix();
void release_seats(unsigned int s_num,struct seat * seats_occ);
void occupy_seats(unsigned int s_num,struct seat * seats_occ);
//int methods
int perform_reservation(unsigned int seats_num,struct seat * seats_occ,struct reservation ** r_entry);
int seats_available(unsigned int num, struct seat * seats);
//char * methods
char * get_reservation_code();

int no_double_seats(struct seat * seats,unsigned int seats_num) {
	int i,j;
	for ( i = 0; i < seats_num; i++ ) {		
		for ( j = 0; j < seats_num; j++ ) {
			if (i == j ) {}
			else if (seats[j].row == seats[i].row && seats[j].col == seats[i].col ) {
				return -1;
			}
		}
	}
	return 1;
}

int check_constrains(unsigned int s_num, struct seat * arr) {
	struct seat * punt = arr;
	while( punt - arr < s_num ) {
		if ( punt->row >= info.rows || punt->col >= info.clmn ) {
			return -1;	
		}
		punt++;	
	}
	return 1;
}

void reservation(int sd) {
	int res;
	unsigned int seats_num = 0;

	//receive seats number
	res = read(sd,&seats_num,sizeof(seats_num));
	if(res < sizeof(seats_num)){
		if(res == -1 ){perror("receive number of seats"); }
		else { puts("Error: received invalid seats num"); }
		return;
	}
		
	//receive seats
	struct seat seats_temp[seats_num];
	res = read(sd,seats_temp,sizeof(seats_temp));
	if(res < sizeof(seats_temp)) {
		if(res == -1) { perror("receive seats"); }
		else { puts("Error: mismatch of seats number recived");}
		return;
	}
	
	if (seats_available(seats_num,seats_temp) == 0 || 
	   (check_constrains(seats_num,seats_temp) == -1) ||
	   (no_double_seats(seats_temp,seats_num) == -1 ) ) { 
		char msg[10] = "RES_ERR";
		if (send(sd,msg,10,0) == -1) { perror("send error error");close(sd); return; }
		if(keep_going == 0 ) { puts("sigpipe received, aborting reservation routine");close(sd); keep_going = 1; }		
	}
	else {
		char msg[10] = "RES_OK";
		unsigned int last_res_index;
		
		if (send(sd,msg,10,0) == -1) { puts("send confirmation error");close(sd); return; }
		if(keep_going == 0 ) { puts("sigpipe received, aborting reservation routine");close(sd); return; }
		struct reservation * r_entry;
		last_res_index = perform_reservation(seats_num,seats_temp,&r_entry);
		if ((save_reservation_array(info.rows*info.clmn,info.key_length)) == -1 ) { delete_last_reservation(last_res_index);return; }		
		if (send(sd,r_entry->reservation_code,info.key_length+1,0) == -1 ) { 
			puts("error sending reservation code");
			delete_last_reservation(last_res_index); 
		}
		if(keep_going == 0 ) { puts("sigpipe received, aborting reservation routine");delete_last_reservation(last_res_index);keep_going = 1;  }
	}
	return;	
}

int perform_reservation(unsigned int seats_num,struct seat * seats_occ,struct reservation ** r_entry) {
	
	occupy_seats(seats_num,seats_occ);	
	
	int i;
	for(i = 0; i < info.rows*info.clmn; i++) {

		if (res_list[i].s_num == 0)
			break;

	}
	struct reservation * new_entry = res_list + i;
	
	//save seats_num into new entry
	new_entry->s_num = seats_num;
		
	//copy the seat buffer into the new entry	
	void * seats_temp = calloc(seats_num,sizeof(struct seat));
	if (seats_temp == NULL ) { perror("Calloc error in perform reservation!"); exit(1); }
	
	new_entry->seats = memcpy(seats_temp,seats_occ,sizeof(struct seat)*seats_num);
	new_entry->reservation_code = get_reservation_code(); 
				
	*r_entry = new_entry;
	return i;	
}

char * get_reservation_code() {

   static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

	char * s =(char *)malloc(info.key_length+1);
	int i;
   
	for (i = 0; i < info.key_length; i++) {
   	s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
   }

   s[info.key_length+1] = '\0';
	return s;

}

int delete_reservation(int sd) {
	char client_key[30];
	if (read(sd,client_key,11) == -1 ) { perror("client_key read error"); }
	if (perform_delete(client_key) == 0) {
		write(sd,"WRONG_KEY",20);
		if(keep_going == 0) { keep_going = 1; }
		close(sd);
		return 0;
	}else {
		save_reservation_array(info.rows*info.clmn,info.key_length);
		write(sd,"DEL_CONFIRMED",20);
		if(keep_going == 0) { keep_going = 1; }
		return 1;
	}
}

int perform_delete(char * ck) {
	struct reservation * punt = res_list;
	unsigned int dim_array = info.rows*info.clmn;
	while ( punt-res_list < dim_array ) {
		if(punt->s_num == 0 ) { punt++; }
		else {
			if (strcmp(punt->reservation_code,ck) == 0) {
				release_seats(punt->s_num,punt->seats);			
				punt->s_num = 0;
				free(punt->reservation_code);
				free(punt->seats);
				return 1;
			} else {
				punt++;
			}
		}
	}
	return 0;	
}


void show_seatsmap(int sd) {
	char mat_raws[3];
	char mat_clmns[3];
	char option[10];
	char * mbuffer;
	
	sprintf(mat_clmns,"%d",info.clmn);
	sprintf(mat_raws,"%d",info.rows);

	//Handshake before sending map
	if (write(sd,mat_raws,3) == -1 ) {return; }
	if(keep_going == 0) { keep_going = 1; return; }	
	
	if (write(sd,mat_clmns,3) == -1) {return; }
	if(keep_going == 0) { keep_going = 1;return; }
	
	//Sending map;
	char * qua;
	int i,j;	
	char (*temp_matrix)[info.clmn] = (char (*)[info.clmn])info.matrix;
	char str_buff[1];
	
	for(i = 0; i < info.rows; i++) {
		for(j = 0; j < info.clmn; j++) {
			sprintf(str_buff,"%c",temp_matrix[i][j]);
			if (write(sd,str_buff,1) == -1 ) { return; }
			if(keep_going == 0) { keep_going = 1;return; }
		}
	}	
}


int listening_function() {
	
	int ds_sock;
	int port = 4444;
	int length_inc;
	int ds_acc;
	
	struct sockaddr_in addr,inc;

	ds_sock = socket(AF_INET,SOCK_STREAM,0);
	
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
		
	int length_addr = sizeof(addr);

	if(bind(ds_sock,(struct sockaddr *)&addr,length_addr)==-1) { perror("Binding error"); exit(1); }
	if(listen(ds_sock,BACKLOG)==-1) { perror("Listening error"); exit(1); }
	length_inc = sizeof(inc);
	
	while(1) {		
		while((ds_acc = accept(ds_sock,(struct sockaddr *)&inc, &length_inc))==-1 );
			printf(">>Connected to socket %d \n",ds_acc);
			perform_action(ds_acc);
			check_res_status();
	}
}

int perform_action(int sock_descriptor) {
	char option[10];

	if(read(sock_descriptor,option,10)==-1) { perror("Reading error init_connection"); }

	if (strcmp(option,"-S\n")==0) {
		show_seatsmap(sock_descriptor);
	}
	if (strcmp(option,"-R\n")==0) {
		reservation(sock_descriptor);
	}
	if (strcmp(option,"-D\n")==0) {
		delete_reservation(sock_descriptor);
	}
	close(sock_descriptor);
}

int create_map(unsigned int raws,unsigned int columns) {

	int i,j,fd;
	info.rows = raws;	
	info.clmn = columns;	
	matrix_init();

}

void matrix_init() {
	int i,j;
	info.matrix = (char *)malloc(info.rows*info.clmn*sizeof(char));
	char (*temp_matrix)[info.clmn] = (char (*)[info.clmn])info.matrix;	
	//How to read this ^: "temp_matrix is a pointer of info.clmn characters"
	
	for(i = 0; i < info.rows; i++) {
		for(j = 0; j < info.clmn; j++) {
			temp_matrix[i][j] = 'O';	
		}
	}
}

void print_matrix() {
	
	int i,j;
	char (*temp_matrix)[info.clmn] = (char (*)[info.clmn])info.matrix;	
	
	for(i = 0; i < info.rows; i++) {
		for(j = 0; j < info.clmn; j++) {
			printf("[%c] ",temp_matrix[i][j]);
		}
		printf("\n");
	}
}
int delete_last_reservation(int last_res_index) {
	release_seats(res_list[last_res_index].s_num,res_list[last_res_index].seats);			
	res_list[last_res_index].s_num = 0;
	free(res_list[last_res_index].reservation_code);
	free(res_list[last_res_index].seats);
}


int save_reservation_array(unsigned int arr_dim,unsigned int chiav_dim){
	
	int res;
	int des_f = open("./seats_res/reservations",O_CREAT | O_WRONLY,0660);
	
	struct reservation * punt = res_list;
	while(punt - res_list < arr_dim){
		
		//write s_num
		res = write(des_f,&(punt->s_num),sizeof(punt->s_num));
		if(res < sizeof(punt->s_num)){
			if(res == -1)
				perror("writing s_num on file");
			else
				puts("error: writing s_num on file");
			return(-1);
		}
		
		if(punt->s_num != 0){
				
			//write resevation_code
			res = write(des_f,punt->reservation_code,chiav_dim+1);
			if(res < chiav_dim+1){
				if(res == -1)
					perror("writing chiavazione on file");
				else
					puts("error: writing chiavazione on file");
			return(-1);
			}
		
			//write the seats array
			res = write(des_f,punt->seats,(punt->s_num)*(sizeof(struct seat)));
			if(res < (punt->s_num)*(sizeof(struct seat))){
				if(res == -1)
					perror("writing seats on file");
				else
					puts("error: writing seats on file");
				return(-1);
			}
		}
		
		punt++;
	}
	return 0;
}


int load_reservation_array(unsigned int arr_dim, unsigned int chiav_dim){
	
	int res;
	int des_f;
	des_f = open("./seats_res/reservations", O_RDONLY | O_CREAT, 0660);					
	
	struct reservation * punt = res_list;
	while(punt - res_list < arr_dim){
		
		//read s_num
		res = read(des_f,&(punt->s_num),sizeof(punt->s_num));
		if(res < sizeof(punt->s_num)){
			if(res == -1)
				perror("reading s_num from file");
		return(-1);
		}
		
		if(punt->s_num != 0){
			
			punt->reservation_code = malloc(chiav_dim+1);
			if(punt->reservation_code == NULL){perror("error in malloc load_reservation_array");return(-1);}
						
			res = read(des_f,punt->reservation_code,chiav_dim+1);
			if(res < chiav_dim+1){
				if(res == -1)
					perror("reading chiavazione from file");
				else
					puts("error: reading chiavazione from file");
			return(-1);
			}
		
			//read seats arr
			punt->seats = malloc((punt->s_num)*(sizeof(struct seat)));
			if(punt->seats == NULL){perror("error in malloc load_reservation_array");return(-1);}
			
			res = read(des_f,punt->seats,(punt->s_num)*(sizeof(struct seat)));
			if(res < (punt->s_num)*(sizeof(struct seat))){
				if(res == -1)
					perror("reading seats from file");
				else
					puts("error: reading seats from file");
				return(-1);
			}
			
			//refill matrix
			occupy_seats(punt->s_num,punt->seats);
		}
		punt++;	
	}

	return 0;
}

void occupy_seats(unsigned int num, struct seat * seats_occ) {
	char (*matrix)[info.clmn] =(char (*)[info.clmn]) info.matrix;
	struct seat * punt = seats_occ;
	while( (punt - seats_occ) < num ) {
		matrix[punt->row][punt->col] = 'X';
		punt++;
	}
}

void release_seats(unsigned int num, struct seat * seats_occ) {
	char (*matrix)[info.clmn] = (char (*)[info.clmn])info.matrix;
	struct seat * punt = seats_occ;
	while(punt - seats_occ < num ) {
		matrix[punt->row][punt->col] = 'O';
		punt++;
	}
}

int seats_available(unsigned int num, struct seat * seats) {
	char (*matrix)[info.clmn] =(char (*)[info.clmn]) info.matrix;
	struct seat * punt = seats;
	while( (punt-seats) < num ){
		if( matrix[punt->row][punt->col] != 'O')
			return 0;
		punt++;
	}
	return 1;
}

int reservation_list_init() {	
	struct reservation * posti_occupati =(struct reservation *)malloc(info.rows*info.clmn*sizeof(struct reservation ));
	res_list = posti_occupati;
}

void check_res_status() {
	int i = 0;	
	for(i = 0; i < info.rows*info.clmn; i++) {
		if (res_list[i].s_num == 0 ) {}
		else { printf("key: %s, seats reserved %d\n",res_list[i].reservation_code,res_list[i].s_num); }
	}
}

void init_rand_generator() {
	srand(time(NULL));
}

void close_routine() {
	save_reservation_array(info.rows*info.clmn,info.key_length);
	exit(0);
}

int new_map(char * row, char * clmn ) {
	unsigned int r = strtol(row,NULL,10);
	unsigned int c = strtol(clmn,NULL,10);
	if ( r == 0 || c == 0 ) { printf("dimension 0 is not accepted\n"); exit(1); }
	printf("The new cinema has %u rows and %u columns\n",r,c);
	create_map(r,c);
	int fd = open("./seats_map/seats.map",O_WRONLY | O_CREAT | O_TRUNC,0660);
	if (fd < 0) { perror("Open issue at matrix_init"); exit(1); }
	write(fd,&r,sizeof(unsigned int));
	write(fd,&c,sizeof(unsigned int));
	fd = open("./seats_res/reservations",O_WRONLY | O_CREAT | O_TRUNC,0660);
	if (fd < 0) { perror("Error in reservations file\n"); exit(1); } 
}

int previous_map() {
	unsigned int r,c;
	int fd = open("./seats_map/seats.map",O_RDONLY,0660);
	if (fd < 0) { perror("Open fails at matrix_init"); exit(1); }
	read(fd,&r,sizeof(unsigned int));
	read(fd,&c,sizeof(unsigned int));
	create_map(r,c);
}

void sig_pipe_handler(int sig_num) {
	keep_going = 0;
	signal(SIGPIPE,sig_pipe_handler);	
}

int main(int argc, char **argv) {
	
	// signal management
	signal(SIGPIPE,sig_pipe_handler);
	
	sigset_t set;
	if(sigfillset(&set)){ perror("filling set of signals"); exit(-1);}
	
	struct sigaction sig_act;
	sig_act.sa_handler = close_routine;
	sig_act.sa_mask = set;
	
	if(sigaction(SIGINT,&sig_act,NULL)){ perror("sigaction"); exit(-1);}
	if(sigaction(SIGTERM,&sig_act,NULL)){ perror("sigaction"); exit(-1);}
	if(sigaction(SIGABRT,&sig_act,NULL)){ perror("sigaction"); exit(-1);}
	if(sigaction(SIGHUP,&sig_act,NULL)){ perror("sigaction"); exit(-1);}
	if(sigaction(SIGQUIT,&sig_act,NULL)){ perror("sigaction"); exit(-1);}
	if(sigaction(SIGILL,&sig_act,NULL)){ perror("sigaction"); exit(-1);}

	if ( argc == 3 ) { new_map(argv[1],argv[2]); }
	else { previous_map(); }

	//initialize the random code generator
	init_rand_generator();

	reservation_list_init();

	info.key_length = 10;
	load_reservation_array(info.rows*info.clmn,info.key_length);
	
	check_res_status();
	
	listening_function();

}
