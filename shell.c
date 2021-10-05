/**
 * shell
 * CS 241 - Fall 2021
 */
#include "format.h"
#include "shell.h"
#include "vector.h"

 #include <unistd.h>
 #include <stdio.h>
 #include <sys/types.h>
 #include <sys/wait.h>
 #include <stdlib.h>
 #include <string.h>
 #include <fcntl.h>

typedef struct process {
    char *command;
    pid_t pid;
} process;

static vector* proc_vec = NULL;
static int word_count = 0;
static char* bg_command = NULL;

char** tokenize_input(char* input, char* delim) {
    int i = 0;
    int limit = 10;
    char** tokenized = malloc(limit * sizeof(char*)); // initially allocate 10 slots for tokenized array

    tokenized[i] = strtok(input, delim);
    while(tokenized[i]) {
        i++;
        if(i > limit) {
            limit *= 2;
            tokenized = realloc(tokenized, limit* sizeof(char*));
        }
        tokenized[i] = strtok(NULL, delim);
    }
    word_count = i-1;
    return tokenized;
}

process_info* convert_to_process_info(process* proc) {
    process_info* p_info = malloc(sizeof(process_info));
    p_info->pid = proc->pid;
    p_info->command = strdup(proc->command);
    
    char* status_file = malloc(50);
    snprintf(status_file, 50, "/proc/%d/stat", proc->pid);
    FILE* status = fopen(status_file, "r");

    char * line = NULL;
    size_t len = 0;
    ssize_t read;

    char** tokens;
    while ((read = getline(&line, &len, status)) != -1) {
        tokens = tokenize_input(line, " \t");
        p_info->state = *(tokens[2]);
    }
    free(line);

    p_info->nthreads = (long int)atoi(tokens[19]);
    p_info->vsize = ((unsigned long int)atoi(tokens[22]))/1024;

    unsigned long int cpu_time = (unsigned long int)atol(tokens[13])/sysconf(_SC_CLK_TCK) + (unsigned long int)atol(tokens[14])/sysconf(_SC_CLK_TCK);
    char str[50];
    execution_time_to_string(str, 50, cpu_time/60, cpu_time%60);
    p_info->time_str = strdup(str);

    FILE* proc_stat = fopen("/proc/stat", "r");
    char * line2 = NULL;
    len = 0;
    read = 0;
    unsigned long long boot_time = 0;
    unsigned long long start_time = (unsigned long long)atoll(tokens[21]);

    free(tokens);
    while ((read = getline(&line2, &len, proc_stat)) != -1) {
        char** tok = tokenize_input(line2, " ");
        int i = 0;
        while(tok[i]) {
            if(!strcmp(tok[0], "btime")) {
                boot_time = (unsigned long long)atoll(tok[1]);
                break;
            }
            i++;
        }
        free(tok);
    }

    free(line2);

    time_t p_start = start_time/sysconf(_SC_CLK_TCK) + boot_time;
    struct tm* start_s = localtime(&p_start);
    char new_str[100];
    time_struct_to_string(new_str, 100, start_s);
    p_info->start_str = strdup(new_str);

    return p_info;
}

int check_redirection_operators(char * command) {
    char* output = strstr(command, ">");
    char* append = strstr(command, ">>");
    char* input = strstr(command, "<");
    if(append) {
        return 1;
    } else if (output) {
        return 2;
    } else if (input) {
        return 3;
    }
    return 0;
}

int execute_external_command(char* command, char* args[]) {
    int background_process = 0;
    if(!strcmp(args[word_count], "&")) {
        background_process = 1;
        args[word_count] = NULL;
    }
    fflush(stdout);
    int redirect = check_redirection_operators(command);
    pid_t child = fork();
    if(child == -1) {
        print_fork_failed();
        return 1;
    } else if (child) {
        int status;
        if(background_process) {
            waitpid(child, &status, WNOHANG);
            process* child_proc = malloc(sizeof(process));
            char* com = strdup(bg_command);
            *child_proc = (process){com, child};
            vector_push_back(proc_vec, child_proc);
            free(bg_command);
            bg_command = NULL;
        } else {
            pid_t res = waitpid(child, &status, 0);
            if(res == -1) {
                print_wait_failed();
                exit(1);
            }
        }
        return status;
    } else {
        if(background_process) {
            int s = setpgid(getpid(), getpid());
            if(s == -1) {
                print_setpgid_failed();
            }
        }

        print_command_executed(getpid());
        if(redirect == 1) {
            char* com = strdup(command);
            char** tokens = tokenize_input(com, ">>");
            if(tokens[1]) {
                int append_file = open(tokens[1]+1, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
                if(append_file == -1) {
                    print_redirection_file_error();
                    exit(1);
                }
                fflush(stdout);
                int stdout_s = dup(1);
                dup2(append_file, 1);
                char** com_tok = tokenize_input(com, " ");
                execvp(com_tok[0], com_tok);
                free(com_tok);
                close(append_file);
                dup2(stdout_s, 1);
                close(stdout_s);
            }
            free(tokens);
            free(com);
        } else if(redirect == 2) {
            char* com = strdup(command);
            char** tokens = tokenize_input(com, ">");
            if(tokens[1]) {
                int append_file = open(tokens[1]+1, O_TRUNC | O_WRONLY);
                if(append_file == -1) {
                    print_redirection_file_error();
                    exit(1);
                }
                fflush(stdout);
                int stdout_s = dup(1);
                dup2(append_file, 1);
                char** com_tok = tokenize_input(com, " ");
                execvp(com_tok[0], com_tok);
                free(com_tok);
                close(append_file);
                dup2(stdout_s, 1);
                close(stdout_s);
            }
            free(tokens);
            free(com);
        } else if(redirect == 3) {
            char* com = strdup(command);
            char** tokens = tokenize_input(com, "<");
            if(tokens[1]) {
                int append_file = open(tokens[1]+1, O_RDONLY);
                if(append_file == -1) {
                    print_redirection_file_error();
                    exit(1);
                }
                fflush(stdin);
                int stdin_s = dup(STDIN_FILENO);
                dup2(append_file, STDIN_FILENO);
                char** com_tok = tokenize_input(com, " ");
                execvp(com_tok[0], com_tok);
                free(com_tok);
                close(append_file);
                dup2(stdin_s, STDIN_FILENO);
                close(stdin_s);
            }
            free(tokens);
            free(com);
        } else {
            execvp(args[0], args);
        }
        print_exec_failed(command);
        exit(1);
    }
    return 1;
}

void run_ps(vector* processes) {
    print_process_info_header();
    size_t i = 0;
    for(i = 0; i < vector_size(processes); i++) {
        process_info* proc_i = convert_to_process_info(vector_get(processes, i));
        if(proc_i->state != 'Z') print_process_info(proc_i);
    }
}

int run_commands(char* command) {
    if(!command) return 1;

    char* token = strdup(command);
    char** tokens = tokenize_input(token, " ");
    int ret_value = 1;
    if(!strcmp(tokens[0], "cd")) {
        if(!tokens[1]) {
            print_no_directory("");
            ret_value = 1;
        } else {
            int c = chdir(tokens[1]);
            if(c == -1) {
                print_no_directory(tokens[1]);
                ret_value = 1;
            } else ret_value = 0;
        }
    } else if (!strcmp(tokens[0], "ps")) {
        run_ps(proc_vec);
    } else if (!strcmp(tokens[0], "kill")) {
        if(!tokens[1]) {
            print_invalid_command(command);
        } else {
            pid_t pid = (pid_t)atoi(tokens[1]);
            int sig_result = kill(pid, SIGKILL);
            if(sig_result == -1) {
                print_no_process_found(pid);
            } else {
                print_killed_process(pid, command);
            }
        }
    } else if (!strcmp(tokens[0], "stop")) {
        if(!tokens[1]) {
            print_invalid_command(command);
        } else {
            pid_t pid = (pid_t)atoi(tokens[1]);
            int sig_result = kill(pid, SIGSTOP);
            if(sig_result == -1) {
                print_no_process_found(pid);
            } else {
                print_stopped_process(pid, command);
            }
        }
    } else if (!strcmp(tokens[0], "cont")) {
        if(!tokens[1]) {
            print_invalid_command(command);
        } else {
            pid_t pid = (pid_t)atoi(tokens[1]);
            int sig_result = kill(pid, SIGCONT);
            if(sig_result == -1) {
                print_no_process_found(pid);
            } else {
                print_continued_process(pid, command);
            }
        }
    } else {
        ret_value = execute_external_command(command, tokens);
    }
    free(token);
    free(tokens);
    return ret_value;
}

int check_logical_operators(vector* vec, char* command) {
    char* push_value = strdup(command);
    char* and_sub = strstr(command, "&&");
    char* or_sub = strstr(command, "||");
    char* semi_sub = strstr(command, ";");

    if(and_sub) {
        char** tokens = tokenize_input(push_value, "&&");
        int i = 0;
        while(tokens[i]) {
            if(run_commands(tokens[i])) {
                break;
            }
            i++;
        }
        vector_push_back(vec, command);
        free(push_value);
        free(tokens);
        return 1;
    } else if (or_sub) {
        char** tokens = tokenize_input(push_value, "||");
        int i = 0;
        while(tokens[i]) {
            if(!run_commands(tokens[i])) {
                break;
            }
            i++;
        }
        vector_push_back(vec, command);
        free(push_value);
        free(tokens);
        return 1;
    } else if (semi_sub) {
        char** tokens = tokenize_input(push_value, ";");
        int i = 0;
        while(tokens[i]) {
            run_commands(tokens[i]);
            i++;
        }
        vector_push_back(vec, command);
        free(push_value);
        free(tokens);
        return 1;
    }
    free(push_value);
    return 0;
}

void get_prefix_command(vector* vec, char* command) {
    size_t i = 0;
    for(i = vector_size(vec); i > 0; i--) {
        char* element = strdup(vector_get(vec, i - 1));
        char* substring = strstr(element, command);
        if(substring) {
            int pos = substring - element;
            if(!pos) {
                element[strcspn(element, "\n")] = 0;
                print_command(element);
                if(!check_logical_operators(vec, element)) {
                    run_commands(element);
                    vector_push_back(vec, vector_get(vec, i-1));
                }
                free(element);
                return;
            }
        }
        free(element);
    }
    print_no_history_match();
}

void sigint_handler(int sig) {
    // Do Nothing
}

int shell(int argc, char *argv[]) {
    // TODO: This is the entry point for your shell.
    signal(SIGINT, sigint_handler);       
    char opt;
    int h_arg = 0;
    int f_arg = 0;
    char* h_filename = NULL;
    char* f_filename = NULL;

    while((opt = getopt(argc, argv, "h:f:")) != -1) 
    {
        switch(opt) 
        {
            case 'f':
                f_arg = 1;
                f_filename = optarg;
                break;
            case 'h':
                h_arg = 1;
                h_filename = optarg;
                break;
            default:
                print_usage();
                exit(1);
                break;
        } 
    }

    FILE * h_file = NULL;
    FILE * f_file = NULL;
    char* h_filepath = get_full_path(h_filename);
    char* f_filepath = get_full_path(f_filename);
    size_t read_index = 0;

    if(argc % 2 == 0 || argc > 5) {
        print_usage();
        free(h_filepath);
        free(f_filepath);
        exit(1);
    }

    if(h_filename) {
        h_file = fopen(h_filepath, "a+");
    }

    if(f_filename) {
        f_file = fopen(f_filepath, "r");
        if(!f_file) {
            print_script_file_error();
            free(h_filepath);
            free(f_filepath);
            exit(1);
        }
    }

    vector* commands = string_vector_create();
    proc_vec = shallow_vector_create();

    char * line = NULL;
    size_t len = 0;
    ssize_t read;

    if(h_arg) {
        while ((read = getline(&line, &len, h_file)) != -1) {
            char* newline = strdup(line);
            newline[strcspn(newline, "\n")] = 0;
            vector_push_back(commands, newline);
            read_index++;
            free(newline);
        }
        free(line);
    }

    char *line2 = NULL;
    char* check = NULL;
    int file_len = 0;
    len = 0;
    read = 0;

    if(f_arg) {
        FILE*  file_check = fopen(f_filepath, "r");
        while ((read = getline(&check, &len, file_check)) != -1) {
            file_len++;
        }
        fclose(file_check);
        free(check);
        int c = 0;
        while ((read = getline(&line2, &len, f_file)) != -1 && c < file_len) {
            char* path = getcwd(NULL, 0);
            if (!path) {
                exit(1);
            }
            char* fileline = strdup(line2);
            fileline[strcspn(fileline, "\n")] = 0;
            print_prompt(path, getpid());
            print_command(fileline);
            if(!check_logical_operators(commands, fileline)) {
                run_commands(fileline);
                vector_push_back(commands, fileline);
            }
            free(fileline);
            free(path);
            c++;
        }
        if(h_arg) {
            size_t i =0;
            for(i = read_index; i < vector_size(commands); i++) {
                fputs(vector_get(commands, i), h_file);
                fputs("\n", h_file);
            }
            if(h_file) fclose(h_file);
        }
        free(line2);
        fclose(f_file);
    }

    char* input = NULL;
    size_t length =0;
    int g_line = 0;

    process* shell = malloc(sizeof(process));
    *shell = (process){"./shell", getpid()};
    vector_push_back(proc_vec, shell);

    while (!f_arg) { // Main loop
        char* path = getcwd(NULL, 0);
        if (!path) {
            exit(1);
        }   
        print_prompt(path, getpid());
        g_line = getline(&input, &length, stdin);
        if(g_line == -1 || !strcmp(input, "exit\n")) {
            if(h_arg) {
                size_t i =0;
                for(i = read_index; i < vector_size(commands); i++) {
                    fputs(vector_get(commands, i), h_file);
                    fputs("\n", h_file);
                }
            }
            if(h_file) fclose(h_file);
            free(path);
            free(input);
            break;
        }
        if(input[0] != '\n') {
            char* inp_copy = strdup(input);
            inp_copy[strcspn(inp_copy, "\n")] = 0;
            int logical = check_logical_operators(commands, inp_copy);
            if(!logical) {
                if(!strcmp(inp_copy, "!history")) {
                    size_t i =0;
                    for(i = 0; i < vector_size(commands); i++) {
                        print_history_line(i, vector_get(commands, i));
                    }
                } else if(input[0] == '#') { // Getting element of history
                    if(strlen(inp_copy) == 1) { //Command is just an empty pound sign
                        print_invalid_command(inp_copy);
                    } else {
                        inp_copy[0] = '0';
                        size_t index = (size_t)atoi(inp_copy);
                        if(index < vector_size(commands)) {
                            char* str = strdup(vector_get(commands, index));
                            str[strcspn(str, "\n")] = 0;
                            print_command(str);
                            if(!check_logical_operators(commands, str)) {
                                run_commands(str);
                                vector_push_back(commands, vector_get(commands, index));
                            }
                            free(str);
                        } else {
                            print_invalid_index();
                        }
                    }
                } else if(input[0] == '!') {
                    char* trimmed = calloc(sizeof(inp_copy), 1);
                    strncpy(trimmed, inp_copy+1, strlen(inp_copy));
                    get_prefix_command(commands, trimmed);
                    free(trimmed);
                } else {
                    char* token = strdup(inp_copy);
                    if(token[strlen(token)-1] == '&') {
                        bg_command = strdup(token);
                    }

                    vector_push_back(commands, inp_copy);
                    run_commands(token);
                    free(token);
                }
            }
            free(path);
            free(inp_copy);
        }
    }

    vector_destroy(commands);
    free(h_filepath);
    free(f_filepath);

    size_t i = 1;
    for(i = 1; i < vector_size(proc_vec); i++) {
        process* proc = vector_get(proc_vec, i);
        kill(proc->pid, SIGKILL);
    }

    vector_destroy(proc_vec);

    return 0;
}
