/*
 * pcc_client.c
 *
 *  Created on: Jun 5, 2018
 *      Author: gasha
 */

#include <stdint.h>
#include <sys/socket.h>
#include <stdio.h> //printf
#include <string.h> //memset
#include <stdlib.h> //for exit(0);
#include <sys/socket.h>
#include <errno.h> //For errno - the error number
#include <netdb.h> //hostent
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <netdb.h>

/// --------- DEFINES ---------
#define ERROR -1
#define SUCCESS 1
#define RANDOM_FILE  "/dev/urandom"

/// --------- HELPER FUNC ---------
//reference: https://stackoverflow.com/questions/791982/determine-if-a-string-is-a-valid-ip-address-in-c
int isValidIpAddress(char *ipAddress) {
	struct sockaddr_in sa;
	int result = inet_pton(AF_INET, ipAddress, &(sa.sin_addr));
	return result;
}

int readFromRandom(char* read_buffer, unsigned int length) {
	//reference: https://stackoverflow.com/questions/2572366/how-to-use-dev-random-or-urandom-in-c
	size_t result = -1;
	int total = 0;
	int random_fd = open(RANDOM_FILE, O_RDONLY);
	if (random_fd < 0) {
		printf("Failed to open /dev/urandom/ %s\n", strerror(errno));
		return ERROR;
	} else {
		while (total < length){
			result = read(random_fd, read_buffer+total, length-total);
			if (result < 0) {
				printf("Failed to read from /dev/urandom/ %s\n", strerror(errno));
				close(random_fd);
				return result;
			}
			total+=result;
		}
	read_buffer[length] = '\0';
	close(random_fd);
	}
	return total;
}

int writeToServer(int socket_fd, char* read_buffer, unsigned int length) {
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

int writeSizeToServer(int socket_fd, unsigned int length){
	uint32_t length_to_send = htonl(length);
	char* to_send = (char*)&length_to_send;
	int size_of_message = sizeof(length_to_send);
	int total = 0;
	ssize_t  bytes_wrote = 0;
	while (total < size_of_message) {
		bytes_wrote = write(socket_fd, to_send + total, size_of_message - total);
		if (bytes_wrote < 0) {
			printf("Error in writing to file: %s\n", strerror(errno));
			return ERROR;
		}
		total += bytes_wrote;
	}
	return total;

}

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

/// --------- MAIN ---------
int main(int argc, char** argv) {
	if (argc != 4) {
		printf("Error in number of given args!\n");
		return ERROR;
	}
	//get all command line args
	char* server_host = argv[1];
	int rc = 0;
	uint16_t server_port = convert(argv[2], strlen(argv[2]), &rc);
	if (!rc) {
		printf("Error : Failed to convert to unsigned int!\n");
		return ERROR;
	}
	unsigned int length = convert(argv[3], strlen(argv[3]), &rc);
	if (!rc) {
		printf("Error : Failed to convert to unsigned int!\n");
		return ERROR;
	}
	//create data structs
	int sockfd = -1;

	unsigned int C = -1;
	char* read_buffer = calloc((length + 1), sizeof(char));
	if  (!read_buffer){
		printf("Error : Failed to malloc read buffer!\n");
		return ERROR;
	}
	int total_read = -1;

	struct sockaddr_in serv_addr; // where we Want to get to
	struct addrinfo* addrinfo_struct; // used for addrinfo
	struct addrinfo* tmp; // used for looping

	//memset data structs
	memset(&serv_addr, 0, sizeof(serv_addr));
	memset(&addrinfo_struct, 0, sizeof(addrinfo_struct));

	//check whether it is a valid ip address
	int ip_to_hostname = isValidIpAddress(server_host);
	if (ip_to_hostname == -1) { //error in inet_pton
		printf("Error in inet_pton: %s\n", strerror(errno));
		return ERROR;
	} else if (ip_to_hostname == 0) { //convert host name to IP address
		//reference: https://www.binarytides.com/hostname-to-ip-address-c-sockets-linux/
		if (getaddrinfo(server_host, NULL, NULL, &addrinfo_struct) != 0) { //error
			printf("error in get addrinfo %s\n", strerror(errno));
			return ERROR;
		}
		// loop through all the results and connect to the first we can
		for (tmp = addrinfo_struct; tmp != NULL; tmp = tmp->ai_next) {
			serv_addr.sin_addr = ((struct sockaddr_in*) tmp->ai_addr )->sin_addr;
		}

		freeaddrinfo(addrinfo_struct); // all done with this structure

	} else {
		if (inet_pton(AF_INET, server_host, &(serv_addr.sin_addr)) != 1) {
			printf("FATAL error inet_pton %s\n", strerror(errno));
			return ERROR;
		}
	}

	//got the ip - now create a tcp connection
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(server_port);

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("Socket creating error%s\n", strerror(errno));
		return ERROR;
	}

	if (connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
		printf("Error : Connect Failed. %s \n", strerror(errno));
		return ERROR;
	}

// open /dev/urandom for reading
	if ((total_read = readFromRandom(read_buffer, length)) != length) {
		printf("read only %ud", total_read);
		printf("Error : Failed to read all the chars from dev/urandom!\n");
		return ERROR;
	}

	//send size of message
	if (writeSizeToServer(sockfd, total_read) < 0) {
		printf("Error : Failed to transfer size of message  to server!\n");
		return ERROR;
	}

	//transfer to the server
	if (writeToServer(sockfd, read_buffer, length) != length) {
		printf("Error : Failed to transfer all the chars to server!\n");
		return ERROR;
	}

	//obtain the count and print
	ssize_t result;
	uint32_t c_val;
	c_val = sizeof(c_val);
	char* read_from_server_buf = (char*)&c_val;
	int read_from_server = 0;
	while (read_from_server < sizeof(unsigned int)) {
		result = read(sockfd, read_from_server_buf + read_from_server,
				c_val - read_from_server);
		if (result < 0) {
			printf("Failed to read from server %s\n", strerror(errno));
			break;
		}
		read_from_server += result;
	}
//convert to unsigned int

//reference: https://stackoverflow.com/questions/34206446/how-to-convert-string-into-unsigned-int-c
	C = ntohl(c_val);

	printf("# of printable characters: %u\n", C);

//exit gracefully
	free(read_buffer);
	close(sockfd);
	return SUCCESS;

}
