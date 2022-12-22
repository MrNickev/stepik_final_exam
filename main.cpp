#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <cerrno>
#include "getopt.h"
#include <regex>
#include <fcntl.h>
#include <sstream>
#include <pthread.h>
#include <string>
#include <sys/epoll.h>
#include <cstring>
#include <signal.h>



using namespace std;

string serv_dir{};


struct fds {
    int epollfd;
    int sockfd;
};

int set_nonblock(int fd) {
    int flags;
#if defined(O_NONBLOCK)
    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;
    return fcntl(fd, F_SETFL, (unsigned) flags | O_NONBLOCK);
#else
    flags = 1;
    return ioctl(fd, FIONBIO, &flags);
#endif

}

inline void add_file_descriptor(int epollfd, int fd, bool oneshot) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    if (oneshot)
        event.events |= EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblock(fd);
}

inline void reset_oneshot(int &epfd, int &fd) {
    struct epoll_event ep_event;
    ep_event.data.fd = fd;
    ep_event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ep_event);
}

string parse_request(const string &for_parse) {

    size_t request_type_pos = for_parse.find("GET /");
    size_t protocol_pos = for_parse.find(" HTTP/1");
    if (request_type_pos == string::npos || protocol_pos == string::npos) return "";
    string ind = for_parse.substr(request_type_pos + 5, protocol_pos - request_type_pos - 5);
    if (ind.size() == 0) return "index.html";

    auto pos = ind.find('?');
    if (pos == string::npos)
        return ind;
    else
        return ind.substr(0, pos);
}

string http_error_404() {
    stringstream ss;
    ss << "HTTP/1.0 404 NOT FOUND";
    ss << "\r\n";
    return ss.str();
}

string http_ok_200(const string &data) {
    stringstream ss;
    ss << "HTTP/1.0 200 OK";
    ss << data;
    return ss.str();
}

inline void f(int &fd, const string &request) {
    string f_name = parse_request(request);
    if (f_name == "") {
        string err = http_error_404();
        send(fd, err.c_str(), err.length() + 1, MSG_NOSIGNAL);
        return;
    } else {
        stringstream ss;
        ss << serv_dir;
        if (serv_dir.length() > 0 && serv_dir[serv_dir.length() - 1] != '/')
            ss << "/";
        ss << f_name;

        FILE *file_in = fopen(ss.str().c_str(), "r");
        char arr[1024];
        if (file_in) {
            stringstream ss;
            string tmp_str;
            char c = '\0';
            while ((c = fgetc(file_in)) != EOF) {
                ss << c;
            }
            tmp_str = ss.str();
            string ok = http_ok_200(tmp_str);
            send(fd, ok.c_str(), ok.size(), MSG_NOSIGNAL);
            fclose(file_in);
        } else {
            string err = http_error_404();
            send(fd, err.c_str(), err.size(), MSG_NOSIGNAL);
        }

    }
}


void *worker(void *arg) {
    int sockfd = ((struct fds *) arg)->sockfd;
    int epollfd = ((struct fds *) arg)->epollfd;
    printf("start new thread to receive data on fd: %d\n", sockfd);
    char buf[10];
    memset(buf, '\0', 10);

    string receive_buf;

    for (;;) {
        int ret = recv(sockfd, buf, 10 - 1, 0);
        if (ret == 0) {
            close(sockfd);
            printf("foreigner closed the connection\n");
            break;
        } else if (ret < 0) {
            if (errno = EAGAIN) {
                reset_oneshot(epollfd, sockfd);
                f(sockfd, receive_buf);

                break;
            }
        } else {
            receive_buf += buf;
        }
    }
    printf("end thread receiving data on fd: %d\n", sockfd);
    pthread_exit(0);
}

int run(const int argc, const char **argv) {
    int masterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (masterSocket < 0)
        perror("fail to create socket!\n"), exit(errno);

    struct sockaddr_in socket_address;
    bzero(&socket_address, sizeof(socket_address));
    socket_address.sin_family = AF_INET;
    get_command_line(argc, (char **) (argv), socket_address, serv_dir);
    int opt = 1;
    if (setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        exit(errno);
    }
    if (bind(masterSocket, (struct sockaddr *) &socket_address, sizeof(socket_address)) < 0) {
        perror("fail to bind socket");
        exit(errno);
    }

    set_nonblock(masterSocket);
    listen(masterSocket, SOMAXCONN);

    struct epoll_event events[1024];
    int epfd = epoll_create1(0);
    if (epfd == -1)
        perror("fail to create epoll\n"), exit(errno);

    add_file_descriptor(epfd, masterSocket, false);

    for (;;) {
        int ret = epoll_wait(epfd, events, 1024, -1);  //Permanent Wait
        if (ret < 0) {
            printf("epoll wait failure!\n");
            break;
        }
        int i;
        for (i = 0; i < ret; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == masterSocket) {
                struct sockaddr_in slave_address;
                socklen_t slave_addrlength = sizeof(slave_address);
                int slaveSocket = accept(masterSocket, (struct sockaddr *) &slave_address, &slave_addrlength);
                add_file_descriptor(epfd, slaveSocket, true);
            } else if (events[i].events & EPOLLIN) {
                pthread_t thread;
                struct fds fds_for_new_worker;
                fds_for_new_worker.epollfd = epfd;
                fds_for_new_worker.sockfd = events[i].data.fd;

                /*Start a new worker thread to serve sockfd*/
                pthread_create(&thread, NULL, worker, &fds_for_new_worker);

            } else {
                printf("Happen unexpected event\n");
            }
        }

    }
    close(masterSocket);
    close(epfd);
    return 0;
}

static void skeleton_daemon() {
    pid_t pid;

    pid = fork();

    if (pid < 0)
        exit(EXIT_FAILURE);

    if (pid > 0)
        exit(EXIT_SUCCESS);

    if (setsid() < 0)
        exit(EXIT_FAILURE);
    
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    pid = fork();

    if (pid < 0)
        exit(EXIT_FAILURE);

    if (pid > 0)
        exit(EXIT_SUCCESS);

    umask(0);

    chdir("/");

    int x;
    for (x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
        close(x);
    }
}

int main(const int argc, const char **argv) {
    skeleton_daemon();
    while (1) {
        run(argc, argv);
    }
    return EXIT_SUCCESS;
}