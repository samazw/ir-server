/* 	$Id: irsend.c,v 5.5 2007/09/29 17:13:14 lirc Exp $	 */

/*
  
  irsend -  application for sending IR-codes via lirc
  
  Copyright (C) 1998 Christoph Bartelmus (lirc@bartelmus.de)
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
  
*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>

#include <stdint.h>

#include "log.h"

#ifndef __u32
typedef uint32_t __u32;
#endif

#define PACKET_SIZE 256
/* three seconds */
#define TIMEOUT 3

int timeout = 0;
char *progname;

void sigalrm(int sig)
{
	timeout = 1;
}

const char *read_string(int fd)
{
	static char buffer[PACKET_SIZE + 1] = "";
	char *end;
	static int ptr = 0;
	ssize_t ret;

	if (ptr > 0) {
		memmove(buffer, buffer + ptr, strlen(buffer + ptr) + 1);
		ptr = strlen(buffer);
		end = strchr(buffer, '\n');
	} else {
		end = NULL;
	}
	alarm(TIMEOUT);
	while (end == NULL) {
		if (PACKET_SIZE <= ptr) {
			mylog("%s: bad packet\n", progname);
			ptr = 0;
			return (NULL);
		}
		ret = read(fd, buffer + ptr, PACKET_SIZE - ptr);

		if (ret <= 0 || timeout) {
			if (timeout) {
				mylog("%s: timeout\n", progname);
			} else {
				alarm(0);
			}
			ptr = 0;
			return (NULL);
		}
		buffer[ptr + ret] = 0;
		ptr = strlen(buffer);
		end = strchr(buffer, '\n');
	}
	alarm(0);
	timeout = 0;

	end[0] = 0;
	ptr = strlen(buffer) + 1;
#       ifdef DEBUG
	printf("buffer: -%s-\n", buffer);
#       endif
	return (buffer);
}

enum packet_state {
	P_BEGIN,
	P_MESSAGE,
	P_STATUS,
	P_DATA,
	P_N,
	P_DATA_N,
	P_END
};

int send_packet(int fd, const char *packet)
{
	int done, todo;
	const char *string, *data;
	char *endptr;
	enum packet_state state;
	int status, n;
	__u32 data_n = 0;

	todo = strlen(packet);
	data = packet;
	while (todo > 0) {
		done = write(fd, (void *)data, todo);
		if (done < 0) {
			mylog("%s: could not send packet: %s", progname, strerror(errno));
			return (-1);
		}
		data += done;
		todo -= done;
	}

	/* get response */
	status = 0;
	state = P_BEGIN;
	n = 0;
	while (1) {
		string = read_string(fd);
		if (string == NULL)
			return (-1);
		switch (state) {
		case P_BEGIN:
			if (strcasecmp(string, "BEGIN") != 0) {
				continue;
			}
			state = P_MESSAGE;
			break;
		case P_MESSAGE:
			if (strncasecmp(string, packet, strlen(string)) != 0 || strlen(string) + 1 != strlen(packet)) {
				state = P_BEGIN;
				continue;
			}
			state = P_STATUS;
			break;
		case P_STATUS:
			if (strcasecmp(string, "SUCCESS") == 0) {
				status = 0;
			} else if (strcasecmp(string, "END") == 0) {
				status = 0;
				return (status);
			} else if (strcasecmp(string, "ERROR") == 0) {
				mylog("%s: command failed: %s", progname, packet);
				status = -1;
			} else {
				goto bad_packet;
			}
			state = P_DATA;
			break;
		case P_DATA:
			if (strcasecmp(string, "END") == 0) {
				return (status);
			} else if (strcasecmp(string, "DATA") == 0) {
				state = P_N;
				break;
			}
			goto bad_packet;
		case P_N:
			errno = 0;
			data_n = (__u32) strtoul(string, &endptr, 0);
			if (!*string || *endptr) {
				goto bad_packet;
			}
			if (data_n == 0) {
				state = P_END;
			} else {
				state = P_DATA_N;
			}
			break;
		case P_DATA_N:
			mylog("%s: %s\n", progname, string);
			n++;
			if (n == data_n)
				state = P_END;
			break;
		case P_END:
			if (strcasecmp(string, "END") == 0) {
				return (status);
			}
			goto bad_packet;
			break;
		}
	}
bad_packet:
	mylog("%s: bad return packet", progname);
	return (-1);
}

int irsend(char *command)
{
	char *lircd = NULL;
	char *address = NULL;
	struct sockaddr_un addr_un;
	struct sockaddr_in addr_in;
	int pid, fd;
	struct sigaction act;
	char buffer[PACKET_SIZE + 1];
	int len;
	
	len = strlen(command);
	if (len > PACKET_SIZE) {
		return(-1);
	}

	strncpy(buffer, command, len);
	strcat(buffer, "\n");

	progname = "irsend";

	pid = fork();
 
	if (pid == 0) {
		lircd = "/var/run/lirc/lircd"; 

		act.sa_handler = sigalrm;
		sigemptyset(&act.sa_mask);
		act.sa_flags = 0;	/* we need EINTR */
		sigaction(SIGALRM, &act, NULL);

		addr_un.sun_family = AF_UNIX;
		strcpy(addr_un.sun_path, lircd);
		fd = socket(AF_UNIX, SOCK_STREAM, 0);

		if (fd == -1) {
			mylog("%s: could not open socket: %s", progname, strerror(errno));
			exit(EXIT_FAILURE);
		};

		if (connect(fd,
			    address ? (struct sockaddr *)&addr_in :
			    (struct sockaddr *)&addr_un, address ? sizeof(addr_in) : sizeof(addr_un)) == -1) {
			mylog("%s: could not connect to socket: %s", progname, strerror(errno));
			exit(EXIT_FAILURE);
		};

		if (address)
			free(address);
		address = NULL;

		mylog("%s: %s", progname, command);
		if (send_packet(fd, buffer) == -1) {
			exit(EXIT_FAILURE);
		}

		close(fd);
		exit(0);
	}

	return(0);
}
