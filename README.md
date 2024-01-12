# HTTPSERVER Assignment 4 

#### Program Description

This program is builds a multithreaded HTTP server that processes HTTP commands from clients.  
This server creates, listens, and accepts connections on a socket that is listening to a port.  
It utilizes a thread pool and a queue of connections in order to bring out the processes in an efficient manner.  
This program assigns different threads with different connections from the queue to optimize the performance of the program.  
This program also assures read-write atomicity and before-after coherency.  
The HTTP commands that this server processes are GET, PUT, and APPEND.  
A GET request indicates that the client would like to receive the content of the specificied file indicated by the URI.  
A PUT request indicates that the client would like to update or replace the content of the file indicated by the URI.  
An APPEND request indicates that the client would like to append content to the end of the file indicated by the URI. 
For each request, the server produces a response with a status code and phrase as well as the HTTP version.  
The response may include the file contents and content length if the request is a GET.  
Alone with this response, this program also produces an audit log of the requests that were made in the order that they were processed.  


#### Function Descriptions

In order to pull off this assignment, I used started with the starter code and began by parsing the request lines.  
I read in the request by using a buffer of 2048 size, and reading in one byte at a time.  
Parsing proved to be one of the hardest parts of the assignment, but by using sscanf and strstr, I was able to do it.  
Once the request line is parsed, I error check if the request line is valid, if so then I am able to process the request.  
To ensure atomicity and coherency, I used flocks in order to maintain the order of operations.  
I have multiple functions in this program with the three primary ones being processGet, processPut, and processAppend.  
In each of these functions, I do each of their respective requests.  
In processGet, I read and write the indentified file contents with the use of read() and write().  
In processPut, I write to a file the message body of the request or create a file with the message body of the reqeust.  
I am able to do these with the use of write() and I also use fstat() to error check.  
In processAppend, I append to a file the contents of the message body of the request.  
processAppend is very similar to processPut, but using O_APPEND instead when opening and writing the files.
In each of these functions as well as my parsing function, I am constantly error checking the various possible errors.  
I also have a couple extra functions such as printResponse, which prints out the formatted response with status code.  
In this printReponse function, I also output to the logfile that can be specified and call fflush after every print.  
In addition to all of these functions, in order too support the multithreaded aspect of this server,  
I have created functions named enqueue and sorter, which both manage the global work queue of connections.  


#### Efficiency 

In terms of efficiency, I read and write in many bytes at a time as opposed to reading one byte at a time except for the header.
This is done in order to minimize the amount of trips we take to disk, ultimately making the process all the more efficient.  
Also, this server's threads do not busy wait, meaning that if no work is to be done, then the thread is set to sleep.  

#### Error Handling

The errors to account for:
* File does not exist
* Internal Server Error

#### Program Instructions

This program compiles using a make file on Ubuntu 20.04 terminal and is written in the C coding language.  
Doing "make" or "make all" will create an executeable file httpserver.  
You can run this file by using the command ./httpserver [-t threads] [-l logfile] (port)  
The threads stands for the amount of threads the server uses and the logfile determines where the audit log should output to.  
Then on the client side of things, you can run "curl -X PUT -d hello localhost:1231/file.txt" to run a basic PUT command.  
What that request will do is it will create or replace the contents of file.txt with the message body "hello".  
You can run an APPEND command by using "curl -X APPEND -d hello localhost:1231/file.txt".  
Lastly, you can run a GET command by using "curl localhost:1221/test.txt".  
You can use "make clean" to remove the executeables and "make format" to format the file using clang.format.  

#### Program Approach

I approached this program knowing that my toughest obstacle would be parsing the request.  
I initially began working on my GET, PUT, and APPEND functions first without worrying about the parse.  
The way that I structured this assignment in my head was receive request, process request, and print the output.  
Receiving the request meant reading from connfd and parsing the requests that I receieved.  
Processing the requests meant that I needed to determine whether or not the request was able to be processed.  
If so, I proceeded with the respective request whether it be GET, PUT, or APPEND.  
In each of these request functions, I use flocks to ensure atomicity.  
Lastly, printing the output after the request has been processed is simple enough as I used a  
function to help me get the proper status phrase from the status code and printed accordingly.  
While writing to logfile, I also use flock with LOCK_EX.   
These things are all pulled out in a reasonably efficient manner.  
In order to figure out multithreading, I created a separate function for the threads to run on.  
I have set a global array of ints with global positions in order to keep track of what connections to work on.  
This set of code is based on the producer-consumer problem that is raised in class.  
This function grabs connections from a "queue", the global array of ints, and handles it from there,  
enabling multiple threads to work on the connections all at once.  
All in all, throughout the process of coding, I constantly compared my output to that of the  
resource binary in order to keep on the right track.  

#### Resources

Coming from C++, I primarily referred to the manpages for what each command does.  
Used the producer-consumer pseudocode revised with locks and signals for my queue of connfds.  
Some commands that I used the manpages on include fflush, flock, sscanf, and pthread functions.  
This program took heavy inspiration from Eugene's sections on multithreading and queues.    