#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <time.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <errno.h>
#include <pthread.h>
#include <ctype.h>

#include "utils.h"

static PCFGENTRY _cfg_poolalloc (PCONFIG p, unsigned int count);
static int _cfg_parse (PCONFIG pconfig);


#ifndef O_BINARY
#define O_BINARY 0
#endif


#if (CFG_MyFileLock==1)
static MyFileLock gMyFileLockArray[MyMaxCfgFile];

static int SetMyFileLock(char *filename,int type)
{
	int ret=-1;
	int i=0;
	int Count=0;

	for (i=0;i<MyMaxCfgFile;i++)
	{
		if (gMyFileLockArray[i].FileName==NULL)
		{
			strcpy(gMyFileLockArray[i].FileName,filename);	//do not support too long file name!
			gMyFileLockArray[i].LockFlag=type;
			ret=0;
			break;	//create the file record the exit
		}

		if (!strcmp(filename,gMyFileLockArray[i].FileName))	//find the record
		{
			if (gMyFileLockArray[i].LockFlag==My_UNLCK)	//if unlock
			{
				gMyFileLockArray[i].LockFlag=type;			//set the lock flag directly
				ret=0;
			}
			else
			{
				do {
				usleep(100000);	// sleep 100ms to check again

				}while ( (Count++<5)&&(gMyFileLockArray[i].LockFlag!=My_UNLCK) );

				if (gMyFileLockArray[i].LockFlag==My_UNLCK)	//if unlock
				{
					gMyFileLockArray[i].LockFlag=type;			//set the lock flag directly
					ret=0;
				}
				else
					ret=-3;
			}
		}
	}

	if (i==MyMaxCfgFile)
		ret=-2;

	return ret;
}

#endif

char * remove_quotes(const char *szString)
{
  char *szWork, *szPtr;

  while (*szString == '\'' || *szString == '\"')
    szString += 1;

  if (!*szString)
    return NULL;
  szWork = strdup (szString);		//string duplicate
  szPtr = strchr (szWork, '\'');		//find the first substring
  if (szPtr)
    *szPtr = 0;
  szPtr = strchr (szWork, '\"');
  if (szPtr)
    *szPtr = 0;

  return szWork;
}

/*
 *  Initialize a configuration, read file into memory
 */
int cfg_init (PCONFIG *ppconf, const char *filename, int doCreate)
{
	PCONFIG pconfig=NULL;
	int ret=0;
	int fd=-1;
	int exist=-1;

	if ( (!filename)||(ppconf==NULL) )		//check input parameters
	{
		return -1;
	}

	*ppconf = NULL;	//init output

	ret=access(filename, F_OK);	//check if exist or not
	if (ret!=0)	//no exist
	{
		if (!doCreate)	//not create new file
		{
			return -2;
		}
		else
		{
			exist=2;		//need to create
		}
	}
	else
	{
		exist=1;		//
	}

	if ((pconfig = (PCONFIG) calloc (1, sizeof (TCONFIG))) == NULL)
	{
		ret=-3;
		goto End;
	}

	pconfig->fileName = strdup (filename);
	pconfig->size=0;
	pconfig->fd=-1;

	// If the file does not exist, try to create it
	if (exist==2)
	{
		fd = creat (filename, 0644);		//create new file
		if (fd>=0){close(fd);}
	}

	//add here

	pconfig->fd=-1;

#if 0
	if ((fd = open (pconfig->fileName, O_RDWR | O_BINARY))<0)
	{
		ret=-4;
		goto End;
	}

	pconfig->fd=fd;

#if (CFG_FLOCK_BSD==1)
	ret=flock(fd,LOCK_EX);		//flock(fp,LOCK_EX|LOCK_NB);		//block write
	if (ret!=0)		//can not get R_lock when writting
	{
		ret=-100;
		goto End;
	}
#elif (CFG_FLOCK_POSIX==1)
	ret=_SimpleSetPosixLock(fd,F_WRLCK);
	if (ret<0)		//can not get R_lock when writting
	{
		ret=-100;
		goto End;
	}
#endif
#endif

	ret=cfg_refresh (pconfig);
	if (ret !=0)		//read in
	{
		ret=-11;
		goto End;
	}

	ret=0;
	*ppconf = pconfig;

End:
	if (ret<0)
	{
		cfg_done (pconfig);
		*ppconf=NULL;
#ifdef DBG_MSG
		printf("error:%s,ret=%d.filename=%s\n",(char *)__FUNCTION__,ret,filename);
#endif
	}
	else
	{
	#if (CFG_ThreadLock==1)
		pthread_mutex_init(&gThreadLock, NULL);
	#endif
	}

	return ret;
}


/*
 *  Free all data associated with a configuration
 */
int cfg_done (PCONFIG pconfig)
{
	int ret=0;

	if (pconfig)
	{
		cfg_freeimage (pconfig);
		if (pconfig->fileName)
		{
			free (pconfig->fileName);
		}
#if 0
#if (CFG_FLOCK_BSD==1)
		if (pconfig->fd>=0)
			flock(pconfig->fd,LOCK_UN);
#elif (CFG_FLOCK_POSIX==1)
		if (pconfig->fd>=0)
			ret=_SimpleSetPosixLock(pconfig->fd,F_UNLCK);
#endif
		if (pconfig->fd>=0)
			close (pconfig->fd);
#endif
		free (pconfig);
		pconfig=NULL;
		ret=0;
	#if (CFG_ThreadLock==1)
		pthread_mutex_destroy(&gThreadLock);
	#endif
	}
	else
		ret=-1;

	return ret;
}


/*
 *  Free the content specific data of a configuration
 */
int cfg_freeimage (PCONFIG pconfig)
{
  char *saveName;
  PCFGENTRY e;
  unsigned int i;

  if (pconfig->image)
  {
    	free (pconfig->image);
	pconfig->image=NULL;
  }
  if (pconfig->entries)
  {
      e = pconfig->entries;
      for (i = 0; i < pconfig->numEntries; i++, e++)
	{
	  if (e->flags & CFE_MUST_FREE_SECTION)
	    free (e->section);
	  if (e->flags & CFE_MUST_FREE_ID)
	    free (e->id);
	  if (e->flags & CFE_MUST_FREE_VALUE)
	    free (e->value);
	  if (e->flags & CFE_MUST_FREE_COMMENT)
	    free (e->comment);
	}
      free (pconfig->entries);
  }

  saveName = pconfig->fileName;
  memset (pconfig, 0, sizeof (TCONFIG));
  pconfig->fileName = saveName;

  return 0;
}


/*
 *  This procedure reads an copy of the file into memory
 *  catching the content based on stat
 */
int cfg_refresh (PCONFIG pconfig)
{
	int ret=0;
	struct stat sb;
	char *mem;
	int fd=-1;

	if (pconfig == NULL)
		return -1;

	if ((fd = open (pconfig->fileName, O_RDONLY | O_BINARY))<0)
  	{
  		ret=-2;
		goto End;
  	}
#if 1
#if (CFG_FLOCK_BSD==1)
	ret=flock(fd,LOCK_SH);
	if (ret!=0)		//can not get R_lock when writting
	{
		ret=-100;
		goto End;
	}
#elif (CFG_FLOCK_POSIX==1)
	ret=_SimpleSetPosixLock(fd,F_RDLCK);
	if (ret<0)		//can not get R_lock when writting
	{
		ret=-100;
		goto End;
	}
#endif
#endif

	//stat (pconfig->fileName, &sb);		//it will open twice, do not use this func.
	ret=fstat(fd,&sb);
	if (ret)
	{
		ret=-3;
		goto End;
	}

	/*
	*  Check to see if our incore image is still valid
	*/
	if (pconfig->image && sb.st_size == pconfig->size
	&& sb.st_mtime == pconfig->mtime)
	{
		ret=0;		//still valid, do NOT need to read in
		goto End;
	}

	/*
	*  If our image is dirty, ignore all local changes
	*  and force a reread of the image, thus ignoring all mods
	*/
	if (pconfig->dirty)
		cfg_freeimage (pconfig);

	/*
	*  Now read the full image
	*/
	mem = (char *) malloc (sb.st_size + 1);
	if (mem == NULL || read (fd, mem, sb.st_size) != sb.st_size)		//read into RAM
	{
		free (mem);mem=NULL;
		ret=-4;
		goto End;
	}
	mem[sb.st_size] = 0;

	/*
	*  Store the new copy
	*/
	cfg_freeimage (pconfig);
	pconfig->image = mem;
	pconfig->size = sb.st_size;
	pconfig->mtime = sb.st_mtime;

	if (_cfg_parse (pconfig)<0)
	{
		cfg_freeimage (pconfig);
		ret=-5;
		goto End;
	}

	ret=0;	//success!

End:
	if (fd>=0)
	{
#if 1
#if (CFG_FLOCK_BSD==1)
		flock(fd,LOCK_UN);
#elif (CFG_FLOCK_POSIX==1)
		_SimpleSetPosixLock(fd,F_UNLCK);
#endif
#endif
		close (fd);
	}
	if (ret<0)		//failed
	{
		pconfig->image = NULL;
	  	pconfig->size = 0;
#ifdef DBG_MSG
		printf("error:%s,ret=%d\n",(char *)__FUNCTION__,ret);
#endif
	}
  	return ret;
}

/*
strchr()���ܣ������ַ���s���״γ����ַ�c��λ��
*/
#define iseolchar(C) (strchr ("\n\r\x1a", C) != NULL)
#define iswhite(C) (strchr ("\f\t ", C) != NULL)

static char *_cfg_skipwhite (char *s)
{
  while (*s && iswhite (*s))
    s++;
  return s;
}


static int _cfg_getline (char **pCp, char **pLinePtr)
{
  char *start;
  char *cp = *pCp;

  while (*cp && iseolchar (*cp))
    cp++;
  start = cp;
  if (pLinePtr)
    *pLinePtr = cp;

  while (*cp && !iseolchar (*cp))
    cp++;
  if (*cp)
    {
      *cp++ = 0;
      *pCp = cp;

      while (--cp >= start && iswhite (*cp));
      cp[1] = 0;
    }
  else
    *pCp = cp;

  return *start ? 1 : 0;
}


static char *rtrim (char *str)
{
  char *endPtr;

  if (str == NULL || *str == '\0')
    return NULL;

  for (endPtr = &str[strlen (str) - 1]; endPtr >= str && isspace (*endPtr);endPtr--)
  	;
  endPtr[1] = 0;		//should be endPrt[1]='\0' ??

  return endPtr >= str ? endPtr : NULL;
}


/*
 *  Parse the in-memory copy of the configuration data
 */
static int _cfg_parse (PCONFIG pconfig)
{
	int ret=0;
	int isContinue, inString;
	char *imgPtr;
	char *endPtr;
	char *lp;
	char *section;
	char *id;
	char *value;
	char *comment;

	if (cfg_valid (pconfig))
	return 0;

	endPtr = pconfig->image + pconfig->size;
	for (imgPtr = pconfig->image; imgPtr < endPtr;)
	{
		if (!_cfg_getline (&imgPtr, &lp))
			continue;

		section = id = value = comment = NULL;
		if (iswhite (*lp))
		{
			lp = _cfg_skipwhite (lp);
			isContinue = 1;
		}
		else
			isContinue = 0;

		if (*lp == '[')
		{
		  section = _cfg_skipwhite (lp + 1);
		  if ((lp = strchr (section, ']')) == NULL)
		    continue;

		  *lp++ = 0;
		  if (rtrim (section) == NULL)
		  {
		      section = NULL;
		      continue;
		  }
		  lp = _cfg_skipwhite (lp);
		}
	    else if (*lp != ';')
		{
		  /* Try to parse
		   *   1. Key = Value
		   *   2. Value (if isContinue)
		   */
		  if (!isContinue)
		  {
		      /* Parse `<Key> = ..' */
		      id = lp;
		      if ((lp = strchr (id, '=')) == NULL)
				continue;
		      *lp++ = 0;
		      rtrim (id);
		      lp = _cfg_skipwhite (lp);
		   }

		  /* Parse value */
		  inString = 0;
		  value = lp;
		  while (*lp)
		  {
		      if (inString)
			{
			  if (*lp == inString)
			    inString = 0;
			}
		      else if (*lp == '"' || *lp == '\'')
			inString = *lp;
		      else if (*lp == ';' && iswhite (lp[-1]))
			{
			  *lp = 0;
			  comment = lp + 1;
			  rtrim (value);
			  break;
			}
		      lp++;
		    }
		}

    if (*lp == ';')
		comment = lp + 1;

    if (cfg_storeentry (pconfig, section, id, value, comment,0) <0)
	{
	  pconfig->dirty = 1;
	  return -1;
	}
    }

  pconfig->flags |= CFG_VALID;

  return 0;
}


int cfg_storeentry (PCONFIG pconfig,char *section,char *id,char *value,char *comment, int dynamic)
{
	PCFGENTRY data;

	if ((data = _cfg_poolalloc (pconfig, 1)) == NULL)
		return -1;

	data->flags = 0;
	if (dynamic)
	{
		if (section)
			section = strdup (section);
		if (id)
			id = strdup (id);
		if (value)
			value = strdup (value);
		if (comment)
			comment = strdup (value);

		if (section)
			data->flags |= CFE_MUST_FREE_SECTION;
		if (id)
			data->flags |= CFE_MUST_FREE_ID;
		if (value)
			data->flags |= CFE_MUST_FREE_VALUE;
		if (comment)
			data->flags |= CFE_MUST_FREE_COMMENT;
	}

	data->section = section;
	data->id = id;
	data->value = value;
	data->comment = comment;

	return 0;
}


static PCFGENTRY _cfg_poolalloc (PCONFIG p, unsigned int count)
{
  PCFGENTRY newBase;
  unsigned int newMax;

  if (p->numEntries + count > p->maxEntries)
    {
      newMax =
	  p->maxEntries ? count + p->maxEntries + p->maxEntries / 2 : count +
	  4096 / sizeof (TCFGENTRY);
      newBase = (PCFGENTRY) malloc (newMax * sizeof (TCFGENTRY));
      if (newBase == NULL)
	return NULL;
      if (p->entries)
	{
	  memcpy (newBase, p->entries, p->numEntries * sizeof (TCFGENTRY));
	  free (p->entries);
	}
      p->entries = newBase;
      p->maxEntries = newMax;
    }

  newBase = &p->entries[p->numEntries];
  p->numEntries += count;

  return newBase;
}


/*** COMPATIBILITY LAYER ***/


int
cfg_rewind (PCONFIG pconfig)
{
  if (!cfg_valid (pconfig))
    return -1;

  pconfig->flags = CFG_VALID;
  pconfig->cursor = 0;

  return 0;
}


/*
 *  returns:
 *	 0 success
 *	-1 no next entry
 *
 *	section	id	value	flags		meaning
 *	!0	0	!0	SECTION		[value]
 *	!0	!0	!0	DEFINE		id = value|id="value"|id='value'
 *	!0	0	!0	0		value
 *	0	0	0	EOF		end of file encountered
 */
int cfg_nextentry (PCONFIG pconfig)
{
  PCFGENTRY e;

  if (!cfg_valid (pconfig) || cfg_eof (pconfig))
    return -1;

  pconfig->flags &= ~(CFG_TYPEMASK);
  pconfig->id = pconfig->value = NULL;

  while (1)
  {
     if (pconfig->cursor >= pconfig->numEntries)
	 {
	   pconfig->flags |= CFG_EOF;
	   return -1;
	 }
     e = &pconfig->entries[pconfig->cursor++];

     if (e->section)
	 {
	   pconfig->section = e->section;
	   pconfig->flags |= CFG_SECTION;
	   return 0;
	 }
     if (e->value)
	 {
	  pconfig->value = e->value;
	  if (e->id)
	  {
	      pconfig->id = e->id;
	      pconfig->flags |= CFG_DEFINE;
	  }
	  else
	    pconfig->flags |= CFG_CONTINUE;
	  return 0;
	 }
   }
}

int cfg_find (PCONFIG pconfig, char *section, char *id)
{
  int atsection=0;

  if (!cfg_valid (pconfig) || cfg_rewind (pconfig))
    return -1;

  while (cfg_nextentry (pconfig) == 0)
  {
     if (atsection)
	 {
	   if (cfg_section (pconfig))
	     return -1;
	   else if (cfg_define (pconfig))
	   {
	      char *szId = remove_quotes (pconfig->id);
	      int bSame;
	      if (szId)
		  {
		    //bSame = !strcasecmp (szId, id);
		    bSame = !strcmp (szId, id);
		    free (szId);
		    if (bSame)
		     return 0;
		  }
	   }
	  }

	 else if (cfg_section (pconfig)
	  //&& !strcasecmp (pconfig->section, section))
	  && !strcmp (pconfig->section, section))
	 {
	   if (id == NULL)
	     return 0;
	   atsection = 1;
	 }

	 if (!section)
	 	atsection=1;
   }
   return -1;
}


/*
 *  Change the configuration
 *
 *  section id    value		action
 *  --------------------------------------------------------------------------
 *   value  value value		update '<entry>=<string>' in section <section>
 *   value  value NULL		delete '<entry>' from section <section>
 *   value  NULL  NULL		delete section <section>
 *   NULL   value value     no [section] item added
 */
int cfg_write ( PCONFIG pconfig,char *section,char *id,char *value)
{
  int ret=0;
  PCFGENTRY e, e2, eSect=0;
  int idx;
  int i;
  char sValue[CFG_MAX_LINE_LENGTH]={0};

	if (!cfg_valid (pconfig))
    	return -1;

	// search the section
	e = pconfig->entries;
	i = pconfig->numEntries;
	eSect = 0;

	if (!section){
		while (i--){
			if (e->id && !strcmp (e->id, id)){
				/* found key - do update */
				if (e->value && (e->flags & CFE_MUST_FREE_VALUE))
				{
					e->flags &= ~CFE_MUST_FREE_VALUE;
					free (e->value);
				}
				pconfig->dirty = 1;
				if ((e->value = strdup (value)) == NULL)
					return -1;
				e->flags |= CFE_MUST_FREE_VALUE;
				return 0;
			}
			e++;
		}
	}

//  ret=cfg_getstring(pconfig,section,id,sValue);		//we allow value=NULL to delete key
//  if ((ret==0)&&(!strcmp(value,sValue)))	//if equal, do not need to write, add on 2013-03-27
//  	return 0;

  while (i--)
  {
      //if (e->section && !strcasecmp (e->section, section))
      if (e->section && !strcmp (e->section, section))
	{
	  eSect = e;
	  break;
	}
      e++;
  }

  // did we find the section? no
  if (!eSect)
  {
      // check for delete operation on a nonexisting section
      if (!id || !value)
	    return 0;

      // add section first
      if (cfg_storeentry (pconfig, section, NULL, NULL, NULL,1) == -1
	  	|| cfg_storeentry (pconfig, NULL, id, value, NULL,1) == -1)
	     return -1;

      pconfig->dirty = 1;
      return 0;
  }

  // ok - we have found the section - let's see what we need to do

  if (id)
  {
     if (value)
	 {
	  /* add / update a key */
	  while (i--)
	  {
	      e++;
	      /* break on next section */
	     if (e->section)
		{
		  /* insert new entry before e */
		  idx = e - pconfig->entries;
		  if (_cfg_poolalloc (pconfig, 1) == NULL)
		    return -1;
		  memmove (e + 1, e,
		      (pconfig->numEntries - idx) * sizeof (TCFGENTRY));
		  e->section = NULL;
		  e->id = strdup (id);
		  e->value = strdup (value);
		  e->comment = NULL;
		  if (e->id == NULL || e->value == NULL)
		    return -1;
		  e->flags |= CFE_MUST_FREE_ID | CFE_MUST_FREE_VALUE;
		  pconfig->dirty = 1;
		  return 0;
		}

	      //if (e->id && !strcasecmp (e->id, id))
	    if (e->id && !strcmp (e->id, id))
		{
		  /* found key - do update */
		  if (e->value && (e->flags & CFE_MUST_FREE_VALUE))
		    {
		      e->flags &= ~CFE_MUST_FREE_VALUE;
		      free (e->value);
		    }
		  pconfig->dirty = 1;
		  if ((e->value = strdup (value)) == NULL)
		    return -1;
		  e->flags |= CFE_MUST_FREE_VALUE;
		  return 0;
		}
	    }

	  /* last section in file - add new entry */
	  if (cfg_storeentry (pconfig, NULL, id, value, NULL,1) == -1)
	    return -1;
	  pconfig->dirty = 1;
	  return 0;
	}
      else
	{
	  /* delete a key */
	  while (i--)
	    {
	      e++;
	      /* break on next section */
	      if (e->section)
		return 0;	/* not found */

	      //if (e->id && !strcasecmp (e->id, id))
	      if (e->id && !strcmp (e->id, id))
		{
		  /* found key - do delete */
		  eSect = e;
		  e++;
		  goto doDelete;
		}
	    }
	  /* key not found - that' ok */
	  return 0;
	}
    }
  else
    {
      /* delete entire section */

      /* find e : next section */
      while (i--)
	{
	  e++;
	  /* break on next section */
	  if (e->section)
	    break;
	}
      if (i < 0)
	e++;

      /* move up e while comment */
      e2 = e - 1;
      while (e2->comment && !e2->section && !e2->id && !e2->value
	  && (iswhite (e2->comment[0]) || e2->comment[0] == ';'))
	  e2--;
      e = e2 + 1;

 doDelete:
      /* move up eSect while comment */
      e2 = eSect - 1;
      while (e2->comment && !e2->section && !e2->id && !e2->value
	  && (iswhite (e2->comment[0]) || e2->comment[0] == ';'))
	  e2--;
      eSect = e2 + 1;

      /* delete everything between eSect .. e */
      for (e2 = eSect; e2 < e; e2++)
	{
	  if (e2->flags & CFE_MUST_FREE_SECTION)
	    free (e2->section);
	  if (e2->flags & CFE_MUST_FREE_ID)
	    free (e2->id);
	  if (e2->flags & CFE_MUST_FREE_VALUE)
	    free (e2->value);
	  if (e2->flags & CFE_MUST_FREE_COMMENT)
	    free (e2->comment);
	}
      idx = e - pconfig->entries;
      memmove (eSect, e, (pconfig->numEntries - idx) * sizeof (TCFGENTRY));
      pconfig->numEntries -= e - eSect;
      pconfig->dirty = 1;
    }

    return 0;
}

int cfg_writelong ( PCONFIG pconfig,char *section,char *id,long value)
{
	int ret=0;
	char sTemp[16]={0};

	sprintf(sTemp,"%ld",value);
	ret=cfg_write(pconfig,section,id,sTemp);

	return ret;
}
int cfg_writeint( PCONFIG pconfig,char *section,char *id,int value)
{
	int ret=0;
	char sTemp[16]={0};

	sprintf(sTemp,"%d",value);
	ret=cfg_write(pconfig,section,id,sTemp);

	return ret;
}
int cfg_writeshort ( PCONFIG pconfig,char *section,char *id,unsigned short value)
{
	int ret=0;
	char sTemp[16]={0};

	sprintf(sTemp,"%d",value);
	ret=cfg_write(pconfig,section,id,sTemp);

	return ret;
}

int cfg_writebyte ( PCONFIG pconfig,char *section,char *id,unsigned char value)
{
	int ret=0;
	char sTemp[16]={0};

	sprintf(sTemp,"%d",value);
	ret=cfg_write(pconfig,section,id,sTemp);

	return ret;
}

/*
key=value	//no space before/after = to avoid some comptible problem.
*/
static void
_cfg_output (PCONFIG pconfig, FILE *fd)
{
  PCFGENTRY e = pconfig->entries;
  int i = pconfig->numEntries;
  int m = 0;
  int j, l;
  int skip = 0;

  while (i--)
    {
      if (e->section)
	{
	  /* Add extra line before section, unless comment block found */
	  if (skip)
	    fprintf (fd, "\n");
	  fprintf (fd, "[%s]", e->section);
	  if (e->comment)
	    fprintf (fd, "\t;%s", e->comment);

	  /* Calculate m, which is the length of the longest key */
	  m = 0;
	  for (j = 1; j <= i; j++)
	    {
	      if (e[j].section)
		break;
	      if (e[j].id && (l = strlen (e[j].id)) > m)
		m = l;
	    }

	  /* Add an extra lf next time around */
	  skip = 1;
	}
      /*
       *  Key = value
       */
      else if (e->id && e->value)
	{
	  if (m)
	    fprintf (fd, "%s=%s",e->id, e->value);
	  else
	    fprintf (fd, "%s=%s", e->id, e->value);
	  if (e->comment)
	    fprintf (fd, "\t;%s", e->comment);
	}
      /*
       *  Value only (continuation)
       */
      else if (e->value)
	{
	  fprintf (fd, "%s", e->value);
	  if (e->comment)
	    fprintf (fd, "\t;%s", e->comment);
	}
      /*
       *  Comment only - check if we need an extra lf
       *
       *  1. Comment before section gets an extra blank line before
       *     the comment starts.
       *
       *          previousEntry = value
       *          <<< INSERT BLANK LINE HERE >>>
       *          ; Comment Block
       *          ; Sticks to section below
       *          [new section]
       *
       *  2. Exception on 1. for commented out definitions:
       *     (Immediate nonwhitespace after ;)
       *          [some section]
       *          v1 = 1
       *          ;v2 = 2   << NO EXTRA LINE >>
       *          v3 = 3
       *
       *  3. Exception on 2. for ;; which certainly is a section comment
       *          [some section]
       *          definitions
       *          <<< INSERT BLANK LINE HERE >>>
       *          ;; block comment
       *          [new section]
       */
      else if (e->comment)
	{
	  if (skip && (iswhite (e->comment[0]) || e->comment[0] == ';'))
	    {
	      for (j = 1; j <= i; j++)
		{
		  if (e[j].section)
		    {
		      fprintf (fd, "\n");
		      skip = 0;
		      break;
		    }
		  if (e[j].id || e[j].value)
		    break;
		}
	    }
	  fprintf (fd, ";%s", e->comment);
	}
      fprintf (fd, "\n");
      e++;
    }
}



/*
 *  Write a formatted copy of the configuration to a file
 *
 *  This assumes that the inifile has already been parsed
 */
static void
_cfg_outputformatted (PCONFIG pconfig, FILE *fd)
{
  PCFGENTRY e = pconfig->entries;
  int i = pconfig->numEntries;
  int m = 0;
  int j, l;
  int skip = 0;

  while (i--)
    {
      if (e->section)
	{
	  /* Add extra line before section, unless comment block found */
	  if (skip)
	    fprintf (fd, "\n");
	  fprintf (fd, "[%s]", e->section);
	  if (e->comment)
	    fprintf (fd, "\t;%s", e->comment);

	  /* Calculate m, which is the length of the longest key */
	  m = 0;
	  for (j = 1; j <= i; j++)
	    {
	      if (e[j].section)
		break;
	      if (e[j].id && (l = strlen (e[j].id)) > m)
		m = l;
	    }

	  /* Add an extra lf next time around */
	  skip = 1;
	}
      /*
       *  Key = value
       */
      else if (e->id && e->value)
	{
	  if (m)
	    fprintf (fd, "%-*.*s = %s", m, m, e->id, e->value);
	  else
	    fprintf (fd, "%s = %s", e->id, e->value);
	  if (e->comment)
	    fprintf (fd, "\t;%s", e->comment);
	}
      /*
       *  Value only (continuation)
       */
      else if (e->value)
	{
	  fprintf (fd, "  %s", e->value);
	  if (e->comment)
	    fprintf (fd, "\t;%s", e->comment);
	}
      /*
       *  Comment only - check if we need an extra lf
       *
       *  1. Comment before section gets an extra blank line before
       *     the comment starts.
       *
       *          previousEntry = value
       *          <<< INSERT BLANK LINE HERE >>>
       *          ; Comment Block
       *          ; Sticks to section below
       *          [new section]
       *
       *  2. Exception on 1. for commented out definitions:
       *     (Immediate nonwhitespace after ;)
       *          [some section]
       *          v1 = 1
       *          ;v2 = 2   << NO EXTRA LINE >>
       *          v3 = 3
       *
       *  3. Exception on 2. for ;; which certainly is a section comment
       *          [some section]
       *          definitions
       *          <<< INSERT BLANK LINE HERE >>>
       *          ;; block comment
       *          [new section]
       */
      else if (e->comment)
	{
	  if (skip && (iswhite (e->comment[0]) || e->comment[0] == ';'))
	    {
	      for (j = 1; j <= i; j++)
		{
		  if (e[j].section)
		    {
		      fprintf (fd, "\n");
		      skip = 0;
		      break;
		    }
		  if (e[j].id || e[j].value)
		    break;
		}
	    }
	  fprintf (fd, ";%s", e->comment);
	}
      fprintf (fd, "\n");
      e++;
    }
}

/*
 *  Write the changed file back
 */
int cfg_commit (PCONFIG pconfig)
{
	int ret=0;

	int fd=-1;
	FILE *fp=NULL;

#if (CFG_ThreadLock==1)
//	ret=pthread_mutex_trylock(&gThreadLock);
//	if (ret==EBUSY)
//		return -11;
	pthread_mutex_lock(&gThreadLock);
#endif

	if (!cfg_valid (pconfig))
	{
		ret=-1;
		goto End;
	}

	if (pconfig->dirty)
	{

#if 1
		if ((fd = open (pconfig->fileName, O_RDWR | O_BINARY)) <0)
		{
			ret=-4;
			goto End;
		}

#if (CFG_FLOCK_BSD==1)			//yingcai add
		ret=flock(fd,LOCK_EX);		//flock(fp,LOCK_EX|LOCK_NB);		//block write
		if (ret!=0)		//can not get R_lock when writting
		{
			ret=-100;
			goto End;
		}
#elif (CFG_FLOCK_POSIX==1)
		ret=_SimpleSetPosixLock(fd,F_WRLCK);
		if (ret<0)		//can not get R_lock when writting
		{
			ret=-100;
			goto End;
		}
#endif
#endif
		if ((fp = fopen (pconfig->fileName, "w")) == NULL)
		{
			ret=-2;
			goto End;
		}

      		//_cfg_outputformatted (pconfig,fp);
      		_cfg_output(pconfig,fp);
		if (fp){
			fflush(fp);
			fclose(fp);
		}

		pconfig->dirty = 0;
      		ret=0;
    	}

End:

#if 1
#if (CFG_FLOCK_BSD==1)
	if (fd>=0)
		flock(fd,LOCK_UN);
#elif (CFG_FLOCK_POSIX==1)
	if (fd>=0)
		_SimpleSetPosixLock(fd,F_UNLCK);
#endif
	close (fd);
#endif
	if (ret<0)
	{
#ifdef DBG_MSG
		printf("error:%s,ret=%d\n",(char *)__FUNCTION__,ret);
#endif
	}

#if (CFG_ThreadLock==1)
	pthread_mutex_unlock(&gThreadLock);
#endif

	return ret;
}


int cfg_copy(PCONFIG pconfig,char *newfilename)
{
	int ret=0;

	int fd=-1;
	FILE *fp=NULL;

#if (CFG_ThreadLock==1)
//	ret=pthread_mutex_trylock(&gThreadLock);
//	if (ret==EBUSY)
//		return -11;
	pthread_mutex_lock(&gThreadLock);
#endif

	if (!cfg_valid (pconfig))
	{
		ret=-1;
		goto End;
	}

//	if (pconfig->dirty)
	{

#if 1
		if ((fd = open (newfilename, O_RDWR | O_BINARY)) <0)
		{
			ret=-4;
			goto End;
		}

#if (CFG_FLOCK_BSD==1)
		ret=flock(fd,LOCK_EX);		//flock(fp,LOCK_EX|LOCK_NB);		//block write
		if (ret!=0)		//can not get R_lock when writting
		{
			ret=-100;
			goto End;
		}
#elif (CFG_FLOCK_POSIX==1)
		ret=_SimpleSetPosixLock(fd,F_WRLCK);
		if (ret<0)		//can not get R_lock when writting
		{
			ret=-100;
			goto End;
		}
#endif
#endif
		if ((fp = fopen (newfilename, "w")) == NULL)
		{
			ret=-2;
			goto End;
		}

      		_cfg_outputformatted (pconfig,fp);
		if (fp){
			fflush(fp);
			fclose(fp);
		}

//		pconfig->dirty = 0;
      		ret=0;
    	}

End:

#if 1
#if (CFG_FLOCK_BSD==1)
	if (fd>=0)
		flock(fd,LOCK_UN);
#elif (CFG_FLOCK_POSIX==1)
	if (fd>=0)
		_SimpleSetPosixLock(fd,F_UNLCK);
#endif
	close (fd);
#endif
	if (ret<0)
	{
#ifdef DBG_MSG
		printf("error:%s,ret=%d\n",(char *)__FUNCTION__,ret);
#endif
	}

#if (CFG_ThreadLock==1)
	pthread_mutex_unlock(&gThreadLock);
#endif

	return ret;
}


int
cfg_next_section(PCONFIG pconfig)
{
  PCFGENTRY e;

  do
    if (0 != cfg_nextentry (pconfig))
      return -1;
  while (!cfg_section (pconfig));

  return 0;
}


int
list_sections (PCONFIG pCfg, char * lpszRetBuffer, int cbRetBuffer)
{
  int curr = 0, sect_len = 0;
  lpszRetBuffer[0] = 0;

  if (0 == cfg_rewind (pCfg))
    {
      while (curr < cbRetBuffer && 0 == cfg_next_section (pCfg)
	  && pCfg->section)
	{
	  sect_len = strlen (pCfg->section) + 1;
	  sect_len =
	      sect_len > cbRetBuffer - curr ? cbRetBuffer - curr : sect_len;

	  memmove (lpszRetBuffer + curr, pCfg->section, sect_len);

	  curr += sect_len;
	}
      if (curr < cbRetBuffer)
	lpszRetBuffer[curr] = 0;
      return curr;
    }
  return 0;
}


int
list_entries (PCONFIG pCfg, const char * lpszSection, char * lpszRetBuffer, int cbRetBuffer)
{
  int curr = 0, sect_len = 0;
  lpszRetBuffer[0] = 0;

  if (0 == cfg_rewind (pCfg))
    {
      while (curr < cbRetBuffer && 0 == cfg_nextentry (pCfg))
	{
	  if (cfg_define (pCfg)
	      && !strcmp (pCfg->section, lpszSection) && pCfg->id)
	    {
	      sect_len = strlen (pCfg->id) + 1;
	      sect_len =
		  sect_len >
		  cbRetBuffer - curr ? cbRetBuffer - curr : sect_len;

	      memmove (lpszRetBuffer + curr, pCfg->id, sect_len);

	      curr += sect_len;
	    }
	}
      if (curr < cbRetBuffer)
	lpszRetBuffer[curr] = 0;
      return curr;
    }
  return 0;
}

int cfg_getstring(PCONFIG pconfig, char *section, char *id, char *defval, char *valptr)
{
	int ret=0;

	if(cfg_find(pconfig,section,id) <0)
	{
		if (defval)
			strcpy(valptr,defval);
		else
			ret=-1;
	}
	else
		strcpy(valptr,pconfig->value);

	return ret;
}

int cfg_getlong (PCONFIG pconfig, char *section, char *id, long *valptr)
{
//	if(!pconfig || !section || !id) return -1;
	if(cfg_find(pconfig,section,id) <0)
	{
		return -2;
	}
	if (valptr)
		*valptr = atoi(pconfig->value);
	return 0;
}

int cfg_getint (PCONFIG pconfig, char *section, char *id, int *valptr)
{
	return cfg_getlong(pconfig,section,id,(long *)valptr);
}

int cfg_getshort (PCONFIG pconfig, char *section, char *id, unsigned short *valptr)
{
	if(!pconfig || !section || !id) return -1;
	if(cfg_find(pconfig,section,id) <0)
	{
	//	*valptr =NULL_VAL;
		return -2;
	}
	*valptr = atoi(pconfig->value);
	return 0;
}

int cfg_getbyte(PCONFIG pconfig, char *section, char *id, unsigned char *valptr)
{
	if(!pconfig || !section || !id) return -1;
	if(cfg_find(pconfig,section,id) <0)
	{
		*valptr =0xFF;
		return -2;
	}
	*valptr = atoi(pconfig->value);
	return 0;
}


int cfg_get_item (PCONFIG pconfig, char *section, char *id, char * fmt, ...)
{
	int ret;
	va_list ap;

	if(!pconfig || !section || !id) return -1;
	if(cfg_find(pconfig,section,id) <0) return -1;
	va_start(ap, fmt);
	ret = vsscanf(pconfig->value, fmt, ap );
	va_end(ap);
	if(ret > 0) return 0;
	else return -1;
}

int cfg_write_item(PCONFIG pconfig, char *section, char *id, char * fmt, ...)
{
	int ret;
	char buf[CFG_MAX_LINE_LENGTH];
	va_list ap;
	va_start(ap, fmt);
	ret = vsnprintf(buf, CFG_MAX_LINE_LENGTH, fmt, ap);
	va_end(ap);
	if(ret < 0) return -1;
	return cfg_write(pconfig,section,id,buf);
}

//===================API==================================

int Cfg_ReadStr(char *File,char *Section,char *Entry,char *valptr)
{
	PCONFIG pCfg;
	int ret=-1;

	ret=cfg_init (&pCfg, File, 0);		//do not create new file
	if (!ret)
	{
		cfg_getstring(pCfg, Section, Entry,NULL,valptr);
		cfg_done(pCfg);
	}

	return ret;
}

//decimal number(not 0xHHHH)
int  Cfg_ReadInt(char *File,char *Section,char *Entry,int defval,int *valptr)
{
	PCONFIG pCfg;
	char sValue[64];
	int ret=-1;

	ret=cfg_init (&pCfg, File, 0);		//do not create new file
	if (!ret)
	{
		ret=cfg_getstring(pCfg, Section, Entry,NULL,sValue);
		if (ret==0)	//success
			*valptr = atoi(sValue);		//decimal number
		else
			*valptr = defval;
		cfg_done(pCfg);
	}
	return ret;
}

//decimal number(not 0xHHHH)
int Cfg_ReadShort(char *File,char *Section,char *Entry,unsigned short *valptr)
{
	PCONFIG pCfg;
	char sValue[64];
	int ret=-1;
	char *p=sValue;

	ret=cfg_init (&pCfg, File, 0);		//do not create new file
	if (!ret)
	{
		ret=cfg_getstring(pCfg, Section, Entry, NULL, sValue);
		if (!ret)
		{
			*valptr = atoi(p);		//decimal number
			//*valptr = strtol(sValue,(char**)NULL,10);
			//*valptr = strtol(sValue,(char**)NULL,16);
		}

		cfg_done(pCfg);
	}

	return ret;
}


//decimal number(not 0xHHHH)
int Cfg_WriteInt(char *File,char *Section,char *Entry,int Value)
{
	PCONFIG pCfg;
	int ret=-1;

	char sValue[64];
	sprintf(sValue,"%d",Value);		//convert to string, decimal number

	ret=cfg_init (&pCfg, File, 1);		//create it if not exist
	if (!ret)
	{
		cfg_write (pCfg, Section, Entry, sValue);
		ret = cfg_commit(pCfg);	//save
		cfg_done(pCfg);	//close handle
	}
	return ret;
}

int Cfg_WriteHexInt(char *File,char *Section,char *Entry,int Value)
{
	PCONFIG pCfg;
	int ret=-1;

	char sValue[64];
	sprintf(sValue,"0x%0x",Value);

	ret=cfg_init (&pCfg, File, 1);		//create it if not exist
	if (!ret)
	{
		cfg_write (pCfg, Section, Entry, sValue);
		ret = cfg_commit(pCfg);	//save
		cfg_done(pCfg);	//close handle
	}
	return ret;
}

//decimal number(not 0xHHHH)
int Cfg_WriteStr(char *File,char *Section,char *Entry,char *valptr)
{
	PCONFIG pCfg;
	int ret=-1;

	ret=cfg_init (&pCfg, File, 1);		//create it if not exist
	if (!ret)
	{
		cfg_write (pCfg, Section, Entry, valptr);
		ret = cfg_commit(pCfg);	//save
		cfg_done(pCfg);	//close handle
	}
	return ret;
}


static inline unsigned long fGetSize(const char *filename)
{
	struct stat buf;

	if(stat(filename, &buf)<0)
	{
		return 0;
	}
	return (unsigned long)buf.st_size;
}


//return errcode or written string length
static int _MsgLine2(char *Dir,char *FileName,char *sLine,unsigned int FileSizeLimit)
{
	int ret=0;
	char gsLine[LOG_LINE_LIMIT+32];

	FILE *fp=NULL;
	unsigned long fSize=0;
	static char FullFileName[128]={0};
	struct tm *pLocalTime=NULL;
	time_t timep;

	ret=strlen(sLine);
	if ((ret>LOG_LINE_LIMIT)||(ret<=0))
	{
		ret=-2;goto End;
	}

	time(&timep);
	pLocalTime=localtime(&timep);	//convert to local time to create timestamp
	sprintf(gsLine,"TS:%02d:%02d:%02d %s",pLocalTime->tm_hour,pLocalTime->tm_min,pLocalTime->tm_sec,sLine);
	if (Dir){
		sprintf(FullFileName,"%s/",Dir);
		strcat(FullFileName,FileName);
	}else{
		strcpy(FullFileName,FileName);
	}

	fSize=fGetSize(FullFileName);

	if ((fSize>FileSizeLimit) ||(fSize==0) )		//too large or no logfile exist, create it
		fp = fopen(FullFileName, "w+"); 	//reset to zero
	else
		fp = fopen(FullFileName, "a+"); 	//append to the end of logfile

	if (fp)
	{
		fputs(gsLine,fp);
		fflush(fp);
		fclose(fp);
		ret=strlen(gsLine);
	}

End:
	return ret;
}

//return errcode or written string length
int Log_MsgLine(char *LogFileName,char *sLine)
{
	int ret=0;
	ret=_MsgLine2(NULL,LogFileName,sLine,LOG_FILE_LIMIT);
	return ret;
}

int util_kill(int pid)
{
	int i=0;

    if (pid <= 0) {
        return -1;
    }

    // first, try kill by SIGTERM.
    if (kill(pid, SIGTERM) < 0) {
        return -2;
    }

    // wait to quit.
    for (i = 0; i < 100; i++) {
        int status = 0;
        pid_t qpid = -1;
        if ((qpid = waitpid(pid, &status, WNOHANG)) < 0) {
            return -3;
        }

        // 0 is not quit yet.
        if (qpid == 0) {
            usleep(10 * 1000);
            continue;
        }

        // killed, set pid to -1.
        printf("SIGTERM stop process pid=%d done.\n", pid);

        return 0;
    }

    // then, try kill by SIGKILL.
    if (kill(pid, SIGKILL) < 0) {
        return -5;
    }

    // wait for the process to quit.
    // for example, ffmpeg will gracefully quit if signal is:
    //         1) SIGHUP     2) SIGINT     3) SIGQUIT
    // other signals, directly exit(123), for example:
    //        9) SIGKILL    15) SIGTERM
    int status = 0;
    // @remark when we use SIGKILL to kill process, it must be killed,
    //      so we always wait it to quit by infinite loop.
    while (waitpid(pid, &status, 0) < 0) {
        usleep(10 * 1000);
        continue;
    }

    //printf("SIGKILL stop process pid=%d ok.", pid);

    return 1;
}



