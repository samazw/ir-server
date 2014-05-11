#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

#include "log.h"
#include "net.h"
#include "cec.h"
#include "irsend.h"

#define CHECK_REPEAT (kcount%2 == 0 && kcount != 2 && kcount != 4)

#define PACKET_SIZE 256
#define TIMEOUT 3

int dofork = 1;
int status = STATE_OFF;

void  ALARMhandler(int sig)
{
  signal(SIGALRM, SIG_IGN);          /* ignore this signal       */
  check_state(status);
  signal(SIGALRM, ALARMhandler);     /* reinstall the handler    */
  if (status != STATE_OFF) {
	  alarm(30);
  }
}

int sgetline(int fd, char ** out) 
{ 
    int buf_size = 128; 
    int bytesloaded = 0; 
    int ret;
    char buf; 
    char * buffer = malloc(buf_size); 
    char * newbuf; 

    if (NULL == buffer)
        return -1;

    while ((ret = read(fd, &buf, 1)) != 0) {
        if (ret < 1) {
            // error or disconnect
            free(buffer);
            return -1;
        }

        buffer[bytesloaded] = buf; 
        bytesloaded++; 

        // has end of line been reached?
        if (buf == '\n') 
            break; // yes

        // is more memory needed?
        if (bytesloaded >= buf_size) 
        { 
            buf_size += 128; 
            newbuf = realloc(buffer, buf_size); 

            if (NULL == newbuf) 
            { 
                free(buffer);
                return -1;
            } 

            buffer = newbuf; 
        } 
    } 

    // if the line was terminated by "\r\n", ignore the
    // "\r". the "\n" is not in the buffer
    if ((bytesloaded) && (buffer[bytesloaded-1] == '\r'))
        bytesloaded--;

    *out = buffer; // complete line
    return bytesloaded; // number of bytes in the line, not counting the line break
}

int main(int argc, char *argv[])
{
	int fd, i, ret;
	char *buf1, *buf2, *ptr, *token;
	int kcount;
	struct sockaddr_un addr;
	int c;
	pid_t pid;

	char *lircd = "/var/run/lirc/lircd";
	// char *pidfile = "/var/run/ir-server.pid";
	char *pidfile = NULL;

	addr.sun_family = AF_UNIX;

	static struct option long_options[] = {
		{"foreground", no_argument, NULL, 'f'},
		{"pidfile", no_argument, NULL, 'p'},
		{"lircd", no_argument, NULL, 'l'},
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'v'},
		{0, 0, 0, 0}
	};

	while ((c = getopt_long(argc, argv, "chvfpl", long_options, NULL))
	       != EOF) {
		switch (c) {
		case 'h':
			printf("Usage: %s [socket]\n", argv[0]);
			printf("\t -h --help \t\tdisplay usage summary\n");
			printf("\t -v --version \t\tdisplay version\n");
			return (EXIT_SUCCESS);
		case 'p':
			pidfile = optarg;
			break;
		case 'l':
			lircd = optarg;
			break;
		case 'f':
			dofork = 0;
			break;
		case '?':
			fprintf(stderr, "unrecognized option: -%c\n", optopt);
			fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
			return (EXIT_FAILURE);
		}
	}
	if (argc == optind) {
		/* no arguments */
		strcpy(addr.sun_path, lircd);
	} else if (argc == optind + 1) {
		/* one argument */
		strcpy(addr.sun_path, argv[optind]);
	} else {
		fprintf(stderr, "%s: incorrect number of arguments.\n", argv[0]);
		fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
		return (EXIT_FAILURE);
	}

	mylog_open(dofork, "ir-server");

	if (dofork) {
                pid = fork();
                if (pid == -1) {
                        fprintf(stderr, "fork() failed: %s", strerror(errno));
                        return(1);
                } else if (pid != 0) {
                        // parent
                        return(0);
                }

                /* Clean up */
                setsid();
                chdir("/");
                if((fd = open("/dev/null", O_RDWR)) < 0) {
                        mylog("open(\"/dev/null\") failed: %s", strerror(errno));
                        return(0);
                }

                for (i = 0; i < 3; ++i) {
                        if (dup2(fd, i) < 0) {
                                mylog("dup2() failed for %d: %s", i, strerror(errno));
                        }
                }
        }

	if (pidfile != NULL) {
		pid_t curpid;
		FILE *f;

		curpid = getpid();

		f = fopen(pidfile, "w");
		if(f == NULL) {
			mylog("Can't open pidfile %s", pidfile);
			return(1);
		}
		fprintf(f, "%i\n", curpid);
		fclose(f);
	}

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		perror("socket");
		exit(errno);
	};
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		perror("connect");
		exit(errno);
	};

	cec_init(1, NULL);

	// Turn off everything
	all_power_off();
	udp_broadcast(33333, "Power", strlen("Power"));

	signal(SIGALRM, ALARMhandler);

	for (;;) {
		// read a single line
		ret = sgetline(fd, &buf1);
		if (ret < 0)
			break; // error/disconnect

		for (i=0, buf2=buf1;; i++, buf2=NULL) {
			token = strtok_r(buf2, " ", &ptr);
			if (token == NULL)
				break;
			if (i == 1)
				kcount = atoi(token);
			if (i == 2)
				break;
		}
		mylog("%d: %d %s\n", i, kcount, token);

		if((!strcmp(token, "KEY_POWER") || !strcmp(token, "KEY_POWER2")) && kcount == 0) {
			alarm(0);
			if (status == STATE_OFF) {
				tv_power_on();
				wol("54:04:a6:ed:2e:29");
				irsend("SEND_ONCE PHILIPS_TV POWER_ON 3");
				sleep(2);
				irsend("SEND_ONCE PHILIPS_TV POWER_ON 3");
				philips_hts_power_on();
				sleep(11);
				philips_hts_set_audio_input(1);
				sleep(1);
				tv_set_input_xbmc();
				status = STATE_XBMC;
				alarm(5);
			} else {
				all_power_off();
				udp_broadcast(33333, "Power", strlen("Power"));
				status = STATE_OFF;
				alarm(30);
			}
		} else if(! strcmp(token, "KEY_VOLUMEUP") && CHECK_REPEAT) {
			alarm(0);
			philips_hts_volume_up();
			alarm(30);
		} else if(! strcmp(token, "KEY_VOLUMEDOWN") && CHECK_REPEAT) {
			alarm(0);
			philips_hts_volume_down();
			alarm(30);
		} else if(! strcmp(token, "KEY_MUTE") && CHECK_REPEAT) {
			alarm(0);
			volume_mute();
			alarm(30);
		} else if(! strcmp(token, "KEY_CHANNELUP") && CHECK_REPEAT) {
			udp_broadcast(33333, "Up", strlen("Up"));
		} else if(! strcmp(token, "KEY_CHANNELDOWN") && CHECK_REPEAT) {
			udp_broadcast(33333, "Down", strlen("Down"));
		} else if (CHECK_REPEAT) {
			udp_broadcast(33333, token, strlen(token));
		}

		free(buf1);
	}
	return(0);
}
