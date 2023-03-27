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
#include <sys/queue.h>

#define NUM_ARTICLES 50
#define ARTICLE_LEN 120


// _________________________________rpc call fcns, for server-server calls___________
#include <memory.h> /* for memset */
static struct timeval TIMEOUT = { 25, 0 };
Article_t *
choose_1(int Seqnum,  CLIENT *clnt)
{
	static Article_t clnt_res;

	memset((char *)&clnt_res, 0, sizeof(clnt_res));
	if (clnt_call (clnt, Choose,
		(xdrproc_t) xdr_int, (caddr_t) &Seqnum,
		(xdrproc_t) xdr_Article_t, (caddr_t) &clnt_res,
		TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&clnt_res);
}

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

// this works for server-server or client-server
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

Page_t *
read_1(int Page_num, int Nr, CLIENT *clnt)
{
	read_1_argument arg;
	static Page_t clnt_res;

	memset((char *)&clnt_res, 0, sizeof(clnt_res));
	arg.Page_num = Page_num;
	arg.Nr = Nr;
	if (clnt_call (clnt, Read, (xdrproc_t) xdr_read_1_argument, (caddr_t) &arg,
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

int*
highest_seqnum_1(CLIENT *clnt)
{
	static int clnt_res;

	memset((char *)&clnt_res, 0, sizeof(clnt_res));
	 if (clnt_call (clnt, Highest_seqnum, (xdrproc_t) xdr_void, (caddr_t) NULL,
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

// QUEUE DEFINITIONS
// pthread_cond_t condvar = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; // prevent race condition to access queue
pthread_mutex_t quorum_normal_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t quorum_primary_lock = PTHREAD_MUTEX_INITIALIZER;
STAILQ_HEAD(stailhead, article_queue_entry);
struct stailhead head;

// debug, see all articles:
void print_articles() {
	for (int i = 0; i < NUM_ARTICLES; i++) {
		if (articles[i].seqnum != 0)
			printf("seq: %d, reply seq: %d, text: %-60s\n", articles[i].seqnum, articles[i].reply_seqnum, articles[i].text);
	}

	return;
}

// queue reader thread
void *read_article_send_queue(void* x){
	while(1){ // busy wait for queue to have an entry
		//sleep here for x seconds
		bool_t queue_empty = FALSE;
		pthread_mutex_lock(&lock);
		if(STAILQ_EMPTY(&head) == 0){
			queue_empty = FALSE;
		}
		else{
			queue_empty = TRUE;
		}
		pthread_mutex_unlock(&lock);

		if(queue_empty == FALSE){
			//get head of queue
			struct article_queue_entry *entry;
			pthread_mutex_lock(&lock);
			entry = STAILQ_FIRST(&head);
			STAILQ_REMOVE_HEAD(&head, entries);
			printf("removed entry from queue\n");
			pthread_mutex_unlock(&lock);
			printf("article: %s\n", entry->article.text);
			//send article to primary server if we are not the primary
			if (amPrimary==FALSE){
				bool_t *result = server_write_1(entry->article, primary_server);
				printf("wrote to primary server\n");
				if (result == (bool_t *) NULL) {
					printf("writing server call failed\n");
				}
				else if (result == FALSE) {
					printf("writing server call failed maximum articles reached on server\n");
				}
			}
			

			//send articles to all servers
			if ((num_normal_servers>1) || amPrimary==TRUE){ // if there is 1 normal server and we are not primary, then we don't need to send to anyone
				for (int i = 0; i < num_normal_servers; i++) {
					if ( (strcmp(servers_info[i].ip, server_ip) == 0) && (strcmp(servers_info[i].port, server_port_str) == 0) || ((strcmp(servers_info[i].ip, "") == 0) && (strcmp(servers_info[i].port, "") == 0)) ) {
						printf("skipping self\n");
						continue;
					}
					printf("sending to server %s:%s\n", servers_info[i].ip, servers_info[i].port);
					printf("article to write: %s\n", entry->article.text);
					bool_t *result = server_write_1(entry->article, servers[i]);
					printf("wrote to normal server\n");
					if (result == FALSE) {
						printf("writing server call failed\n");
					}
				}
			}
			
		}
	}

}

//called on a timer every 60 seconds
void* quorum_sync() {
	//for each other server
	//if they have a higher seqnum and seqnums that we don't have in articles, then ask for missing articles using choose
	
	while(1){
		sleep(60);

		// Finds how many articles are there in total. Bit much to compute,
		// so just set to NUM_ARTICLES
		int primary_seqnum = NUM_ARTICLES;
		printf("Syncing\n");
		pthread_mutex_lock(&quorum_normal_lock);
		// Checks which articles this server needs.
		bool_t seqnums_needed[primary_seqnum];
		for(int i = 0; i < primary_seqnum; i++){
			seqnums_needed[i] = TRUE;

			for(int j = 0; j < primary_seqnum; j++){
				if(articles[j].seqnum == (i+1)){
					seqnums_needed[i] = FALSE;
					break;
				}
			}
		}

		Article_t article;
		int server_seqnum;

		// Finds articles the server needs
		if(amPrimary == FALSE){
			for(int i = 0; i < primary_seqnum; i++){
				if(next_aval_article_slot < NUM_ARTICLES && seqnums_needed[i]){
					article = *choose_1(i+1, primary_server);
					if(article.seqnum == (i+1)){
						// write the article
						strncpy(articles[next_aval_article_slot].text, article.text, 120);
						articles[next_aval_article_slot].reply_seqnum = article.reply_seqnum;
						articles[next_aval_article_slot].seqnum = article.seqnum;

						next_aval_article_slot++;
						seqnums_needed[i] = FALSE;
					}
				}
			}
		}

		//Obtaining from normal servers
		for(int i = 0; i < primary_seqnum; i++){
			if(next_aval_article_slot < NUM_ARTICLES && seqnums_needed[i]){
				for(int j = 0; j < num_normal_servers; j++){
					if(strcmp("", servers_info[j].ip) != 0 && strncmp("", servers_info[j].port, 4) != 0){
						//Taking an article
						article = *choose_1(i+1, servers[j]);
						if(article.seqnum == (i+1)){
							// write the article
							strncpy(articles[next_aval_article_slot].text, article.text, 120);
							articles[next_aval_article_slot].reply_seqnum = article.reply_seqnum;
							articles[next_aval_article_slot].seqnum = article.seqnum;

							next_aval_article_slot++;
							seqnums_needed[i] = FALSE;
						}
					}
				}
			}
		}
		highest_seqnum = primary_seqnum;
		pthread_mutex_unlock(&quorum_normal_lock);

	}
	
}

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

	// init thread
	pthread_t thread_id;
	pthread_create(&thread_id, NULL, &read_article_send_queue, NULL);

	//initialize queue
	
	STAILQ_INIT(&head);	
	if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("\n mutex init has failed\n");
        exit(EXIT_FAILURE);
    }

	if(strcmp(mode, "quorum") == 0){
		pthread_t sync_thread_id;
		pthread_create(&sync_thread_id, NULL, &quorum_sync, NULL);
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

// fcn declaration
Article_t *get_article(int Seqnum);

Page_t *
read_1_svc(int Page_num, int Nr, struct svc_req *rqstp)
{	

	static Page_t page;
	memset(&page, 0, sizeof(page));
	static Page_t *result;
	result = &page;
	Article_t  *result_9;

	if (strcmp(mode, "quorum") != 0) {

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
			result->articles[i] = articles[i + (Page_num*10)];
		}

		return result;

	} else { // mode is quorum

		// we only have 5 pages for 50 articles
		if (Page_num > 5) {
			Page_num = 5;
		} else if (Page_num < 1) {
			Page_num = 1;
		}

		// convert page num to have the first page be page 0
		Page_num--;

		// get the page num and associated seqnums
		// 10 articles per page
		int needed_seqnums[10];
		for (int i = 0; i < 10; i++) {
			needed_seqnums[i] = i + (Page_num*10) + 1;
		}

		memset(result, 0, sizeof(*result));

		// do Nr reads for each article in the page, since the article type is what our consistency is based on
		for (int i = 0; i < 10; i++) {
			int cur_Nr = Nr;
			int needed_seqnum = needed_seqnums[i];
			
			// a normal server asks the primary to contact Nw servers, based on the Nw the client asked for.
			// so similarily to before, a normal server forwards this rpc call from the client, by making this rpc call to the server
			// to make this happen we have two cases primary or not primary as before. 
			// in a qourum, each server has a timer interupt, which causes it to contact other servers and ask for missing articles. It can know if it has missing articles
			// by asking for the highest seqnum at the server using get_seqnum. and it can request missing articles using choose. All of this is done by using the rpc object at servers[i].
			// choose is only done by server-server rpcs, to get an article with a specific seqnum.
			// A client requests pages and it can choose from the page. 
			// contact Nr normal servers with the article
			// from our servers objects, we want to pick Nr amount of them and server_write to them, but we don't want to pick the same one twice
			// so we save that in contacted_nums of servers

			// contact ourselves

			Article_t *cur = NULL;
			cur = get_article(needed_seqnum);
			
			if (cur->seqnum != -1) {
				result->articles[i] = *cur;
				continue;
			} 
			cur_Nr--;

			if (cur_Nr == 0) {
				continue;
			}

			// try to contact primary

			if (amPrimary == FALSE) {
				result_9 = choose_1(needed_seqnum, primary_server);
				if (result_9 == (Article_t *) NULL) {
					clnt_perror (primary_server, "call failed");
				} else if (result_9->seqnum != -1){
					result->articles[i] = *cur;
					continue;
				} else {
					cur_Nr--;
				}
			}
			
			if (cur_Nr == 0) {
				continue;
			}

			// we already contacted ourselves and primary (Nr = Nr-2 if we and the primary are different)
			printf("contacting remote servers to get article\n");

				
			int contacted_nums[cur_Nr];
			//memset(contacted_nums, -1, sizeof(contacted_nums));
			int servers_left = cur_Nr;
			int contacted_nums_aval_slot = 0;
			int server_to_contact;
			int random_num;
			int f = 0;
			int failed_servers_num = 0;
			int server_nums[num_normal_servers];
			int failed_servers[num_normal_servers];

			// Create list of servers to contact
			for(int i = 0; i < num_normal_servers; i++){
				server_nums[i] = i;
			}

			while( !(servers_left == 0 || f == num_normal_servers) ){
				// Get random server to contact
				random_num = (rand() % (num_normal_servers-f));
				server_to_contact = server_nums[random_num];
				f++;
				// Remove the random server from available servers.
				for(int i = random_num; i < num_normal_servers - f; i++){
					server_nums[i] = server_nums[i+1];
				}

				if((strcmp(servers_info[server_to_contact].ip, server_ip) == 0) && (strncmp(servers_info[server_to_contact].port, server_port_str, 4) == 0) ) {
					//printf("Not contacting a server\n");
					//failed_servers[failed_servers_num] = server_to_contact;
					//failed_servers_num++;
					continue;
				}

				//contact the server we picked randomly

				result_9 = choose_1(needed_seqnum, servers[server_to_contact]);
				if (result_9 == (Article_t *) NULL) {
					//clnt_perror (servers[server_to_contact], "call failed");
					failed_servers[failed_servers_num] = server_to_contact;
					failed_servers_num++;
				}
				else{
					contacted_nums[contacted_nums_aval_slot] = server_to_contact;
					// success case, save the article
					result->articles[i] = *result_9;
					servers_left--;
					contacted_nums_aval_slot++;
					printf("Contacted %d servers\n", f-failed_servers_num);
					if(failed_servers_num > 0){
						printf("Failed to contact %d servers\nFailed servers:\n", failed_servers_num);
					}
					for(int i = 0; i < failed_servers_num; i++){
						printf("\t%s", servers_info[failed_servers[i]].ip);
					}
					break; // Does the server need to contact any more servers?
				}
			}
		
		} // end of for loop, primary has either contacted enough servers or failed to contact enough servers.
		return result;

	} // end of read for a qourum

	// never reached since both quorum and non quorum modes return their result above

	// in the case of a qourum, contact primary (unless we are the primary) and ask it to contact Nr normal servers
	// This contacting works the same way as in the write, where we keep picking servers too contact, ie copy and paste primary and non primary case
	// the difference is that instead of writing we are trying to get 10 lowest number pages in the page, because this way we would be seeing all of the data
	// we want the most up to date data, so we contact the other servers to get the articles in between
	// To do this instead of doing a server write to Nw, we do multiple choose calls too each server to get the articles belonging to the page
	// It's fine if the choose fails, since not all servers are expected to have all articles. An article will be at Nw servers.

	// the data item which got Nw writes is the article, so it's also the item we must do Nr choose calls
	// but we are doing this multiple times to get a whole up to date page

	// for each article in the page
	// 	do Nr choose(seqnum) calls, with the servers being selected randomly
	//  copy pasted code for writing goes here

	// page 0 has seqnums 1- 10, page 1 has seqnums 11- 20, ...

	return result;
}

char **
get_mode_1_svc(struct svc_req *rqstp)
{
	static char * result;
	
	result = mode;

	return &result;
}

// used only when connecting to a new server and mode is local-write

// when a client connects to a new server for local write, it calls fetch with it's saved seqnums, so that the server can retrieve those from the other servers
// read your writes consistency
bool_t *
fetch_articles_1_svc(Written_seqnums_t written_seqnums,  struct svc_req *rqstp)
{
	static bool_t result;
	result = FALSE;
	if (strcmp(mode, "local-write") == 0){
		printf("received fetch rpc call\n");
		int seqnums_needed[NUM_ARTICLES];
		int num_seqnums_needed = 0;
		//check if we have the articles that the client is asking for
		for (int i = 0; i < written_seqnums.num_seqnums; i++) {
			int seqnum = written_seqnums.seqnums[i];
			// check if we have the article with that seqnum
			if (seqnum == 0){
				continue;
			}
			for (int j = 0; j < NUM_ARTICLES; j++) {
				if (articles[j].seqnum == seqnum) {
					// we have the article, so we don't need to fetch it
					break;
				}
				// we don't have the article, so we need to fetch it
				seqnums_needed[num_seqnums_needed] = seqnum;
				num_seqnums_needed++;
			}
		}
		// if we don't have any articles, we don't need to fetch anything
		if (num_seqnums_needed == 0) {
			result = TRUE;
			return &result;
		}
		// we need to fetch articles from the other servers
		// get the articles any other servers that we are connected to
		Article_t* returned_article;
		int num_acquired = 0;
		for (int i = 0; i<num_seqnums_needed; i++) {
			// for every article needed, contact every server
			//if we arent the primary, contact the primary first
			if (amPrimary==FALSE){
				returned_article = choose_1(seqnums_needed[i], primary_server); // choose returns an article with invalid seqnum if it fails
				if (returned_article->seqnum == seqnums_needed[i]) {
					// we got the article, so we don't need to contact any more servers
					//write the article to our local storage
					// check if article can be written
					if (next_aval_article_slot >= NUM_ARTICLES) {
						result = FALSE;
						return &result;
					}
					strncpy(articles[next_aval_article_slot].text, returned_article->text, 120);
					articles[next_aval_article_slot].reply_seqnum = returned_article->reply_seqnum;
					articles[next_aval_article_slot].seqnum = returned_article->seqnum;
					next_aval_article_slot++;
					num_acquired++;
					continue;
				}
			}
			//check rest of the servers
			//according to local-write this server then becomes the primary so we should send to everyone
			for (int j = 0; j< num_normal_servers; j++) {
				if ( (strcmp(servers_info[j].ip, server_ip) == 0) && (strcmp(servers_info[j].port, server_port_str) == 0)  || (strcmp(servers_info[i].ip, "") == 0) && (strcmp(servers_info[i].port, "") == 0)) { // we dont want to contact ourselves
					continue;
				}
				returned_article = choose_1(seqnums_needed[i], servers[j]);
				if (returned_article->seqnum == seqnums_needed[i]) {
					// we got the article, so we don't need to contact any more servers
					//write the article to our local storage
					// check if article can be written
					if (next_aval_article_slot >= NUM_ARTICLES) {
						result = FALSE;
						return &result;
					}
					// primary also writes the article to itself, this way it doesn't call server_write on itself
					strncpy(articles[next_aval_article_slot].text, returned_article->text, 120);
					articles[next_aval_article_slot].reply_seqnum = returned_article->reply_seqnum;
					articles[next_aval_article_slot].seqnum = returned_article->seqnum;
					next_aval_article_slot++;
					num_acquired++;
					break;
				}
			}
			result = FALSE; // if we get here that means we did not find a result 
			return &result;
		}
		result = TRUE; // found all seq nums and wrote them, return true
	}
	return &result;
}

// works differently based on mode
// can be called either client to server, or server to server
//returns a seqnum of the written article
int *
write_1_svc(Article_t Article, int Nw, char *sender_ip, char *sender_port,  struct svc_req *rqstp)
{	
	printf("received write rpc call\n");
	int  *result_5;
	int  *result_6;
	bool_t  *result_7;

	static int result;

	// get seqnum for article only if the client is the one who has contacted us:
	
	// seqnum is not set on a client-server rpc, but it is set on server-server rpcs
	// if it's set, no need to get a seqnum
	if (Article.seqnum == -1) {
		int seqnum;
		if (amPrimary == TRUE) {
			// THIS is the primary, give out a seqnum
			seqnum = available_seqnum;

			//update state
			highest_seqnum = available_seqnum;
			available_seqnum++;

		} else {
			// normal server gets seqnum from primary
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

	// writes happen differently based on mode
	if (strcmp(mode, "primary-backup") == 0) {
		
		//primary contacts everyone regardless of who client or server, started the rpc
		if (amPrimary == TRUE) {
			printf("contacting normal servers\n");
			// contact normal servers with the article
			for (int i = 0; i < num_normal_servers; i++) {

				//printf("server ip: %s server port: %s given ip: %s given port: %s\n", servers_info[i].ip, servers_info[i].port, sender_ip, sender_port);
				// ensure that servers we are sending too are not the sender server
				// lets say this is a server-server rpc, then don't write to the sending server, because it already knows that it needs to write
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
			// primary also writes the article to itself, this way it doesn't call server_write on itself
			strncpy(articles[next_aval_article_slot].text, Article.text, 120);
			articles[next_aval_article_slot].reply_seqnum = Article.reply_seqnum;
			articles[next_aval_article_slot].seqnum = Article.seqnum;
			next_aval_article_slot++;

			result = Article.seqnum;
		} else {
			// we got the rpc, and we are not the primary

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
				// write article too ourselves and return, since we didn't call server_write on ourselves:

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
		// a normal server asks the primary to contact Nw servers, based on the Nw the client asked for.
		// so similarily to before, a normal server forwards this rpc call from the client, by making this rpc call to the server
		// to make this happen we have two cases primary or not primary as before. 

		// in a qourum, each server has a timer interupt, which causes it to contact other servers and ask for missing articles. It can know if it has missing articles
		// by asking for the highest seqnum at the server using get_seqnum. and it can request missing articles using choose. All of this is done by using the rpc object at servers[i].
		// choose is only done by server-server rpcs, to get an article with a specific seqnum.
		// A client requests pages and it can choose from the page. 
		

		if (amPrimary == TRUE) {
			
			// contact Nw normal servers with the article
			
			// from our servers objects, we want to pick Nw amount of them and server_write to them, but we don't want to pick the same one twice
			// so we save that in contacted_nums of servers

			int contacted_nums[Nw];
			//memset(contacted_nums, -1, sizeof(contacted_nums));
			int contacted_nums_aval_slot = 0;
			int servers_left = Nw;
			int server_to_contact;
			int random_num;
			int f = 0;
			int failed_servers_num = 0;

			int server_nums[num_normal_servers];
			int failed_servers[num_normal_servers];
			
			// we need to be able to handle the case where Nw is N, so write to ourselves
			// note the recieve Nw is either Nw from the client or Nw-1 from a different server that the client contacted

			strncpy(articles[next_aval_article_slot].text, Article.text, 120);
			articles[next_aval_article_slot].reply_seqnum = Article.reply_seqnum;
			articles[next_aval_article_slot].seqnum = Article.seqnum;
			next_aval_article_slot++;

			servers_left--;

			// Create list of servers to contact
			for(int i = 0; i < num_normal_servers; i++){
				server_nums[i] = i;
			}

			
			printf("contacting given nW-1 servers\n");

			while( !(servers_left == 0 || f == num_normal_servers) ){
				
				// Get random server to contact

				random_num = (rand() % (num_normal_servers-f));
				server_to_contact = server_nums[random_num];
				//printf("%d %s %s\n", server_to_contact, servers_info[server_to_contact].ip, servers_info[server_to_contact].port);
				f++;
				// Remove the random server from available servers.
				//printf("%d %d %d\n", random_num, num_normal_servers, f);
				

				for(int i = random_num; i < num_normal_servers - f; i++){
					printf("i == %d\n", i);
					server_nums[i] = server_nums[i+1];
				}


				//printf("Contacting servers now\n");
				//contact the server we picked randomly
				//printf("contacting server: %s %s sender server: %s %s\n", servers_info[server_to_contact].ip, servers_info[server_to_contact].port, sender_ip, sender_port);

				if((strcmp(servers_info[server_to_contact].ip, sender_ip) == 0) && (strncmp(servers_info[server_to_contact].port, sender_port, strlen(sender_port)) == 0) ) {
					//printf("Not contacting a server\n");
					//failed_servers[failed_servers_num] = server_to_contact;
					//failed_servers_num++;
					continue;
				}
				// primary server isn't in servers, so it's not contacted
				else{
					printf("Contacting a server at ip: %s port: %s \n", servers_info[server_to_contact].ip, servers_info[server_to_contact].port);
					result_7 = server_write_1(Article, servers[server_to_contact]);	
				}

				// a similar primary and non primary structure should happen on a read, where the server forwards the read to the primary, and the primary reads from Nr servers 

				// return -1 to signify failure and allow the server to keep the article or allow it to be lost
				// the client should not expect that this article write has been applied
				if (result_7 == (bool_t *) NULL) {
					clnt_perror (servers[server_to_contact], "call failed");
					failed_servers[failed_servers_num] = server_to_contact;
					failed_servers_num++;
				}
				else{
					contacted_nums[contacted_nums_aval_slot] = server_to_contact;
					servers_left--;
					contacted_nums_aval_slot++;
				}
				//printf("New loop, servers_left: %d\n", servers_left);
			} // end of for loop, primary has either contacted Nw servers or failed to contact enough servers.
			
			printf("Contacted %d servers\n", f-failed_servers_num);
			if(failed_servers_num > 0){
				printf("Failed to contact %d servers\nFailed servers:\n", failed_servers_num);
			}
			for(int i = 0; i < failed_servers_num; i++){
				printf("%s %s\n", servers_info[failed_servers[i]].ip, servers_info[failed_servers[i]].port);
			}

			// Failed to write to Nw servers. May have written to some servers.
			if(servers_left > 0){
				result = -1;
			}
			else{
				result = 1;
			}

			return &result;

		} else {
			// case of a qourum write where this isn't the primary, so forward this write via an rpc to the primary

			// We write to ourselves to handle the case of Nw == N
			pthread_mutex_lock(&quorum_normal_lock);
			
			strncpy(articles[next_aval_article_slot].text, Article.text, 120);
			articles[next_aval_article_slot].reply_seqnum = Article.reply_seqnum;
			articles[next_aval_article_slot].seqnum = Article.seqnum;
			next_aval_article_slot++;

			printf("Nw : %d\n", Nw);
			Nw--;
			// check for case where Nw was 1
			if (Nw <= 0) {
				result = Article.seqnum;
				pthread_mutex_unlock(&quorum_normal_lock);
				return &result;
			}

			// ask primary to contact Nw-1 servers
			printf("contacting primary\n");

			// have the primary not contact us based on ip and port str, shown in 630, since we will be blocked on the write
			result_5 = write_1(Article, Nw, server_ip, server_port_str, primary_server);

			pthread_mutex_unlock(&quorum_normal_lock);
			if (result_5 == (int *) NULL) {
				clnt_perror (primary_server, "call failed");
				result = -1;
				return &result;
			} else {
				result = *result_5;
			}
		}
	} else if (strcmp(mode, "local-write") == 0) {
		// in the local write, it doesn't matter if the server is normal or not, 
		// the server must write to itself and then return to the client, (local!)
		// However, it must still lazly write to the servers it knows. 
		// Add the article (that's whats getting written) to a queue, which is picked up by a reader thread
		// And, the reader thread pops from the queue and writes to all the servers (in a for loop) using server_write w/o sideeffects
		// 

		// Also, the client may switch servers, and will call fetch, so that a server can get all of the articles the client wrote.
		// Thats what Written_seqnums_t from the client is for. client calls fetch_articles_1_svc on joining a server and gives the server the seqnums it wrote. This way the client see their writes.
		// on a fetch, find a server that has the articles, which may involve contacting multiple servers


		//write to ourselves
		if (next_aval_article_slot >= NUM_ARTICLES) {
			result = FALSE;
			return &result;
		}
		strncpy(articles[next_aval_article_slot].text, Article.text, 120);
		articles[next_aval_article_slot].reply_seqnum = Article.reply_seqnum;
		articles[next_aval_article_slot].seqnum = Article.seqnum;
		next_aval_article_slot++;
		printf("wrote to self\n");

		//create article_queue_entry
		article_queue_entry *new_node = malloc(sizeof(article_queue_entry));
		strncpy(new_node->article.text, Article.text, 120);
		new_node->article.reply_seqnum = Article.reply_seqnum;
		new_node->article.seqnum = Article.seqnum;
		
		//lock mutex
		pthread_mutex_lock(&lock);
		// add to the queue
		STAILQ_INSERT_TAIL(&head, new_node, entries);
		printf("added to queue\n");
		pthread_mutex_unlock(&lock);


		result = Article.seqnum;
		return &result;
	
	} else {
		printf("Server's mode is invalid\n");
		result = -1;
	}

	return &result;
}


/* _____________ server rpc calls made between servers: _____________ */

// only the primary can give out seqnums for new articles
// all modes require contacting the primary for an available seqnum
// this way servers do not issue conflicting seqnums for articles
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


// save an article to THIS server, do nothing else
bool_t *
server_write_1_svc(Article_t Article,  struct svc_req *rqstp)
{
	printf("server_write_1_svc called\n");
	printf("Article.text: %s\n", Article.text);
	static bool_t  result;

	// check if article can be written
	if (next_aval_article_slot >= NUM_ARTICLES) {
		result = FALSE;
		return &result;
	}
	//check if articles seqnum already exists
	for (int i = 0; i < NUM_ARTICLES; i++) {
		if (articles[i].seqnum == Article.seqnum) {
			result = TRUE;
			return &result; // we want to return true because the article already exists
		}
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
// use for qourum sync, or local write fetch, to get a article with a specific seqnum from a server
Article_t *
choose_1_svc(int Seqnum,  struct svc_req *rqstp)
{
	static Article_t  result;

	// find the article:

	bool_t found = FALSE;

	pthread_mutex_lock(&quorum_normal_lock);
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
	pthread_mutex_unlock(&quorum_normal_lock);

	return &result;
}

Article_t *
get_article(int Seqnum)
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