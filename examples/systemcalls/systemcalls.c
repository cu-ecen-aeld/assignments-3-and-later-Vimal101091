#include "systemcalls.h"

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

    // Run the system command
    int ret = system(cmd);

    if (ret == -1) {
        return false;
    }
    
    return true;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);

    char *command[count + 1];
    for (int i = 0; i < count; i++) {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    va_end(args);

    pid_t pid = fork();
    if (pid < 0) {
        printf("fork failed: %s\n", strerror(errno));
        return false;
    }

    if (pid == 0) {
        // child
        execv(command[0], command);
        printf("execv failed: %s\n", strerror(errno));
        _exit(1);
    } else {
        // parent
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            printf("waitpid failed: %s\n", strerror(errno));
            return false;
        }

        if (WIFEXITED(status)) {
            return WEXITSTATUS(status) == 0;
        } else {
            printf("Child did not exit normally\n");
            return false;
        }
    }
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
     va_list args;
    va_start(args, count);

    char *command[count + 1];
    int i;
    for (i = 0; i < count; i++) {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    va_end(args);

    pid_t pid = fork();
    if (pid < 0) {
        printf("fork failed: %s\n", strerror(errno));
        return false;
    }

    if (pid == 0) {
        // Child process: Redirect stdout to the output file
        int fd = open(outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            printf("open failed: %s\n", strerror(errno));
            _exit(1);
        }

        if (dup2(fd, STDOUT_FILENO) < 0) {
            printf("dup2 failed: %s\n", strerror(errno));
            close(fd);
            _exit(1);
        }

        close(fd); 

        // Execute the command
        execv(command[0], command);

        // execv failed
        printf("execv failed: %s\n", strerror(errno));
        _exit(1);
    } else {
        // Parent process: wait for child to finish
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            printf("waitpid failed: %s\n", strerror(errno));
            return false;
        }

        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            return exit_code == 0;
        } else {
            printf("Child did not exit normally\n");
            return false;
        }
    }
}
