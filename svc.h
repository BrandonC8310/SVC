#ifndef svc_h
#define svc_h

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <ctype.h>
#include <errno.h>

typedef struct file file;
struct file {
    int file_hash;
    char* file_name;
};

typedef struct file_change change;
struct file_change {
    file f;
    int change_mode; // 1 == add; 2 == del; 3 == mod;
    int prev_hash;
    int new_hash;
};

typedef struct node node;
struct node {
    char* commit_id; //hex
    char* branch_name;
    int file_num;
    file* files;
    int prev_size;
    struct node** prev_arr;
    int change_num;
    change * commit_change;
    char* message;
};

typedef struct svc svc;
struct svc {
    char branch_name[50]; // current branch name
    char last_branch_name[50];
    node* current_branch_head;
    int branch_num;
    char** branch_name_arr;
    node** head_arr; // array of heads of each branch(pointers)
    int file_num;
    file* tracking_files; // real-time tracking files
    int change_num;
    change* changes_from_last; // the changes from last commit
    int commit_num;
    node** all_commits;
    int implicitly_rm_num;
    char** remove_buffer; // array of file paths that are implicitly removed
};

typedef struct resolution {
    // NOTE: DO NOT MODIFY THIS STRUCT
    char *file_name;
    char *resolved_file;
} resolution;

void *svc_init(void);

void free_file(file* f_arr, int size);

void cleanup(void *helper);

int hash_file(void *helper, char *file_path);

void update_file_hash(void* helper);

void file_change(void *helper);

int file_name_cmp(const void* a, const void* b);

void save_files(void* ,node*);

void check_implicit_add(void* helper);

void update_recover_save(char* scr_path, char* dest_path);

char* commit_prefix(char* commit_id, int hash);

char *svc_commit(void *helper, char *message);

void *get_commit(void *helper, char *commit_id);

char **get_prev_commits(void *helper, void *commit, int *n_prev);

void print_commit(void *helper, char *commit_id);

int svc_branch(void *helper, char *branch_name);

int svc_checkout(void *helper, char *branch_name);

char **list_branches(void *helper, int *n_branches);

int svc_add(void *helper, char *file_name);

int svc_rm(void *helper, char *file_name);

int svc_reset(void *helper, char *commit_id);

char *svc_merge(void *helper, char *branch_name, resolution *resolutions, int n_resolutions);

#endif
