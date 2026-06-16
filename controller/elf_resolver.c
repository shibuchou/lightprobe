#define _GNU_SOURCE

#include "controller_internal.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int resolve_symbol(
    const char *elf_path,
    const char *symbol_name,
    uint64_t base_addr,
    uint64_t *runtime_addr)
{
    int fd;

    Elf64_Ehdr ehdr;

    Elf64_Shdr *shdrs = NULL;

    char *shstrtab = NULL;

    int ret = -1;

    fd = open(elf_path, O_RDONLY);

    if (fd < 0) {
        return -1;
    }

    /*
     * read elf header
     */
    if (read(fd, &ehdr, sizeof(ehdr))
        != sizeof(ehdr)) {

        goto out;
    }

    /*
     * check elf magic
     */
    if (memcmp(ehdr.e_ident,
               ELFMAG,
               SELFMAG) != 0) {

        errno = ENOENT;

        goto out;
    }

    /*
     * read section headers
     */
    shdrs = malloc(
        ehdr.e_shentsize *
        ehdr.e_shnum);

    if (shdrs == NULL) {
        goto out;
    }

    if (lseek(fd,
              ehdr.e_shoff,
              SEEK_SET) < 0) {

        goto out;
    }

    if (read(fd,
             shdrs,
             ehdr.e_shentsize *
             ehdr.e_shnum)
        != (ssize_t)(
            ehdr.e_shentsize *
            ehdr.e_shnum)) {

        goto out;
    }

    /*
     * read section string table
     */
    {
        Elf64_Shdr *shstr =
            &shdrs[ehdr.e_shstrndx];

        shstrtab = malloc(shstr->sh_size);

        if (shstrtab == NULL) {
            goto out;
        }

        if (lseek(fd,
                  shstr->sh_offset,
                  SEEK_SET) < 0) {

            goto out;
        }

        if (read(fd,
                 shstrtab,
                 shstr->sh_size)
            != (ssize_t)shstr->sh_size) {

            goto out;
        }
    }

    /*
     * traverse sections
     */
    for (int i = 0;
         i < ehdr.e_shnum;
         i++) {

        Elf64_Shdr *symtab_hdr;

        Elf64_Shdr *strtab_hdr;

        Elf64_Sym *symbols = NULL;

        char *strtab = NULL;

        int sym_count;

        symtab_hdr = &shdrs[i];

        /*
         * support:
         * .dynsym
         * .symtab
         */
        if (symtab_hdr->sh_type != SHT_DYNSYM &&
            symtab_hdr->sh_type != SHT_SYMTAB) {

            continue;
        }

        /*
         * corresponding string table
         */
        strtab_hdr =
            &shdrs[symtab_hdr->sh_link];

        /*
         * read string table
         */
        strtab = malloc(strtab_hdr->sh_size);

        if (strtab == NULL) {
            continue;
        }

        if (lseek(fd,
                  strtab_hdr->sh_offset,
                  SEEK_SET) < 0) {

            free(strtab);

            continue;
        }

        if (read(fd,
                 strtab,
                 strtab_hdr->sh_size)
            != (ssize_t)strtab_hdr->sh_size) {

            free(strtab);

            continue;
        }

        /*
         * read symbol table
         */
        symbols = malloc(symtab_hdr->sh_size);

        if (symbols == NULL) {

            free(strtab);

            continue;
        }

        if (lseek(fd,
                  symtab_hdr->sh_offset,
                  SEEK_SET) < 0) {

            free(strtab);
            free(symbols);

            continue;
        }

        if (read(fd,
                 symbols,
                 symtab_hdr->sh_size)
            != (ssize_t)symtab_hdr->sh_size) {

            free(strtab);
            free(symbols);

            continue;
        }

        sym_count =
            symtab_hdr->sh_size /
            sizeof(Elf64_Sym);

        /*
         * traverse symbols
         */
        for (int j = 0;
             j < sym_count;
             j++) {

            const char *name;

            name =
                strtab +
                symbols[j].st_name;

            if (strcmp(name,
                       symbol_name) == 0) {

                *runtime_addr =
                    base_addr +
                    symbols[j].st_value;

                ret = 0;

                free(strtab);
                free(symbols);

                goto out;
            }
        }

        free(strtab);
        free(symbols);
    }

    errno = ENOENT;

out:

    free(shdrs);

    free(shstrtab);

    close(fd);

    return ret;
}