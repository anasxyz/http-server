#ifndef _UTIL_H_
#define _UTIL_H_

/**
 * @brief logs a message to the server log file or stdout/stderr.
 * @param type the type of log message (e.g., 'E' for error).
 * @param fmt the format string for the log message.
 * @param extra_fmt the format string for any additional data to be logged.
 */
void logs(char type, const char *fmt, const char *extra_fmt, ...);

/**
 * @brief exits the program with a status code 1.
 * I forgot why I needed this function.
 */
void exits();

/**
 * @brief sets the socket to non-blocking mode.
 * @param fd the file descriptor of the socket.
 * @return 0 on success, -1 on failure.
 */
int set_nonblocking(int fd);

/**
 * @brief sets up a socket to listen on the provided port with all the required
 * settings to listen for incoming connections.
 * @param fd the file descriptor of the socket.
 * @return 0 on success, -1 on failure.
 */
int setup_listening_socket(int port);

/**
 * @brief gets the status message for a given status code.
 * @param code the status code.
 * @return a pointer to the status message string.
 */
char *get_status_message(int code);

#endif // _UTIL_H_
