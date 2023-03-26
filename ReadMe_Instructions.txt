Group Members:
Nicholas Starinets, stari219@umn.edu, 5599802 - worked on primary backup, client cmd interface, and rpc C setup
Trey Taylor, tayl1931@umn.edu, 5546464 - worked on quorums


Compilation:
Type make on the command line. This will produce executables communicate_client and communicate_server.


Running the code:

First, start all of the servers listed in servers.txt by typing ./communicate_server server's_ip server's_port mode
Mode is one of primary-backup, quorum, or local-write.

Start a client by typing ./communicate_client server_ip server_port mode Nr Nw
The ip and port are the needed so that the client can connect to a server initially.
Mode is one of primary-backup, quorum, or local-write.
Nr and Nw are used for the quorum mode and are ignored otherwise.

Note, all servers and client must be on the same mode, and all clients must have the same Nr, Nw values.


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
                                    The modes provide sequential, qourum, and read-your-write consistency respectively.


The servers are mainly passive, however they output which rpc got called and some state information about what actions they are taking communication wise.
Each server must know the ground truth about the other servers in the system, ie their ip's port's and who is the primary. This is why servers.txt is needed.


Design:

The client runs a simple interface loop that takes in tokens from the command line and executes an appropriate rpc call.


Test Cases:

