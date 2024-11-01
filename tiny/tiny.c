/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_header(int fd, char *filename, int filesize, char *version);
void serve_static(int fd, char *filename, int filesize, char *version);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);

    printf("# Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit
    Close(connfd);  // line:netp:tiny:close
  }
}

void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s\n", buf); // GET / HTTP/1.1
  sscanf(buf, "%s %s %s", method, uri, version);
  // strcasecmp(const char *s1, const char *s2);
  //  : 문자열이 같으면 0, s1이 s2 보다 작으면 0보다 작은 값 ...
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(fd, method, "501", "NOT IMPLEMENTED", "Tiny couldn't find this file");
    return;
  }
  read_requesthdrs(&rio);
  is_static = parse_uri(uri, filename, cgiargs);
  // stat(const char *filename, struct stat *stat);
  //  : filename 이름을 가진 파일의 상태나 파일의 정보를 얻는 함수
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "NOT FOUND", "Tiny couldn't find this file");
    return;
  }

  if (strcasecmp(method, "HEAD") == 0) {
      serve_header(fd, filename, sbuf.st_size, version);
      return;
  }

  if (is_static) {
    // S_ISREG(m) => m & 0170000
    //  : 일반 파일인 경우 0이 아닌 값 리턴
    // S_IRUSR, S_IREAD => 00400
    //  : 소유자 읽기 권한
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "FORBIDDEN", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, version);
  } else {
    // S_IXUSR, S_IEXEC => 00100
    //  : 소유자 실행/검색(디렉토리) 권한
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "FORBIDDEN", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  sprintf(body, "<html><title>Tiny Error</html></title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));  
}

void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];
  int i = 0;

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("[%d]: %s", ++i, buf);
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;
  
  // #include <string.h>
  // char *strstr(const char *s1, const char *s2);
  // finds the first occurrence of s2 in s1
  if(!strstr(uri, "cgi-bin")) {
    // strcpy(char *s1, const char *s2);
    //  : s1 에 s2 를 복사
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    // strcat(char *s1, const char *s2);
    //  : s2 를 s1 에 연결
    strcat(filename, uri);
    if (uri[strlen(uri) - 1] == '/') {
      strcat(filename, "home.html");
    }
    return 1;
  } 
  // /cgi-bin/adder?123&456
  ptr = index(uri, '?');
  if(ptr) {
    strcpy(cgiargs, ptr + 1);
    *ptr = '\0';
  } else {
    strcpy(cgiargs, "");
  }
  strcpy(filename, ".");
  strcat(filename, uri);
  return 0;
}

void serve_header(int fd, char *filename, int filesize, char *version) {
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  get_filetype(filename, filetype);
  sprintf(buf, "%s 200 OK\r\n", version);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "%sConnection: close\r\n", buf);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
}

void serve_static(int fd, char *filename, int filesize, char *version) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];
  
  serve_header(fd, filename, filesize, version);

  printf("%s", buf);

  srcfd = Open(filename, O_RDONLY, 0);
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  srcp = (char *)Malloc(filesize);
  Rio_readn(srcfd, srcp, filesize);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  // Munmap(srcp, filesize);
  Free(srcp);
}

void serve_dynamic(int fd, char *filename, char *cgiargs) {
  char buf[MAXLINE], *emptylist[] = { NULL };
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) {
    setenv("QUERY_STRING", cgiargs, 1);
    Dup2(fd, STDOUT_FILENO);
    Execve(filename, emptylist, environ);
  }
  wait(NULL);
}

void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html")) {
    strcpy(filetype, "text/html");
  } else if (strstr(filename, ".gif")) {
    strcpy(filetype, "image/gif");
  } else if (strstr(filename, ".png")) {
    strcpy(filetype, "image/png");
  } else if (strstr(filename, ".jpg")) {
    strcpy(filetype, "image/jpeg");
  } else if (strstr(filename, ".mpg")) {
    strcpy(filetype, "video/mpeg");
  } else {
    strcpy(filetype, "text/plain");
  }
}