#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#define MAX_CHAR 2048
#define MAX_VAR 512

//fg_only stores the status of foreground only mode,
//0 for off, 1 for on.
static int fg_only = 0;

//Struct to store command information
struct command{
  char *input;
  char *output;
  int background;
  char *args[MAX_VAR];
};
static struct command com;

static int childStatus = 0; //Status of last child process
static pid_t bgPID = 0;     //Stores process ID of a background process
static int bgStatus = 0;    //Status of background process                     


char *expand(char *str1);
void fgmode_on(int signo);
void fgmode_off(int signo);
void getStatus(int statNum);

/*----------------------------------------------------------------------*
 *Main function gets user input, expands any instances of $$->PID,      *
 *parses the input into a command struct, and executes the desired      *
 *command. Has built-in capabilities to deal with cd, status, and exit. *
 *----------------------------------------------------------------------*/
int main(){

  //Ignore SIGINT in parent procee
  struct sigaction SIGINT_struct = {.sa_handler = SIG_IGN};
  sigaction(SIGINT, &SIGINT_struct, NULL);

  //Block SIGTSTP in parent
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGTSTP);
  sigprocmask(SIG_SETMASK, &set, NULL);
  struct sigaction SIGTSTP_struct = {.sa_handler = fgmode_on};
  sigaction(SIGTSTP, &SIGTSTP_struct, NULL);

  for (;;){
    //Unblock SIGTSTP in parent
    sigdelset(&set, SIGTSTP);
    sigprocmask(SIG_SETMASK, &set, NULL);
    
    //Set or reset the command struct
    com.input = NULL;
    com.output = NULL;
    com.background = 0;

    //------------------------------------------------------
    //Get input from user
    char *user_input = malloc(MAX_CHAR * sizeof(char) + 1);
    
    printf(": ");
    fflush(NULL);
    fgets(user_input, MAX_CHAR + 1, stdin);

    //Re-block SIGTSTP
    sigaddset(&set, SIGTSTP);
    sigprocmask(SIG_SETMASK, &set, NULL);

    //Check if blank line or comment
    if (strcmp(user_input, "\n") == 0 || strncmp(user_input, "#", 1) == 0){
      goto reset;
    }

    //Expand $$ to the process ID
    char *expanded_input = expand(user_input);

     //Get first token from input and store as command name
    char *tok = strtok(expanded_input, " \n");

    //----------------------------------
    //If command is exit, exit smallsh -
    //----------------------------------
    if (strcmp(tok, "exit") == 0){
      free(expanded_input);
      goto exit;
    }

    //--------------------------------------------------------
    //If command is status, print the most recent exit value,-
    //or termination value. Return to get user input.        -
    //--------------------------------------------------------
    if (strcmp(tok, "status") == 0){
      getStatus(childStatus);
      goto reset;
    }

    //----------------------------------------------------
    //Parse expanded user input, store command arguments -
    //in the command struct. Check for redirections      -
    //and if should be run in the background.            -
    //----------------------------------------------------
    for (int i=0; ; i++){
      if (!tok){ 
        com.args[i] = malloc(sizeof(char) + 1);
        com.args[i] = NULL;
        break;
      }
      if ((strcmp(tok, "&") == 0)){ 
        tok = strtok(NULL, " \n"); //Check if last char in input
        if (!tok){
          if (fg_only == 0){//If last char and foreground only is off
            com.background =1;
          }
          com.args[i] = NULL;
          break;
        }
        //Not last char, continue parsing
        com.args[i] = malloc(sizeof(char) + 1);
        strcpy(com.args[i], "&");
        tok = strtok(NULL, " \n");
        continue;
      }
      //Set input redirect
      if (strcmp(tok, "<") == 0){
        tok = strtok(NULL, " \n");
        com.input = malloc(strlen(tok) + 1);
        strcpy(com.input, tok);
        tok = strtok(NULL, " \n");
        i--;
        continue;
      }
      //Set output redirect
      if (strcmp(tok, ">") == 0){
        tok = strtok(NULL, " \n");
        com.output = malloc(strlen(tok) + 1);
        strcpy(com.output, tok);
        tok = strtok(NULL, " \n");
        i--;
        continue;
      }
      //Save argument and get next token
      com.args[i] = malloc(sizeof(char) * strlen(tok) + 1);
      strcpy(com.args[i], tok);
      tok = strtok(NULL, " \n");
    }

    //---------------------------------------------
    //Change working directory
    if (strcmp(com.args[0], "cd") == 0){
      int dir;
      char *dirt = getcwd(NULL, 100);
      if (com.args[1] == NULL){ //If no args given to cd, get home dir
         dir = chdir(getenv("HOME"));
        }
      else{
       dir = chdir(com.args[1]);
      }
      continue;
    }
   
    //---------------------------------------------------
    //Execute the command in a forked child process
    pid_t spawnPid = fork();

    switch(spawnPid){

      case -1: //Fork failed
        perror("fork()\n");
        exit(1);
        break;

      case 0: //To be done in the child process
        //Ignore SIGTSTP in child process
        SIGTSTP_struct.sa_handler = SIG_IGN;
        sigaction(SIGTSTP, &SIGTSTP_struct, NULL);
  
        //Set SIGINT to default in foreground child process
        if (com.background == 0){
          SIGINT_struct.sa_handler = SIG_DFL;
          sigaction(SIGINT, &SIGINT_struct, NULL); 
        }
        //Redirect I/O if needed
        if (com.input != NULL || com.output != NULL){
          //If com.input is set, redirect Input
          if (com.input){
            int inputFD = open(com.input, O_RDONLY);
            if (inputFD == -1){ //Error opening com.input
              perror("open()");
              exit(1);
            }
            int resultIn = dup2(inputFD, 0);
            if (resultIn == -1){ //Error with dup2
              perror("dup2()");
              exit(2);
            }
          }
          //If com.output is set, redirect outpu
          if (com.output){
            int outputFD = open(com.output, O_CREAT | O_WRONLY | O_TRUNC, 00600);
            if (outputFD == -1){
              perror("open()"); //Error opening com.output
              exit(1);
            }
            int resultOut = dup2(outputFD, 1);
            if (resultOut == -1){ //Error with dup2
              perror("dup2()");
              exit(2);
            }
          }
        }
        //Backgroud process automatically redirect to /dev/null
        //unless specifically directed elsewhere.
        if (com.background == 1){
          if (com.input == NULL){
            int bgInFD = open("/dev/null", O_RDONLY);
            if (bgInFD == -1){
              perror("open()");
              exit(1);
            }
            int bgIn = dup2(bgInFD, 0);
            if (bgIn == -1){
              perror("dup2()");
              exit(2);
            }
          }
          if (com.output == NULL){
            int bgOutFD = open("/dev/null", O_WRONLY);
            if (bgOutFD == -1){
              perror("open()");
              exit(1);
            }
            int bgOut = dup2(bgOutFD, 1);
            if (bgOut == -1){
              perror("dup2()");
              exit(2);
            }
          }
        }
        execvp(com.args[0], com.args);
        perror("execvp");
        exit(2);
        break;

      default: //To be done in the parent process
        //If background process, dont block parent process
        if (com.background == 1){
          bgPID = spawnPid;
          printf("background pid is %d\n", spawnPid);
          fflush(stdout);
          spawnPid = waitpid(spawnPid, NULL, WNOHANG);//Wait without blocking
        }
        else{ //Foreground process
          spawnPid = waitpid(spawnPid, &childStatus, 0);      
          if (WIFSIGNALED(childStatus)){ //Child process terminated abnormally
            printf(" terminated by signal %d\n", WTERMSIG(childStatus));
            fflush(stdout);
          }
        }
    }
    reset:
      if (bgPID != 0){ //Check for a background process
        spawnPid = waitpid(bgPID, &bgStatus, WNOHANG); //returns PID if done
        if (spawnPid == -1){
          perror("waitpid()");
          exit(3);
        }
        else if (spawnPid > 0){ //Process is done
          printf("background pid %d is done ", spawnPid);
          fflush(stdout);
          getStatus(bgStatus);
          bgPID = 0;//Reset
          bgStatus = 0;
        }
      }
    fflush(NULL);
    free(expanded_input);
  }
  //Exit smallsh
  exit:
    return 0;
}

/*---------------------------------------------------------------------------*
 *Expand takes a string pointer to the user input and expands any $$ for PID.*
 *If $$ is not found, the original string is returned, unchanged.            *
 *---------------------------------------------------------------------------*/
char *expand(char *str1){
  char *found = strstr(str1, "$$");
  if (found) {
    int pid = getpid();
    char *pchar = malloc(sizeof(pid));
    sprintf(pchar, "%d", pid); //Set pchar string to the PID

    char *temp = realloc(str1, (strlen(str1) + strlen(pchar) + 3));
    if (!temp){ //Realloc failed
      perror("expand");
      exit(1);
    }
    else {
      str1 = temp;
    }
    char*found = strstr(str1, "$$");

    memmove(found + strlen(pchar) - 2, found, strlen(found) + 1); //Move everything after $$, the length of PID
    memcpy(found, pchar, strlen(pchar)); //Copy PID into new opened space
  }
  return str1;
}
      
//-------------------------------------------------------------
//Handler function for setting foreground only mode on. Sets  -
//fgmode_off as the new handler function for SIGTSTP and sets -
//fg_only variable.                                           -
//-------------------------------------------------------------
void fgmode_on(int signo){
  struct sigaction SIGTSTP_struct = {.sa_handler = fgmode_off};
  sigaction(SIGTSTP, &SIGTSTP_struct, NULL);
  write(STDOUT_FILENO, "entering foreground only mode\n", 31);
  fg_only = 1;
}

//-------------------------------------------------------------
//Handler function for setting foreground only mode off. Sets  -
//fgmode_on as the new handler function for SIGTSTP and sets -
//fg_only variable.                                           -
//-------------------------------------------------------------
void fgmode_off(int signo){
  struct sigaction SIGTSTP_struct = {.sa_handler = fgmode_on};
  sigaction(SIGTSTP, &SIGTSTP_struct, NULL);
  write(STDOUT_FILENO, "exiting foreground only mode\n", 30);
  fg_only = 0;
}

//---------------------------------------------------------------
//Helper function to print the most recent child process status -
//---------------------------------------------------------------
void getStatus(int statNum){
  if (WIFEXITED(statNum)){
    printf("exit status %d\n", WEXITSTATUS(statNum));
    fflush(stdout);
  }
  else if (WIFSIGNALED(statNum)){
    printf("terminated by signal %d\n", WTERMSIG(statNum));
    fflush(stdout);
  }
}

