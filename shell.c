#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"
#include "list.h"
/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))
int MAX_SIG = 31;
int num_bg_processes = 0;
int num_bg_programs = 0;
/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;
char *redirect_right = ">";
char *redirect_left = "<";
char *pipe_char = "|";


/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;
struct list process_list;
struct process_node node;
typedef struct process_node { 
  struct termios* terminal_settings;
  int pid;
  int bg; 
  struct list_elem elem;
} process_node_t;
/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens* tokens);
int cmd_help(struct tokens* tokens);
int cmd_pwd(struct tokens* tokens);
int cmd_cd(struct tokens* tokens);
int cmd_wait(struct tokens* tokens);


/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens* tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t* fun;
  char* cmd;
  char* doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
    {cmd_help, "?", "show this help menu"},
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_pwd, "pwd", "prints the current working directory"},
    {cmd_cd, "cd", "changes the current working directory to that supplied"},
    {cmd_wait, "wait", "waits for all background processes to finish"},
};
/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens* tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}
int cmd_pwd(unused struct tokens* tokens) { 
  char cwd[4096];
  if (getcwd(cwd, 4096) != NULL) { 
    printf("%s\n", cwd);
  } else { 
    perror("Error getting the cwd");
  }
  return 0;
}
int cmd_cd(unused struct tokens* tokens) { 
 char *desired_cwd;
 if (tokens_get_length(tokens) == 1) { 
  desired_cwd = "";
 } else { 
  desired_cwd = tokens_get_token(tokens, 1); 
 }
  if (chdir(desired_cwd) != 0) { 
    perror("Error changing directories");
  }
  return 0;
}
int cmd_wait(unused struct tokens* tokens) {
  printf("%d", num_bg_programs);
  while (num_bg_programs) { 
    continue;
  }
  printf("%d", num_bg_programs);
  return 0;
}
/* Exits this shell */
int cmd_exit(unused struct tokens* tokens) { exit(0); }

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}
void sigchild_handler(int signum) { 
  num_bg_programs -= 1;
}
int set_handler(int set_val) { 
  struct sigaction sa;
  if (set_val == 1) { 
    sa.sa_handler = SIG_DFL;
  } else { 
    
    sa.sa_handler = SIG_IGN;
  }
  sa.sa_flags = 0;
  //iterate over all signal numbers, ignoring all except the REQ noes
  for (int i = 1; i <= MAX_SIG; i++) {
    if (i != SIGKILL && i != SIGSTOP) {  //not sure whetehr we are even allowed to change it even if back to default. so just in case wil just leave them out
      if (sigaction(i, &sa, NULL) == -1) { 
        perror("sigaction failed 2");
        return 1;
      }
    }
  }
  if (set_val) { 
    struct sigaction sa2;
    sa2.sa_handler = sigchild_handler;
    sa2.sa_flags = SA_NODEFER; // async handling

    if (sigaction(SIGCHLD, &sa2, NULL) == -1) { 
      perror("sigaction failed 2");
      return 1;
    }
  }
  return 0;

}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
    
  }
}
int execute_process(struct tokens* tokens, int start_index, int end_index) {
  char* final_token = tokens_get_token(tokens, tokens_get_length(tokens) - 1);
  if (strcmp(final_token, "&") == 0) { 
    end_index -= 1;
  }
  char *input_path = tokens_get_token(tokens, start_index);
  int input_path_len = strlen(input_path);
  for (int i = start_index; i < end_index; i++) {
    char *token = tokens_get_token(tokens, i);
    if ((strcmp(token, redirect_left) == 0) || (strcmp(token, redirect_right) == 0) || (strcmp(token, pipe_char) == 0)) {
      end_index = i; 
      break;
    }
  }
  size_t num_args = end_index - start_index; //note: end index is not inclusive whic
  char **args = (char **) malloc(sizeof(char*) * (num_args+1));
  int args_index = 0;
  for (int i = start_index; i < end_index; i++) {
    char *token = tokens_get_token(tokens, i);
    args[args_index] = (char *) malloc(strlen(token) + 1);
    strcpy(args[args_index], token);
    args_index += 1;
  }
  args[num_args] = NULL;
  execv(input_path, args);

  char *all_paths = getenv("PATH");
  if (all_paths == NULL) { 
    perror("path env not set");
  }

  char *curr_path = strtok(all_paths, ":");
  while (curr_path != NULL) { 
    
    int len_curr_path = strlen(curr_path);    
    args[0] = malloc(len_curr_path + input_path_len + 2);
    strcpy(args[0], curr_path);
    args[0][len_curr_path] = '/';
    strcpy(args[0] + len_curr_path + 1, input_path);

    execv(args[0], args);
    curr_path = strtok(NULL, ":");
  }
  perror("program was not executed!");
  return 1;
}
void remove_node(int desired_pid) { 
  struct list_elem *ptr;
  for (ptr = list_begin(&process_list); ptr != list_end(&process_list); ptr = list_next(ptr)) {
      process_node_t *node;
      node = list_entry(ptr, process_node_t, elem);
      if (node->pid == desired_pid) { 
        list_remove(&node->elem);
        if (node->bg) { 
          num_bg_processes -= 1;
        }
        return;
      }
  }
  perror("node not in process list - cannot remove");
  
}
void add_node(int pid, int bg) { 
  process_node_t *new_node;
  new_node = malloc(sizeof(process_node_t));
  if (new_node == NULL) { 
    perror("malloc failed");
    exit(1);
  }
  new_node->terminal_settings = malloc(sizeof(struct termios));
  new_node->pid = pid;
  new_node->bg = bg;
  if (bg) { 
    num_bg_processes += 1;
  }
  list_push_back(&process_list, &new_node->elem);
}
int main(unused int argc, unused char* argv[]) {
  init_shell();

  list_init(&process_list); //initalize our list for processes

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens* tokens = tokenize(line);
    size_t num_args = tokens_get_length(tokens);
    char* final_token = tokens_get_token(tokens, num_args - 1);  
    int fundex = lookup(tokens_get_token(tokens, 0));
    int background_process = 0;
    if (fundex == 1) {
      cmd_table[fundex].fun(tokens);
    }
    if (strcmp(final_token, "&") == 0) { 
      background_process = 1;
    }
    int pid1 = fork();
    if (pid1 > 0) { //parent
      if (set_handler(0)) { 
        perror("setting handler failed for parent");
        return 1;
      };
      add_node(pid1, background_process);
      if (!background_process) { 
        int status;
        waitpid(pid1, &status, 0);
        if (status) { 
          perror("sub shell failed");
        }
      } else { 
        num_bg_programs += 1;
      }
      remove_node(pid1);        
      if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);
      /* Clean up memory */
      tokens_destroy(tokens);
    } else if (pid1 < 0) { 
      perror("error while forking");
    } else { 
      //ELSE WE ARE THE CHILD SHELL which will run the program

      /* Find which built-in function to run. */
      if (fundex >= 0) {
        cmd_table[fundex].fun(tokens);
      } else {
        if (background_process) { //in the case where we are a background process change the gpid
          if (setpgid(0, 0) == -1) { 
            perror("setpgid failed");
            exit(1); // Terminate child process
          }
        }
        //find theh # of pipes if any
        int num_pipes = 0;
        for (int i = 0; i < num_args; i++) {
          char *token = tokens_get_token(tokens, i);
          if (strcmp(token, pipe_char) == 0) { 
            num_pipes += 1;
          }
        }
        //set up our pipe array accordingly 
        int pipefds[num_pipes][2];
        int prev_index = 0;
        int pipe_index = 0;
        char* prev_token = "";
        bool pipe_seen = false;

        for (int i = 0; i < num_args; i++) {
          char *token = "";
          while (i < num_args) {
            token = tokens_get_token(tokens, i);
            if (strcmp(token, pipe_char) == 0 || strcmp(token, redirect_left) == 0  || strcmp(token, redirect_right) == 0 ) { 
              break;
            }
            i += 1;
          }
          if (strcmp(token, redirect_right) == 0) { 
              char *filename = tokens_get_token(tokens, i+1); //expects <program> > filename.txt
              int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
              if (fd == -1) { 
                perror("error opening file");
              }
            
              int ret = dup2(fd, STDOUT_FILENO);
              if (ret == -1) { 
                perror("error while duping");
              }
              close(fd);
            } else if (strcmp(token, redirect_left) == 0) { 

              char *filename = tokens_get_token(tokens, i+1); //expects <program> < filename.tx
              int fd = open(filename, O_RDONLY | O_CREAT, 0644);
              if (fd == -1) { 
                perror("error opening file");
              }
              int ret = dup2(fd, STDIN_FILENO);
              if (ret == -1) { 
                perror("error while duping");
              }
              close(fd);
            } else if (strcmp(token, pipe_char) == 0 || i == num_args) { //is a pipe or last proces in the pipeland/CL
              
              if (pipe(pipefds[pipe_index]) == -1) { 
                perror("error while piping");
              }
              
              int pid = fork();
              if (pid == 0) { 
                if (strcmp(token, pipe_char) == 0) { 
                  if (pipe_index < num_pipes) { 
                  close(pipefds[pipe_index][0]); //close the reading pipe for the child;
                  }
                  int ret;
                  if (!pipe_seen) { //edge case for the first programe
                    ret = dup2(pipefds[pipe_index][1], STDOUT_FILENO); //link child's stdout to the write pipe
                    if (ret == -1) {
                      perror("error while dupin 1");
                    }
                  } else { //normal intermediate case
                    //if ur middle child, u first read from the read pipe from the prev pipe, close it, then use that to exec
                    ret = dup2(pipefds[pipe_index-1][0], STDIN_FILENO); 
                    if (ret == -1) {
                      perror("error while duping 2");
                    }
                    close(pipefds[pipe_index-1][0]); //after stin to prev write, we no longer need this pipe
                    if (pipe_index < num_pipes) { 
                      ret = dup2(pipefds[pipe_index][1], STDOUT_FILENO); //link child's stdout to the write pipe
                      if (ret == -1) {
                        perror("error while duping 3");
                      }
                    }
                  } 
                }
                if (i == num_args && pipe_seen && strcmp(prev_token, pipe_char) == 0) { //edge case
                  int ret = dup2(pipefds[pipe_index-1][0], STDIN_FILENO); 
                  if (ret == -1) {
                    perror("error while duping 4");
                  }
                  close(pipefds[pipe_index-1][0]);
                }
                execute_process(tokens, prev_index, i);
                perror("exec failed");
              } else if (pid > 0) { 
                //parent process
                if (strcmp(token, pipe_char) == 0) { 
                  close(pipefds[pipe_index][1]);
                  pipe_seen = true;
                  pipe_index += 1;
                }
                
                int status;
                if (!background_process) { 
                  waitpid(pid, &status, 0);
                }
            
                
                waitpid(pid, &status, 0);
                
                if (status) { 
                  perror("error from child exit!");
                }
                
                prev_index = i + 1;
                prev_token = token;
              } else { 
                perror("fork failed");
              }
              
              }
            }
        }
        exit(0);
      }
  }
  return 0;
}
