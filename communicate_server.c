#include "communicate.h"

#include <stdio.h>
#include <stdlib.h>
#include <rpc/pmap_clnt.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define NUM_ARTICLES 50
#define ARTICLE_LEN 120


// _________________________________rpc call fcns, for server-server calls___________
#include <memory.h> /* for memset */
static struct timeval TIMEOUT = { 25, 0 };

bool_t *
server_write_1(Article_t Article,  CLIENT *clnt)
{
	static bool_t clnt_res;

	memset((char *)&clnt_res, 0, sizeof(clnt_res));
	if (clnt_call (clnt, Server_write,
		(xdrproc_t) xdr_Article_t, (caddr_t) &Article,
		(xdrproc_t) xdr_bool, (caddr_t) &clnt_res,
		TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&clnt_res);
}

int *
write_1(Article_t Article, int Nw, char *sender_ip, char *sender_port,  CLIENT *clnt)
{
	write_1_argument arg;
	static int clnt_res;

	memset((char *)&clnt_res, 0, sizeof(clnt_res));
	arg.Article = Article;
	arg.Nw = Nw;
	arg.sender_ip = sender_ip;
	arg.sender_port = sender_port;
	if (clnt_call (clnt, Write, (xdrproc_t) xdr_write_1_argument, (caddr_t) &arg,
		(xdrproc_t) xdr_int, (caddr_t) &clnt_res,
		TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&clnt_res);
}

int *
get_seqnum_1(CLIENT *clnt)
{
	static int clnt_res;

	memset((char *)&clnt_res, 0, sizeof(clnt_res));
	 if (clnt_call (clnt, Get_seqnum, (xdrproc_t) xdr_void, (caddr_t) NULL,
		(xdrproc_t) xdr_int, (caddr_t) &clnt_res,
		TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&clnt_res);
}

// _________________________________end of rpc call fcns_____________________________

// state for the server

int server_port;
char *server_port_str;
char *server_ip;

char *mode;
int available_seqnum = 1;
int highest_seqnum = 0;

// array of servers, which will be maloced at initialization
int num_normal_servers;
CLIENT **servers;

struct server_info_t {
	char ip[100];
	char port[10];
};
typedef struct server_info_t server_info_t;

server_info_t *servers_info;

//seperate connection to the primary server, unless we are the primary
CLIENT *primary_server;
bool_t amPrimary = FALSE;

Article_t articles[NUM_ARTICLES];
int next_aval_article_slot = 0;


// setup the rpc connection object:
CLIENT *setup_connection(char *con_server_ip, char *con_server_port) {
	//printf("creating connection with ip %s, port %s\n", con_server_ip, con_server_port);

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

// read servers.txt to setup the connections and primary information
// returns FALSE on failure

bool_t setup_connections() {
	// open the file:

	FILE *fp;

	fp = fopen("servers.txt", "r");
	if (fp == NULL) {
		printf("Failed to setup connections, could not open servers.txt\n");
		return FALSE;
	}

	int max_line_size = 255;
	char linebuf[max_line_size];


	// count the number of lines:

	int num_lines = 0;
	while(fgets(linebuf, sizeof(linebuf), fp)){
		num_lines++;
	}

	rewind(fp);


	// malloc space for connections

	// subtract 1 because primary con is dealt with separately

	num_normal_servers = num_lines - 1;
	servers = malloc(num_normal_servers * sizeof(CLIENT *));
	servers_info = malloc(num_normal_servers * sizeof(server_info_t));

	if (servers == NULL) {
		printf("malloc failed\n");
		fclose(fp);
		return FALSE;
	}


	// get connection information from file:

	bool_t foundPrimary = FALSE;

	for(int i = 0; i < num_lines; i++) {
		// each line contains a server ip and port, and then may contain the word Primary.
		fgets(linebuf, sizeof(linebuf), fp);

		char *con_server_ip = strtok(linebuf, " ");

		char *con_server_port_str = strtok(NULL, " ");
		int con_server_port_int = atoi(con_server_port_str);

		char *con_server_option = strtok(NULL, " ");

		//printf("%s %s", con_server_ip, con_server_port_str);

		// two cases either this is the primary line or it's a normal line
		if ( (con_server_option != NULL) && ((strncmp(con_server_option, "Primary", 7) == 0) || (strncmp(con_server_option, "primary", 7) == 0)) ) {
			foundPrimary = TRUE;

			// two cases, either we are the primary or we must be able to connect to the primary
			if ( (strcmp(server_ip, con_server_ip) == 0) && (server_port == con_server_port_int) ) {
				amPrimary = TRUE;
			} else {
				primary_server = setup_connection(con_server_ip, con_server_port_str);
				if (primary_server == NULL) {
					clnt_pcreateerror (con_server_ip);
					free(servers);
					free(servers_info);
					fclose(fp);
					return FALSE;
				}
			}

		} else {
			// two cases, either we are the normal server or we must be able to connect to the normal server
			if ( (strcmp(server_ip, con_server_ip) == 0) && (server_port == con_server_port_int) ) {
				continue;
			} else {
				servers[i] = setup_connection(con_server_ip, con_server_port_str);
				if (servers[i] == NULL) {
					clnt_pcreateerror (con_server_ip);
				}

				// save info about servers i
				server_info_t server_info;
				strcpy(server_info.ip, con_server_ip);

				// saved server port may have a newline
				strcpy(server_info.port, con_server_port_str);

				servers_info[i] = server_info;

			}
		}

	} 

	// check if file didn't have primary
	if (foundPrimary == FALSE) {
		printf("servers.txt does not have primary server info\n");
		free(servers);
		free(servers_info);
		fclose(fp);
		return FALSE;
	}

	fclose(fp);

	return TRUE;
}

// close rpc connections to servers
bool_t close_connections() {

	for (int i = 0; i < num_normal_servers; i++) {
		clnt_destroy(servers[i]);
	}

	// free space for connection objects
	free(servers);

	// if we are not the primary, close the connection to the primary
	if (amPrimary == FALSE) {
		clnt_destroy(primary_server);
	}
}

// cleanup steps on shutdown
void server_shutdown() {
	if (close_connections() == TRUE) {
		exit(0);
	} else {
		exit(EXIT_FAILURE);
	}
}

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

// initialization fcn, which is called from main in communicate_svc
void initialize(char *_server_ip, char *_server_port_str, char *_mode) {
	// set state to given args

	server_port_str = _server_port_str;
	server_port = atoi(_server_port_str);
	server_ip = _server_ip;

	if (check_mode(_mode) == TRUE){
		mode = _mode;
	} else {
		printf("Invalid mode. Using default mode, which is primary-backup\n");
		mode = "primary-backup";
	}

	// setup the connections and primary information
	if (setup_connections() == FALSE) {
		printf("failed to setup connections to servers\n");
		exit(EXIT_FAILURE);
	}
}

/* _____________ rpc calls made by clients: _____________ */

bool_t *
ping_1_svc(struct svc_req *rqstp)
{
	static bool_t  result;

	result = TRUE;

	printf("Got pinged\n");

	return &result;
}


Page_t *
read_1_svc(int Page_num, int Nr, struct svc_req *rqstp)
{	
	/* for testing purposes
	strcpy(articles[0].text, "top level");
	articles[0].seqnum = 1;
	articles[0].reply_seqnum = -1;
	
	strcpy(articles[1].text, "a reply");
	articles[1].seqnum = 2;
	articles[1].reply_seqnum = 1;
	*/
	static Page_t  result;

	printf("a client read 10 articles from page %d\n", Page_num);

	// we only have 5 pages for 50 articles
	if (Page_num > 5) {
		Page_num = 5;

	} else if (Page_num < 1) {
		Page_num = 1;
	}

	// convert page num to have the first page be page 0
	Page_num--;

	for (int i = 0; i < 10; i++) {
		result.articles[i] = articles[i + (Page_num*10)];
	}


	return &result;
}

char **
get_mode_1_svc(struct svc_req *rqstp)
{
	static char * result;
	
	result = mode;

	return &result;
}

// used only when connecting to a new server and mode is local-write
bool_t *
fetch_articles_1_svc(Written_seqnums_t written_seqnums,  struct svc_req *rqstp)
{
	static bool_t  result;

	/*
	 * insert server code here
	 */

	return &result;
}

// works differently based on mode
int *
write_1_svc(Article_t Article, int Nw, char *sender_ip, char *sender_port,  struct svc_req *rqstp)
{	
	printf("recieved write rpc call\n");
	int  *result_5;
	int  *result_6;
	bool_t  *result_7;

	static int result;

	// get seqnum for article only if the client is the one who has contacted us:
	
	if (Article.seqnum == -1) {
		int seqnum;
		if (amPrimary == TRUE) {
			seqnum = available_seqnum;

			//update state
			highest_seqnum = available_seqnum;
			available_seqnum++;

		} else {

			result_6 = get_seqnum_1(primary_server);

			if (result_6 == (int *) NULL) {
				clnt_perror (primary_server, "call failed");
				result = FALSE;
				return &result;
			}

			seqnum = *result_6;

		}
		Article.seqnum = seqnum;
	}


	if (strcmp(mode, "primary-backup") == 0) {
		
		if (amPrimary == TRUE) {
			printf("contacting normal servers\n");
			// contact normal servers with the article
			for (int i = 0; i < num_normal_servers; i++) {

				//printf("server ip: %s server port: %s given ip: %s given port: %s\n", servers_info[i].ip, servers_info[i].port, sender_ip, sender_port);
				// ensure that servers we are sending too are not the sender server
				if ( (strcmp(servers_info[i].ip, sender_ip) == 0) && (strncmp(servers_info[i].port, sender_port, (strlen(sender_port)-1)) == 0) ) {
					continue;
				}


				result_7 = server_write_1(Article, servers[i]);
				
				// return -1 to signify failure and allow the server to keep the article or allow it to be lost
				// the client should not expect that this article write has been applied
				if (result_7 == (bool_t *) NULL) {
					clnt_perror (servers[i], "call failed");
					result = -1;
					return &result;
				}
			}
			// write article too ourselves:

			// check if article can be written
			if (next_aval_article_slot >= NUM_ARTICLES) {
				result = FALSE;
				return &result;
			}
			// write the article
			strncpy(articles[next_aval_article_slot].text, Article.text, 120);
			articles[next_aval_article_slot].reply_seqnum = Article.reply_seqnum;
			articles[next_aval_article_slot].seqnum = Article.seqnum;
			next_aval_article_slot++;

			result = Article.seqnum;
		} else {
			printf("contacting primary\n");
			// ask primary to contact all servers
			result_5 = write_1(Article, Nw, server_ip, server_port_str, primary_server);

			if (result_5 == (int *) NULL) {
				clnt_perror (primary_server, "call failed");
				result = -1;
				return &result;

			} else if (*result_5 == -1) {
				result = -1;
				return &result;

			} else {
				// write article too ourselves and return:

				// check if article can be written
				if (next_aval_article_slot >= NUM_ARTICLES) {
					result = FALSE;
					return &result;
				}

				// write the article
				strncpy(articles[next_aval_article_slot].text, Article.text, 120);
				articles[next_aval_article_slot].reply_seqnum = Article.reply_seqnum;
				articles[next_aval_article_slot].seqnum = Article.seqnum;

				next_aval_article_slot++;

				result = *result_5;
			}
		}

	} else if (strcmp(mode, "quorum") == 0) {
		/*
		if (amPrimary == TRUE) {
			printf("contacting nW servers\n");
			
			// contact Nw normal servers with the article
			
			int contacted_nums[Nw];
			memset(contacted_nums, -1, sizeof(contacted_nums));
			int contacted_nums_aval_slot = 0;

			int random_num;

			for (int i = 0; i < Nw; i++) {
				
				// get a random number to determine which server to contact
				
				while (TRUE) {
					int upper = num_normal_servers;
					int lower = 0;
					random_num = (rand() % (upper - lower + 1)) + lower;

					int found = FALSE;
					for (int i = 0; i < Nw; i++) {
						if (contacted_nums[i] == random_num) {
							found = TRUE;
							break;
						}
					}

					if (found == FALSE) {
						break;
					}

				}

				// save the random number
				contacted_nums[contacted_nums_aval_slot] = random_num;
				contacted_nums_aval_slot++;

				struct svc_req *rqstp


				result_7 = server_write_1(Article, servers[random_num]);
				
				// return -1 to signify failure and allow the server to keep the article or allow it to be lost
				// the client should not expect that this article write has been applied
				if (result_7 == (bool_t *) NULL) {
					clnt_perror (servers[i], "call failed");
					result = -1;
					return &result;
				}
			}

		} else {
			printf("contacting primary\n");
			// ask primary to contact Nw servers
			result_5 = write_1(Article, Nr, Nw, primary_server);

			if (result_5 == (int *) NULL) {
				clnt_perror (primary_server, "call failed");
				result = -1;
				return &result;

			} else if (*result_5 == -1) {
				result = -1;
				return &result;

			} else {
				result = *result_5;
			}
		}
	*/
	} else if (strcmp(mode, "local-write") == 0) {
	/*
		printf("contacting normal servers\n");
		// contact all servers with the article
		for (int i = 0; i < num_normal_servers; i++) {
		
			struct svc_req *rqstp
			result_7 = server_write_1(Article, servers[i]);
			
			// return -1 to signify failure and allow the server to keep the article or allow it to be lost
			// the client should not expect that this article write has been applied
			if (result_7 == (bool_t *) NULL) {
				clnt_perror (servers[i], "call failed");
				result = -1;
				return &result;
			}
		}
	*/
	} else {
		printf("Server's mode is invalid\n");
		result = -1;
	}

	return &result;
}


/* _____________ server rpc calls made between servers: _____________ */

// only the primary can give out seqnums for new articles
int *
get_seqnum_1_svc(struct svc_req *rqstp)
{
	static int result;

	// make sure that we are the primary
	if (amPrimary == FALSE) {
		result = -1;
		return &result;
	}

	result = available_seqnum;

	//update state
	highest_seqnum = available_seqnum;
	available_seqnum++;

	return &result;
}


bool_t *
server_write_1_svc(Article_t Article,  struct svc_req *rqstp)
{
	static bool_t  result;

	// check if article can be written
	if (next_aval_article_slot >= NUM_ARTICLES) {
		result = FALSE;
		return &result;
	}

	// write the article
	strncpy(articles[next_aval_article_slot].text, Article.text, 120);
	articles[next_aval_article_slot].reply_seqnum = Article.reply_seqnum;
	articles[next_aval_article_slot].seqnum = Article.seqnum;

	next_aval_article_slot++;

	result = TRUE;

	return &result;
}

// only the primary should be asked this during a quorum sync
int *
highest_seqnum_1_svc(struct svc_req *rqstp)
{
	static int  result;

	// zero means the zero has no articles
	// also all empty articles have been initialized to a seqnum of 0 by C on declaration.
	int highest_seqnum = 0;

	for (int i = 0; i < NUM_ARTICLES; i++) {
		Article_t cur = articles[i];
		
		if (cur.seqnum > highest_seqnum) {
			highest_seqnum = cur.seqnum;
		}
	}

	result = highest_seqnum;

	return &result;
}

// only used during quorum sync 
Article_t *
choose_1_svc(int Seqnum,  struct svc_req *rqstp)
{
	static Article_t  result;

	// find the article:

	bool_t found = FALSE;

	for (int i = 0; i < NUM_ARTICLES; i++) {
		Article_t cur = articles[i];
		
		if (cur.seqnum == Seqnum) {
			
			//fill in the result article
			strncpy(result.text, cur.text, 120);
			result.reply_seqnum = cur.reply_seqnum;
			result.seqnum = cur.seqnum;

			found = TRUE;
			break;
		}
	}

	// check if article wasn't found
	if (found == FALSE) {
		// fill in the the invalid result article

		char empty_text[120];
		memset(empty_text, 0, 120);

		strncpy(result.text, empty_text, 120);
		result.reply_seqnum = -1;
		result.seqnum = -1;

	}

	return &result;
}
