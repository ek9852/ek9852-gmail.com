/*
 * This file implement the TI RPRC file format
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <libelf.h>
#include <errno.h>
#include <inttypes.h>
#include <gelf.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Unsopport for big endian archs.
#endif

static int xwrite(int fd, char *buffer, size_t len) {
    size_t done = 0;
    size_t n;

    while (done < len) {
        n = write(fd, buffer + done, len - done);
        if (n == 0) {
            /* file system full */
            fprintf(stderr, "Write error\n");
            exit(1);
        } else if (n != (size_t)-1) {
            /* some bytes written, continue */
            done += n;
        } else if (errno != EAGAIN && errno != EINTR) {
            /* real error */
            fprintf(stderr, "Write error\n");
            exit(1);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *elffilename = NULL;
    const char *rprcfilename = NULL;
    int ret;

    printf("Convert elf to TI rprc file\n");

    if (argc != 3) {
        printf("Usage: %s: <input elf file> <output rprc file>\n", argv[0]);
        return 1;
    }
    elffilename = argv[1];
    rprcfilename = argv[2];

    int fd;
    fd = open(elffilename, O_RDONLY);
    if (fd == -1) {
        printf("Cannot open input elf file: %s\n", elffilename);
        return 1;
    }

    /* Before we start tell the ELF library which version we are using.  */
    elf_version (EV_CURRENT);

    Elf *elf = elf_begin(fd, ELF_C_READ, NULL);
    if (elf == NULL) {
        printf("Cannot generate Elf descriptor: %s\n",  elf_errmsg (-1));
        close(fd);
        return -1;
    }
    GElf_Ehdr ehdr_mem;
    GElf_Ehdr *ehdr = gelf_getehdr (elf, &ehdr_mem);
    if (ehdr == NULL) {
        printf("Cannot read Elf header\n");
        ret = 2;
        goto out1;
    }

    int wr_fd;
    wr_fd = open(rprcfilename, O_WRONLY | O_CREAT | O_TRUNC, 0660);
    if (wr_fd == -1) {
        printf("Cannot open output file: %s\n", rprcfilename);
        ret = 2;
        goto out1;
    }

    size_t shnums;
    if (elf_getshdrnum(elf, &shnums) < 0) {
        printf("cannot determine number of sections: %s", elf_errmsg (-1));
        ret = 3;
        goto out2;
    }

    size_t phnums;
    if (elf_getphdrnum(elf, &phnums) < 0) {
        printf("cannot determine number of pgoram: %s", elf_errmsg (-1));
        ret = 3;
        goto out2;
    }

    printf("Total no. of sections: %zu\n", shnums);

    char *buf = (char*)malloc(64 * 1024);

    /* skip header since we don't know total count */
    if (lseek(wr_fd, 4+8+4+4+4, SEEK_SET) != 4+8+4+4+4) {
        printf("Cannot seek\n");
        ret = 5;
        goto out3;
    }

    /* loop once to determine the number of sections needs to be program */
    int section_to_program = 0;
    int cnt;
    uint32_t v32;
    uint64_t v64;
    for (cnt = 0; cnt < shnums; ++cnt) {
        Elf_Scn *scn = elf_getscn (elf, cnt);
        if (scn == NULL) {
            printf("Cannot get section: %s", elf_errmsg (-1));
            ret = 5;
            goto out3;
        }
        GElf_Shdr shdr_mem;
        GElf_Shdr *shdr = gelf_getshdr (scn, &shdr_mem);
        if (shdr == NULL) {
            printf("cannot get section header: %s", elf_errmsg (-1));
            ret = 6;
            goto out3;
        }
        if (!((shdr->sh_type == SHT_PROGBITS) && (shdr->sh_size != 0) && (shdr->sh_flags & SHF_ALLOC))) {
            continue;
        }
        /* convert sh_addr to physical address */
        GElf_Addr phyaddr;
        int i;
        for (i = 0; i < phnums; ++i) {
            GElf_Phdr phdr_mem;
            GElf_Phdr *phdr = gelf_getphdr (elf, i, &phdr_mem);
            if (phdr == NULL) {
                printf("cannot get program: %s", elf_errmsg (-1));
                ret = 6;
                goto out3;
            }
            if ((phdr->p_type == PT_LOAD) && (phdr->p_filesz != 0) && (phdr->p_vaddr <= shdr->sh_addr) &&
                    (phdr->p_vaddr + phdr->p_filesz > shdr->sh_addr) &&
                    (phdr->p_offset <= shdr->sh_offset) &&
                    (phdr->p_offset + phdr->p_filesz > shdr->sh_offset)) {
                phyaddr = phdr->p_paddr + (shdr->sh_addr - phdr->p_vaddr);
                break;
            }
        }
        if (i == phnums) {
            continue;
        }

        /* write section */
        v64 = phyaddr;
        xwrite(wr_fd, (char*)&v64, 8);
        v32 = ((shdr->sh_size + 3) >> 2) << 2;
        xwrite(wr_fd, (char*)&v32, 4);
        v32 = 0; // reserved
        xwrite(wr_fd, (char*)&v32, 4);
        v64 = 0; // reserved
        xwrite(wr_fd, (char*)&v64, 8);

        if (lseek(fd, shdr->sh_offset, SEEK_SET) != shdr->sh_offset) {
            fprintf(stderr, "seek error: %zu %d\n", shdr->sh_offset, errno);
            ret = 8;
            goto out3;
        }
        int to_read, remaining;
        remaining =  shdr->sh_size;

        while(remaining > 0) {
            to_read = remaining;
            if (to_read > 64 * 1024) {
                to_read = 64 * 1024;
            }
            if (to_read != read(fd, buf, to_read)) {
                fprintf(stderr, "read error\n");
                ret = 2;
                goto out3;
            }
            xwrite(wr_fd, buf, to_read);
            remaining -= to_read;
        }
        if (shdr->sh_size % 8) {
            xwrite(wr_fd, "\x0\x0\x0\x0\x0\x0\x0\x0", 8 - (shdr->sh_size % 8));
        }
        section_to_program++;
    }
    printf("Section to program: %d\n", section_to_program);

    /* write back header since now we know the total counts */
    if (lseek(wr_fd, 0, SEEK_SET) != 0) {
        printf("Cannot seek\n");
        ret = 5;
        goto out3;
    }
    xwrite(wr_fd, "RPRC", 4); 
    v64 = ehdr_mem.e_entry;
    xwrite(wr_fd, (char*)&v64, 8);
    v32 = section_to_program;
    xwrite(wr_fd, (char*)&v32, 4);
    v32 = 1; // RPRC version
    xwrite(wr_fd, (char*)&v32, 4);
    v32 = 0; // reserved
    xwrite(wr_fd, (char*)&v32, 4);

    ret = 0;
out3:
    free(buf);
out2:
    close(wr_fd);
out1:
    close(fd);
    elf_end(elf);
    return ret;
}
