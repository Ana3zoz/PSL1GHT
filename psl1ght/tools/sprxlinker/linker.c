#include <libelf.h>

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __BIG_ENDIAN__
#define BE16(num) (num)
#define BE32(num) (num)
#else
#define BE16(num) (((num << 8) | ((num >> 8) & 0xFF)) & 0xFFFF)
#define BE32(num) ((BE16(num) << 16) | BE16(num >> 16))
#endif

typedef struct
{
	uint32_t header1;
	uint16_t header2;
	uint16_t imports;
	uint32_t zero1;
	uint32_t zero2;
	uint32_t name;
	uint32_t fnid;
	uint32_t stub;
	uint32_t zero3;
	uint32_t zero4;
	uint32_t zero5;
	uint32_t zero6;
} __attribute__((__packed__)) Stub;

unsigned char prx_magic[] = {
	0x00, 0x00, 0x00, 0x28, // Size
	0x1b, 0x43, 0x4c, 0xec
};

Elf_Scn* GetSection(Elf* elf, const char* name)
{
	Elf64_Ehdr* ehdr = elf64_getehdr(elf);

	if (!ehdr)
		return NULL;

	Elf_Data* data = elf_getdata(elf_getscn(elf, ehdr->e_shstrndx), NULL);

	if (!data)
		return NULL;

	for (Elf_Scn* scn = NULL; (scn = elf_nextscn(elf, scn));) {
		Elf64_Shdr* shdr = elf64_getshdr(scn);
		if (shdr && !strcmp(name, (const char*)data->d_buf + shdr->sh_name))
			return scn;
	}
	return NULL;
}

int main(int argc, const char* argv[])
{
	if (argc < 2) {
		printf("Usage: %s [elf path]\n", argv[0]);
		return 0;
	}
	int fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Unable to open elf file: %s\n", argv[1]);
		return 1;
	}

	elf_version(EV_CURRENT);
	Elf* elf = elf_begin(fd, ELF_C_READ, NULL);
	if (!elf) {
		fprintf(stderr, "libelf could not read elf file.\n");
		return 1;
	}

	Elf_Scn* prx = GetSection(elf, ".sys_proc_prx_param");
	if (!prx || memcmp(elf_getdata(prx, NULL)->d_buf, prx_magic, sizeof(prx_magic))) {
		fprintf(stderr, "elf does not have a prx parameter section.\n");
		return 1;
	}

	Elf_Scn* stubsection = GetSection(elf, ".lib.stub");
	Elf_Data* stubdata = elf_getdata(stubsection, NULL);
	Elf64_Shdr* stubshdr = elf64_getshdr(stubsection);
	Stub* stubbase = (Stub*)stubdata->d_buf;
	size_t stubcount = stubdata->d_size / sizeof(Stub);
	Elf_Scn* fnidsection = GetSection(elf, ".rodata.sceFNID");
	Elf64_Shdr* fnidshdr = elf64_getshdr(fnidsection);
	for (Stub* stub = stubbase; stub < stubbase + stubcount; stub++) {
		uint32_t fnid = BE32(stub->fnid);
		uint32_t end = fnidshdr->sh_addr + fnidshdr->sh_size;
		for (Stub* substub = stubbase; substub < stubbase + stubcount; substub++) {
			if (stub == substub)
				continue;
			uint32_t newfnid = BE32(substub->fnid);
			if (newfnid >= fnid && newfnid < end)
				end = newfnid;
		}
		uint16_t fnidcount = (end - fnid) / 4;
		if (BE16(stub->imports) != fnidcount) {
			lseek(fd, stubshdr->sh_offset + (stub - stubbase) * sizeof(Stub) + offsetof(Stub, imports), SEEK_SET);
			fnidcount = BE16(fnidcount);
			write(fd, &fnidcount, sizeof(fnidcount));
		}
	}

	elf_end(elf);

	close(fd);

	return 0;
}
