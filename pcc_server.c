/*
 * pcc_server.c
 *
 *  Created on: Jun 5, 2018
 *      Author: gasha
 */

#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <limits.h>

/// --------- DEFINES ---------
#define ERROR -1
#define SUCCESS 1
#define RANDOM_FILE "/dev/urandom"
#define MAX_LISTEN 100
#define TOTAL_PRINTABLE 95
#define OFFSET 32

/// --------- GLOBAL PARAMS ---------

pthread_mutex_t pcc_mutex;
int printable_chars[TOTAL_PRINTABLE];
int num_of_threads = 0;
pthread_t * threads = NULL;
pthread_attr_t attr;
bool accepting_clients = true;
int listenfd = -1;

/// --------- HELPER FUNCTIONS --------
unsigned int convert(char *st, int size_of_arr, int* rc) {
	for (int i = 0; i < size_of_arr; i++) {
		if (!isdigit(st[i])) {
			*rc = 0;
			return 0L;
		}
	}
	*rc = 1;
	return (strtoul(st, 0L, 10));
}

void sigint_handler(int signum) {
	accepting_clients = false;
	if (close(listenfd) < 0) {
		printf("Error: %s \n", strerror(errno));
		exit(ERROR);
	}
	int i = 0;
	//join threads
	for (i = 0; i < num_of_threads; i++) {
		if (pthread_join(threads[i], NULL) != 0) {
			printf("Error joining thread: %s\n", strerror(errno));
			exit(ERROR);
		}
	}
	// print pcc_counter values
	for (int i = 0; i < TOTAL_PRINTABLE; i++) {
		int c = i + OFFSET;
		printf("char '%c': %u times\n", c, printable_chars[i]);
	}
	// exit gracefully
	if (pthread_attr_destroy(&attr) != 0) {
		printf("Error destroy attr: %s\n", strerror(errno));
		exit(ERROR);
	}
	if (pthread_mutex_destroy(&pcc_mutex) != 0) {
		printf("Error destroy mutex: %s\n", strerror(errno));
		exit(ERROR);
	}
	free(threads);

}

int sigint_register() {
	struct sigaction new_action_term;
	memset(&new_action_term, 0, sizeof(new_action_term));
	new_action_term.sa_handler = sigint_handler;
	return sigaction(SIGINT, &new_action_term, NULL);
}

int isPrintableChar(char c) {
	if (c >= 32 && c <= 126)
		return 1;
	else
		return 0;
}

unsigned int countPrintableChars (char* in_buffer, unsigned int arr_size, unsigned int* result_arr){
	unsigned int counter = 0;
	int index = 0;
	for (int i=0; i<arr_size; i++){
		if (isPrintableChar(in_buffer[i])){
			counter=counter+1;
			index = in_buffer[i]-OFFSET;
			result_arr[index]+=1;
		}
	}
	return counter;
}

void updateGlobalCount(unsigned int* thread_arr){
	for (int i = 0; i < TOTAL_PRINTABLE; i++) {
		printable_chars[i] = printable_chars[i] +  thread_arr[i];
	}
}
//TODO: pass another argument of length of read_buffer
int readFromClients(int socket_fd, char* read_buffer, unsigned int length) {
	int total = 0;
	ssize_t result;
	while (total < length) {
		 result = read(socket_fd, read_buffer + total,
				length - total);
		if (result < 0) {
			printf("Error in reading from client the message len: %s\n", strerror(errno));
			return ERROR;
		}
		total += result;
	}
	return total;
}

int writeToClient(int socket_fd, char* read_buffer, unsigned int length) {
	int total = 0;
	ssize_t result;
	while (total < length) {
		 result = write(socket_fd, read_buffer + total,
				length - total);
		if (result < 0) {
			printf("Error in writing to file: %s\n", strerror(errno));
			return ERROR;
		}
		total += result;
	}
	return total;
}

/// --------- THREAD FUNCTION ---------

void * handle_connection(void* a_input) {
	//get variable
	int fd = *((int* ) a_input);
	uint32_t message_len;
	char* read_buffer = (char*)&message_len;
	int size_of_message = sizeof(message_len);
	//read len of message from client
	if (readFromClients(fd, read_buffer, size_of_message)<0){
		printf("Error in reading from client the message len: %s\n", strerror(errno));
		exit(ERROR);
	}
	//prepare reading message from client
	unsigned int message_len_ui = ntohl(message_len);
	char* message = malloc(message_len * sizeof(char));
	if (!message){
		printf("Error in malloc of the message%s\n", strerror(errno));
		exit(ERROR);
	}
	memset(message, 0, message_len_ui);

	//read message from client
	if (readFromClients(fd, message, message_len_ui)<0){
		printf("Error in reading from client the message len: %s\n", strerror(errno));
		exit(ERROR);
	}
	//count printable chars
	unsigned int* thread_arr = malloc(TOTAL_PRINTABLE*sizeof(unsigned int));
	if (!thread_arr){
		printf("Error in malloc of the message%s\n", strerror(errno));
		exit(ERROR);
	}
	memset(thread_arr, 0, TOTAL_PRINTABLE);

	//return num of printable chars to client
	uint32_t client_count = htonl(countPrintableChars(message, message_len_ui, thread_arr));
	char* write_buffer = (char*)&client_count;
	unsigned int size_of_count = sizeof(client_count);
	if (writeToClient(fd, write_buffer, size_of_count)<0){
			printf("Error in reading from client the message len: %s\n", strerror(errno));
			exit(ERROR);
	}

	//update array
	if (pthread_mutex_lock(&pcc_mutex) != 0) {
		printf("Error locking the mutex: %s\n", strerror(errno));
		exit(ERROR);
	}

	updateGlobalCount(thread_arr);

	if (pthread_mutex_unlock(&pcc_mutex) != 0) {
		printf("Error unlock mutex: %s\n", strerror(errno));
		exit(ERROR);
	}
	//exit gracefully
	close(fd);
	free(thread_arr);
	free(message);
	free((int*)a_input);
	pthread_exit(NULL);
}

/// --------- MAIN ---------
int main(int argc, char** argv) {
	if (argc != 2) {
		printf("Error in number of given args!\n");
		return ERROR;
	}
	//init pcc_count
	int rc = 0;
	uint16_t server_port = convert(argv[1], strlen(argv[1]), &rc);
	if (!rc) {
		printf("Error : Failed to convert to unsigned int!\n");
		return ERROR;
	}
	memset(printable_chars, 0, TOTAL_PRINTABLE);

	//register handler
	if (sigint_register() != 0) {
		printf("Signal handle registration failed %s\n", strerror(errno));
		return ERROR;
	}

	//create socket
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd == -1) {
		printf("Failed creating listening socket %s\n", strerror(errno));
		return ERROR;
	}

	int reuse_socket = 1;
	//reference: https://stackoverflow.com/questions/24194961/how-do-i-use-setsockoptso-reuseaddr
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
			(const char*) &reuse_socket, sizeof(reuse_socket)) < 0) {
		printf("Failed reusing socket %s\n", strerror(errno));
		return ERROR;
	}

	//structs preparation
	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(struct sockaddr_in));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(server_port);

	//bind
	if (bind(listenfd, (struct sockaddr *) &serv_addr,
			sizeof(struct sockaddr_in)) == -1) {
		printf("Failed binding socket %s\n", strerror(errno));
		close(listenfd);
		return ERROR;
	}
	//listen
	if (listen(listenfd, MAX_LISTEN) == -1) {
		printf("Failed to start listening to incoming connections %s\n",
				strerror(errno));
		close(listenfd);
		return ERROR;
	}

	//prepartion for threads
	//For portability, explicitly create threads in a joinable state
	if (pthread_attr_init(&attr) != 0) {
		printf("Error initializing pthread attr: %s\n", strerror(errno));
		return ERROR;
	}
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) != 0) {
		printf("Error set detach state of pthread attr: %s\n", strerror(errno));
		return ERROR;
	}

	if (pthread_mutex_init(&pcc_mutex, NULL) != 0) {
		printf("Failed to init mutex %s\n", strerror(errno));
		return ERROR;
	}

	while (1) {	// accept new connection
		int connfd = accept(listenfd, NULL, NULL);
		if ((errno == EAGAIN) || (errno == EINTR))
			break;
		if (connfd < 0) {
			printf("Failed to accept client connection %s\n", strerror(errno));
			close(listenfd);
			return ERROR;
		}
		if (accepting_clients) {
			//create threads array
			if (num_of_threads == 0) {
				threads = (pthread_t*) calloc(
						(num_of_threads + 1), sizeof(pthread_t));
				num_of_threads+= 1;
			} else {
				num_of_threads += 1;
				threads = realloc(threads, num_of_threads * sizeof(pthread_t));
			}
			if (!threads) {
				printf("Error allocating memory to thread array: %s\n",
						strerror(errno));
				num_of_threads = 0;
				return ERROR;
			}
			// setting argument
			int* connfd_ptr = calloc(1, sizeof(int));
			*connfd_ptr = connfd;

			int rc = pthread_create(&threads[num_of_threads - 1], &attr,
					handle_connection, (void *) connfd_ptr);
			if (rc != 0) {
				printf("Error creating thread: %s\n", strerror(errno));
				return ERROR;
			}
			//continue accepting new threads
		}
		else{
			close(connfd);
		}
	}
	return SUCCESS;

}
