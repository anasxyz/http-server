
# http-server

**HTTP server written from scratch in C**  

> ⚠️ This is an educational project.

## Features
- High performance and low resource usage.
- Full HTTP 1.1 support including request parsing, request routing, keep-alive connections, mime-type detection, and response handling with error codes.
- Event-driven architecture, non-blocking I/O using Linux's `epoll()` to handle thousands of concurrent connections simultaneously without blocking.
- Master-worker model with a Master process using POSIX signals to handle a configurable number of worker processes which deal with connection requests, allowing for more connections to be handled simultaneously
- Highly configurable via external configuration file, supporting virtual hosting, route definitions, URL rewriting, redirection, aliasing, fallbacks, directory autoindexing, and more.
- Scalable and tunable for resource management.
- Fast zero-copy static file serving using Linux's `sendfile()`, along with implemented file caching.
- Custom Hashmap and Timer wheel data structure implementations for O(1) connection storage and lookup to handle server-side connection timeouts.
- CLI tool to check server status, version/build, run/kill server, and more.

## Installation / Build Instructions

```
$ git clone https://github.com/anasxyz/http-server.git    # HTTPS clone  

$ git clone git@github.com:anasxyz/http-server.git        # SSH clone  

$ cd http-server  

$ sudo make                                               # to build

$ sudo make install                                       # to build and install system-wide
```


## Usage

To run the server after installing:
```
$ http-server run
```

To stop/kill the server:
```
$ http-server kill
```

To check the available commands and arguments:
```
$ http-server -h            # or --help
```

To check the server's status (master process PID, total active connections, uptime, etc.) while running:
```
$ http-server -s            # or --status
```

The server automatically runs as a daemon (in the background, detached from the terminal), to run the server in the foreground (in the terminal):
```
$ http-server run -f       # or --foreground
```

To check the server application's version/build:
```
$ http-server -v           # or --version
```

## Configuration


