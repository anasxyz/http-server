#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "defaults.h"
#include "server.h"

int kill_server() {
  FILE *f = fopen(global_config->pid_file, "r");
  if (!f)
    return -1;
  int pid;
  fscanf(f, "%d", &pid);
  fclose(f);

  if (kill(pid, SIGTERM) == -1) {
    perror("Failed to kill server");
    return -1;
  }
  return 0;
}

void daemonise() {
  pid_t pid = fork();
  if (pid < 0)
    exit(EXIT_FAILURE); // fork failed
  if (pid > 0)
    exit(EXIT_SUCCESS); // parent exits

  if (setsid() < 0)
    exit(EXIT_FAILURE); // new session
  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP, SIG_IGN);

  pid = fork();
  if (pid < 0)
    exit(EXIT_FAILURE);
  if (pid > 0)
    exit(EXIT_SUCCESS);

  umask(0);

  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
}

int is_server_running() {
  if (!global_config->pid_file)
    return 0;

  FILE *f = fopen(global_config->pid_file, "r");
  if (!f)
    return 0;

  int pid;
  if (fscanf(f, "%d", &pid) != 1) {
    fclose(f);
    return 0;
  }
  fclose(f);

  if (kill(pid, 0) == 0) {
    return 1;
  } else {
    if (errno == ESRCH)
      return 0;
    return 1;
  }
}

int get_total_connections() {
  const char *shm_name = "/server_connections";

  int shm_fd;
  atomic_int *total_connections_shm;
  int connection_count = -1;

  // open the shared memory object in read-only mode
  shm_fd = shm_open(shm_name, O_RDONLY, 0666);
  if (shm_fd == -1) {
    if (errno == ENOENT) {
      return 0;
    }
    perror("ERROR: shm_open failed");
    return -1;
  }

  total_connections_shm =
      mmap(NULL, sizeof(atomic_int), PROT_READ, MAP_SHARED, shm_fd, 0);
  if (total_connections_shm == MAP_FAILED) {
    perror("ERROR: mmap failed");
    close(shm_fd);
    return -1;
  }

  connection_count = atomic_load(total_connections_shm);

  munmap(total_connections_shm, sizeof(atomic_int));
  close(shm_fd);

  return connection_count;
}
void display_status() {
  FILE *f;
  int pid;

  int status = is_server_running();
  if (status == 0) {
    printf("Server is not running.\n");
    return;
  } else if (status == -1) {
    printf("Could not determine server status due to an error.\n");
    return;
  }

  f = fopen(global_config->pid_file, "r");
  if (!f) {
    // this case should not be hit if is_server_running() returned 1
    perror("Failed to open PID file after server was confirmed running");
    return;
  }
  fscanf(f, "%d", &pid);
  fclose(f);

  printf("Server Status:\n");
  printf("  PID: %d\n", pid);

  // get the uptime by reading from the /proc filesystem
  char proc_path[256];
  snprintf(proc_path, sizeof(proc_path), "/proc/%d/stat", pid);

  f = fopen(proc_path, "r");
  if (f) {
    long start_time_ticks;
    long system_uptime_ticks;
    long clock_ticks_per_sec = sysconf(_SC_CLK_TCK);

    // read the 22nd field (start time) from the stat file
    // the * in %*d tells scanf to read and discard the value
    // reading all the fields is more robust than just seeking
    if (fscanf(f,
               "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u "
               "%*d %*d %*d %*d %*d %*d %ld",
               &start_time_ticks) == 1) {
      fclose(f);

      f = fopen("/proc/uptime", "r");
      if (f) {
        if (fscanf(f, "%ld", &system_uptime_ticks) == 1) {
          fclose(f);

          long uptime_seconds =
              (long)(system_uptime_ticks -
                     (start_time_ticks / (double)clock_ticks_per_sec));
          long hours = uptime_seconds / 3600;
          long minutes = (uptime_seconds % 3600) / 60;
          long seconds = uptime_seconds % 60;

          printf("  Uptime: %ldh %ldm %lds\n", hours, minutes, seconds);
        } else {
          fclose(f);
          printf("  Error: Failed to read system uptime.\n");
        }
      } else {
        printf("  Error: Failed to open /proc/uptime.\n");
      }
    } else {
      fclose(f);
      printf("  Error: Failed to parse process stat file.\n");
    }
  } else {
    printf("  Error: Failed to open /proc/%d/stat. Check permissions or if the "
           "process has terminated.\n",
           pid);
  }

  printf("  Total Connections: %d\n", get_total_connections());

  if (global_config) {
    printf("  Config File: %s\n", global_config->pid_file);
    printf("  Workers: %d\n", global_config->worker_processes);
  }
}

void print_usage() {
  printf("Usage: %s [run | kill | restart] [OPTIONS]\n", NAME);
  printf("\nOptions:\n");
  printf("  -c <file>, --config <file>   Specify config file (default: "
         "/etc/%s/%s.conf)\n",
         NAME, NAME);
  printf("  -h, --help                   Show this help message\n");
  printf("  -v, --version                Show version\n");
  printf("  -f, --foreground             Run the server in the foreground\n");
  printf("  -s, --status                 Check if the server is running\n");
}

int cli_handler(int argc, char *argv[]) {
  char *config_path = DEFAULT_CONFIG_PATH;
  int foreground = 0;
  char *command = NULL;

  if (argc < 2) {
    print_usage();
    return 1;
  }

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
      if (i + 1 < argc) {
        config_path = argv[i + 1];
        i++;
      }
    } else if (strcmp(argv[i], "-f") == 0 ||
               strcmp(argv[i], "--foreground") == 0) {
      foreground = 1;
    } else {
      if (command == NULL) {
        command = argv[i];
      }
    }
  }

  load_config(config_path);
  check_config();

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage();
      return 0;
    } else if (strcmp(argv[i], "-v") == 0 ||
               strcmp(argv[i], "--version") == 0) {
      printf("%s version %s\n", NAME, VERSION);
      return 0;
    } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--status") == 0) {
      if (is_server_running() == 1) {
        display_status();
      } else {
        printf("Server is not running.\n");
      }
      return 0;
    }
  }

  if (command == NULL) {
    print_usage();
    return 1;
  }

  pid_t pid = getpid();

  if (strcmp(command, "run") == 0) {
    if (is_server_running()) {
      fprintf(stderr, "Server is already running.\n");
      return 1;
    }
    printf("Starting server with PID %d...\n", pid);
    printf("Run '%s kill' to kill the server.\n", argv[0]);
    if (!foreground) {
      daemonise();
    }
    start_server();
  } else if (strcmp(command, "kill") == 0) {
    if (!is_server_running()) {
      fprintf(stderr, "Server is not running.\n");
      return 1;
    }
    printf("Killing server...\n");
    kill_server();
  } else if (strcmp(command, "restart") == 0) {
    if (is_server_running()) {
      printf("Restarting server...\n");
      kill_server();
      sleep(1);
    }
    printf("New server instance is running.\n");
    if (!foreground) {
      daemonise();
    }
    start_server();
  } else {
    fprintf(stderr, "Unknown command '%s'\n", command);
    print_usage();
    return 1;
  }

	return 0;
}
