#ifndef __NET_H__
#define __NET_H__

#define ETH_ALEN        6

int udp_broadcast(int, char *, int);
int wol(char *);

#endif /* __NET_H__ */
