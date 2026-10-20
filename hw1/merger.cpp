#include "merger_parser.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <vector>
#include <string>

//elif

#define PIPE(fd) socketpair(AF_UNIX, SOCK_STREAM, 0, fd)

static const char *col_type_to_str(column_type_t t) {
    switch (t) {
        case TYPE_TEXT: return "text";
        case TYPE_NUM:  return "num";
        case TYPE_DATE: return "date";
        default:        return "error";
    }
}

static const char *cmp_to_str(filter_cmp_t c) {
    switch (c) {
        case CMP_GT: return "-g";
        case CMP_LT: return "-l";
        case CMP_EQ: return "-e";
        case CMP_GE: return "-ge";
        case CMP_LE: return "-le";
        case CMP_NE: return "-ne";
        default:     return "-error";
    }
}

static void execl_creator(operator_t *op) {

    std::string str = std::to_string(op->column);
    const char* col_to_str = str.c_str();

    switch (op->type) {
        case OP_SORT:
            if (op->reverse)
                execl("./sort", "sort", "-c", col_to_str, "-t", col_type_to_str(op->col_type), "-r", (char *)NULL);
            else
                execl("./sort", "sort", "-c", col_to_str, "-t", col_type_to_str(op->col_type), (char *)NULL);
            break;

        case OP_FILTER:
            execl("./filter", "filter","-c", col_to_str, "-t", col_type_to_str(op->col_type),cmp_to_str(op->cmp), op->cmp_value,(char *)NULL);
            break;

        case OP_UNIQUE:
            execl("./unique", "unique", "-c", col_to_str, "-t", col_type_to_str(op->col_type), (char *)NULL);
            break;

        default:
            fprintf(stderr, "no operator found\n");
            exit(EXIT_FAILURE);
    }
    perror("execl");
    exit(EXIT_FAILURE);
}


int create_operator_process(operator_chain_t &chain, const char* filename, std::vector<pid_t> &op_pids, std::vector<pid_t> &feeder_pids) {
    int K = chain.num_ops;

    int fd_feed[2];
    if (PIPE(fd_feed) == -1) { perror("PIPE fd_feed"); exit(EXIT_FAILURE); }

    int fd_out[2];
    if (PIPE(fd_out) == -1) { perror("PIPE fd_out"); exit(EXIT_FAILURE); }

    // intermediate pipes
    int fd_mid[MAX_OPERATORS][2];
    for (int j = 0; j < K - 1; j++) {
        if (PIPE(fd_mid[j]) == -1) { perror("PIPE fd_mid"); exit(EXIT_FAILURE); }
    }

    // forks
    for (int j = 0; j < K; j++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }

        if (pid == 0) {
            // child
            int in_fd  = (j == 0)     ? fd_feed[0]    : fd_mid[j-1][0];
            int out_fd = (j == K - 1) ? fd_out[1]      : fd_mid[j][1];

            dup2(in_fd,  STDIN_FILENO);
            dup2(out_fd, STDOUT_FILENO);

            // close 
            close(fd_feed[0]); close(fd_feed[1]);
            close(fd_out[0]);  close(fd_out[1]);
            for (int k = 0; k < K - 1; k++) {
                close(fd_mid[k][0]);
                close(fd_mid[k][1]);
            }

            execl_creator(&chain.ops[j]);
        }

        op_pids.push_back(pid);
    }

    // parent closes unused fds
    close(fd_out[1]);
    for (int j = 0; j < K - 1; j++) {
        close(fd_mid[j][0]);
        close(fd_mid[j][1]);
    }

    pid_t feeder = fork();
    if (feeder < 0) { perror("fork feeder"); exit(EXIT_FAILURE); }

    if (feeder == 0) {
        close(fd_feed[0]); // feeder only writes

        FILE *csv = fopen(filename, "r");
        if (!csv) { perror("fopen"); exit(EXIT_FAILURE); }

        char line[MAX_LINE_SIZE];
        int  line_num = 0;
        int  start    = chain.start_line;
        int  end      = chain.end_line;

        while (fgets(line, sizeof(line), csv)) {
            line_num++;
            if (line_num >= start && line_num <= end)
                write(fd_feed[1], line, strlen(line));
            if (line_num > end) break;
        }
        fclose(csv);
        close(fd_feed[1]);
        _exit(EXIT_SUCCESS);
    }

    close(fd_feed[0]);
    close(fd_feed[1]);
    feeder_pids.push_back(feeder);

    return fd_out[0];
}

void create_submerger_process(operator_chain_t &chain, const char* filename, std::vector<pid_t> &feeder_pids, std::vector<std::vector<pid_t>> &all_pids) {

    merger_node_t *sub_root = chain.merger_child;
    int start = chain.start_line;
    int end = chain.end_line;

    for(int i = 0; i < sub_root->num_chains; i++) {
        operator_chain_t &sub_chain = sub_root->chains[i];
        
        if (sub_chain.num_ops == 0) {
            continue;
        }

        if (sub_chain.merger_child == NULL) { // operator chain 
            std::vector<pid_t> op_pids;
            int subchain_diff = sub_chain.end_line - sub_chain.start_line ;
            sub_chain.start_line = start + sub_chain.start_line - 1; 
            sub_chain.end_line = sub_chain.start_line + subchain_diff;
            int out = create_operator_process(sub_chain, filename, op_pids, feeder_pids);
            char buf[MAX_LINE_SIZE];
            int n;
            while ((n = read(out, buf, sizeof(buf))) > 0)
                fwrite(buf, 1, n, stdout);
            close(out);
            all_pids.push_back(op_pids);
        } else {   
            int global_start = start + sub_chain.start_line - 1;
            int global_end = start + sub_chain.end_line - 1;
            sub_chain.start_line = global_start;
            sub_chain.end_line = global_end;
            create_submerger_process(sub_chain, filename, feeder_pids, all_pids);
        }
    }
    
}


int main(int argc, char **argv) {
    merger_node_t *root = parse_merger_input(stdin);
    if (!root) {
        fprintf(stderr, "merger: parse error\n");
        return 1;
    }
    std::vector<pid_t> feeder_pids;
    std::vector<std::vector<pid_t>> all_pids; // op pids per chain

    for (int i = 0; i < root->num_chains; i++) {
        operator_chain_t &chain = root->chains[i];

        if (chain.num_ops == 0) {
            continue;
        }

        if (chain.merger_child == NULL) { // operator chain 
            std::vector<pid_t> op_pids;
            int out_rfd = create_operator_process(chain, root->filename, op_pids, feeder_pids);
            char buf[MAX_LINE_SIZE];
            int n;
            while ((n = read(out_rfd, buf, sizeof(buf))) > 0)
                fwrite(buf, 1, n, stdout);
            close(out_rfd);
            all_pids.push_back(op_pids);
        } else { // sub-merger 
            
            create_submerger_process(chain, root->filename, feeder_pids, all_pids);
        }
    }

    // wait for 
    std::vector<std::pair<pid_t,int>> statuses;
    for (auto &pids : all_pids) {
        for (pid_t pid : pids) {
            int status;
            waitpid(pid, &status, 0);
            statuses.push_back({pid, WEXITSTATUS(status)});
        }
    }

    // reap feeders
    for (pid_t pid : feeder_pids) {
        int status;
        waitpid(pid, &status, 0);
    }
    

    for (auto &[pid, st] : statuses)
        printf("EXIT-STATUS %d %d\n", pid, st);

    fflush(stdout);
    free_merger_tree(root);
    return 0;
}