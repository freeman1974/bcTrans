#ifndef __UTILS_H__
#define __UTILS_H__

#ifdef __cplusplus
       extern "C" {
#endif

#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>

#define R_OK 4 /* Test for read permission. */
#define W_OK 2 /* Test for write permission. */
#define X_OK 1 /* Test for execute permission. */
#define F_OK 0 /* Test for existence. */

#define	CFG_FLOCK_BSD	0		//enable flock or not
#define	CFG_FLOCK_POSIX	0		//enable flock or not, using posix standard
#define	CFG_FMagic	0			//check file Magic
#define	CFG_MyFileLock	0		//my file lock for the whole /mnt/mtd/*.conf
#define CFG_MAX_LINE_LENGTH 1024
#define	CFG_ThreadLock	 0	// 0:no lock, 1:lock

#define	NULL_STR	'\0'
#define	NULL_VAL	0xFFFF
#define	MAX_FILE_NAME		128

#if (CFG_MyFileLock==1)
typedef struct
{
	char LockFlag;
	char FileName[MAX_FILE_NAME];
}MyFileLock;

#define My_RDLCK		1
#define My_WRLCK		2
#define My_UNLCK		0

#define MyMaxCfgFile		32
#endif

/* configuration file entry */
typedef struct TCFGENTRY
  {
    char *section;
    char *id;
    char *value;
    char *comment;
    unsigned short flags;
  }
TCFGENTRY, *PCFGENTRY;

/* values for flags */
#define CFE_MUST_FREE_SECTION	0x8000
#define CFE_MUST_FREE_ID	0x4000
#define CFE_MUST_FREE_VALUE	0x2000
#define CFE_MUST_FREE_COMMENT	0x1000

/* configuration file */
typedef struct TCFGDATA
  {
    char *fileName;		// Current file name
    int fd;			//file handler

    int dirty;			// Did we make modifications?

    char *image;		// In-memory copy of the file
    size_t size;		// Size of this copy (excl. \0)
    time_t mtime;		// Modification time

    unsigned int numEntries;
    unsigned int maxEntries;
    PCFGENTRY entries;

    /* Compatibility */
    unsigned int cursor;
    char *section;
    char *id;
    char *value;
    char *comment;
    unsigned short flags;

  }
TCONFIG, *PCONFIG;

#define CFG_VALID		0x8000
#define CFG_EOF			0x4000

#define CFG_ERROR		0x0000
#define CFG_SECTION		0x0001
#define CFG_DEFINE		0x0002
#define CFG_CONTINUE		0x0003

#define CFG_TYPEMASK		0x000F
#define CFG_TYPE(X)		((X) & CFG_TYPEMASK)
#define cfg_valid(X)	((X) != NULL && ((X)->flags & CFG_VALID))
#define cfg_eof(X)	((X)->flags & CFG_EOF)
#define cfg_section(X)	(CFG_TYPE((X)->flags) == CFG_SECTION)
#define cfg_define(X)	(CFG_TYPE((X)->flags) == CFG_DEFINE)
#define cfg_cont(X)	(CFG_TYPE((X)->flags) == CFG_CONTINUE)

char * remove_quotes(const char *szString);

int cfg_init (PCONFIG * ppconf, const char *filename, int doCreate);
int cfg_done (PCONFIG pconfig);

int cfg_freeimage (PCONFIG pconfig);
int cfg_refresh (PCONFIG pconfig);
int cfg_storeentry (PCONFIG pconfig, char *section, char *id,char *value, char *comment, int dynamic);
int cfg_rewind (PCONFIG pconfig);
int cfg_nextentry (PCONFIG pconfig);
int cfg_find (PCONFIG pconfig, char *section, char *id);
int cfg_next_section (PCONFIG pconfig);
int cfg_write (PCONFIG pconfig, char *section, char *id, char *value);
int cfg_writelong ( PCONFIG pconfig,char *section,char *id,long value);
int cfg_writeint( PCONFIG pconfig,char *section,char *id,int value);
int cfg_writeshort ( PCONFIG pconfig,char *section,char *id,unsigned short value);
int cfg_writebyte ( PCONFIG pconfig,char *section,char *id,unsigned char value);
int cfg_commit (PCONFIG pconfig);
int cfg_copy(PCONFIG pconfig,char *newfilename);
int cfg_getstring(PCONFIG pconfig, char *section, char *id, char *defval, char *valptr);
int cfg_getlong (PCONFIG pconfig, char *section, char *id, long *valptr);
int cfg_getint (PCONFIG pconfig, char *section, char *id, int *valptr);
int cfg_getshort (PCONFIG pconfig, char *section, char *id, unsigned short *valptr);
int cfg_getbyte(PCONFIG pconfig, char *section, char *id, unsigned char *valptr);
int cfg_get_item (PCONFIG pconfig, char *section, char *id, char * fmt, ...);
int cfg_write_item(PCONFIG pconfig, char *section, char *id, char * fmt, ...);

int list_entries (PCONFIG pCfg, const char * lpszSection, char * lpszRetBuffer, int cbRetBuffer);
int list_sections (PCONFIG pCfg, char * lpszRetBuffer, int cbRetBuffer);

//APIs//decimal number(not 0xHHHH)
int Cfg_ReadStr(char *File,char *Section,char *Entry,char *valptr);
int Cfg_ReadInt(char *File,char *Section,char *Entry,int defval,int *valptr);
int Cfg_ReadShort(char *File,char *Section,char *Entry,unsigned short *valptr);
int Cfg_WriteInt(char *File,char *Section,char *Entry,int Value);
int Cfg_WriteHexInt(char *File,char *Section,char *Entry,int Value);
int Cfg_WriteStr(char *File,char *Section,char *Entry,char *valptr);


#define MIN(a,b) ((a>b)?b:a)
#define KB 1024
#define MB (KB*KB)
#define	LOG_FILE_LIMIT	(5*MB)
#define	LOG_FILE_DIR	"/tmp/"
#define LOG_LINE_LIMIT 	1000

int Log_MsgLine(char *LogFileName,char *sLine);
int util_kill(int pid);


#ifdef __cplusplus
}
#endif

#endif

