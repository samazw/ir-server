#ifndef __LOG_H__
#define __LOG_H__

#define SYSLOG 1

void mylog_open(int, char *); 
void mylog(char *, ...);

#endif /* __LOG_H__ */
