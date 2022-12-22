#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/bind/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <streambuf>
#include <vector>
#include <syslog.h>
#include <unistd.h>

using namespace std;
using boost::asio::ip::tcp;

namespace misc_strings {
    const char name_value_separator[] = { ':', ' ' };
    const char crlf[] = { '\r', '\n' };
} // namespace misc_strings

void cout_time() {
    time_t now = time(0);
    cout << ctime(&now);
}

class session
{
public:
    session(boost::asio::io_context& io_context, const string& directory)
            : strand_(boost::asio::make_strand(io_context)),
              socket_(strand_),
              directory_(directory)
    {
    }

    tcp::socket& socket()
    {
        return socket_;
    }

    void start()
    {
        socket_.async_read_some(boost::asio::buffer(data_, max_length),
                                boost::bind(&session::handle_read, this,
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred));
    }

private:
    void handle_read(const boost::system::error_code& error,
                     size_t bytes_transferred)
    {
        if (!error)
        {
            cout << "------------------------------------------" << endl;
            cout_time();
            string request = string(data_, bytes_transferred);
            cout << request;
            cout.flush();

            vector<string> lines;
            boost::split(lines, request, boost::is_any_of("\n"));
            cout << "first line: " << lines[0] << endl;

            vector<string> tokens;
            boost::split(tokens, lines[0], boost::is_any_of(" "));
            cout << "method: " << tokens[0] << endl;
            cout << "uri: " << tokens[1] << endl;
            cout << "http: " << tokens[2] << endl;

            string uri = tokens[1];
            string path = uri;
            size_t size = path.find('?');
            if (size != string::npos) {
                path = path.substr(0, size);
            }
            cout << "path: " << path << endl;
            string file_path = directory_ + path;
            cout << "file_path: " << file_path << endl;

            std::vector<boost::asio::const_buffer> buffers;

            ifstream stream(file_path);
            string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            if (stream.is_open()) {
                cout << "content: " << content << endl;
                buffers.push_back(boost::asio::buffer("HTTP/1.0 200 OK"));
            } else {
                cout << "Error openning file" << endl;
                buffers.push_back(boost::asio::buffer("HTTP/1.0 404 Not Found"));
            }
            stream.close();

            buffers.push_back(boost::asio::buffer(misc_strings::crlf));
            buffers.push_back(boost::asio::buffer("Content-Type: text/html; charset=utf-8"));
            buffers.push_back(boost::asio::buffer(misc_strings::crlf));
            string content_length = "Content-Length: " + boost::lexical_cast<std::string>(content.size());
            buffers.push_back(boost::asio::buffer(content_length));
            buffers.push_back(boost::asio::buffer(misc_strings::crlf));
            buffers.push_back(boost::asio::buffer(misc_strings::crlf));
            buffers.push_back(boost::asio::buffer(content));

            boost::asio::async_write(socket_, buffers,
                                     boost::bind(&session::handle_write, this,
                                                 boost::asio::placeholders::error));
        }
        else
        {
            delete this;
        }
    }

    void handle_write(const boost::system::error_code& error)
    {
        if (!error)
        {
            boost::system::error_code ignored_ec;
            socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
            socket_.close();
        }
        else
        {
            delete this;
        }
    }

    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    tcp::socket socket_;
    enum { max_length = 1024 };
    char data_[max_length];
    string directory_;
};

class server
{
public:
    server(boost::asio::io_context& io_context, const string& address, const short& port, const string& directory)
            : io_context_(io_context),
              acceptor_(io_context),
              address_(address),
              port_(port),
              directory_(directory)
    {
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::make_address_v4(address), port);
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();

        start_accept();
    }

    void run()
    {
        cout_time();
        cout << "ip: " << address_ << endl;
        cout << "port: " << port_ << endl;
        cout << "directory: " << directory_ << endl;
        cout << endl;

        // Create a pool of threads to run all of the io_contexts.
        std::vector<boost::shared_ptr<boost::thread> > threads;
        for (std::size_t i = 0; i < 8; ++i)
        {
            boost::shared_ptr<boost::thread> thread(new boost::thread(
                    boost::bind(&boost::asio::io_context::run, &io_context_)));
            threads.push_back(thread);
        }

        // Wait for all threads in the pool to exit.
        for (std::size_t i = 0; i < threads.size(); ++i)
            threads[i]->join();
    }

private:
    void start_accept()
    {
        session* new_session = new session(io_context_, directory_);
        acceptor_.async_accept(new_session->socket(),
                               boost::bind(&server::handle_accept, this, new_session,
                                           boost::asio::placeholders::error));
    }

    void handle_accept(session* new_session,
                       const boost::system::error_code& error)
    {
        if (!error)
        {
            new_session->start();
        }
        else
        {
            delete new_session;
        }

        start_accept();
    }

    boost::asio::io_context& io_context_;
    tcp::acceptor acceptor_;
    string address_;
    short port_;
    string directory_;
};

int main(int argc, char *argv[])
{
    try
    {
        int opt;
        string address;
        int port;
        string directory;
        while ((opt = getopt(argc, argv, "d:h:p:")) != -1) {
            switch (opt) {
                case 'h':
                    address = optarg;
                    break;
                case 'p':
                    port = atoi(optarg);
                    break;
                case 'd':
                    directory = optarg;
                    break;
                default:
                    cout << "error" << endl;
                    return 1;
            }
        }

        boost::asio::io_context io_context;

        // Initialise the server before becoming a daemon. If the process is
        // started from a shell, this means any errors will be reported back to the
        // user.
        server s(io_context, address, port, directory);

        // Register signal handlers so that the daemon may be shut down. You may
        // also want to register for other signals, such as SIGHUP to trigger a
        // re-read of a configuration file.
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait(
                boost::bind(&boost::asio::io_context::stop, &io_context));

        // Inform the io_context that we are about to become a daemon. The
        // io_context cleans up any internal resources, such as threads, that may
        // interfere with forking.
        io_context.notify_fork(boost::asio::io_context::fork_prepare);

        // Fork the process and have the parent exit. If the process was started
        // from a shell, this returns control to the user. Forking a new process is
        // also a prerequisite for the subsequent call to setsid().
        if (pid_t pid = fork())
        {
            if (pid > 0)
            {
                // We're in the parent process and need to exit.
                //
                // When the exit() function is used, the program terminates without
                // invoking local variables' destructors. Only global variables are
                // destroyed. As the io_context object is a local variable, this means
                // we do not have to call:
                //
                //   io_context.notify_fork(boost::asio::io_context::fork_parent);
                //
                // However, this line should be added before each call to exit() if
                // using a global io_context object. An additional call:
                //
                //   io_context.notify_fork(boost::asio::io_context::fork_prepare);
                //
                // should also precede the second fork().
                exit(0);
            }
            else
            {
                syslog(LOG_ERR | LOG_USER, "First fork failed: %m");
                return 1;
            }
        }

        // Make the process a new session leader. This detaches it from the
        // terminal.
        setsid();

        // A process inherits its working directory from its parent. This could be
        // on a mounted filesystem, which means that the running daemon would
        // prevent this filesystem from being unmounted. Changing to the root
        // directory avoids this problem.
        chdir("/");

        // The file mode creation mask is also inherited from the parent process.
        // We don't want to restrict the permissions on files created by the
        // daemon, so the mask is cleared.
        umask(0);

        // A second fork ensures the process cannot acquire a controlling terminal.
        if (pid_t pid = fork())
        {
            if (pid > 0)
            {
                exit(0);
            }
            else
            {
                syslog(LOG_ERR | LOG_USER, "Second fork failed: %m");
                return 1;
            }
        }

        // Close the standard streams. This decouples the daemon from the terminal
        // that started it.
        close(0);
        close(1);
        close(2);

        // We don't want the daemon to have any standard input.
        if (open("/dev/null", O_RDONLY) < 0)
        {
            syslog(LOG_ERR | LOG_USER, "Unable to open /dev/null: %m");
            return 1;
        }

        // Send standard output to a log file.
        const char* output = "/tmp/asio.daemon.out";
        const int flags = O_WRONLY | O_CREAT | O_APPEND;
        const mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
        if (open(output, flags, mode) < 0)
        {
            syslog(LOG_ERR | LOG_USER, "Unable to open output file %s: %m", output);
            return 1;
        }

        // Also send standard error to the same log file.
        if (dup(1) < 0)
        {
            syslog(LOG_ERR | LOG_USER, "Unable to dup output descriptor: %m");
            return 1;
        }

        // Inform the io_context that we have finished becoming a daemon. The
        // io_context uses this opportunity to create any internal file descriptors
        // that need to be private to the new process.
        io_context.notify_fork(boost::asio::io_context::fork_child);

        // The io_context can now be used normally.
        syslog(LOG_INFO | LOG_USER, "Daemon started");
        //io_context.run();
        s.run();
        syslog(LOG_INFO | LOG_USER, "Daemon stopped");
    }
    catch (std::exception& e)
    {
        syslog(LOG_ERR | LOG_USER, "Exception: %s", e.what());
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}