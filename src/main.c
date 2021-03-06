/*
 * vim: sw=2 ts=2 expandtab
 *
 * "THE PIZZA-WARE LICENSE" (derived from "THE BEER-WARE LICENCE"):
 * Thomas Kolb <cfr34k@tkolb.de> wrote this file. As long as you retain this
 * notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a pizza in return.
 * - Thomas Kolb
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#include <signal.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "logger.h"

#define MAX_CLIENTS 64
#define RDATA_LEN 1024

#define max(a, b) ((a > b) ? a : b)

int running = 1;

// signal handler for SIGTERM, SIGINT, etc.
// sets the flag for a clean shutdown
void sig_shutdown_handler(int sig) {
	LOG(LVL_DEBUG, "Handling signal: %i", sig);
	running = 0;
}

void init_signal_handlers(void) {
	struct sigaction sa;
	sa.sa_handler = sig_shutdown_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_restorer = NULL;

	if(sigaction(SIGTERM, &sa, NULL) == -1) {
		LOG(LVL_ERR, "sigaction [SIGTERM] failed: %s", strerror(errno));
	}

	if(sigaction(SIGINT, &sa, NULL) == -1) {
		LOG(LVL_ERR, "sigaction [SIGINT] failed: %s", strerror(errno));
	}

	sa.sa_handler = SIG_IGN;

	if(sigaction(SIGPIPE, &sa, NULL) == -1) {
		LOG(LVL_ERR, "sigaction [SIGPIPE] failed: %s", strerror(errno));
	}
}

int listen_socket(int listen_port)
{
	struct sockaddr_in a;
	int s;

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		LOG(LVL_DEBUG, "socket failed: %d = %s", errno, strerror(errno));
		return -1;
	}
	memset(&a, 0, sizeof(a));
	a.sin_port = htons(listen_port);
	a.sin_family = AF_INET;
	if (bind(s, (struct sockaddr *) &a, sizeof(a)) == -1) {
		LOG(LVL_ERR, "bind failed: %d = %s", errno, strerror(errno));
		close(s);
		return -1;
	}
	LOG(LVL_DEBUG, "accepting connections on port %d", listen_port);
	listen(s, 10);
	return s;
}

void shutdown_client(int *fdp) {
	shutdown(*fdp, SHUT_RDWR);
	close(*fdp);
	*fdp = -1;
}

int accept_new_clients(int ls, int *clients) {

	return 1;
}

int send_message(int *clients, char *message) {

	return 1;
}

void fsleep(float t) {
	struct timespec ts;
	ts.tv_sec = (int)t;
	ts.tv_nsec = (t - (int)t) * 1e9;

	nanosleep(&ts, NULL);
}

int parse_cmdline(int argc, char **argv, uint16_t *port, char **filename)
{
	int opt;
	long larg;

	// default values
	*port = 1234;

	while((opt = getopt(argc, argv, "p:")) != -1) {
		switch(opt) {
			case 'p':
				errno = 0;
				larg = strtol(optarg, NULL, 10);

				if(errno != 0) {
					LOG(LVL_ERR, "%l is not a valid integer.", optarg);
					return -1;
				}

				if(larg < 1 || larg > 65535) {
					LOG(LVL_ERR, "Port number is out of range [1, 65535].", optarg);
					return -1;
				}

				*port = (uint16_t)larg;
				break;
			default:
				// unknown option
				return -1;
		}
	}

	if(optind >= argc) {
		// no non-option arguments left, but needed
		LOG(LVL_ERR, "Too few arguments.");
		return -1;
	}

	*filename = argv[optind];

	return 0;
}

void show_usage(char *progname)
{
	fprintf(stderr, "\nUsage: %s [-p <port>] <filename>\n\n", progname);
	fprintf(stderr, "port\t\t Port to use for incoming connections (default: 1234)\n");
	fprintf(stderr, "filename\t Input data source (FIFO etc.), use '-' for stdin\n");
}

int main(int argc, char **argv)
{
	int ls = -1;
	int i;
	int input_fd = -1;
	int clients[MAX_CLIENTS];
	fd_set rd;
	fd_set wr;
	int r;
	char buf[128];
	char rdata[RDATA_LEN];
	int rdata_cur_len = 0;
	int nfds = 0;

	int should_reopen;

	char *filename;
	uint16_t port;


	logger_init();

	// parse command line
	r = parse_cmdline(argc, argv, &port, &filename);
	if(r < 0) {
		LOG(LVL_ERR, "Failed to parse command line.");
		show_usage(argv[0]);
		return EXIT_FAILURE;
	}

	// determine type of input file
	if(strcmp(filename, "-") == 0) {
		// use stdin, does not need to be opened
		input_fd = 0;
		should_reopen = 0;
	} else {
		struct stat st;

		// check for FIFO or sockets (will be reopened on close)
		r = stat(filename, &st);
		if(r == -1) {
			LOG(LVL_ERR, "Failed to stat('%s'): %i = %s!", filename, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		if(S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode)) {
			should_reopen = 1;
		} else {
			should_reopen = 0;
		}

		// initial open
		input_fd = open(filename, O_RDONLY);
		if(input_fd == -1) {
			LOG(LVL_ERR, "Cannot open('%s', O_RDONLY): %i = %s!", filename, errno, strerror(errno));
			return EXIT_FAILURE;
		}
	}

	init_signal_handlers();

	ls = listen_socket(port);
	if(ls == -1) {
		close(input_fd);
		return EXIT_FAILURE;
	}

	for(i = 0; i < MAX_CLIENTS; i++) {
		clients[i] = -1;
	}

	while(running) {
		// check if input was closed. If it was a FIFO or socket, reopen it, else quit.
		if(input_fd == -1) {
			if(should_reopen) {
				input_fd = open(filename, O_RDONLY);
				if(input_fd == -1) {
					LOG(LVL_ERR, "Cannot open('%s', O_RDONLY) again: %i = %s!", filename, errno, strerror(errno));
					break;
				}
			} else {
				break;
			}
		}

		FD_ZERO(&wr);
		FD_ZERO(&rd);

		// add server socket
		FD_SET(ls, &rd);

		// add input fd
		FD_SET(input_fd, &rd);

		// add client sockets
		nfds = max(ls, input_fd);
		for(i = 0; i < MAX_CLIENTS; i++) {
			int s = clients[i];

			if(s != -1) {
				nfds = max(nfds, s);
				FD_SET(s, &rd);
			}
		}

		r = select(nfds + 1, &rd, NULL, NULL, NULL);

		// check select result for errors
		if(r == -1) {
			if(errno == EINTR) {
				continue;
			} else {
				LOG(LVL_ERR, "select failed for read list: %d = %s", errno, strerror(errno));
				break;
			}
		}

		// check wether there are new clients
		if(FD_ISSET(ls, &rd)) {
			// find a free fd
			for(i = 0; i < MAX_CLIENTS; i++) {
				if(clients[i] == -1) {
					break;
				}
			}

			if(i < MAX_CLIENTS) {
				// a free slot was found
				r = accept(ls, NULL, NULL);

				if(r == -1) {
					LOG(LVL_ERR, "accept failed: %d = %s", errno, strerror(errno));
					continue;
				} else {
					LOG(LVL_DEBUG, "new client #%i (fd=%i) accepted", i, r);
					clients[i] = r;
				}

				// a client was added, and there might be more, so try again
				continue;
			}
		}

		// check wether there is new data on the input fd
		if(FD_ISSET(input_fd, &rd)) {
			r = read(input_fd, rdata, RDATA_LEN);

			if(r == -1) {
				// read error
				LOG(LVL_ERR, "input_fd read failed (r=%i, %i = %s)", r, errno, strerror(errno));
				break;
			} else if(r == 0) {
				// nothing read, must be end of input
				LOG(LVL_INFO, "input_fd EOF");
				close(input_fd);
				input_fd = -1;
				continue;
			} else {
				rdata_cur_len = r;
			}
		}

		// check input (and EOF) for all clients
		for(i = 0; i < MAX_CLIENTS; i++) {
			int *s = clients + i;

			if(*s != -1 && FD_ISSET(*s, &rd)) {
				r = recv(*s, buf, 128, 0);
				if(r == 0) {
					LOG(LVL_DEBUG, "client #%i (fd=%i) disconnected", i, *s);
					shutdown_client(s);
					continue;
				} else if(r == -1) {
					LOG(LVL_WARN, "client #%i (fd=%i) recv failed (%i = %s)", i, *s, errno, strerror(errno));
					shutdown_client(s);
					continue;
				}
			}
		}


		// check for I/O on all clients
		if(rdata_cur_len > 0) {
			nfds = 0;
			for(i = 0; i < MAX_CLIENTS; i++) {
				int s = clients[i];

				if(s != -1) {
					nfds = max(nfds, s);
					FD_SET(s, &wr);
				}
			}

			if(nfds == 0) {
				continue; // main loop
			}

			r = select(nfds + 1, NULL, &wr, NULL, NULL);

			// check select result for errors
			if(r == -1) {
				if(errno == EINTR) {
					continue;
				} else {
					LOG(LVL_ERR, "select failed for write list: %d = %s", errno, strerror(errno));
					break;
				}
			}

			for(i = 0; i < MAX_CLIENTS; i++) {
				int *s = clients + i;

				if(*s != -1 && FD_ISSET(*s, &rd)) {
					r = recv(*s, buf, 128, 0);
					if(r == 0) {
						LOG(LVL_DEBUG, "client #%i (fd=%i) disconnected", i, *s);
						shutdown_client(s);
						continue;
					} else if(r == -1) {
						LOG(LVL_WARN, "client #%i (fd=%i) recv failed (%i = %s)", i, *s, errno, strerror(errno));
						shutdown_client(s);
						continue;
					}
				}

				if(*s != -1 && FD_ISSET(*s, &wr)) {
					r = send(*s, rdata, rdata_cur_len, 0);
					if(r < 1) {
						LOG(LVL_WARN, "client #%i (fd=%i) dropped on write (r=%i)", i, *s, r);
						shutdown_client(s);
					}
				}
			}

			// data was processed
			rdata_cur_len = 0;
		}
	}

	LOG(LVL_INFO, "Closing all the sockets.");

	for(i = 0; i < MAX_CLIENTS; i++) {
		if(clients[i] != -1) {
			shutdown_client(clients + i);
		}
	}

	close(ls);
	close(input_fd);
}
