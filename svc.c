#include "svc.h"

void *svc_init(void) {
    // initialising everything
    svc* helper = (svc*)malloc(sizeof(svc));
    // initialising the current branch as master
    strcpy(helper->branch_name, "master");
    helper->branch_num = 1;
    strcpy(helper->last_branch_name, "master");
    // When creating new branches, an "empty" commit will be created
    // as a place holder, whose commit id is NULL.
    // Such commits are referred as "void commit" in codes below.
    helper->current_branch_head = (node*)malloc(sizeof(node));
    helper->current_branch_head->commit_id = NULL;
    helper->current_branch_head->branch_name = strdup("master");
    helper->current_branch_head->file_num = 0;
    helper->current_branch_head->files= NULL;
    helper->current_branch_head->prev_size = 0;
    helper->current_branch_head->change_num = 0;
    // array of heads of each branch
    helper->head_arr = (node**)malloc(sizeof(node*));;
    helper->head_arr[0] = helper->current_branch_head;
    // current tracking files
    helper->tracking_files = NULL;
    helper->file_num = 0;
    // how the files change comparing to last commit
    helper->changes_from_last = NULL;
    helper->change_num = 0;
    // array of names of each branch
    helper->branch_name_arr = (char**)malloc(sizeof(char*));
    helper->branch_name_arr[0] = strdup("master");
    // array of all commits, even if they are not reachable
    helper->commit_num = 1;
    helper->all_commits = (node**)malloc(sizeof(node*));
    helper->all_commits[0] = helper->current_branch_head;;
    // number of files that are implicitly removed
    helper->implicitly_rm_num = 0;
    return helper;
}

void free_file(file* f_arr, int size) {
    // freeing file array
    for (int i = 0; i < size; i++) {
        free(f_arr[i].file_name);
    }
    free(f_arr);
}

void cleanup(void *helper) {
    // freeing everything that were alloc'd
    svc* s = (svc*)helper;
    free_file(s->tracking_files, s->file_num);
    for (int i = 0; i < s->commit_num; i++) {
        node* n = s->all_commits[i];
        if (n->change_num != 0) {
            free(n->commit_id);
            for (int i = 0; i < n->change_num; i++) {
                free(n->commit_change[i].f.file_name);
            }
            free(n->commit_change);
            free(n->message);
        }
        free(n->branch_name);
        if (n->file_num != 0) {
            free_file(n->files, n->file_num);
        } else if (n->files != NULL) {
            free(n->files);
        }
        if (n->prev_size != 0) {
            free(n->prev_arr);
        }
        free(n);
    }
    free(s->head_arr);
    free(s->all_commits);
    for (int i = 0; i < s->branch_num; i++) {
        free(s->branch_name_arr[i]);
    }
    free(s->branch_name_arr);
    if (s->implicitly_rm_num != 0) {
        for (int i = 0; i < s->implicitly_rm_num; i++) {
            free(s->remove_buffer[i]);
        }
        free(s->remove_buffer);
    }
    free(helper);
}

int hash_file(void *helper, char *file_path) {
    // return the hash value of each given file
    if (file_path == NULL) {
        return -1;
    }
    int hash = 0;
    for (int i = 0; i < strlen(file_path); i++) {
        hash += (unsigned char)file_path[i];
    }
    hash %= 1000;
    // read each char from the given file path
    FILE * scr = fopen(file_path, "r");
    unsigned int c;
    if (scr != NULL) {
        while ((c = fgetc(scr)) != EOF) {
            hash += c;
        }
        hash %= 2000000000;
        fclose(scr);
        return hash;
    } else {
        // no ﬁle exists at the given path
        return -2;
    }

}

void update_file_hash(void* helper) {
    // updating the hash value of each tracking files
    // as well as checking if files are implicitly removed
    svc* s = (svc*)helper;
    int new_num = s->file_num;
    for (int i = 0; i < s->file_num; i++) {
        int num = hash_file(NULL, s->tracking_files[i].file_name);
        if (num == -2) {
            // cannot open this file -> has been implicitly removed
            // I keep tracking such files in case they are implicitly added back
            // Because I am using a real-time file array (s->tracking_files)
            // every file changes(user adding/removing) should be reflected in this array
            new_num--;
            s->implicitly_rm_num++;
            if (s->implicitly_rm_num == 1) {
                s->remove_buffer = (char**)malloc(sizeof(char*));
            } else {
                s->remove_buffer = (char**)realloc(s->remove_buffer, sizeof(char*) * s->implicitly_rm_num);
            }
            s->remove_buffer[s->implicitly_rm_num - 1] = strdup(s->tracking_files[i].file_name);
            free(s->tracking_files[i].file_name);
            s->tracking_files[i].file_name = 0;
        }
    }
    if (new_num != s->file_num) {
        // deleting such files from current tracking files
        // will be considered as removed later
        if (new_num != 0) {
            file* new_files = (file*)malloc(sizeof(file) * new_num);
            int j = 0;
            for (int i = 0; i < s->file_num; i++) {
                if (s->tracking_files[i].file_name != 0) {
                    new_files[j] = s->tracking_files[i];
                    j++;
                }
            }
            free(s->tracking_files);
            s->tracking_files = new_files;
            s->file_num = new_num;
        }
        s->file_num = new_num;
    }
    // updating hash value of modified files
    for (int i = 0; i < s->file_num; i++) {
        int num = hash_file(NULL, s->tracking_files[i].file_name);
        if (s->tracking_files[i].file_hash != num) {
            s->tracking_files[i].file_hash = num;
        }
    }

}

void file_change(void *helper) {
    // return the change structure, which contains the changing mode, file name, hash
    // and previous hash(if needed).
    svc* s = (svc*)helper;
    if (s->file_num == 0) {
        return;
    } else {
        update_file_hash(helper);
    }
    if (s->current_branch_head->commit_id == NULL && strcmp(s->branch_name, "master") == 0) {
        //very first commit
        change* c = (change*)malloc(sizeof(change) * s->file_num);
        for (int i = 0; i < s->file_num; i++) {
            (c + i)->f.file_name = strdup(s->tracking_files[i].file_name);
            (c + i)->f.file_hash = s->tracking_files[i].file_hash;
            (c + i)->change_mode = 1;
        }
        s->changes_from_last = c;
        s->change_num = s->file_num;
    } else {
        int diff_file;
        change* c = (change*)malloc(sizeof(change));
        int num = 0;
        node* last = s->current_branch_head;
        // modification
        for (int i = 0; i < s->file_num; i++) {
            diff_file = 0;
            for (int j = 0; j < last->file_num; j++) {
                if (strcmp(s->tracking_files[i].file_name, last->files[j].file_name) == 0) {
                    // same file path
                    // check if modified
                    if (s->tracking_files[i].file_hash != last->files[j].file_hash) {
                        // modified
                        num++;
                        c = (change*)realloc(c, sizeof(change) * num);
                        (c + num - 1)->change_mode = 3;
                        (c + num - 1)->f.file_name = strdup(s->tracking_files[i].file_name);
                        (c + num - 1)->f.file_hash = s->tracking_files[i].file_hash;
                        (c + num - 1)->new_hash = s->tracking_files[i].file_hash;
                        (c + num - 1)->prev_hash = last->files[j].file_hash;
                    }
                } else {
                    diff_file++;
                }
            }
            if (diff_file == last->file_num) {
                // added file
                num++;
                c = (change*)realloc(c, sizeof(change) * num);
                (c + num - 1)->f.file_name = strdup(s->tracking_files[i].file_name);
                (c + num - 1)->f.file_hash = s->tracking_files[i].file_hash;
                (c + num - 1)->change_mode = 1;
            }
        }
        // deletion
        for (int j = 0; j < last->file_num; j++) {
            diff_file = 0;
            for (int i = 0; i < s->file_num; i++) {
                if (strcmp(s->tracking_files[i].file_name, last->files[j].file_name) != 0) {
                    diff_file++;
                }
            }
            if (diff_file == s->file_num) {
                // deleted file
                num++;
                c = (change *) realloc(c, sizeof(change) * num);
                (c + num - 1)->f.file_name = strdup(last->files[j].file_name);
                (c + num - 1)->f.file_hash = last->files[j].file_hash;
                (c + num - 1)->change_mode = 2;
            }
        }
        if (num == 0) {
            //there is no change this time
            free(c);
            s->change_num = 0;
            s->changes_from_last = NULL;
        } else {
            s->changes_from_last = c;
            s->change_num = num;
        }

    }
}

int file_name_cmp(const void* a, const void* b) {
    // the comparator of two change structure
    // comparing the file name of two change structure
    change* c1 = (change*)a;
    change* c2 = (change*)b;
    char* s1 = strdup(c1->f.file_name);
    char* s2 = strdup(c2->f.file_name);
    int j = 0;
    char ch;
    while (s1[j]) {
        ch = s1[j];
        s1[j] = tolower(ch);
        j++;
    }
    j = 0;
    while (s2[j]) {
        ch = s2[j];
        s2[j] = tolower(ch);
        j++;
    }
    int result = strcmp(s1, s2);
    free(s1);
    free(s2);
    return result;
}

void check_implicit_add(void* helper) {
    // if the user manually add a file back,
    // my real-time file array needs also to add that file back.
    svc* s = (svc*)helper;
    for (int i = 0; i < s->implicitly_rm_num; i++) {
        int contain = 0;
        for (int k = 0; k < s->file_num; k++) {
            if (strcmp(s->tracking_files[k].file_name, s->remove_buffer[i]) == 0) {
                contain = 1;
            }
        }
        if (contain) {
            continue;
        }
        int hash = hash_file(helper, s->remove_buffer[i]);
        if (hash != -2) {
            s->file_num++;
            s->tracking_files = (file*)realloc(s->tracking_files, sizeof(file) * s->file_num);
            s->tracking_files[s->file_num - 1].file_name = strdup(s->remove_buffer[i]);
            s->tracking_files[s->file_num - 1].file_hash = hash;
        }
    }
}

void update_recover_save(char* scr_path, char* dest_path) {
    // update the hash value from source file to destination file
    // recover files, including the content
    // create and save a new file
    FILE * scr = fopen(scr_path, "r");
    FILE * f = fopen(dest_path, "w+");
    unsigned int c;
    if (scr != NULL) {
        while ((c = fgetc(scr)) != EOF) {
            fputc(c, f);

        }
        fclose(scr);
        fclose(f);
    }
}

char* commit_prefix(char* commit_id, int hash) {
    // get the file name of each committed saved file
    char file_hash[10];
    sprintf(file_hash, "%d", hash);
    char* path = (char*)malloc(strlen(commit_id) + 7 + strlen(file_hash) + 1);
    strcpy(path, "C-");
    strcat(path, commit_id);
    strcat(path, "-");
    strcat(path, file_hash);
    strcat(path, ".txt");
    return path;
}

void save_files(void* helper, node* commit) {
    // save the file in each commit
    // more like a snapshot of current tracking files when committing
    // saved files can be accessed later
    char* commit_id = commit->commit_id;
    for (int i = 0; i < commit->file_num; i++) {
        char* path = commit_prefix(commit_id, commit->files[i].file_hash);
        update_recover_save(commit->files[i].file_name, path);
        free(path);
    }
}

char *svc_commit(void *helper, char *message) {
    // make a commit of current tracking files in current branch
    if (!message)
        return NULL;
    svc* s = (svc*)helper;
    check_implicit_add(s);
    file_change(s);
    // no change
    if (s->change_num == 0) {
        return NULL;
    }
    // sorting the change array (by their file names, alphabetically)
    qsort(s->changes_from_last, s->change_num, sizeof(change), file_name_cmp);

    int id = 0;
    for (int i = 0; i < strlen(message); i++) {
        id += (unsigned char)message[i];
    }
    id %= 1000;
    for (int i = 0; i < s->change_num; i++) {
        if (s->changes_from_last[i].change_mode == 1) {
            // add
            id += 376591;
        } else if (s->changes_from_last[i].change_mode == 2) {
            // del
            id += 85973;
        } else {
            // mod
            id += 9573681;
        }
        for (int j = 0; j < strlen(s->changes_from_last[i].f.file_name); j++) {
            id = (id * (((unsigned char)(s->changes_from_last[i].f.file_name[j])) % 37) ) % 15485863 + 1;
        }
    }
    char hex[10];
    sprintf(hex, "%.6x", id);
    char* commit_id = strdup(hex);
    node* head = s->current_branch_head;

    if (head->commit_id == NULL) {
        // first commit in this branch
        node* new_commit = head;
        if (strcmp(new_commit->branch_name, "master") != 0) {
            free_file(new_commit->files, new_commit->file_num);
        }
        new_commit->commit_id = commit_id;
        new_commit->file_num = s->file_num;
        new_commit->files = (file*)malloc(sizeof(file) * s->file_num);
        // deep copy file array
        for (int i = 0; i < s->file_num; i++) {
            (new_commit->files + i)->file_name = strdup(s->tracking_files[i].file_name);
            (new_commit->files + i)->file_hash = s->tracking_files[i].file_hash;
        }
        new_commit->commit_change = s->changes_from_last;
        new_commit->message = strdup(message);
        new_commit->change_num = s->change_num;
        save_files(s, new_commit);

    } else {
        // not the first commit in this branch
        node* new_commit = (node*)malloc(sizeof(node));
        new_commit->commit_id = commit_id;
        new_commit->file_num = s->file_num;
        new_commit->files = (file*)malloc(sizeof(file) * s->file_num);
        // deep copy file array
        for (int i = 0; i < s->file_num; i++) {
            (new_commit->files + i)->file_name = strdup(s->tracking_files[i].file_name);
            (new_commit->files + i)->file_hash = s->tracking_files[i].file_hash;
        }
        new_commit->branch_name = strdup(s->branch_name);
        new_commit->prev_size = 1;
        new_commit->prev_arr = (node**)malloc(sizeof(node*));
        new_commit->prev_arr[0] = head;
        new_commit->commit_change = s->changes_from_last;
        new_commit->message = strdup(message);
        new_commit->change_num = s->change_num;
        save_files(s, new_commit);
        for (int i = 0; i < s->branch_num; i++) {
            if (strcmp(s->head_arr[i]->commit_id, head->commit_id) == 0) {
                // changing the head of current branch
                s->head_arr[i] = new_commit;
                break;
            }
        }
        s->current_branch_head = new_commit;
        s->commit_num++;
        s->all_commits = (node**)realloc(s->all_commits, sizeof(node*) * s->commit_num);
        s->all_commits[s->commit_num - 1] = new_commit;

    }
    // re-calculate the implicit addition after each commit
    if (s->implicitly_rm_num != 0) {
        for (int i = 0; i < s->implicitly_rm_num; i++) {
            free(s->remove_buffer[i]);
        }
        free(s->remove_buffer);
    }
    s->implicitly_rm_num = 0;
    return commit_id;
}



void *get_commit(void *helper, char *commit_id) {
    // get the particular commit structure(node)
    if (commit_id == NULL) {
        return NULL;
    }
    svc* s = (svc*)helper;
    for (int i = 0; i < s->commit_num; i++) {
        if (s->all_commits[i]->commit_id == NULL) {
            continue;
        }
        if (strcmp(s->all_commits[i]->commit_id, commit_id) == 0) {
            return s->all_commits[i];
        }
    }
    return NULL;
}

char **get_prev_commits(void *helper, void *commit, int *n_prev) {
    // get the parent comment: commit->pre_arr
    if (n_prev == NULL) {
        return NULL;
    }
    if (commit == NULL) {
        *n_prev = 0;
        return NULL;
    }
    node* c = (node*)commit;
    if (c->prev_size == 0) {
        *n_prev = 0;
        return NULL;
    } else {
        *n_prev = c->prev_size;
        char** result = (char**)malloc(sizeof(char*) * c->prev_size);
        for (int i = 0; i < c->prev_size; i++) {
            result[i] = c->prev_arr[i]->commit_id;
        }
        return result;
    }
}

void print_commit(void *helper, char *commit_id) {
    // printf the information of a particular commit
    if (commit_id == NULL) {
        printf("Invalid commit id");
        return;
    }
    node* commit = get_commit(helper, commit_id);
    if (commit) {
        printf("%s [%s]: %s\n", commit->commit_id, commit->branch_name, commit->message);
        for (int i = 0; i < commit->change_num; i++) {
            if (commit->commit_change[i].change_mode == 1) {
                //adding
                printf("    + %s\n", commit->commit_change[i].f.file_name);
            }
        }
        for (int i = 0; i < commit->change_num; i++) {
            if (commit->commit_change[i].change_mode == 2) {
                //deleting
                printf("    - %s\n", commit->commit_change[i].f.file_name);
            }
        }
        for (int i = 0; i < commit->change_num; i++) {
            if (commit->commit_change[i].change_mode == 3) {
                //modifying
                printf("    / %s [%10d -> %10d]\n", commit->commit_change[i].f.file_name, commit->commit_change[i].prev_hash, commit->commit_change[i].new_hash);
            }
        }
        printf("\n    Tracked files (%d):\n", commit->file_num);
        for (int i = 0; i < commit->file_num; i++) {
            printf("    [%10d] %s\n", commit->files[i].file_hash, commit->files[i].file_name);
        }
    } else {
        printf("Invalid commit id");
    }
}

int svc_branch(void *helper, char *branch_name) {
    // create a new branch
    // using a void commit as a place holder in the array of branch's head
    if (branch_name == NULL) {
        return -1;
    }
    for (int i = 0; i < strlen(branch_name); i++) {
        int c = (unsigned char)branch_name[i];
        if (    (c >=48 && c <= 57) // 0 - 9
                || (c >=65 && c <= 90) // A - Z
                || (c >=97 && c <= 122) // a - z
                || c == 95 || c == 45 || c == 47 // _, -, /.
                ) {
        } else {
            // invalid
            return -1;
        }
    }
    svc* s = (svc*)helper;
    for (int i = 0; i < s->branch_num; i++) {
        if (strcmp(s->head_arr[i]->branch_name, branch_name) == 0){
            // already exists
            return -2;
        }
    }
    check_implicit_add(s);

    file_change(s);
    if (s->change_num != 0) {
        for(int i = 0; i < s->change_num; i++) {
            free(s->changes_from_last[i].f.file_name);
        }
        free(s->changes_from_last);
        //uncommitted changes
        return -3;
    }
    s->branch_num++;
    s->head_arr = (node**)realloc(s->head_arr, sizeof(node*) * s->branch_num);
    node* void_node = (node*)malloc(sizeof(node));
    void_node->commit_id = NULL;
    void_node->branch_name = strdup(branch_name);
    void_node->prev_size = 1;
    void_node->prev_arr = (node**)malloc(sizeof(node*));
    void_node->prev_arr[0] = s->current_branch_head;
    void_node->change_num = 0;
    void_node->file_num = 0;
    void_node->file_num = s->file_num;
    void_node->files = (file*)malloc(sizeof(file) * s->file_num);
    for (int k = 0; k < s->file_num; k++) {
        void_node->files[k].file_name = strdup(s->tracking_files[k].file_name);
        void_node->files[k].file_hash = s->tracking_files[k].file_hash;
    }

    s->head_arr[s->branch_num - 1] = void_node;
    s->branch_name_arr = (char**)realloc(s->branch_name_arr, sizeof(char*) * s->branch_num);
    char* new_branch_name = strdup(branch_name);
    s->branch_name_arr[s->branch_num - 1] = new_branch_name;
    s->commit_num++;
    s->all_commits = (node**)realloc(s->all_commits, sizeof(node*) * s->commit_num);
    s->all_commits[s->commit_num - 1] = void_node;
    return 0;
}

int svc_checkout(void *helper, char *branch_name) {
    // make a particular branch the active branch
    if (branch_name == NULL) {
        return -1;
    }
    svc* s = (svc*)helper;
    for (int i = 0; i < s->branch_num; i++) {
        if (strcmp(s->head_arr[i]->branch_name, branch_name) == 0){
            check_implicit_add(s);
            file_change(s);
            if (s->change_num != 0) {
                //uncommitted changes
                for(int j= 0; j < s->change_num; j++) {
                    free(s->changes_from_last[j].f.file_name);
                }
                free(s->changes_from_last);
                return -2;
            }
            // set the branch name
            memset(s->last_branch_name, 0, 50);
            strcpy(s->last_branch_name, s->branch_name);
            memset(s->branch_name, 0, 50);
            strcpy(s->branch_name, branch_name);
            s->current_branch_head = s->head_arr[i];
            char* c_id;
            // update the current tracking file to files of the branch's head
            if (s->current_branch_head->commit_id == NULL) {
                c_id = s->current_branch_head->prev_arr[0]->commit_id;
                free_file(s->tracking_files, s->file_num);
                s->file_num = s->current_branch_head->prev_arr[0]->file_num;
                file* new_files = (file*)malloc(sizeof(file) * s->file_num);
                for (int j = 0; j < s->file_num; j++) {
                    new_files[j].file_name = strdup(s->current_branch_head->prev_arr[0]->files[j].file_name);
                    new_files[j].file_hash = s->current_branch_head->prev_arr[0]->files[j].file_hash;
                }
                s->tracking_files = new_files;
            } else {
                c_id = s->current_branch_head->commit_id;
                free_file(s->tracking_files, s->file_num);
                s->file_num = s->head_arr[i]->file_num;
                file* new_files = (file*)malloc(sizeof(file) * s->file_num);
                for (int j = 0; j < s->file_num; j++) {
                    new_files[j].file_name = strdup(s->head_arr[i]->files[j].file_name);
                    new_files[j].file_hash = s->head_arr[i]->files[j].file_hash;
                }
                s->tracking_files = new_files;
            }
            for (int k = 0; k < s->file_num; k++) {
                // update the actual file in the computer
                char* path = commit_prefix(c_id, s->tracking_files[k].file_hash);
                update_recover_save(path, s->tracking_files[k].file_name);
                free(path);
            }
            return 0;
        }
    }
    return -1;
}

char **list_branches(void *helper, int *n_branches) {
    // return a list of branches
    if (n_branches == NULL) {
        return NULL;
    }
    svc* s = (svc*)helper;
    *n_branches = s->branch_num;
    char** result = (char**)malloc(sizeof(char*) * s->branch_num);
    for (int i = 0; i < s->branch_num; i++) {
        printf(s->branch_name_arr[i]);
        result[i] = s->branch_name_arr[i];
    }
    return result;
}

int svc_add(void *helper, char *file_name) {
    // add a new file to the tracking files(svc's)
    if (!file_name) {
        return -1;
    }
    if (access(file_name, F_OK) == -1) {
        // file does not exist or cannot be opened
        return -3;
    }
    int hash = hash_file(helper, file_name);
    svc* svc_struct = (svc*)helper;
    for (int i = 0; i < svc_struct->file_num; i++) {
        if (svc_struct->tracking_files[i].file_hash == hash) {
            // already added
            return -2;
        }
    }
    svc_struct->file_num++;
    if (svc_struct->file_num == 1) {
        // initializing
        svc_struct->tracking_files = (file*)malloc(sizeof(file) * svc_struct->file_num);
    } else {
        svc_struct->tracking_files = (file*)realloc(svc_struct->tracking_files, sizeof(file) * svc_struct->file_num);
    }
    file* new_file = svc_struct->tracking_files + svc_struct->file_num - 1;
    new_file->file_hash = hash;
    new_file->file_name = strdup(file_name);
    return hash;
}

int svc_rm(void *helper, char *file_name) {
    // remove a file from tracking files(svc's)
    if (!file_name) {
        return -1;
    }
    int exists = 0;
    file f;
    svc* svc_struct = (svc*)helper;
    int i = 0;
    for (; i < svc_struct->file_num; i++) {
        if (strcmp(svc_struct->tracking_files[i].file_name, file_name)== 0) {
            // can be removed
            exists = 1;
            f = svc_struct->tracking_files[i];
            break;
        }
    }
    if (exists) {
        for (; i < svc_struct->file_num - 1; i++) {
            svc_struct->tracking_files[i] = svc_struct->tracking_files[i + 1];
        }
        svc_struct->file_num--;
        svc_struct->tracking_files = (file*)realloc(svc_struct->tracking_files, sizeof(file) * svc_struct->file_num);
        free(f.file_name);
        return f.file_hash;
    } else {
        // cannot be removed or file has not being tracked
        return -2;
    }
}

int svc_reset(void *helper, char *commit_id) {
    // reset the svc to a particular commit
    // recover all files as the files in that commit
    if (commit_id == NULL) {
        return -1;
    }
    node* commit = get_commit(helper, commit_id);
    svc* s = (svc*)helper;
    if (commit != NULL) {
        free_file(s->tracking_files, s->file_num);
        s->file_num = commit->file_num;
        file* new_files = (file*)malloc(sizeof(file) * s->file_num);
        for (int j = 0; j < s->file_num; j++) {
            new_files[j].file_name = strdup(commit->files[j].file_name);
            new_files[j].file_hash = commit->files[j].file_hash;
        }
        s->tracking_files = new_files;

        // recover all currently tracking files!!
        for (int i = 0; i < s->file_num; i++) {
            // open saved file
            char* path = commit_prefix(commit_id, s->tracking_files[i].file_hash);
            update_recover_save(path, s->tracking_files[i].file_name);
            free(path);
        }
        // update the head of current branch
        // if the current branch has no commit, create a void commit
        // as the  place holder
        node* head = s->current_branch_head;
        int first = 0;
        if (strcmp(head->branch_name, commit->branch_name) != 0) {
            // first of the branch -- void_node
            first = 1;
        }
        int i = 0;
        for (; i < s->branch_num; i++) {
            if (strcmp(s->head_arr[i]->branch_name, s->branch_name) == 0) {
                break;
            }
        }
        if (first) {
            // void node
            node* void_node = (node*)malloc(sizeof(node));
            void_node->commit_id = NULL;
            void_node->branch_name = strdup(s->branch_name);
            void_node->files = (file*)malloc(sizeof(file) * s->file_num);
            void_node->file_num = s->file_num;
            void_node->prev_size = 1;
            void_node->prev_arr = (node**)malloc(sizeof(node*));
            void_node->prev_arr[0] = commit;
            void_node->change_num = 0;
            for (int k = 0; k < s->file_num; k++) {
                void_node->files[k].file_name = strdup(s->tracking_files[k].file_name);
                void_node->files[k].file_hash = s->tracking_files[k].file_hash;
            }
            s->current_branch_head = void_node;
            s->commit_num++;
            s->all_commits = (node**)realloc(s->all_commits, sizeof(node*) * s->commit_num);
            s->all_commits[s->commit_num - 1] = void_node;

            s->head_arr[i] = void_node;
        } else {
            s->current_branch_head = commit;
            s->head_arr[i] = s->current_branch_head;
        }
        return 0;
    } else {
        //commit does not exist
        return -2;
    }
}


char *svc_merge(void *helper, char *branch_name, struct resolution *resolutions, int n_resolutions) {
    // merge a branch with the current branch
    if (branch_name == NULL) {
        printf("Invalid branch name");
        return NULL;
    }
    svc* s = (svc*)helper;
    int find = 0;
    // find the merging branch
    for (int i = 0; i < s->branch_num; i++) {
        if (strcmp(s->branch_name_arr[i], branch_name) == 0) {
            find = 1;
        }
    }
    if (find == 0) {
        printf("Branch not found");
        return NULL;
    }
    // the merging branch exists
    if (strcmp(s->branch_name, branch_name) == 0) {
        printf("Cannot merge a branch with itself");
        return NULL;
    }
    check_implicit_add(s);
    file_change(s);
    //uncommitted changes
    if (s->change_num != 0) {
        for(int i = 0; i < s->change_num; i++) {
            free(s->changes_from_last[i].f.file_name);
        }
        free(s->changes_from_last);
        printf("Changes must be committed");
        return NULL;
    }

    // current branch's head
    node* commit1 = s->current_branch_head;
    // find the other branch's head
    node* commit2 = NULL;
    for (int i = 0; i < s->branch_num; i++) {
        if (strcmp(s->head_arr[i]->branch_name, branch_name) == 0) {
            commit2 = s->head_arr[i];
            break;
        }
    }
    // store the file path that exists in either commit1 or commit2 or both
    // which will be removed after merging
    char** del_arr = NULL;
    int del_num = 0;
    // tracking files array after merging
    file* new_files = NULL;
    // open and store the files in resolution array
    int i = 0;
    for (int j = 0; j < n_resolutions; j++) {
        FILE * scr = fopen(resolutions[j].resolved_file, "r");
        FILE * f = fopen(resolutions[j].file_name, "w+");
        unsigned int c;
        if (scr != NULL) {
            // if the file exist -> update the current file content in the computer
            // add it to the new tracking files
            i++;
            if (i == 1) {
                new_files = (file*)malloc(sizeof(file));
            } else {
                new_files = (file*)realloc(new_files, sizeof(file) * i);
            }
            new_files[i - 1].file_name = strdup(resolutions[j].file_name);

            while ((c = fgetc(scr)) != EOF) {
                fputc(c, f);
            }
            fclose(scr);
            fclose(f);
            new_files[i - 1].file_hash = hash_file(helper, resolutions[j].file_name);
        } else {
            // this file will not be added after merging
            del_num++;
            if (del_num == 1) {
                del_arr = (char**)malloc(sizeof(char*));

                del_arr[0] = resolutions[j].file_name;
            } else {
                del_arr = (char**)realloc(del_arr, sizeof(char*) * del_num);
                del_arr[del_num - 1] = resolutions[j].file_name;
            }
        }

    }
    int new_file_num = i;

    // add non-conflict file in commit1
    for (i = 0; i < commit1->file_num; i++) {
        find = 0;
        // do not add the files in del_arr to the new tracking files array
        for (int z = 0; z < del_num; z++) {
            if (strcmp(del_arr[z], commit1->files[i].file_name) == 0) {
                find = 1;
                break;
            }
        }
        if (find) {
            continue;
        }
        int diff_num = 0;
        for (int j = 0; j < n_resolutions; j++) {

            if (strcmp(resolutions[j].file_name, commit1->files[i].file_name) != 0) {
                diff_num++;
            }
        }
        if (diff_num == n_resolutions) {
            new_file_num++;
            if (new_file_num == 1) {
                new_files = (file*)malloc(sizeof(file) * 1);
            } else {
                new_files = (file*)realloc(new_files, sizeof(file) * new_file_num);
            }
            new_files[new_file_num - 1].file_name = strdup(commit1->files[i].file_name);
            new_files[new_file_num - 1].file_hash = commit1->files[i].file_hash;
            // update or recover all tracking files
            char* path = commit_prefix(commit1->commit_id, commit1->files[i].file_hash);
            update_recover_save(path, commit1->files[i].file_name);
            free(path);
        }

    }

    // add non-conflict file in commit2
    int num2 = 0;
    file* only_in_2 = NULL;
    for (i = 0; i < commit2->file_num; i++) {
        find = 0;
        // do not add the files in del_arr to the new tracking files array
        for (int z = 0; z < del_num; z++) {
            if (strcmp(del_arr[z], commit2->files[i].file_name) == 0) {
                find = 1;
                break;
            }
        }
        if (find) {
            continue;
        }
        int diff_num = 0;
        for (int j = 0; j < new_file_num; j++) {

            if (strcmp(new_files[j].file_name, commit2->files[i].file_name) != 0) {

                diff_num++;
            }
        }
        if (diff_num == new_file_num) {
            // add the files to the new tracking files array,
            // however I need to add them to a buffer array(only_in_2),
            // and then add the buffer array to the new tracking files array
            num2++;
            if (num2 == 1) {
                only_in_2 = (file*)malloc(sizeof(file));;
            } else {
                only_in_2 = (file*)realloc(only_in_2, sizeof(file) * num2);

            }
            only_in_2[num2 - 1].file_hash = commit2->files[i].file_hash;
            only_in_2[num2 - 1].file_name = strdup(commit2->files[i].file_name);
            // update or recover all tracking files
            char* path = commit_prefix(commit2->commit_id, commit2->files[i].file_hash);
            update_recover_save(path, commit2->files[i].file_name);
            free(path);
        }
    }
    // add only_in_2 into the new tracking files array if there is any.
    if (num2 != 0) {
        new_file_num += num2;
        new_files = (file*)realloc(new_files, sizeof(file) * new_file_num);
        for (i = new_file_num - num2; i < new_file_num; i++) {
            new_files[i].file_hash = only_in_2[i - new_file_num + num2].file_hash;
            new_files[i].file_name = only_in_2[i - new_file_num + num2].file_name;
        }
        free(only_in_2);
    }
    // updating tracking files
    if (s->file_num != 0) {
        free_file(s->tracking_files, s->file_num);
    }
    s->tracking_files = new_files;
    s->file_num = new_file_num;
    // free useless array
    if (del_num != 0) {
        free(del_arr);
    }
    // merge is just like a commit
    char* message = (char*)malloc(15 + strlen(branch_name));
    strcpy(message, "Merged branch ");
    strcat(message, branch_name);
    char* commit_id = svc_commit(s, message);
    free(message);
    if (commit_id) {
        printf("Merge successful");
        // set the second previous commit
        s->current_branch_head->prev_size++;
        s->current_branch_head->prev_arr = (node**)realloc(s->current_branch_head->prev_arr,
                                                           s->current_branch_head->prev_size * sizeof(node*));
        s->current_branch_head->prev_arr[1] = commit2;
    }
    return commit_id;
}
