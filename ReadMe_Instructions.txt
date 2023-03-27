Group Members:
Nicholas Starinets, stari219@umn.edu, 5599802 - worked on primary backup, client cmd interface, and rpc C setup
Trey Taylor, tayl1931@umn.edu, 5546464 - worked on quorums


Compilation:
Type make on the command line. This will produce executables communicate_client and communicate_server.


Running the code:

First, start all of the servers listed in servers.txt by typing ./communicate_server server's_ip server's_port mode
Mode is one of primary-backup, quorum, or local-write.

Start a client by typing ./communicate_client server_ip server_port mode Nr Nw
The ip and port are needed so that the client can connect to a server initially.
Mode is one of primary-backup, quorum, or local-write.
Nr and Nw are used for the quorum mode and are ignored otherwise.

Note, all servers and clients must be on the same mode, and all clients must have the same Nr, Nw values.


Instructions:

The client interface supports the following commands

  exit or ctrl-C
  ping                            - pings the server
  post Article_text               - post an article to the servers
  read page_num                   - read snippets of articles on the given page
  reply reply_seqnum Article_text - post an article that is replying to an article with a specific seqnum
  connect server_ip server_port   - change the connection to the given server, see servers.txt for available server information
  get_mode                        - get the connected server's mode
  change_mode mode_str            - changes ONLY the client's mode. mode is one of primary-backup, quorum, local-write.
                                    The modes provide sequential, quorum, and read-your-write consistency respectively.


The servers are mainly passive, however they output which rpc got called and some state information about what actions they are taking communication wise.
Each server must know the ground truth about the other servers in the system, ie their ip's port's and who is the primary. This is why servers.txt is needed.


Design:

The client runs a simple interface loop that takes in tokens from the command line and executes an appropriate rpc call.
On a write, the client only contacts the server it's connected to. And, on a read the client only contacts the server it's connected to.
This server in turn may contact other servers depending on the consistency mode. In short, the consistency mode implementation is transparent
to the client. However, the client must keep track of sequence numbers of articles that it has written. It will send use a fetch rpc call to
ask any server it connects to, to retrieve the articles with such sequence numbers, when the mode is set to local-write. 

The client may change the server it is connected to at any time by using the connect command. The rpc calls it makes remain unchanged.
Namely, the client uses the write rpc call to ask the servers to write the article. This call takes in the article text, sequence number,
and reply number of a higher level article, which means that posts and replies both use the write rpc. Next, the client uses the read rpc
to retrieve pages of articles. A page has 10 articles. Reads work simply when the mode is either primary backup or local-write, as a read
will just return articles at the server within the page's sequence number range (ie page 0 has seqnums from 1 to 10). On the other hand,
a read during a quorum mode will require the contacted server to read from up to Nr servers until it has each of the articles in the page.
Notice, the unit of consistency is the article because it is read Nr times and written Nw times. Writes will always involve the contacted server
propagating the article to the other servers. This happens directly in the local-write mode, as the server will send out the article to all known servers.
On the other hand, this happens indirectly in primary backup and quorum mode. To elaborate, in primary backup the server will forward the article to the primary
server, so that it gets written to all of the servers. And, in quorum mode the server will forward the article to the primary server, so that it gets written to
Nw servers. Observe, that this forwarding mechanism causes the initial sending server to wait until the primary has completed the write, and only then to tell the
client whether or not their write was successful.

In primary backup mode all of the servers must be up and working in unison. By this I mean that an article has to be written to all of the servers and thus they must be
available when writes are taking place. Otherwise, servers not having a written article will cause us to not have sequential consistency. In short, this means that
when a client hears back that their write was successful, the write has been propagated by the primary server through all of the servers. Furthermore, a read from any
server is guaranteed to see that written article. Notice that this is sequentially consistent because any articles written afterwards get written afterwards by the single
thread primary rpc handler. And, thus multiple clients reading articles throughout time will also reflect the order in which they are written by the primary. What I am
saying is that the primary write rpc implementation serializes the order in which articles get distributed to all of the servers. 

In quorum mode all of the servers do not run in unison and do not have to all be available to work completely, though the primary server should always be available.
When writing to Nw servers, the server which the client contacts writes to itself first then tells the primary server to write to Nw-1 servers. Primary server writes to itself 
and then it attempts to write to Nw-2 servers which are randomly chosen (does not include the originally called server or primary). When reading a 
page (a set of 10 sequential articles) from Nr servers, the contacted server first checks to see if it has the article. If not, it then starts contacting servers, starting with the primary and after that random servers, up to Nr-1 times until it obtains the needed article. This repeats for a whole page. quorum_sync makes sure all the servers have all articles after a specified amount of time.
It first checks which sequence numbers the server has, and then afterwards attempts to get those articles from a server.



Test Cases:

Tests for primary backup mode:

First, the mode on all of the servers and all of the clients must be primary backup.

Test Case: Test that having just a primary server works.
Test Setup: One primary is set up and One client is connected to it. The client writes articles "hi", "test", "bye, which is a reply to article 2", and then reads them back from the primary.
Expected State Change: The primary has the written articles and retrieves them for the client on reads.
Actual State Change: Success

Test Case: Test that having just a primary server and one backup server works.
Test Setup: One primary is set up and One client is connected to it. A backup is also set up. 
            Then, the client writes articles "hi", "test", "bye, which is a reply to article 2",
            and then reads them back from the primary. Next, the client connects to the backup server
            and also reads these articles on page 1.
Expected State Change: The primary has the written articles to itself and the backup, and both servers retrieve them for the client on reads.
Actual State Change: Success

Test Case: Test that having just a primary, a backup, and two clients works.
Test Setup: The servers are set up. Client one is connected to the primary. Client Two is connected to the backup.
            Next, client two writes the article "hi". Afterwards, client one writes the article "bye".
Expected State Change: Both of the clients do a read and see each other's articles. This means that the articles
                       were propagated successfully. Also, the article written first must have a sequence number of
                       1 and the article written next must have a sequence number of 2.
Actual State Change: Success
