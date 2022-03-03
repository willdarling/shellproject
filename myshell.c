#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Specified print function */
void myPrint(char *msg)
{
    write(STDOUT_FILENO, msg, strlen(msg));
}

/* Error shorthand */
void myError() 
{
    char error_message[30] = "An error has occurred\n";
    write(STDOUT_FILENO, error_message, strlen(error_message));
}

void myFree(int argc, char **argv) 
{
    int i;
    for(i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
}

/* Sets up batch mode by either returning the FILE* of stdin or the batch file */
void changeInput(char* path) 
{
    int in;
    if((in = open(path, O_RDONLY)) == -1) {
        myError();
        exit(1);
    }
    dup2(in, STDIN_FILENO); 
}

int allWhiteSpace(char *arr) 
{
    int len = strlen(arr);
    int i;
    for (i = 0; i < len; i++) {
        if(isgraph(arr[i])) {
            return 0;
        }
    }
    return 1;
}

/* Returns 1 if the length of the command line exceeds 512 bytes
 * as well as prints an error and the rest of the line
*/
int tooLong(char *cmd_buff) 
{
    int len;
    if((len = strlen(cmd_buff)) > 513) {
        myPrint(cmd_buff);
        if (cmd_buff[513] != '\n') {
            char arb_buff[2048];
            if (fgets(arb_buff, 2048, stdin) == NULL) {
                myError();
                exit(1);
            } 
            myPrint(arb_buff);
        }
        myError();
        return 1;
    }
    return 0;
}

/* Takes a full command line and parses tokens seperated by white space 
 * storing the tokens in a variable length, heap allocated, NULL terminated
 * array. Stores the length of the array (excluding the NULL) in *argc 
 */
char** getCommand(int *argc, char *buff)
{
    char **argv; 
    char *arg, *state, *rdr;
    
    argv = malloc(514);

    int i = 0;
    for (arg = strtok_r(buff," \r\n\t",&state); arg; arg = strtok_r(NULL," \r\n\t",&state)) {
        if ((rdr = strchr(arg,'>')) != NULL) {
            *rdr = '\0';             
            if (arg[0] != '\0') {
                argv[i++] = strdup(arg);
            }
            if (rdr[1] == '+') {
                argv[i++] = strdup(">+");
                rdr += 2;                           
            }
            else {
                argv[i++] = strdup(">");
                rdr++;
            }
            if (rdr[0] != '\0') {
                argv[i++] = strdup(rdr);
            }
        }
        else {
            argv[i++] = strdup(arg);
        }
    }

    argv[i] = malloc(sizeof(char*));
    argv[i] = (char*) NULL;
    *argc = i;
    return argv;
}

/* Checks to see if the command is a built in command, executes it if it is so
 * and returns 1 if a command was executed, 0 otherwise 
 */
int executeBuiltIn(int argc, char **argv) 
{
    char *arg = argv[0];

/* Check for built-in commands */
    /* exit */
    if (!strcmp(arg, "exit")) {
        if (argc != 1) {
            myError();
            return 1;
        }
        exit(0);
    }

    /* pwd */
    if (!strcmp(arg, "pwd")) {
        if (argc != 1) {
            myError();
            return 1;
        }
        char buff[200];
        if (getcwd(buff, 200) == NULL) {
            myError();
            return 1;
        }
        myPrint(buff);
        myPrint("\n");
        return 1;
    }

    /* cd */
    if (!strcmp(arg, "cd"))  {
        if (argc > 2) {
            myError();
            return 1;
        }
        char *path = NULL;
        if (argc == 1) {
            path = getenv("HOME");
        }
        else if (argc == 2) {
            path = argv[1];
        }
        if (path == NULL) {
            myError();
            return 1;
        }
        if (chdir(path)) { //chdir returns 0 on success
            myError();
        }
        return 1;
    }       

    return 0;
}

int setupAdvRdr(char* path) 
{
    int out, trueout;
    char *truepath;
    
    truepath = path;
    path = "pleasedeargoddonotlettherebeafilewiththisnameohsweetbabyjesus";

    if((out = open(path, O_CREAT|O_WRONLY, S_IWUSR|S_IRUSR)) == -1) {
        myError();
        return -1;
    }

    pid_t pid = fork();

    if (pid == 0) {
        return out;
    }

    else {
        int status;
        waitpid(pid, &status, 0);

        if ((trueout = open(truepath, O_CREAT|O_RDONLY, S_IWUSR|S_IRUSR)) == -1) {
            return -1;
        } 

        char buff[64];
        while (read(trueout, buff, 64)) {
            write(out, buff, 64);
        }

        if(rename(path, truepath) == -1) {
            myError();
            exit(1);
        }

        exit(0);
        return -1;   
    }
}

/* Standard redirection setup */
int setupRdr(char* path)
{
    return open(path, O_CREAT|O_EXCL|O_WRONLY, S_IRUSR|S_IWUSR);
}

/* Sets up the redirection and reformats argv to get rid of the redirection
 * arguments. Returns NULL if the redirection arguments are malformed, the
 * correct argv list otherwise.
 */
char** redirect(int argc, char **argv)
{
    int out, i;
    char *arg, *path;

/* Determine if there is redirection, and what kind it is */
    int flag = -1;

    for (i = 0; i < argc; i++) {
        arg = argv[i];
        if (!strcmp(arg, ">")) {
            flag = 0;
            argv[i] = (char*) NULL; //Terminates argv before redirection sign
            i++;
            break;
        }
        if (!strcmp(arg, ">+")) {
            flag = 1;
            argv[i] = (char*) NULL; //See above
            i++;
            break;
        }
    }
    if (flag == -1) { //No redirection, return argv unmodified
        return argv;
    }

    if (argc-i != 1) { //Too many args after redirection sign
        return NULL;
    }

    path = argv[i];
    
    if (flag) { //Setup advanced redirection
        out = setupAdvRdr(path);
    }
    else {
        out = setupRdr(path);
    }
    
    if (dup2(out, STDOUT_FILENO) == -1) {
        myError();
        exit(1);
    }
   
    return argv;
}

/* Executes a command using execvp(). This function should only ever be
 * called by a child process as it will always exit() at completion
 */
void executeCommand(int argc, char **argv) 
{
    
    if((argv = redirect(argc, argv)) == NULL) { //Something went wrong with redirection
        myError(); 
        exit(1);
    }

    if((execvp(argv[0], argv)) == -1) { //Something went wrong with executing the command
        myError();
        exit(1);
    }
    exit(1);
}

/* Executes valid commands and throws errors if invalid */
void execute(int argc, char **argv) 
{
/* Built-In Command */
    if (executeBuiltIn(argc, argv)) {
        return;
    }

/* Other Command */
    pid_t pid = fork();

    if (pid == 0) {
        executeCommand(argc, argv);
    }
    else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    } 
    else {
        myError();
        exit(1);
    }
}

/* main loop */
int main(int argc, char *argv[]) 
{

    if(argc > 2) {
        myError();
        exit(1);
    }

    char cmd_buff[514];
    char *pinput;
    int cmd_argc;
    char **cmd_argv; 
    char *sbuff;
    char *cmd;

    int batch = (argc == 2);

    if(batch) {
        changeInput(argv[1]);
    }

    while (1) { 
    /* Get the command line input */
        if (!batch) {
            myPrint("myshell> ");
        }
        if ((pinput = fgets(cmd_buff, 520, stdin)) == NULL) {
            exit(0);
        }
        if (allWhiteSpace(cmd_buff) || tooLong(cmd_buff)) {
            continue;
        }
        if (batch) {
            myPrint(cmd_buff);
        }

    /* Break up the commands by semicolon and execute them */
        for (cmd = strtok_r(cmd_buff,";",&sbuff); cmd; cmd = strtok_r(NULL,";",&sbuff)) {
            
            cmd_argv = getCommand(&cmd_argc, cmd);

            if(*cmd_argv != NULL) { //More than just a single NULL
                execute(cmd_argc, cmd_argv);
            }

            myFree(cmd_argc, cmd_argv);  
        }
    }
}

