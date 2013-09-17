//vispatch.cpp

#include "endianfix.h"

#ifdef _UNIX_

#include "swchpal.h"
#include "unix.h"
#include <unistd.h>
#include <sys/param.h>

#else

#include "swchpal.h"

#include <fltenv.h>
#include <fltpnt.h>
#include <dos.h>

#endif // _UNIX_

#if defined (__APPLE__) || defined (MACOSX)

#include <ctype.h>

#endif // __APPLE__ || MACOSX

struct visdat_t
{
    char        File[32];
    uint32_t    len;
    uint32_t    vislen;
    uint8_t*    visdata;
    uint32_t    leaflen;
    uint8_t*    leafdata;
};

int32_t*        Line            = 0;
int32_t*        Line2           = 0;
int32_t*        Page            = 0;
int32_t*        Strange         = 0;
int32_t         DWidth          = 800;
int32_t         DHeight         = 600;
int32_t         ScreenMode      = 0;
int32_t         DDepth          = 8;

FILE*           InFile          = 0;
FILE*           OutFile         = 0;
FILE*           fVIS            = 0;
FILE*           VISout          = 0;

visdat_t*       visdat          = 0;

pakheader_t     NewPak          = { 0 };
pakentry_t      NewPakEnt[2048] = { 0 };
int32_t         NPcnt           = 0;
int32_t         numvis          = 0;

#ifdef _UNIX_

DIR*            dptr            = 0;
struct dirent*  de              = 0;
struct FIND     fe              = { 0 };
regex_t         rexp;
char            lx_Path[256]    = { 0 };
char            lx_File[256]    = { 0 };

#endif // _UNIX_

int32_t         mode            = 0;
int32_t         cnt             = 0;
int32_t         usepak          = 0;

char            FinBSP[256]     = "*.BSP";
char            PinPAK[256]     = "PAK*.PAK";
char            VIS[256]        = "patch.vis";
char            FoutBSP[256]    = "";
char            FoutPak[256]    = "Pak*.Pak";
char            File[256]       = "Pak*.Pak";
char            CurName[38]     = "";
char            Path[256]       = "";
char            Path2[256]      = "";
char            TempFile[256]   = "~vistmp.tmp";
char            FilBak[256]     = "";
struct FIND*    entry           = 0;
char*           path            = 0;
int32_t         vispos          = 0;
int32_t         pakpos          = 0;

void            loadvis (FILE* fp);
void            freevis (void);
int32_t         OthrFix (uint32_t Offset, uint32_t Lenght);
void            usage (const char* pCmdName, const char* pErrorMsg);

#ifdef _UNIX_

void            fcloseall();
char*           strlwr(char* string);
char*           strrev(char* string);
int32_t         strcmpi(char* a, char* b);
struct FIND*    findnext( void );
int32_t         filesize(char* filename);
char*           build_regexp(char* string);
struct FIND*    findfirst(char* path, int32_t unknown);
void            _dos_setfileattr(char* filename, mode_t attributes);

// This code mainly makes use of Posix compliant calls so
// it should compile under most Unix platforms

void fcloseall()
{
    if (InFile)
    {
        fclose(InFile);
    }
    
    if (OutFile)
    {
        fclose(OutFile);
    }
    
    if (fVIS)
    {
        fclose(fVIS);
    }
}

char*   strlwr(char *string)
{
    char*   p = string;

    while(*p)
    {
        *p= (char) tolower(*p);
        ++p;
	}

    return(string);
}

char*   strrev(char *string)
{
    char*   p=string;
    char    swap;
    int32_t i = ((int32_t) strlen(string))-1;

    while(p < (string+i))
    {
        swap        = *(string+i);
        *(string+i) = *p;
        *p          = swap;
        i--;
        p++;
    }
    
    return(string);
}

int32_t strcmpi(char* a, char* b)
{
  return(strcasecmp(a,b));
}

struct FIND *findnext (void)
{
    static FIND fe;
    char fnBuffer[256];
    struct stat status;

    de = readdir( dptr );
    
    if(de == NULL)
    {
        return( (struct FIND *) NULL );
    }
    
    // If doesn't match, return next match
    if(regexec(&rexp, de->d_name, 0, 0, 0))
    {
        //printf("%s did not match %s\n", de->d_name, lx_File);
        return(findnext());
    }

    strcpy(fe.name, de->d_name);
    strcpy(fnBuffer, lx_Path);
    strcat(fnBuffer, "/");
    strcat(fnBuffer, fe.name);
    stat( fnBuffer, &status );
    // Filemodes mapped to attributes
    fe.attribute = status.st_mode;
    // Only return files!
  
    if(S_ISREG(status.st_mode)) 
    {
        //printf("Returning %s\n", fe.name);
        return(&fe);
    }

    return(findnext());
}

int32_t filesize(char *filename)
{
    struct stat status;

    if(stat(filename, &status)==-1)
    {
        return(-1);
    }
    
    return (int32_t) (status.st_size);
}

// Converts file wildcards to regexp string
char*   build_regexp(char *string)
{
    char*       p = string;
    char*       pos;
    static char newstring[512];
    char        tempstr[512];

    p = newstring;
    strcpy(newstring, string);
    // First pass: comment any regexp special characters
    while((pos = strpbrk(p, ".+[]()|\\^$")) != NULL)
    {
        strcpy(tempstr, pos);
        *pos = '\\';
        *(pos+1) = 0;
        strcat(p, tempstr);
        p = pos+2;
    }

    // Then build regexp for ? and *
    p = newstring;
    while((pos = strpbrk(p, "?*")) != NULL)
    {
        strcpy(tempstr, pos);
        *pos = '.';
        *(pos+1) = 0;
        strcat(p, tempstr);
        p = pos+2;
    }

    //printf("\nWildcard String = %s  RegExp String = %s\n", string, newstring);
    return(newstring);
}

struct FIND*    findfirst(char* path, int32_t unknown)
{
    char *p;
  
    // Free Static data on second pass
    if(dptr)
    {
        closedir(dptr);
        regfree(&rexp);
    } 

    p = strrchr(path, '/'); 
  
    if(!p)
    {
        //printf("No // in filename\n");
        return(NULL);
    }

    *p++=0;
    strcpy(lx_Path, path);
    strcpy(lx_File, p);
    *(p-1) = '/';

    dptr = opendir(lx_Path);
    if(!dptr)
    {
        //printf("Bad Dptr: %s\n");
        return(NULL);
    }

    // Compile the regular expression matcher
    regcomp( &rexp, build_regexp(lx_File), REG_EXTENDED | REG_ICASE | REG_NOSUB);

    return(findnext());
}

void _dos_setfileattr(char *filename, mode_t attributes)
{
    chmod(filename, attributes);
}

#endif // _UNIX_

void usage (const char* pCmdName, const char* pErrorMsg)
{
    if (pErrorMsg != NULL)
    {
        fprintf (stdout, "\n%s: %s\n%s: Try \'vispatch -help\' for more information.\n", pCmdName, pErrorMsg, pCmdName);
        exit (EXIT_FAILURE);
    }

    fprintf (stdout, "Usage: %s [-help] [-data visfile] [-dir workdir] [-extract|-new] pakfile\n\n"
                     "Files:\n"
                     "  -data visfile: The patchdata to use, default: \"patch.vis\".\n"
                     "  -dir workdir:  The directory that holds the pak file, default: current dir.\n"
                     "  pakfile:       The pak file to use, default: \"PAK*.PAK\".\n"
                     "Mode:\n"
                     "  -extract:      Append the visdata of the pakfile to the visfile.\n"
                     "  -new:          Create a new pakfile instead of overwriting \"pakfile\".\n"
                     "  vispatch will overwrite \"pakfile\" by default. Be carefull!\n", pCmdName);

    exit (EXIT_SUCCESS);
}

int main(int argc,char **argv)
{
    printf("Vis Patch v1.2a by Andy Bay (ABay@Teir.Com)\n");
    printf("Big endian & 64-bit support: Axel 'awe' Wefers (awe@fruitz-of-dojo.de)\n");
    int32_t tmp;
#ifdef _UNIX_
    char 	currentDir[MAXPATHLEN];
    
    getcwd (currentDir, MAXPATHLEN);
    snprintf (Path, sizeof(Path) - 1, "%s/", currentDir);
#endif // _UNIX_
    if (argc>1)
    {
        for (tmp=1;tmp<argc;tmp++)
        {
			strlwr(argv[tmp]);
            if (argv[tmp][0]=='-' || argv[tmp][0]=='/')
            {
                if (argv[tmp][0]=='/')
                {
                    argv[tmp][0]='-';
                }
                
                if (strcmp(argv[tmp],"-help")==0)
                {
                    usage (argv[0], NULL);
                }
                
                if (strcmp(argv[tmp],"-data")==0)
                {
                    argv[tmp][0]=0;
                    strcpy(VIS,argv[++tmp]);
                    argv[tmp][0]=0;
                    printf("The Vis data source is %s.\n",VIS);
                }

                if (strcmp(argv[tmp],"-dir")==0)
                {
                    argv[tmp][0]=0;
                    strcpy(Path,argv[++tmp]);
                    argv[tmp][0]=0;
#ifdef _UNIX_
                    if(Path[strlen(Path)-1] != '/')
                    {
                        strcat(Path, "/");
                    }
#endif // _UNIX_
                    printf("The pak/bsp directory is %s.\n",Path);
                }

                if (strcmp(argv[tmp],"-extract")==0)
                {
                    mode=1;
                    argv[tmp][0]=0;
                    printf("Extracting vis data to VisPatch.dat, auto-append.\n");
                }
                if (strcmp(argv[tmp],"-new")==0)
                {
                    mode = 2;
                    argv[tmp][0]=0;
                    strcpy(Path2,Path);
                    strcat(Path2,FoutPak);
                    entry = findfirst (Path2,0);
                    cnt = 0;
                    while (entry != NULL)
                    {
                        cnt++;
                        entry = findnext ();
                    }
                    sprintf(FoutPak,"%spak%i.pak",Path,cnt);
                    printf("The new pak file is called %s.\n",FoutPak);
                }
            }
            if (tmp<argc)
            {
                if (strlen(argv[tmp])) 
                {
                    strcpy(File,argv[tmp]);
                }
            }

        }
    }
    
    sprintf(TempFile,"%s%s",Path,"~vistmp.tmp");

    if (mode==0||mode == 2)
    {
        strcpy(Path2,Path);
        strcat(Path2,File);
        strcpy(FilBak,File);
        entry = findfirst (Path2, 0);
        while (entry != NULL)
        {
            strcpy(File,entry->name);
            strcpy(Path2,Path);
            strcat(Path2,File);
            if(entry->attribute&_A_ARCH)
            {
                printf("%s",Path2);
                entry->attribute = entry->attribute - _A_ARCH;
                _dos_setfileattr(Path2,entry->attribute);
            }
            entry = findnext ();
        }
        int32_t chk=0;
        fVIS = fopen(VIS,"rb");
        if (!fVIS)
        {
            usage (argv[0], "couldn't find the vis source file.");
        }
        
        loadvis(fVIS);
        strcpy(Path2,Path);
        strcat(Path2,FilBak);
        OutFile = fopen(TempFile,"w+b");
        entry = findfirst (Path2, 0);
        cnt = 0;
        while (entry != NULL)
        {
            cnt++;
            strcpy(File,entry->name);
            if(entry->attribute&_A_ARCH)
            {
                entry = findnext ();
                continue;
            }
            strcpy(Path2,Path);
            strcat(Path2,File);
            InFile=fopen(Path2,"rb");
            if (!InFile)
            {
                usage (argv[0], "couldn't find the level file.");
            }
            
            chk = ChooseLevel(File,0,100000);
            
            if(mode == 0)
            {
                NPcnt = 0;
                fclose(OutFile);
                fclose(InFile);
                
                if(chk>0)
                {
                    remove(Path2);
                    rename(TempFile,Path2);
                }
                OutFile = fopen(TempFile,"w+b");
            }
            else if(usepak == 1)
            {
                fclose(InFile);
            }
            else if(chk > 0)
            {
                //printf("%i\n",chk);
                fclose(OutFile);
                fclose(InFile);
                strcpy(Path2,Path);
                strcat(Path2,File);
                strcpy(File,Path2);
                strrev(File);
                File[0] = 'k';
                File[1] = 'a';
                File[2] = 'b';
                strrev(File);
                remove(File);
                strcpy(Path2,Path);
                strcat(Path2,CurName);
                rename(Path2,File);
                rename(TempFile,Path2);
                OutFile = fopen(TempFile,"w+b");
            }
            else
            {
                fclose(OutFile);
                fclose(InFile);
                OutFile = fopen(TempFile,"w+b");
            }
            entry = findnext ();

        }
        
        fcloseall();

        if(mode == 2 && usepak == 1)
        {
            rename(TempFile,FoutPak);
        }
        
        freevis();
    }
    else if (mode == 1)
    {
        if(filesize(VIS)==-1)
        {
            fVIS = fopen(VIS,"wb");
        }
        else
        {
            fVIS = fopen(VIS,"r+b");
        }
        
        strcpy(Path2,Path);
        strcat(Path2,File);
        entry = findfirst (Path2, 0);
        while (entry != NULL)
        {
            strcpy(File,entry->name);
            strcpy(Path2,Path);
            strcat(Path2,File);
            InFile=fopen(Path2,"r+b");
            
            if (!InFile)
            {
                usage (argv[0], "couldn't find the level file.");
            }
            
            ChooseFile(File,0,0);
            entry = findnext ();
        }
    }
    return 0;
}

int32_t ChooseLevel(char *FileSpec,uint32_t Offset,int32_t length)
{
    int32_t tmp=0;

    //printf("Looking at file %s %li %i.\n",FileSpec,length,mode);

    if (strstr(strlwr(FileSpec),".pak"))
    {
        printf("Looking at file %s.\n",FileSpec);usepak=1;tmp=PakFix(Offset);
    }
    else if (length > 50000 && strstr(strlwr(FileSpec),".bsp"))
    {
        printf("Looking at file %s.\n",FileSpec);
        strcpy(CurName, FileSpec);
        tmp=BSPFix(Offset);
    }
    else if (mode == 0 && Offset > 0)
    {
        tmp = OthrFix(Offset, length);
    }
    else if (mode == 2 && Offset > 0)
    {
        NPcnt--;
    }
    
    return tmp;
}

int32_t PakFix(uint32_t Offset)
{
    int32_t     ugh;
    int32_t     test;
    pakheader_t Pak;
    
    test = (int32_t) fwrite (&Pak, sizeof(pakheader_t),1,OutFile);
    
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    fseek(InFile,Offset,SEEK_SET);
    fread (&Pak,sizeof(pakheader_t),1,InFile);
    uint32_t numentry=Pak.dirsize/64;
    pakentry_t *PakEnt=(pakentry_t *) malloc(numentry*sizeof(pakentry_t));
    fseek(InFile,Offset+Pak.diroffset,SEEK_SET);
    fread (PakEnt,sizeof(pakentry_t),numentry,InFile);
    for (uint32_t pakwalk=0;pakwalk<numentry;pakwalk++)
    {
        strcpy((char *) NewPakEnt[NPcnt].filename,(char *) PakEnt[pakwalk].filename);
        strcpy(CurName,(char *) PakEnt[pakwalk].filename);
        NewPakEnt[NPcnt].size = PakEnt[pakwalk].size;
        ChooseLevel((char *)PakEnt[pakwalk].filename,Offset+PakEnt[pakwalk].offset,PakEnt[pakwalk].size);
        NPcnt++;
    }
    free(PakEnt);
    //fseek(OutFile,0,SEEK_END);
    fflush(OutFile);
    Pak.diroffset = (int32_t) ftell(OutFile);
    //printf("PAK diroffset = %li, entries = %i\n",Pak.diroffset,NPcnt);
    Pak.dirsize = 64*NPcnt;
    ugh = (int32_t) ftell(OutFile);
    test = (int32_t) fwrite (&NewPakEnt[0],sizeof(pakentry_t),NPcnt,OutFile);
    if (test < NPcnt)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }

    fflush(OutFile);
    //chsize(fileno(OutFile),ftell(OutFile));
    fseek(OutFile,0,SEEK_SET);
    test = (int32_t) fwrite (&Pak, sizeof(pakheader_t),1,OutFile);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    fseek(OutFile,ugh,SEEK_SET);
    return (int32_t) numentry;
}

int32_t OthrFix(uint32_t Offset, uint32_t Length)
{
    int32_t test;
//    int32_t tmperr=
    fseek(InFile,Offset,SEEK_SET);
    NewPakEnt[NPcnt].offset = (int32_t) ftell(OutFile);
    NewPakEnt[NPcnt].size = Length;
    void *cpy;
    cpy = malloc(Length);
    fread (cpy, 1, Length, InFile);
    test = (int32_t) fwrite (cpy, 1, Length, OutFile);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }

    free(cpy);
    return 1;
}

int32_t BSPFix(uint32_t InitOFFS)
{
    int32_t test;
    fflush(OutFile);
    NewPakEnt[NPcnt].offset = (int32_t) ftell(OutFile);
    if (NewPakEnt[NPcnt].size==0) NewPakEnt[NPcnt].size=filesize(File);

    //printf("Start: %i\n",NewPakEnt[NPcnt].offset);

    long here;
//    int32_t tmperr=
    fseek(InFile,InitOFFS,SEEK_SET);
    dheader_t bspheader;
    int32_t tmp=(int32_t) fread(&bspheader, sizeof(dheader_t),1,InFile);
    if (tmp==0) return 0;
    printf("Version of bsp file is: %d\n",bspheader.version);
    printf("Vis info is at %d and is %d long.\n",bspheader.visilist.offset,bspheader.visilist.size);
    char *cpy;
    test = (int32_t) fwrite(&bspheader,sizeof(dheader_t),1,OutFile);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }


    char VisName[38];
    strcpy(VisName,CurName);
    strrev(VisName);
    strcat(VisName,"/");
    VisName[strcspn (VisName, "\\/")] = 0;
    strrev(VisName);
    int32_t good=0;
    here = (uint32_t) ftell(OutFile);
    bspheader.visilist.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    //printf("%s %s %i\n",VisName,CurName,good);
    for(tmp = 0;tmp<numvis;tmp++)
    {
        ////("%s  ",
        if(!strcmpi(visdat[tmp].File,VisName))
        {
            good = 1;
            //printf("Name: %s Size: %li %i\n",VisName,visdat[tmp].vislen,tmp);
            fseek(OutFile,here,SEEK_SET);
            bspheader.visilist.size = visdat[tmp].vislen;
            test = (int32_t) fwrite((void *) visdat[tmp].visdata,1,bspheader.visilist.size,OutFile);
            if (test == 0)
            {
                printf("Not enough disk space!!!  Failing.");
                fcloseall();
                remove(TempFile);
                freevis();
            }

            fflush(OutFile);
            bspheader.leaves.size   = visdat[tmp].leaflen;
            bspheader.leaves.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
            test = (int32_t) fwrite((void *) visdat[tmp].leafdata,1,bspheader.leaves.size,OutFile);
            if (test == 0)
            {
                printf("Not enough disk space!!!  Failing.");
                fcloseall();
                remove(TempFile);
                freevis();
            }
        }
    }
    if(good == 0)
    {
        if(usepak == 1)
        {
            fseek(InFile,InitOFFS, SEEK_SET);
            fseek(OutFile,NewPakEnt[NPcnt].offset,SEEK_SET);
            if(mode == 0)
            {
                char *cpy;
                cpy = (char *) malloc(NewPakEnt[NPcnt].size);
                fread((void *) cpy, NewPakEnt[NPcnt].size,1,InFile);
                test = (int32_t) fwrite((void *) cpy,NewPakEnt[NPcnt].size,1, OutFile);
                if (test == 0)
                {
                    printf("Not enough disk space!!!  Failing.");
                    fcloseall();
                    remove(TempFile);
                    freevis();
                }
                free(cpy);
                return 1;
            }
            else
            {
                NPcnt--;
                return 0;
            }
        }
        else
            return 0;//Individual file and it doesn't matter.
        //("not good\n");
        /*cpy = malloc(bspheader.visilist.size);
        fseek(InFile,InitOFFS+bspheader.visilist.offset, SEEK_SET);
        fread(cpy, 1,bspheader.visilist.size,InFile);
        fwrite(cpy,bspheader.visilist.size,1,OutFile);
        free(cpy);

        cpy = malloc(bspheader.leaves.size);
        fseek(InFile,InitOFFS+bspheader.leaves.offset, SEEK_SET);
        fread(cpy, 1,bspheader.leaves.size,InFile);
        bspheader.leaves.offset = ftell(OutFile)-NewPakEnt[NPcnt].offset;
        fwrite(cpy,bspheader.leaves.size,1,OutFile);
        free(cpy);*/
        //("K: %i\n",ftell(OutFile));

    }

    cpy = (char *) malloc(bspheader.entities.size);
    fseek(InFile,InitOFFS+bspheader.entities.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.entities.size,InFile);
    bspheader.entities.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    test = (int32_t) fwrite((void *) cpy,bspheader.entities.size,1,OutFile);
    if (test == 0) 
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    free(cpy);
    //printf("A: %i %i\n",bspheader.entities.offset,ftell(OutFile));

    cpy = (char *) malloc(bspheader.planes.size);
    fseek(InFile,InitOFFS+bspheader.planes.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.planes.size,InFile);
    bspheader.planes.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    test = (int32_t) fwrite((void *) cpy,bspheader.planes.size,1,OutFile);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    free(cpy);
    //printf("B: %i\n",ftell(OutFile));

    cpy = (char *) malloc(bspheader.miptex.size);
    fseek(InFile,InitOFFS+bspheader.miptex.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.miptex.size,InFile);
    bspheader.miptex.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    test = (int32_t) fwrite((void *) cpy,bspheader.miptex.size,1,OutFile);
    free(cpy);
    if (test == 0) 
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }

    //("C: %i\n",ftell(OutFile));

    cpy = (char *) malloc(bspheader.vertices.size);
    fseek(InFile,InitOFFS+bspheader.vertices.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.vertices.size,InFile);
    bspheader.vertices.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    test = (int32_t) fwrite((void *) cpy,bspheader.vertices.size,1,OutFile);
    free(cpy);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }

    cpy = (char *) malloc(bspheader.nodes.size);
    fseek(InFile,InitOFFS+bspheader.nodes.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.nodes.size,InFile);
    bspheader.nodes.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    test = (int32_t) fwrite((void *) cpy,bspheader.nodes.size,1,OutFile);
    free(cpy);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }

    cpy = (char *) malloc(bspheader.texinfo.size);
    fseek(InFile,InitOFFS+bspheader.texinfo.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.texinfo.size,InFile);
    bspheader.texinfo.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    test = (int32_t) fwrite((void *) cpy,bspheader.texinfo.size,1,OutFile);
    free(cpy);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    //("G: %i\n",ftell(OutFile));

    cpy = (char *) malloc(bspheader.faces.size);
    fseek(InFile,InitOFFS+bspheader.faces.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.faces.size,InFile);
    bspheader.faces.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    test = (int32_t) fwrite((void *) cpy,bspheader.faces.size,1,OutFile);
    free(cpy);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    //("H: %i\n",ftell(OutFile));

    cpy = (char *) malloc(bspheader.lightmaps.size);
    fseek(InFile,InitOFFS+bspheader.lightmaps.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.lightmaps.size,InFile);
    bspheader.lightmaps.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    test = (int32_t) fwrite((void *) cpy,bspheader.lightmaps.size,1,OutFile);
    free(cpy);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    //("I: %i\n",ftell(OutFile));

    cpy = (char *) malloc(bspheader.clipnodes.size);
    fseek(InFile,InitOFFS+bspheader.clipnodes.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.clipnodes.size,InFile);
    bspheader.clipnodes.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    test = (int32_t) fwrite((void *) cpy,bspheader.clipnodes.size,1,OutFile);
    free(cpy);
    //("J: %i\n",ftell(OutFile));


    cpy = (char *) malloc(bspheader.lface.size);
    fseek(InFile,InitOFFS+bspheader.lface.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.lface.size,InFile);	
    bspheader.lface.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    test = (int32_t) fwrite((void *) cpy,bspheader.lface.size,1,OutFile);
    free(cpy);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    //("L: %i\n",ftell(OutFile));

    cpy = (char *) malloc(bspheader.edges.size);
    fseek(InFile,InitOFFS+bspheader.edges.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.edges.size,InFile);
    bspheader.edges.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    test = (int32_t) fwrite((void *) cpy,bspheader.edges.size,1,OutFile);
    free(cpy);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    //("M: %i\n",ftell(OutFile));

    cpy = (char *) malloc(bspheader.ledges.size);
    fseek(InFile,InitOFFS+bspheader.ledges.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.ledges.size,InFile);
    bspheader.ledges.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    test = (int32_t) fwrite((void *) cpy,bspheader.ledges.size,1,OutFile);
    free(cpy);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    //("N: %i\n",ftell(OutFile));

    cpy = (char *) malloc(bspheader.models.size);
    fseek(InFile,InitOFFS+bspheader.models.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.models.size,InFile);
    bspheader.models.offset = ((int32_t) ftell(OutFile))-NewPakEnt[NPcnt].offset;
    test = (int32_t) fwrite((void *) cpy,bspheader.models.size,1,OutFile);
    free(cpy);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }

    here=ftell(OutFile);
    //("O: %i\n",here);
    fflush(OutFile);

    fseek(OutFile,NewPakEnt[NPcnt].offset, SEEK_SET);
    test = (int32_t) fwrite(&bspheader,sizeof(dheader_t),1,OutFile);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    fseek(OutFile,here, SEEK_SET);
    NewPakEnt[NPcnt].size = ((int32_t) ftell(OutFile)) - NewPakEnt[NPcnt].offset;

    //("End: %i\n",ftell(OutFile));

return 1;
}

int32_t ChooseFile(char *FileSpec,uint32_t Offset,int32_t length){
    int32_t tmp=0;
    
    if (length == 0 && strstr(strlwr(FileSpec),".pak"))
    {
        printf("Looking at file %s.\n",FileSpec);
        tmp=PakNew(Offset);
    }
    if (strstr(strlwr(FileSpec),".bsp"))
    {
        printf("Looking at file %s.\n",FileSpec);
        strcpy(CurName, FileSpec);
        tmp = BSPNew(Offset);
    }
    return tmp;
}

int32_t PakNew(uint32_t Offset)
{
    pakheader_t Pak;
    fseek(InFile,Offset,SEEK_SET);
    fread(&Pak,sizeof(pakheader_t),1,InFile);
    uint32_t numentry=Pak.dirsize/64;
    pakentry_t *PakEnt= (pakentry_t *) malloc(numentry*sizeof(pakentry_t));
    fseek(InFile,Offset+Pak.diroffset,SEEK_SET);
    fread(PakEnt,sizeof(pakentry_t),numentry,InFile);
    for (uint32_t pakwalk=0;pakwalk<numentry;pakwalk++)
    {
        strcpy((char *) NewPakEnt[NPcnt].filename,(const char *) PakEnt[pakwalk].filename);
        strcpy(CurName,(const char *) PakEnt[pakwalk].filename);
        NewPakEnt[NPcnt].size = PakEnt[pakwalk].size;
        ChooseFile((char *)PakEnt[pakwalk].filename,Offset+PakEnt[pakwalk].offset,PakEnt[pakwalk].size);
        NPcnt++;
    }
    free(PakEnt);
    return (int32_t) numentry;
}

int32_t BSPNew(uint32_t InitOFFS)
{
    int32_t test;
    uint32_t tes;
//    int32_t tmperr=
    fseek(InFile,InitOFFS,SEEK_SET);
#if !defined (__APPLE__) && !defined (MACOSX)
    uint32_t len;
#endif /* !__APPLE__ && !MACOSX */
    dheader_t bspheader;
    int32_t tmp=(int32_t) fread(&bspheader, sizeof(dheader_t),1,InFile);
    if (tmp==0)
    {
        return 0;
    }
    
    printf("Version of bsp file is:  %d\n",bspheader.version);
    printf("Vis info is at %d and is %d long\n",bspheader.visilist.offset,bspheader.visilist.size);
    printf("Leaf info is at %d and is %d long\n",bspheader.leaves.offset,bspheader.leaves.size);
    char *cpy;

    char VisName[38];
    strcpy(VisName,CurName);
    strrev(VisName);
    strcat(VisName,"/");
    VisName[strcspn (VisName, "/\\")] = 0;
    strrev(VisName);
    cpy = (char *) malloc(bspheader.visilist.size);
    fseek(InFile,InitOFFS+bspheader.visilist.offset, SEEK_SET);
    fread(cpy, 1,bspheader.visilist.size,InFile);
#if defined (__APPLE__) || defined (MACOSX)
    if (filesize(VIS) > -1)
#else
    len = filesize(VIS);
    //("%i\n",len);
    if(len > -1)
#endif /* __APPLE__ ||ÊMACOSX */
        fseek(fVIS,0,SEEK_END);
    test = (int32_t) fwrite(&VisName,1,32,fVIS);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    tes = bspheader.visilist.size+bspheader.leaves.size+8;
    Endian32_Swap (tes);
    test = (int32_t) fwrite(&tes,sizeof(int32_t),1,fVIS);
    Endian32_Swap (tes);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }

    Endian32_Swap (bspheader.visilist.size);
    test = (int32_t) fwrite(&bspheader.visilist.size,sizeof(int32_t),1,fVIS);
    Endian32_Swap (bspheader.visilist.size);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    test = (int32_t) fwrite((void *) cpy,bspheader.visilist.size,1,fVIS);
    free(cpy);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }

    cpy = (char *) malloc(bspheader.leaves.size);
    fseek(InFile,InitOFFS+bspheader.leaves.offset, SEEK_SET);
    fread((void *) cpy, 1,bspheader.leaves.size,InFile);
    Endian32_Swap (bspheader.leaves.size);
    test = (int32_t) fwrite(&bspheader.leaves.size,sizeof(int32_t),1,fVIS);
    Endian32_Swap (bspheader.leaves.size);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }
    test = (int32_t) fwrite((void *) cpy,bspheader.leaves.size,1,fVIS);
    free(cpy);
    if (test == 0)
    {
        printf("Not enough disk space!!!  Failing.");
        fcloseall();
        remove(TempFile);
        freevis();
    }

    return 1;
}

void loadvis(FILE *fp)
{
    uint32_t cnt=0;
    uint32_t tmp;
    char Name[32];
    uint32_t go;
    fseek(fp,0,SEEK_END);
    size_t len = ftell(fp);
    fseek(fp,0,SEEK_SET);
    while(ftell(fp) < len)
    {
        cnt++;
        fread(Name,1,32,fp);
        fread(&go,1,sizeof(int32_t),fp);
        Endian32_Swap (go);
        fseek(fp,go,SEEK_CUR);
    }
    visdat = (visdat_t *) malloc(sizeof(visdat_t)*cnt);
    if(visdat == 0)
    {
        printf("Ack, not enough memory!");
        exit(1);
    }
    
    fseek(fp,0,SEEK_SET);
    
    for(tmp=0;tmp<cnt;tmp++)
    {
        fread((void *) visdat[tmp].File,1,32,fp);
        fread((void *) &visdat[tmp].len,1,sizeof(int32_t),fp);
        Endian32_Swap (visdat[tmp].len);
        fread((void *) &visdat[tmp].vislen,1,sizeof(int32_t),fp);
        Endian32_Swap (visdat[tmp].vislen);
        visdat[tmp].visdata = (unsigned char *) malloc(visdat[tmp].vislen);
        if(visdat[tmp].visdata == 0)
        {
            printf("Ack, not enough memory!");
            exit(2);
        }
        fread((void *) visdat[tmp].visdata,1,visdat[tmp].vislen,fp);
        fread((void *) &visdat[tmp].leaflen,1,sizeof(int32_t),fp);
        Endian32_Swap (visdat[tmp].leaflen);
        visdat[tmp].leafdata = (unsigned char *) malloc(visdat[tmp].leaflen);
        if(visdat[tmp].leafdata == 0)
        {
            printf("Ack, not enough memory!");
            exit(2);
        }
        fread((void *)visdat[tmp].leafdata,1,visdat[tmp].leaflen,fp);
    }
    numvis = cnt;
}

void freevis()
{
    for(int32_t tmp=0;tmp<numvis;tmp++)
    {
        free(visdat[tmp].visdata);
        free(visdat[tmp].leafdata);
    }
    
    free(visdat);
}
