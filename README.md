# http-server

**A lightweight HTTP web server written from scratch in C**

> ⚠️ This is an educational project.

## Features

- [ ] Configurable server
- [ ] Static file serving with MIME type support  
- [ ] Basic proxying to upstream backend servers  
- [ ] Configurable routing with fallback files  
- [ ] Support for custom error pages  
- [ ] URL rewriting and redirection  
- [ ] Access control for hidden files and directories  
- [ ] Logging (access and error logs)  
- [ ] HTTP connection management (keep-alive, timeouts)  
- [ ] Basic support for compression (gzip)

## Installation / Build Instructions

Step-by-step instructions for compiling and installing the server.

```
git clone https://github.com/anasxyz/http-server.git
cd http-server
make
```


## Usage

```bash
./server
```

## Configuration

Example `server.conf`:

```conf
port 8080
root /var/www/
alias /images/ /var/www/stuff/assets/images/
try_files /index.html /index.htm /homepage.html
```
-   `port`: The TCP port on which the server listens for incoming HTTP connections.
    
-   `root`: The root directory where your static files are served from. If not specified in the configuration file, the default root directory will be set to ```/var/www/```
    
-   `alias`: Maps a URL prefix (e.g., `/images/`) to a different directory on your filesystem.
    
-   `try_files`: Specifies a list of fallback files to try serving if the requested path does not exist. Unlike other servers' equivalent implementation of this configuration option, this server automatically tries the requested file directly first, so you only need to list fallback options.
