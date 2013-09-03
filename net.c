#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>

#include "net.h"
#include "log.h"

/* Input an Ethernet address and convert to binary. */
static int in_ether (char *bufp, unsigned char *addr)
{
#ifdef DEBUG
    char *orig;
    orig = bufp;
#endif
    char c;
    int i;
        unsigned char *ptr = addr;
    unsigned val;

    i = 0;
    while ((*bufp != '\0') && (i < ETH_ALEN)) {
        val = 0;
        c = *bufp++;
        if (isdigit(c))
            val = c - '0';
        else if (c >= 'a' && c <= 'f')
            val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            val = c - 'A' + 10;
        else {
#ifdef DEBUG
            fprintf(stderr, "in_ether(%s): invalid ether address!\n", orig);
#endif
            errno = EINVAL;
            return (-1);
        }
        val <<= 4;
        c = *bufp;
        if (isdigit(c))
            val |= c - '0';
        else if (c >= 'a' && c <= 'f')
            val |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            val |= c - 'A' + 10;
        else if (c == ':' || c == 0)
            val >>= 4;
        else {
#ifdef DEBUG
            fprintf(stderr, "in_ether(%s): invalid ether address!\n", orig);
#endif
            errno = EINVAL;
            return (-1);
        }
        if (c != 0)
            bufp++;
        *ptr++ = (unsigned char) (val & 0377);
        i++;

        /* We might get a semicolon here - not required. */
        if (*bufp == ':') {
            if (i == ETH_ALEN) {
                    ;           /* nothing */
            }
            bufp++;
        }
    }
    return (0);
} /* in_ether */

int udp_broadcast(int port, char *buf, int buflen) {
	struct sockaddr_in dest;
	int sock;
	int bEnable=1;

	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		mylog("error openening UDP socket");
	}

	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bEnable, sizeof(int))) {
		mylog("error setting broadcast permissions");
	}

	memset((char *) &dest, 0, sizeof(struct sockaddr_in));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(port);
	dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	if (sendto(sock, (unsigned char *) buf, buflen, 0, (struct sockaddr *) &dest, sizeof(struct sockaddr_in))==-1) {
		mylog("error sending UDP packet");
	}

	close(sock);
        return(0);
}

int wol(char *addr) {
	unsigned char ethaddr[8];
	char *ptr;
        char buf [128];
	int i, j;

	if (in_ether (addr, ethaddr) < 0) {
		mylog("%s: invalid hardware address", addr);
		return (-1);
	}

	/* Build the message to send - 6 x 0xff then 16 x MAC address */
        ptr = buf;
        for (i = 0; i < 6; i++)
                *ptr++ = 0xff;
        for (j = 0; j < 16; j++)
                for (i = 0; i < ETH_ALEN; i++)
                        *ptr++ = ethaddr [i];

	udp_broadcast(60000, buf, 102);
        return(0);
}
