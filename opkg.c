#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <archive.h>
#include <archive_entry.h>

#define BLOCK_SIZE 10240
#define DB_DIR "/var/db/opkg/packages"

void print_usage(const char *prog_name) {
    fprintf(stderr, "Onix Package Manager (opkg)\n");
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s create <pkg_name> <version> <binary_path>\n", prog_name);
    fprintf(stderr, "  %s install <package.o.pkg>\n", prog_name);
    fprintf(stderr, "  %s remove <pkg_name>\n", prog_name);
}


int handle_create(const char *name, const char *version, const char *binary_path) {
    char cmd[512]; // Fixed: Explicit buffer size to prevent memory corruption
    printf("Creating package '%s.o.pkg' for Onix...\n", name);

    system("rm -rf stage");
    if (system("mkdir -p stage/metadata stage/files/bin") != 0) return 1;

    snprintf(cmd, sizeof(cmd), "cp %s stage/files/bin/", binary_path);
    if (system(cmd) != 0) return 1;

    FILE *info = fopen("stage/metadata/opkg-info", "w");
    if (!info) return 1;
    fprintf(info, "pkg_name: %s\n", name);
    fprintf(info, "pkg_version: %s\n", version);
    fprintf(info, "pkg_description: built package for Onix\n");
    fclose(info);

    snprintf(cmd, sizeof(cmd), "tar -czf %s.o.pkg -C stage metadata files", name);
    if (system(cmd) != 0) return 1;

    system("rm -rf stage");
    printf("Successfully built %s.o.pkg!\n", name);
    return 0;
}


void parse_package_name(struct archive *a, struct archive_entry *entry, char *out_name, size_t max_len) {
    la_int64_t size = archive_entry_size(entry);
    char *buffer = malloc(size + 1);
    if (!buffer) return;

    if (archive_read_data(a, buffer, size) == size) {
        buffer[size] = '\0';
        printf("=== PACKAGE MANIFEST ===\n%s========================\n", buffer);
        
        char *line = strtok(buffer, "\n");
        while (line) {
            if (strncmp(line, "pkg_name:", 9) == 0) {
                sscanf(line, "pkg_name: %s", out_name);
                break;
            }
            line = strtok(NULL, "\n");
        }
    }
    free(buffer);
}


int handle_install(const char *filename) {
    struct archive *a;
    struct archive_entry *entry;
    int r;
    char pkg_name[128] = "unknown";
    FILE *list_file = NULL;

    system("mkdir -p " DB_DIR);

    a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_filter_gzip(a);

    r = archive_read_open_filename(a, filename, BLOCK_SIZE);
    if (r != ARCHIVE_OK) {
        fprintf(stderr, "Error opening package: %s\n", archive_error_string(a));
        archive_read_free(a);
        return 1;
    }

    printf("Installing package: %s\n", filename);

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *current_path = archive_entry_pathname(entry);

        if (strcmp(current_path, "metadata/opkg-info") == 0) {
            parse_package_name(a, entry, pkg_name, sizeof(pkg_name));
            
            char list_path[256]; 
            snprintf(list_path, sizeof(list_path), DB_DIR "/%s.list", pkg_name);
            list_file = fopen(list_path, "w");
            continue; 
        }

       
        if (strncmp(current_path, "files/", 6) == 0) {
            const char *dest_path = current_path + 6; 
            
            
            if (strlen(dest_path) == 0 || dest_path[strlen(dest_path) - 1] == '/') {
                continue;
            }

            archive_entry_set_pathname(entry, dest_path);
            printf("Extracting: /%s\n", dest_path);
            
            r = archive_read_extract(a, entry, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM);
            if (r != ARCHIVE_OK) {
                fprintf(stderr, "Extraction error: %s\n", archive_error_string(a));
            } else if (list_file) {
                fprintf(list_file, "/%s\n", dest_path);
            }
        }
    }

    if (list_file) fclose(list_file);
    archive_read_free(a);
    printf("Installation complete. Registered as '%s'.\n", pkg_name);
    return 0;
}


int handle_remove(const char *pkg_name) {
    char list_path[256]; 
    char file_path[256]; 
    snprintf(list_path, sizeof(list_path), DB_DIR "/%s.list", pkg_name);

    if (access(list_path, F_OK) != 0) {
        fprintf(stderr, "Error: Package '%s' is not installed.\n", pkg_name);
        return 1;
    }

    FILE *list_file = fopen(list_path, "r");
    if (!list_file) {
        fprintf(stderr, "Error: Failed to open file manifest list.\n");
        return 1;
    }

    printf("Removing package '%s'...\n", pkg_name);

    while (fgets(file_path, sizeof(file_path), list_file)) {
        file_path[strcspn(file_path, "\r\n")] = '\0';

        if (strlen(file_path) > 0) {
            printf("Deleting: %s\n", file_path);
            if (unlink(file_path) != 0) {
                perror("Warning: Could not remove file");
            }
        }
    }

    fclose(list_file);

    if (unlink(list_path) != 0) {
        perror("Warning: Could not remove tracking manifest");
    }

    printf("Package '%s' successfully uninstalled.\n", pkg_name);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *subcommand = argv[1];

    if (strcmp(subcommand, "create") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: 'create' requires package name, version, and binary path.\n");
            return 1;
        }
        return handle_create(argv[2], argv[3], argv[4]);
    } 
    else if (strcmp(subcommand, "install") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'install' requires a target .o.pkg file path.\n");
            return 1;
        }
        return handle_install(argv[2]);
    } 
    else if (strcmp(subcommand, "remove") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'remove' requires a package name target.\n");
            return 1;
        }
        return handle_remove(argv[2]);
    } 
    else {
        fprintf(stderr, "Unknown subcommand: %s\n", subcommand);
        print_usage(argv[0]);
        return 1;
    }
}
