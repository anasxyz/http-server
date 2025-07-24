

# http-server

**A lightweight HTTP web server written from scratch in C**  

> ⚠️ This is an educational project.

## Features
- ✅ Proud to mention that it's completely memory safe with no memory leaks.
- ✅ Partial implementation of the HTTP protocol with request parsing and response generation.
- ✅ Partial Event-driven architecture with non-blocking I/O via `epoll`.
- ✅ Fast and efficient zero-copy static file serving with full MIME type support and directory indexing.
- ✅ Basic reverse proxying to upstream backend servers.  
- ✅ Basic routing engine supporting URL rewriting and redirection, fallback files, and aliasing.
- ✅ Easy to use compact configuration system with flexible syntax.
- ✅ Basic Logging and monitoring with customisable access and error logs.

---

- ❌ Advanced configuration support for environment variables and dynamic reloads without downtime.
- ❌ Security features including access control, hidden file protection, IP address filtering, and support for HTTPS/TLS encryption.
- ❌ HTTP/1.1 and HTTP/2 support with keep-alive connection management, pipelining, and request multiplexing.
- ❌ Support for virtual hosts (multiple domains on one server).
- ❌ Support for HTTP caching headers, ETags, and conditional GET requests. 
- ❌ Compression support with gzip and brotli to optimise bandwidth usage.
- ❌ Modular and extensible architecture with support for custom plugins and extensions.
- ❌ Configurable rate limiting and connection throttling to protect against DDoS attacks.
- ❌ Support for WebSocket proxying and HTTP/2 server push.

## Installation / Build Instructions

Step-by-step instructions for compiling and installing the server.

```
$ git clone https://github.com/anasxyz/http-server.git
$ cd http-server
$ make
```

## Usage

```bash
./server
```

## Configuration

Example `server.conf`:

```conf
# Server settings
port 8080
root /var/www/

# Logging
access_log /var/log/http-server/access.log
error_log /var/log/http-server/error.log

# Static file handling
index /index.html /index.htm
try_files $uri $uri/ /404.html

# URL mappings
alias /images/ /var/www/assets/images/
alias /docs/ /var/www/manuals/

# Reverse proxy
proxy /api/ http://localhost:5050/
proxy /external/ http://localhost:5050/page/hello
proxy /other/ http://example.com/
```

-   `port`: The TCP port on which the server listens for incoming HTTP connections.

-   `root`: The root directory where your static files are served from. If not specified in the configuration file, the default root directory will be set to `/var/www/`

- `access_log`: The path to the access log file.

- `error_log`: The path to the error log file.

- `index`: List of index files to serve when a directory is requested (e.g. `/index.html`, `/index.htm`).

-   `try_files`: Specifies a list of fallback files to try serving if the requested path does not exist.
	- `$uri`: Serve requested URI directly.
	- `$uri/`: Serve requested but treated as a directory.
	- Any remaining paths (e.g. `/404.html`) are treated as fallback files if the previous options fail.

-   `alias`: Maps a URL prefix (e.g., `/images/`) to a different directory on your filesystem.

- `proxy`: Forwards requests to an upstream server. You can rewrite the target path:
	- `proxy /api/ http://localhost:5050/` → `/api/foo` becomes `/foo` on upstream server.
	- `proxy /external/ http://localhost:5050/page/hello` → `/external/test` becomes `/page/hello/test`.

