/*******************************************************************************

NAME            dcdElf.h
SUMMARY         %description%
VERSION         %version: WIC~6 %
UPDATE DATE     %date_modified: Mon Nov 18 15:34:02 2013 %
PROGRAMMER      %created_by:    jparnell %

        Copyright 2010-2014 NetApp, Inc. All Rights Reserved.

DESCRIPTION: This file includes Elf structures and some new macros

*******************************************************************************/


#ifndef __INCdcdElf
#define __INCdcdElf


typedef unsigned long   Elf32_Addr;
typedef unsigned short  Elf32_Half;
typedef unsigned long   Elf32_Off;
typedef long            Elf32_Sword;
typedef unsigned long   Elf32_Word;

typedef unsigned long long Elf64_Addr;
typedef unsigned short     Elf64_Half;
typedef unsigned long long Elf64_Off;
typedef unsigned long long Elf64_Xword;
typedef unsigned long      Elf64_Word;

/* 
 * Elf header
 */
#define EI_NIDENT    16

typedef struct 
{
    unsigned char   e_ident[EI_NIDENT];
    Elf32_Half      e_type;
    Elf32_Half      e_machine;
    Elf32_Word      e_version;
    Elf32_Addr      e_entry;
    Elf32_Off       e_phoff;
    Elf32_Off       e_shoff;
    Elf32_Word      e_flags;
    Elf32_Half      e_ehsize;
    Elf32_Half      e_phentsize;
    Elf32_Half      e_phnum;
    Elf32_Half      e_shentsize;
    Elf32_Half      e_shnum;
    Elf32_Half      e_shstrndx;
} Elf32_Ehdr;

typedef struct
{
    unsigned char   e_ident[EI_NIDENT];
    Elf64_Half      e_type;
    Elf64_Half      e_machine;
    Elf64_Word      e_version;
    Elf64_Addr      e_entry;
    Elf64_Off       e_phoff;
    Elf64_Off       e_shoff;
    Elf64_Word      e_flags;
    Elf64_Half      e_ehsize;
    Elf64_Half      e_phentsize;
    Elf64_Half      e_phnum;
    Elf64_Half      e_shentsize;
    Elf64_Half      e_shnum;
    Elf64_Half      e_shstrndx;
} Elf64_Ehdr;

/* 
 * e_ident[] values
 */
#define EI_MAG0       0
#define EI_MAG1       1
#define EI_MAG2       2
#define EI_MAG3       3
#define EI_CLASS      4
#define EI_DATA       5
#define EI_VERSION    6
#define EI_OSABI      7
#define EI_ABIVERSION 8
#define EI_PAD        9

#define ELFMAG0       0x7f
#define ELFMAG1       'E'
#define ELFMAG2       'L'
#define ELFMAG3       'F'
#define ELFMAG        "\177ELF"
#define SELFMAG       4

/* 
 * EI_CLASS 
 */
#define ELFCLASSNONE  0
#define ELFCLASS32    1
#define ELFCLASS64    2

/* 
 * EI_DATA 
 */
#define ELFDATANONE   0
#define ELFDATA2LSB   1
#define ELFDATA2MSB   2

/* 
 * e_type 
 */
#define ET_NONE       0
#define ET_REL        1
#define ET_EXEC       2
#define ET_DYN        3
#define ET_CORE       4
#define ET_LOPROC     0xff00
#define ET_HIPROC     0xffff

/* 
 * e_machine
 */
#define EM_NONE         0   /* No machine */
#define EM_M32          1   /* AT&T WE 32100 */
#define EM_SPARC        2   /* SPARC */
#define EM_386          3   /* Intel 80386 */
#define EM_68K          4   /* Motorola 68000 */
#define EM_88K          5   /* Motorola 88000 */
#define EM_486          6   /* Intel 80486 */
#define EM_860          7   /* Intel 80860 */
#define EM_MIPS         8   /* MIPS RS3000 Big-Endian */
#define EM_MIPS_RS4_BE  10  /* MIPS RS4000 Big-Endian */
#define EM_PPC_OLD      17  /* PowerPC - old */
#define EM_PPC          20  /* PowerPC */
#define EM_RCE_OLD      25  /* RCE - old */
#define EM_NEC_830      36  /* NEC 830 series */
#define EM_RCE          39  /* RCE */
#define EM_MCORE        39  /* MCORE */
#define EM_ARM          40  /* ARM  */
#define EM_SH           42  /* SH */
#define EM_COLDFIRE     52  /* Motorola ColdFire */
#define EM_SC           58  /* SC */
#define EM_M32R         36929 /* M32R */
#define EM_NEC          36992 /* NEC 850 series */

/* 
 * e_flags
 */
#define EF_PPC_EMB          0x80000000
#define EF_MIPS_NOREORDER   0x00000001
#define EF_MIPS_PIC         0x00000002
#define EF_MIPS_CPIC        0x00000004
#define EF_MIPS_ARCH        0xf0000000
#define EF_MIPS_ARCH_MIPS_2 0x10000000
#define EF_MIPS_ARCH_MIPS_3 0x20000000

#define EF_NOFLAGS  0
/* 
 * e_version and EI_VERSION 
 */
#define EV_NONE     0
#define EV_CURRENT  1

/*
 * Default Values 
 */
#define EI_DEFAULT    0
#define EE_NOPOINT    0    /*e_entry holds 0, if the file has no
                             associated entry point*/
#define ES_NOSECTION  0    /*Value 0, if file has no section */
/*
 * OSABI for FreeBSD
 */
#define ELFOSABI_FREEBSD    9

/* 
 * Special section indexes
 */
#define SHN_UNDEF      0
#define SHN_LORESERVE  0xff00
#define SHN_LOPROC     0xff00
#define SHN_HIPROC     0xff1f
#define SHN_ABS        0xfff1
#define SHN_COMMON     0xfff2
#define SHN_HIRESERVE  0xffff
#define SHN_GHCOMMON   0xff00


/*
 * Program header
 */
typedef struct 
{
    Elf32_Word    p_type;
    Elf32_Off     p_offset;
    Elf32_Addr    p_vaddr;
    Elf32_Addr    p_paddr;
    Elf32_Word    p_filesz;
    Elf32_Word    p_memsz;
    Elf32_Word    p_flags;
    Elf32_Word    p_align;
} Elf32_Phdr;

typedef struct
{
    Elf64_Word    p_type;
    Elf64_Word    p_flags;
    Elf64_Off     p_offset;
    Elf64_Addr    p_vaddr;
    Elf64_Addr    p_paddr;
    Elf64_Xword   p_filesz;
    Elf64_Xword   p_memsz;
    Elf64_Xword   p_align;
}Elf64_Phdr;


/* 
 * p_type 
 */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_LOPROC  0x70000000
#define PT_HIPROC  0x7fffffff

/*
 * p_flags
 */
#define PF_X            0x1
#define PF_W            0x2
#define PF_R            0x4
#define PF_MASKPROC     0xf0000000

/*
 * Notes Header
 */
typedef struct
{
    Elf32_Word n_namesz;
    Elf32_Word n_descsz;
    Elf32_Word n_type;
} Elf32_Nhdr;

/*
 * Notes in ET_CORE
 */
#define NT_PRSTATUS    1
#define NT_PRFPREG     2
#define NT_PRPSINFO    3
#define NT_TASKSTRUCT  4

typedef struct regPentium
{
    UINT32 r_fs;
    UINT32 r_es;
    UINT32 r_ds;
    UINT32 r_edi;
    UINT32 r_esi;
    UINT32 r_ebp;
    UINT32 r_isp;
    UINT32 r_ebx;
    UINT32 r_edx;
    UINT32 r_ecx;
    UINT32 r_eax;
    UINT32 r_trapno;
    UINT32 r_err;
    UINT32 r_eip;
    UINT32 r_cs;
    UINT32 r_eflags;
    UINT32 r_esp;
    UINT32 r_ss;
    UINT32 r_gs;
} gregsetPentium_t;

typedef struct regPpc
{
    UINT32 r_gpr[32];               /* 128 */
    UINT32 r_pc;                    /* 132 */
    UINT32 r_msr;                   /* 136 */
    UINT32 r_cr;                    /* 140 */
    UINT32 r_lr;                    /* 144 */
    UINT32 r_ctr;                   /* 148 */
    UINT32 r_xer;                   /* 152 */

} gregsetPpc_t;

//* BeginGearsBlock Cpp HW_Processor_Pentium
typedef gregsetPentium_t gregset_t;
//* EndGearsBlock Cpp HW_Processor_Pentium
//* BeginGearsBlock Cpp HW_Processor_PPC
//typedef gregsetPpc_t gregset_t;
//* EndGearsBlock Cpp HW_Processor_PPC

/* Note: Elf structures must be size_t aligned */
typedef struct prstatus
{
    UINT32    pr_version;           /* 4 */
    size_t    pr_statussz;          /* 8 */
    size_t    pr_gregsetsz;         /* 12 */
    size_t    pr_fpregsetsz;        /* 16 */
    UINT32    pr_osreldate;         /* 20 */
    UINT32    pr_cursig;            /* 24 */
    pid_t     pr_pid;               /* 28 */
    gregset_t pr_reg;               /* x86:104, ppc32:180 */
} prstatus_t;

#define    PRARGSZ          80    /* Maximum argument bytes saved */
#define    PRPSINFO_VERSION 1     /* Current version of prpsinfo_t */
#define    PRSTATUS_VERSION 1     /* Current version of prstatus_t */
#define    MAXCOMLEN        16
#define    NOTES_NAME       "FreeBSD" 
#define    PR_OSRELDATE     900021    /* FreeBSD version history */

/* Note: Elf structures must be size_t aligned */
typedef struct prpsinfo 
{
    UINT32  pr_version;             /* 4 */
    size_t  pr_psinfosz;            /* 8 */
    char    pr_fname[MAXCOMLEN];    /* 24 */
    char    pr_psargs[PRARGSZ];     /* 104 */
} prpsinfo_t;

#endif        /* End of __INCdcdElf */
