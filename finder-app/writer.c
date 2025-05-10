#include <stdio.h>
#include <string.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
    // Initialize syslog 
    openlog("WriteToFileApp", LOG_CONS, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "Insufficient arguments");
        closelog();
        return 1;
    }

    const char *filename = argv[1];
    const char *text = argv[2];

    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        syslog(LOG_ERR, "Failed to open file '%s'", filename); 
        closelog();
        return 1;
    }

    if (fwrite(text, sizeof(char), strlen(text), fp) < strlen(text)) {
        syslog(LOG_ERR, "Failed to write to file '%s'", filename);
        fclose(fp);
        closelog();
        return 1;
    } else {
        syslog(LOG_INFO, "Successfully wrote '%s' to '%s'", text, filename);
    }

    fclose(fp);
    closelog();
    return 0;
}
