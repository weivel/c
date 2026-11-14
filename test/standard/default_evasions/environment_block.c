extern char **environ;

const char *first_variable(void) {
    return environ[0];
}
