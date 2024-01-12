#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include <pthread.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/file.h>

#define OPTIONS              "t:l:"
#define BUF_SIZE             4096
#define DEFAULT_THREAD_COUNT 4
static FILE *logfile;
#define LOG(...) fprintf(logfile, __VA_ARGS__);

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t fill = PTHREAD_COND_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;

int boundedBuf[4096] = { 0 };
int in = 0;
int out = 0;
int counter = 0;
pthread_t *threadArr;
int threads = DEFAULT_THREAD_COUNT;

// Converts a string to an 16 bits unsigned integer.
// Returns 0 if the string is malformed or out of the range.
static size_t strtouint16(char number[]) {
    char *last;
    long num = strtol(number, &last, 10);
    if (num <= 0 || num > UINT16_MAX || *last != '\0') {
        return 0;
    }
    return num;
}
// Creates a socket for listening for connections.
// Closes the program and prints an error message on error.
static int create_listen_socket(uint16_t port) {
    struct sockaddr_in addr;
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        err(EXIT_FAILURE, "socket error");
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htons(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *) &addr, sizeof addr) < 0) {
        err(EXIT_FAILURE, "bind error");
    }
    if (listen(listenfd, 128) < 0) {
        err(EXIT_FAILURE, "listen error");
    }
    return listenfd;
}

/**
   Defining request struct to hold values.
 */
typedef struct {
    char type[8];
    char uri[19];
    char version[10];
    char header[2048];
    int length;
    int Id;
    char message[2048];
    int fd;
} Request;

/**
   Retrieves the status phrase from the status code.
 */
char *getStatusPhrase(int statusCode) {
    //printf("Getting status phrase\n");
    if (statusCode == 200) {
        return "OK";
    } else if (statusCode == 201) {
        return "Created";
    } else if (statusCode == 404) {
        return "Not Found";
    } else if (statusCode == 500) {
        return "Internal Server Error";
    }
    return "NARTZ";
}

/**
   Prints the response given the status code.
 */
void printResponse(Request newRequest, int statusCode) {
    char *statusPhrase = getStatusPhrase(statusCode);
    strcpy(newRequest.message, statusPhrase);
    strncat(newRequest.message, "\n", 1);
    int contentLength = strlen(newRequest.message);

    dprintf(newRequest.fd, "HTTP/1.1 %d %s\r\n", statusCode, statusPhrase);
    dprintf(newRequest.fd, "Content-Length: %d\r\n\r\n", contentLength);
    dprintf(newRequest.fd, "%s", newRequest.message);

    int logNum = fileno(logfile);
    flock(logNum, LOCK_EX);
    LOG("%s,%s,%d,%d\n", newRequest.type, newRequest.uri, statusCode, newRequest.Id);
    fflush(logfile);
    flock(logNum, LOCK_UN);

    return;
}

/**
   Performs a GET request.
 */
void processGet(Request newRequest) {
    int statusCode = 0;

    int file = open(&newRequest.uri[1], O_RDONLY, S_IRWXU);
    if (file < 0) {
        if (errno == 2) {
            //printf("FILE DONT EXIST\n");
            statusCode = 404;
        } else {
            statusCode = 500;
        }
    }

    if (statusCode != 0) {
        printResponse(newRequest, statusCode);
        return;
    }

    flock(file, LOCK_SH);
    struct stat st;
    fstat(file, &st);

    //printf("Processing %s\n", newRequest.type);

    char *buff = (char *) malloc(4096);
    int totalBytes = st.st_size;

    statusCode = 200;
    char *statusPhrase = getStatusPhrase(statusCode);
    int bytesRead = read(file, buff, 4096);
    dprintf(newRequest.fd, "HTTP/1.1 %d %s\r\n", statusCode, statusPhrase);
    dprintf(newRequest.fd, "Content-Length: %d\r\n\r\n", totalBytes);

    while (bytesRead != 0) {
        //printf("Bytes Read: %d\n", bytesRead);
        write(newRequest.fd, buff, bytesRead);
        bytesRead = read(file, buff, 4096);
    }

    free(buff);
    flock(file, LOCK_UN);

    int logNum = fileno(logfile);
    flock(logNum, LOCK_EX);
    LOG("%s,%s,%d,%d\n", newRequest.type, newRequest.uri, statusCode, newRequest.Id);
    fflush(logfile);
    flock(logNum, LOCK_UN);

    close(file);
}

/**
   Performs a PUT request.
 */
void processPut(Request newRequest) {
    int statusCode = 0;
    int created = 0;
    if (access(&newRequest.uri[1], F_OK) < 0) {
        statusCode = 201;
        int create = open(&newRequest.uri[1], O_CREAT | O_WRONLY, S_IRWXU);
        created = 1;
        close(create);
    }

    //printf("Processing %s\n", newRequest.type);

    int file = open(&newRequest.uri[1], O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
    if (file < 0) {
        statusCode = 500;
        printResponse(newRequest, statusCode);
        return;
    }

    flock(file, LOCK_EX);
    char req[4096];
    while (newRequest.length > 0) {
        if (newRequest.length < 4096) {
            int readAm = read(newRequest.fd, req, newRequest.length);
            int writeAm = write(file, req, readAm);
            newRequest.length = newRequest.length - writeAm;
        } else {
            int readAm = read(newRequest.fd, req, 4096);
            int writeAm = write(file, req, readAm);
            newRequest.length = newRequest.length - writeAm;
        }
    }
    flock(file, LOCK_UN);

    if (created != 1) {
        statusCode = 200;
    }

    printResponse(newRequest, statusCode);

    close(file);
}

/**
   Performs an APPEND request.
 */
void processAppend(Request newRequest) {
    int file = open(&newRequest.uri[1], O_WRONLY | O_APPEND, S_IRWXU);
    int statusCode = 0;
    if (file < 0) {
        //printf("ERROR NUMBER: %d\n", errno);
        if (errno == 2) {
            //printf("FILE DONT EXIST\n");
            statusCode = 404;
        } else {
            statusCode = 500;
        }
    }

    //struct stat st;
    //fstat(file, &st);

    if (statusCode != 0) {
        printResponse(newRequest, statusCode);
        return;
    }

    flock(file, LOCK_EX);
    char req[4096];
    while (newRequest.length > 0) {
        if (newRequest.length < 4096) {
            int readAm = read(newRequest.fd, req, newRequest.length);
            int writeAm = write(file, req, readAm);
            newRequest.length = newRequest.length - writeAm;
        } else {
            int readAm = read(newRequest.fd, req, 4096);
            int writeAm = write(file, req, readAm);
            newRequest.length = newRequest.length - writeAm;
        }
    }
    flock(file, LOCK_UN);

    statusCode = 200;

    printResponse(newRequest, statusCode);

    close(file);
}

static void handle_connection(int connfd) {
    // make the compiler not complain/
    //entire request header fits in 2048b

    char buf[BUF_SIZE];
    char req[2048];
    Request newRequest;
    newRequest.fd = connfd;
    int count = 0;

    memset(&newRequest.type[0], 0, sizeof(newRequest.type));
    memset(&newRequest.uri[0], 0, sizeof(newRequest.uri));
    memset(&newRequest.version[0], 0, sizeof(newRequest.version));
    memset(&newRequest.header[0], 0, sizeof(newRequest.header));
    newRequest.length = 0;
    newRequest.Id = 0;
    memset(&newRequest.message[0], 0, sizeof(newRequest.message));
    memset(&buf[0], 0, sizeof(buf));

    int completeReq = 0;
    int bytesRead = 0;

    while (completeReq != 1) {
        //printf("incomplete request\n");
        memset(&req[0], 0, sizeof(req));
        bytesRead = read(connfd, req, 1);
        memcpy(buf + strlen(buf), req, 1);
        //printf("REQ: %s\n", req);
        //printf("BUF: %s\n", buf);
        for (int i = 0; i < (int) strlen(buf); i++) {
            if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
                completeReq = 1;
                break;
            }
        }
        if (bytesRead == 0) {
            completeReq = 1;
        }
    }

    //printf("BUF: %s\n", buf);

    char get[] = "GET";
    char put[] = "PUT";
    char append[] = "APPEND";
    char properId[] = "Request-Id:";
    char properHeader[] = "Content-Length:";

    for (int i = 0; i < (int) strlen(buf); i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') {
            count++;
        }
    }

    //printf("RN's: %d\n", count);

    if (count == 2) {
        sscanf(buf, "%s %s %s\r\n", newRequest.type, newRequest.uri, newRequest.version);
        //printf("COUNT IS 2\n");
    } else {

        sscanf(buf, "%s %s %s\r\n", newRequest.type, newRequest.uri, newRequest.version);
        int position = 0;
        for (int i = 0; i < (int) strlen(buf); i++) {
            //printf("%c\n", buf[i]);
            if (buf[i] == '\r' && buf[i + 1] == '\n') {
                position = i + 2;
                break;
            }
        }

        while (count > 2) {
            //printf("RN's: %d\n", count);
            //printf("Current Position: %d\n", position);
            sscanf(buf + position, "%s", newRequest.header);

            //printf("Current Header: %s\n", newRequest.header);

            if (strcmp(newRequest.header, properHeader) == 0) {
                //printf("did match header\n");
                char fill[30];

                sscanf(buf + position, "%s %d\r\n", fill, &newRequest.length);

            } else if (strcmp(newRequest.header, properId) == 0) {
                char fill[30];

                sscanf(buf + position, "%s %d\r\n", fill, &newRequest.Id);
            }
            for (int i = position; i < (int) strlen(buf); i++) {
                if (buf[i] == '\r' && buf[i + 1] == '\n') {
                    position = i + 2;
                    break;
                }
            }
            count--;
            //printf("LENGHTHH: %d\n", newRequest.length);
        }
        //printf("LENGHTHH: %d\n", newRequest.length);
        //printf("ID: %d\n", newRequest.Id);
        //printf("message: %s\n", newRequest.message);
    }

    if (strcasecmp(newRequest.type, get) == 0) {
        processGet(newRequest);
    } else if (strcasecmp(newRequest.type, put) == 0) {
        processPut(newRequest);
    } else if (strcasecmp(newRequest.type, append) == 0) {
        processAppend(newRequest);
    }

    memset(&newRequest.type[0], 0, sizeof(newRequest.type));
    memset(&newRequest.uri[0], 0, sizeof(newRequest.uri));
    memset(&newRequest.version[0], 0, sizeof(newRequest.version));
    memset(&newRequest.header[0], 0, sizeof(newRequest.header));
    newRequest.length = 0;
    newRequest.Id = 0;
    memset(&newRequest.message[0], 0, sizeof(newRequest.message));
    memset(&buf[0], 0, sizeof(buf));
}

void enqueue(int connfd) {
    pthread_mutex_lock(&lock);
    while (counter == 4096) {
        pthread_cond_wait(&empty, &lock);
    }
    boundedBuf[in] = connfd;
    in = (in + 1) % 4096;
    counter += 1;
    pthread_mutex_unlock(&lock);
    pthread_cond_signal(&fill);
}

void *sorter(void *arg) {
    (void) arg;

    //pthread_mutex_lock(&lock);
    while (1) {
        pthread_mutex_lock(&lock);
        while (counter <= 0) {
            pthread_cond_wait(&fill, &lock);
        }

        int next = boundedBuf[out];
        out = (out + 1) % 4096;
        counter -= 1;

        pthread_mutex_unlock(&lock);
        pthread_cond_signal(&empty);
        handle_connection(next);
        close(next);
    }

    return NULL;
}

static void sigterm_handler(int sig) {
    if (sig == SIGTERM) {
        warnx("received SIGTERM");
        //for (int i = 0; i < threads; i++) {
        //    if (pthread_join(threadArr[i], NULL) != 0) {
        //       err(1, "pthread_join failed");
        //   }
        //}

        free(threadArr);
        fclose(logfile);
        exit(EXIT_SUCCESS);
    } else if (sig == SIGINT) {
        warnx("received SIGINT");
        //for (int i = 0; i < threads; i++) {
        //    if (pthread_join(threadArr[i], NULL) != 0) {
        //        err(1, "pthread_join failed");
        //   }
        //}

        free(threadArr);
        fclose(logfile);
        exit(EXIT_SUCCESS);
    }
}
static void usage(char *exec) {
    fprintf(stderr, "usage: %s [-t threads] [-l logfile] <port>\n", exec);
}
int main(int argc, char *argv[]) {
    int opt = 0;
    logfile = stderr;
    while ((opt = getopt(argc, argv, OPTIONS)) != -1) {
        switch (opt) {
        case 't':
            threads = strtol(optarg, NULL, 10);
            if (threads <= 0) {
                errx(EXIT_FAILURE, "bad number of threads");
            }
            break;
        case 'l':
            logfile = fopen(optarg, "w");
            if (!logfile) {
                errx(EXIT_FAILURE, "bad logfile");
            }
            break;
        default: usage(argv[0]); return EXIT_FAILURE;
        }
    }
    if (optind >= argc) {
        warnx("wrong number of arguments");
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    uint16_t port = strtouint16(argv[optind]);
    if (port == 0) {
        errx(EXIT_FAILURE, "bad port number: %s", argv[1]);
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);
    int listenfd = create_listen_socket(port);
    //LOG("port=%" PRIu16 ", threads=%d\n", port, threads);

    threadArr = calloc(sizeof(*threadArr), threads);
    for (int i = 0; i < threads; i++) {
        //printf("creating thread #%d\n", i+1);

        if (pthread_create(&threadArr[i], NULL, sorter, NULL) != 0) {
            err(1, "pthread_create failed");
        }

        //if (pthread_join(threadArr[i], NULL) != 0) {
        //    err(1, "pthread_create failed");
        //}

        //printf("done creating/joining thread #%d\n", i+1);
    }

    for (;;) {
        int connfd = accept(listenfd, NULL, NULL);
        if (connfd < 0) {
            warn("accept error");
            continue;
        }
        enqueue(connfd);
    }
    return EXIT_SUCCESS;
}
