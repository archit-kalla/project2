/* <communicate.x> - IDL file for RPCGen */

struct Article_t {
    char text[120];
    int seqnum;

    /*-1 means this is a top level article */
    int reply_seqnum;
};

struct Page_t {
    Article_t articles[10];
};

/* NUM_ARTICLES stored at servers is 50, which is the max that any client could've written in the local-write protocol */
struct Written_seqnums_t {
    int seqnums[50];
};


program COMMUNICATE {
    version COMMUNICATE_VERSION
    {   
        /* _____________ rpc calls made by clients: _____________ */
        
        bool Ping () = 1;

        /* this call is propogated to primary in a quorum, which will then read from Nr, 
           otherwise it's just a normal server read */
        Page_t Read(int Page_num, int Nr) = 2;

        string Get_mode () = 3;

        /* NUM_ARTICLES stored at servers is 50, which is the max that any client could've written */
        /* when a client connects to a local write server, the server
           must fetch articles that the client previously wrote. 
           And it'll fetch articles at higher levels recursively if these articles were replies */
        bool Fetch_articles (Written_seqnums_t written_seqnums) = 4;

        /* this call is propogated to primary in a quorum, which will then write to Nw.
           In a primary-backup, this call is propogated to the primary which will write to all servers.
           In a local-write, the server will write to all servers in a writer thread*/
        /* returns seqnum of written article to client */
        int Write (Article_t Article, int Nw, string sender_ip, string sender_port) = 5;

        /* _____________ server rpc calls made between servers: _____________ */

        /* get available highest seqnum from primary */
        int Get_seqnum () = 6;

        /* used when a server writes an article to another server */
        bool Server_write (Article_t Article) = 7;
        
        /* for sync op in qourum, which uses this call in 
           combination with the Choose call. ie server asks other 
           servers for articles it doesn't have based on highest valid seqnum at primary.*/
        int Highest_seqnum () = 8;

        /* used during a local write fetch or for quorum sync op*/
        Article_t Choose (int Seqnum) = 9;


    } = 1;
} = 0x20000002;