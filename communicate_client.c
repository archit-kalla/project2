#include "communicate.h"

#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>

// rpc connection object
CLIENT *clnt;

// consistency and operation mode
char *mode;
int Nr;
int Nw;

// page buf for use with read
Page_t pagebuf;

// used for keeping track of written seqnums
Written_seqnums_t written_seqnums;
int latest_written_seqnum; // keeps track of highest index of written_seqnums


// returns TRUE if mode is valid
bool_t check_mode(char *mode) {
	if (strcmp(mode, "primary-backup") == 0 
		|| strcmp(mode, "quorum") == 0 
		|| strcmp(mode, "local-write") == 0) {

		return TRUE;
	} else {
		return FALSE;
	}
}

// setup the rpc connection object:
CLIENT *setup_connection(char *con_server_ip, char *con_server_port) {

	// create this servers socket 
	// note, this server acts as a client to other servers
	// and has client connection "objects" for them
	int sockfd;
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("socket creation failed");
		return NULL;
	}
	
	// fill in information about the given server
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	   
	servaddr.sin_family = AF_INET;
	int server_port_int = atoi(con_server_port);
	servaddr.sin_port = htons(server_port_int);

	//fill in the IP address as it's binary representation
	if (inet_pton(AF_INET, con_server_ip, &servaddr.sin_addr) <= 0) {
		printf("Invalid IP address\n");
		close(sockfd);
		return NULL;
	}

	//setup the timeout for rpc calls
	struct timeval wait;
	memset(&wait, 0, sizeof(wait));
	wait.tv_sec = 2;

	// create the object
	CLIENT *clnt = clntudp_create (&servaddr, COMMUNICATE, COMMUNICATE_VERSION, wait, &sockfd);
	if (clnt == NULL) {
		clnt_pcreateerror (con_server_ip);
		return NULL;
	}

	return clnt;
}

void print_page(Page_t page) {
	//print the first page as long as it exists
	if (page.articles[0].seqnum != 0)
		printf("seq: %d, reply seq: %d, text: %-60s\n", page.articles[0].seqnum, page.articles[0].reply_seqnum, page.articles[0].text);
	
	//max indent is 9*2 spaces
	char indent[19];
	
	// setup indents for each possible article (10 in the page)
	int indentCnt[10];
	for (int i = 0; i < (sizeof(indentCnt) / sizeof(int)); i++){
		indentCnt[i] = 0;
	}

	// print each article
	for (int i=1; i < 10; i++){
		Article_t article = page.articles[i];
		
		// skip empty articles
		if (article.seqnum == 0)
			continue;

		// setup the article indent
		// note, the higher level article must've been posted before the reply
		for (int j=0; j < i; j++){
			Article_t cur = page.articles[j];

			// skip empty articles
			if (cur.seqnum == 0)
				continue;

			if (cur.seqnum == article.reply_seqnum) {
				indentCnt[i] = indentCnt[j] + 1;
				break;
			}
		}

		
		// for (int i = 0; i < (sizeof(indentCnt) / sizeof(int)); i++){
		// 	printf("indent at i %d is %d\n", i, indentCnt[i]);
		// }

		//setup the indent
		for (int j = 0; j < sizeof(indent); j++){
			if (j >= (indentCnt[i] * 2)){
				indent[j] = '\0';
				break;
			} else {
				indent[j] = ' ';
			}
		}
		
		printf("%s", indent);
		printf("seq: %d, reply seq: %d, text: %-60s\n", article.seqnum, article.reply_seqnum, article.text);
	}
}

/*
void print_page(Page_t page){
	if (page.articles[0].seqnum != 0)
		printf("seq: %d, reply seq: %d, text: %-60s\n", page.articles[0].seqnum, page.articles[0].reply_seqnum, page.articles[0].text);

	for(int i = 1; i < 10; i++){

	}
}

void print_page_recursion(Page_t page, int article, int indentation){
	Article_t article = page.articles[article];

	//max indent is 9*2 spaces
	char indent[19];

	if(identation < 18){
		indentation = 18;
	}

	int j = 0;
	for (; j < indentation; j++){
		indent[j] = 
	}

	printf("seq: %d, reply seq: %d, text: %-60s\n", article.seqnum, article.reply_seqnum, article.text);
}

*/


void cmd_loop() {
	int usrBuf_len = 200;
	char usrBuf[usrBuf_len];

	// setup rpc args and vars.

	bool_t  *result_1;
	Page_t  *result_2;
	int read_1_Page_num;
	int read_1_Nr;
	char * *result_3;
	bool_t  *result_4;
	Written_seqnums_t  fetch_articles_1_written_seqnums;
	int  *result_5;
	Article_t write_1_Article;
	int write_1_Nw;
	char *write_1_sender_ip;
	char *write_1_sender_port;
	int  *result_6;
	bool_t  *result_7;
	Article_t server_write_1_Article;
	int  *result_8;
	Article_t  *result_9;
	int choose_1_Seqnum;

	//cmd loop

	while(1) {
   		
		//allow the client to provide a command
		
		printf("> ");


		//fill in the usrBuf with the command and command args
		memset(usrBuf, 0, usrBuf_len);
		fgets(usrBuf, usrBuf_len, stdin);
		//reply newline with null terminator
		for (int i = 0; i < usrBuf_len; i++) {
			if (usrBuf[i] == '\n') {
				usrBuf[i] = '\0';
				break;
			}
		}

		//printf("usrbuf: %s", usrBuf);

		//get the first token, which is the command
		char *token = strtok(usrBuf, " ");
		if (token == (char *)NULL) {
			//command is either ping or exit
			token = usrBuf;
		}

		// execute the needed rpc command

		if (strcmp(token, "exit") == 0){
			return;

		} else if (strcmp(token, "ping") == 0){
			// rpc call
			result_1 = ping_1(clnt);
			
			if (result_1 == (bool_t *) NULL) {
				clnt_perror (clnt, "call failed");
			} else if (*result_1 == FALSE) { 
				printf("ping failed\n");
			} else {
				printf("pinged successfully\n");
			}

		} else if (strcmp(token, "post") == 0){
			//bug just get whole text

			char *text = strtok(NULL, "");
			if (text == NULL) {
				printf("post failed, no article was provided\n");
				continue;
			}

			// create article
			write_1_Article.reply_seqnum = 0;
			write_1_Article.seqnum = -1;
			strncpy(write_1_Article.text, (text), 120);   				

			// post rpc based on mode

			write_1_Nw = Nw;

			//client ip and port is not neccessary, it's mainly used by server-server rpcs
			write_1_sender_ip = "unknown";
			write_1_sender_port = "unknown";

			result_5 = write_1(write_1_Article, write_1_Nw, write_1_sender_ip, write_1_sender_port, clnt); 


			// display rpc result

			if (result_5 == (int *) NULL) {
				clnt_perror (clnt, "call failed");
			} else if (*result_5 == -1) { 
				printf("post failed\n");
			} else {
				printf("posted successfully\n");
			}
			written_seqnums.seqnums[latest_written_seqnum] = *result_5;
			latest_written_seqnum++;
			written_seqnums.num_seqnums = latest_written_seqnum;
		} else if (strcmp(token, "read") == 0){
			// get arg from cmd

			char *read_1_Page_num_str = strtok(NULL, " ");

			if (read_1_Page_num_str == NULL) {
				printf("read failed, no page number was provided\n");
				continue;
			}

			read_1_Page_num = atoi(read_1_Page_num_str);

			// rpc call

			read_1_Nr = Nr;

			result_2 = read_1(read_1_Page_num, read_1_Nr, clnt);

			if (result_2 == (Page_t *) NULL) {
				clnt_perror (clnt, "call failed");
			} else {
				print_page(*result_2);
				pagebuf = *result_2;
			}


		} else if (strcmp(token, "reply") == 0){
			char *reply_seqnum_str = strtok(NULL, " ");
			char *text = strtok(NULL, "");

			if ( (reply_seqnum_str == NULL) || (text == NULL) ) {
				printf("reply failed, no seqnum was provided, or no text was provided\n");
				continue;
			}


			// create article
			write_1_Article.reply_seqnum = atoi(reply_seqnum_str);
			write_1_Article.seqnum = -1;
			strncpy(write_1_Article.text, (text), 120);

			// post rpc based on mode

			write_1_Nw = Nw;

			//client ip and port is not neccessary, it's mainly used by server-server rpcs
			write_1_sender_ip = "unknown";
			write_1_sender_port = "unknown";

			result_5 = write_1(write_1_Article, write_1_Nw, write_1_sender_ip, write_1_sender_port, clnt);



			// display rpc result

			if (result_5 == (int *) NULL) {
				clnt_perror (clnt, "call failed");
			} else if (*result_5 == -1) { 
				printf("post failed\n");
			} else {
				printf("posted successfully\n");
			}

		} else if (strcmp(token, "change_mode") == 0){
			
			char *change_mode_str = strtok(NULL, " ");

			if ( (change_mode_str == NULL) || (check_mode(change_mode_str) == FALSE) ){

				printf("change_mode failed, no mode was provided or the mode was invalid\n");
				continue;
			}

			mode = change_mode_str;

		} else if (strcmp(token, "get_mode") == 0){
			
			result_3 = get_mode_1(clnt);
			
			if (result_3 == (char **) NULL) {
				clnt_perror (clnt, "call failed");

			} else {
				printf("server mode: %s\n", *result_3);
			}

		} else if (strcmp(token, "connect") == 0){
			char *con_server_ip = strtok(NULL, " ");
			char *con_server_port = strtok(NULL, " ");

			if ( (con_server_ip == NULL) || (con_server_port == NULL) ){
				printf("connect failed, the ip and port were not provided\n");
				continue;
			}

			clnt_destroy(clnt);
			clnt = setup_connection(con_server_ip, con_server_port);
			if (clnt == NULL) {
				printf("Failed to setup the new connection\n");
				return;
			}
			fetch_articles_1(written_seqnums, clnt);		//local_write only

		}  else if (strcmp(token, "choose") == 0){
			char *seqnum_str = strtok(NULL, " ");

			if ( seqnum_str == NULL ){
				printf("choose failed, the provided seqnum was invalid\n");
				continue;
			}

			int seqnum = atoi(seqnum_str);
			
			bool_t found = FALSE;
			// find the seqnum in the page
			// page always has 10 articles even if they are all empty. Empty articles have an invalid seqnum of 0.
			for (int i = 0; i < 10; i++) {
				if (pagebuf.articles[i].seqnum == seqnum) {
					// we found the article
					printf("Article: %s\n", pagebuf.articles[i].text);
					found = TRUE;
					break;
				}
			}
			if (found == FALSE) {
				printf("Article not found\n");
			}
		}else {
			printf("command not found\n");
		}

	}
}


int
main (int argc, char *argv[])
{	
	/* testing for print page
	Page_t test_page;
	memset((void*)&test_page, 0, sizeof(test_page));

	test_page.articles[0].seqnum = 1;
	strcpy(test_page.articles[0].text, "test article");
	
	test_page.articles[1].seqnum = 2;
	test_page.articles[1].reply_seqnum = 1;
	strcpy(test_page.articles[1].text, "test article reply to 1");

	test_page.articles[2].seqnum = 3;
	test_page.articles[2].reply_seqnum = 2;
	strcpy(test_page.articles[2].text, "test article reply to 2");

	test_page.articles[5].seqnum = 6;
	test_page.articles[5].reply_seqnum = 1;
	strcpy(test_page.articles[5].text, "test article reply to 1");

	test_page.articles[3].seqnum = 4;
	strcpy(test_page.articles[3].text, "another test article");

	test_page.articles[4].seqnum = 5;
	test_page.articles[4].reply_seqnum = 4;
	strcpy(test_page.articles[4].text, "test article reply to 4");

	print_page(test_page);
	exit(EXIT_SUCCESS); */

	// get information about server to which the client should connect too initially,
	// and get the mode through cmd args

	if (argc < 6) {
		printf ("usage: %s server_ip server_port mode Nr Nw\n\
mode is one of primary-backup, quorum, or local-write\n\
Nr and Nw are used for the quorum mode and are ignored otherwise\n", argv[0]);
		exit (1);
	}

	char *con_server_ip = argv[1];
	char *con_server_port_str = argv[2];

	char *_mode = argv[3];

	if (check_mode(_mode) == TRUE){
		mode = _mode;
	} else {
		printf("Invalid mode. Using default mode, which is primary-backup\n");
		mode = "primary-backup";
	}

	Nr = atoi(argv[4]);
	Nw = atoi(argv[5]);

	// setup the rpc connection object:
	clnt = setup_connection(con_server_ip, con_server_port_str);
	if (clnt == NULL) {
		clnt_pcreateerror (con_server_ip);
		exit(EXIT_FAILURE);
	}

	// print usage explanation
	printf("Enter any of the following commands: \n\
  exit or ctrl-C\n\
  ping                            - pings the server\n\
  post Article_text               - post an article to the servers\n\
  read page_num                   - read snippets of articles on the given page\n\
  reply reply_seqnum Article_text - post an article that is replying to an article with a specific seqnum\n\
  connect server_ip server_port   - change the connection to the given server, see servers.txt for available server information\n\
  get_mode                        - get the servers mode\n\
  change_mode mode_str            - changes ONLY the client's mode. mode is one of primary-backup, quorum, local-write.\n\
                                    The modes provide sequential, qourum, and read-your-write consistency respectively.\n\n");

	// go into cmd loop
	cmd_loop();

	//clean up steps
	clnt_destroy(clnt);
	exit(0);
}
