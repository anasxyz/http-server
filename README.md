
# http-server

**HTTP server written from scratch in C**  

> ⚠️ This is an educational project. Not for production use.

## Table of Contents
1. [Features](#features)
2. [Prerequisites](#prerequisites)
3. [Installation / Build Instructions](#installation--build-instructions)
4. [Usage](#usage)
5. [Configuration](#configuration)
    - [Global directives](#global-directives)
    - [HTTP Block](#http-block)
    - [Host Block](#host-block)
    - [SSL Sub-block](#ssl-sub-block)
    - [Route block](#route-block)
6. [Limits](#limits)

## Features
- High performance and low resource usage.
- HTTP 1.1 support including request parsing, request routing, keep-alive connections, mime-type detection, and response handling with error codes.
- Event-driven architecture, non-blocking I/O using Linux's `epoll()` to handle thousands of concurrent connections simultaneously without blocking.
- Master-worker model with a Master process using POSIX signals to handle a configurable number of worker processes which deal with connection requests, allowing for more connections to be handled simultaneously
- Highly configurable via external configuration file, supporting virtual hosting, route definitions, URL rewriting, redirection, aliasing, fallbacks, directory autoindexing, and more.
- Scalable and tunable for resource management.
- Fast zero-copy static file serving using Linux's `sendfile()`, along with implemented file caching.
- Custom Hashmap and Timer wheel data structure implementations for O(1) connection storage and lookup to handle server-side connection timeouts.
- CLI tool to check server status, version/build, run/kill server, and more.

## Prerequisites

Before building and running http-server, make sure your system has the following:

- Linux-based OS (Ubuntu, Debian, Fedora, or similar).

- C Compiler such as `gcc` or `clang`.

- `make` build tool.

- Optional: sudo privileges if you want to install the server system-wide.

> 📌 Note: This server is designed and tested for Linux. Other operating systems may not be fully supported.

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

The server automatically runs as a daemon (in the background, detached from the terminal), to run the server in the foreground (in the terminal):
```
$ http-server run -f         # or --foreground
```

By default, the path for the configuration file will be `/etc/http-server/http-server.conf` after installation. If you want to specify a different path for a configuration file:
```
$ http-server -c <file>      # or --config <file>
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

To check the server application's version/build:
```
$ http-server -v           # or --version
```

## Configuration

A typical configuration file can be found in `config/http-server.conf`. As you can see, it is inspired by Nginx, as most of this server is really. Only with a few tiny differences.

#### Global directives:

`max_connections` - maximum number of simultaneous that can be handled. 
> 📌 This Normally would be higher than expected traffic for the server, but also keep ing mind system limits like file descriptor limits (`ulimit -n`).

`worker_processes` - number of request handling processes to be spawned. 
> 📌 This should generally be set equal to the number of CPU cores on the machine running the server (e.g., 4 for a quad core).

`user` - system user to run the server as (e.g., www-data). (⚠️ not implemented yet)

`pid_file` - file path to store the master process's PID.

`log_file` - file path for main server log file. (⚠️ not implemented yet)

#### HTTP Block
Controls HTTP-wide defaults: `http.new ... http.end`   

`default_buffer_size`, `body_buffer_size`, `headers_buffer_size` - memory buffer sizes for request/response handling.
> 📌 Using larger buffers can help with big requests, but keep in mind that each connection consumes its own buffers. If too many clients are connected at the same time, this can increase memory usage and negatively affect overall performance.

`mime` - path to MIME types definition file.

`default_type` - default fallback MIME type when main MIME type cannot be determined (e.g., `text/plain`).

`access_log` - file path for HTTP requests. (⚠️ not implemented yet)

`error_log` - file path for errors. (⚠️ not implemented yet)

`log_format` - logging format for server's log messages. (⚠️ not implemented yet)

`sendfile` - enable or disable the use of the zero-copy file serving. When this is disabled, the server will use `write()` instead of `sendfile()`.

#### Host Block
Defines a virtual host: `host.new ... host.end`

`listen` - port number to listen on.

`name` - domain names served by this host (e.g. example.com, myserver.net) 

`content_dir` - root directory for files. By default it will be `/var/www/`.

`index_files` - default index file(s) when a directory is requested (e.g., index.html, index.htm)

`timeout` - idle timeout per connection (e.g. `13s`, `1m`, `2h`).
> 📌 Shorter timeouts may save resources but also may disconnect slow clients.

#### SSL Sub-block
For HTTPS hosts: `ssl.new ... ssl.end`

`cert_file` - file path to SSL/TLS certificate. (⚠️ not implemented yet)

`key_file` - file path to private key. (⚠️ not implemented yet)

`protocols` - allowed TLS protocol versions. (⚠️ not implemented yet)
> 📌 Only put modern protocols like TLSv1.2 and TLSv1.3.

`ciphers` - allowed cipher suites. (⚠️ not implemented yet)
> 📌 Use strong ciphers.

#### Route block
Defines routing rules inside a host: `route.new ... route.end`

`uri` - requested path prefix this route applies to (e.g., `/`, `/images/`, `/page1`).

`content_dir` - override host's root directory for this route.

`index_files` - override host's index files list for this route.

`allow / deny` - IP based access control. Supports single IPs, IP lists, or CIDR ranges). (⚠️ not implemented yet) 
> 📌 `allow` takes precedence over `deny`

`return` - immediately return a specific HTTP status code (e.g., 301, 503). (⚠️ not implemented yet) 

`redirect` - redirect the request to a different URL. (⚠️ not implemented yet) 

`proxy_url` - reverse proxy destination for this route. (⚠️ not implemented yet) 

`etag_header` - custom ETag header for cache validation. (⚠️ not implemented yet)

`expires_header` - set cache expiration time (e.g., 1m, 1h) (⚠️ not implemented yet)

## Limits

- No full HTTP/1.1 spec coverage (chunked encoding, POST body limits).
- Basic security only (no rate limiting, sandbox, etc.).
- Configuration parser is simple and not as robust as other bigger servers like nginx/apache.
- And a lot more... so it is NOT by any means a full-fledged server or intended for production use (yet), it's just a project for education purposes.
