/**
 * @file processtools.c
 * @brief Tools to manage processes
 *
 *
 * Manages structure PROCESSINFO.
 *
 * @see @ref page_ProcessInfoStructure
 *
 *
 */

#ifndef __STDC_LIB_EXT1__
typedef int errno_t;
#endif

static int CTRLscreenExitLine = 0; // for debugging

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif


#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h> // mmap()

#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>

#include <sys/types.h>
#include <unistd.h> // getpid()

#include <sys/stat.h>

#include <ctype.h>
#include <fcntl.h>
#include <ncurses.h>

#include <dirent.h>

#include <locale.h>
#include <wchar.h>

#include <pthread.h>

#include "CommandLineInterface/timeutils.h"

#include "CLIcore.h"
#include "COREMOD_tools/COREMOD_tools.h"
#include "TUItools.h"

#define SHAREDPROCDIR data.shmdir

#include <processtools.h>

#include "processinfo_setup.h"
#include "processinfo_procdirname.h"
#include "processinfo_SIGexit.h"
#include "processinfo_shm_create.h"
#include "processinfo_shm_list_create.h"
#include "processinfo_exec_start.h"
#include "processinfo_exec_end.h"


#include "procCTRL_PIDcollectSystemInfo.h"
#include "procCTRL_GetCPUloads.h"
#include "procCTRL_GetNumberCPUs.h"
#include "procCTRL_processinfo_scan.h"

#include "procCTRL_TUI.h"

#ifdef USE_HWLOC
#include <hwloc.h>
#endif



// shared memory access permission
#define FILEMODE 0666

// What do we want to compute/print ?
#define CMDPROC_CONTEXTSWITCH 1
#define CMDPROC_CPUUSE        1
#define CMDPROC_MEMUSE        1

#define CMDPROC_PROCSTAT 1




static short unsigned int wrow, wcol;

PROCESSINFOLIST *pinfolist;

#define NBtopMax 5000

/*
static int   toparray_PID[NBtopMax];
static char  toparray_USER[NBtopMax][32];
static char  toparray_PR[NBtopMax][8];
static int   toparray_NI[NBtopMax];
static char  toparray_VIRT[NBtopMax][32];
static char  toparray_RES[NBtopMax][32];
static char  toparray_SHR[NBtopMax][32];
static char  toparray_S[NBtopMax][8];
static float toparray_CPU[NBtopMax];
static float toparray_MEM[NBtopMax];
static char  toparray_TIME[NBtopMax][32];
static char  toparray_COMMAND[NBtopMax][32];

static int NBtopP; // number of processes scanned by top
*/

// timing info collected to optimize this program
//static struct timespec t1;
//static struct timespec t2;
static struct timespec tdiff;

// timing categories
static double scantime_cpuset;
static double scantime_status;
static double scantime_stat;
static double scantime_pstree;
static double scantime_top;
static double scantime_CPUload;
static double scantime_CPUpcnt;




// returns loop status
// 0 if loop should exit, 1 otherwise

int processinfo_loopstep(PROCESSINFO *processinfo)
{
    int loopstatus = 1;

    while (processinfo->CTRLval == 1) // pause
    {
        usleep(50);
    }
    if (processinfo->CTRLval == 2) // single iteration
    {
        processinfo->CTRLval = 1;
    }
    if (processinfo->CTRLval == 3) // exit loop
    {
        loopstatus = 0;
    }

    if (data.signal_INT == 1) // CTRL-C
    {
        loopstatus = 0;
    }

    if (data.signal_HUP == 1) // terminal has disappeared
    {
        loopstatus = 0;
    }

    if (processinfo->loopcntMax != -1)
        if (processinfo->loopcnt >= processinfo->loopcntMax - 1)
        {
            loopstatus = 0;
        }

    return loopstatus;
}

int processinfo_compute_status(PROCESSINFO *processinfo)
{
    int compstatus = 1;

    // CTRLval = 5 will disable computations in loop (usually for testing)
    if (processinfo->CTRLval == 5)
    {
        compstatus = 0;
    }

    return compstatus;
}




PROCESSINFO *processinfo_shm_link(const char *pname, int *fd)
{
    struct stat file_stat;

    *fd = open(pname, O_RDWR);
    if (*fd == -1)
    {
        perror("Error opening file");
        exit(0);
    }
    fstat(*fd, &file_stat);
    //printf("[%d] File %s size: %zd\n", __LINE__, pname, file_stat.st_size);

    PROCESSINFO *pinfolist = (PROCESSINFO *)
        mmap(0, file_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
    if (pinfolist == MAP_FAILED)
    {
        close(*fd);
        fprintf(stderr, "Error mmapping the file");
        exit(0);
    }

    return pinfolist;
}



int processinfo_shm_close(PROCESSINFO *pinfo, int fd)
{
    struct stat file_stat;
    fstat(fd, &file_stat);
    munmap(pinfo, file_stat.st_size);
    close(fd);
    return EXIT_SUCCESS;
}

int processinfo_cleanExit(PROCESSINFO *processinfo)
{

    if (processinfo->loopstat != 4)
    {
        struct timespec tstop;
        struct tm      *tstoptm;
        char            msgstring[STRINGMAXLEN_PROCESSINFO_STATUSMSG];

        clock_gettime(CLOCK_REALTIME, &tstop);
        tstoptm = gmtime(&tstop.tv_sec);

        if (processinfo->CTRLval == 3) // loop exit from processinfo control
        {
            sprintf(msgstring,
                    "CTRLexit %02d:%02d:%02d.%03d",
                    tstoptm->tm_hour,
                    tstoptm->tm_min,
                    tstoptm->tm_sec,
                    (int) (0.000001 * (tstop.tv_nsec)));
            strncpy(processinfo->statusmsg,
                    msgstring,
                    STRINGMAXLEN_PROCESSINFO_STATUSMSG - 1);
        }

        if (processinfo->loopstat == 1)
        {
            sprintf(msgstring,
                    "Loop exit %02d:%02d:%02d.%03d",
                    tstoptm->tm_hour,
                    tstoptm->tm_min,
                    tstoptm->tm_sec,
                    (int) (0.000001 * (tstop.tv_nsec)));
            strncpy(processinfo->statusmsg,
                    msgstring,
                    STRINGMAXLEN_PROCESSINFO_STATUSMSG - 1);
        }

        processinfo->loopstat = 3; // clean exit
    }

    // Remove processinfo shm file on clean exit
    char procdname[STRINGMAXLEN_FULLFILENAME];
    processinfo_procdirname(procdname);

    char SM_fname[STRINGMAXLEN_FULLFILENAME];
    WRITE_FULLFILENAME(SM_fname,
                       "%s/proc.%s.%06d.shm",
                       procdname,
                       processinfo->name,
                       processinfo->PID);
    remove(SM_fname);

    return 0;
}



int processinfo_WriteMessage(PROCESSINFO *processinfo, const char *msgstring)
{
    struct timespec tnow;
    struct tm      *tmnow;

    DEBUG_TRACEPOINT(" ");

    clock_gettime(CLOCK_REALTIME, &tnow);
    tmnow = gmtime(&tnow.tv_sec);

    strcpy(processinfo->statusmsg, msgstring);

    DEBUG_TRACEPOINT(" ");

    fprintf(processinfo->logFile,
            "%02d:%02d:%02d.%06d  %8ld.%09ld  %06d  %s\n",
            tmnow->tm_hour,
            tmnow->tm_min,
            tmnow->tm_sec,
            (int) (0.001 * (tnow.tv_nsec)),
            tnow.tv_sec,
            tnow.tv_nsec,
            (int) processinfo->PID,
            msgstring);

    DEBUG_TRACEPOINT(" ");
    fflush(processinfo->logFile);
    return 0;
}

int processinfo_CatchSignals()
{
    if (sigaction(SIGTERM, &data.sigact, NULL) == -1)
    {
        printf("\ncan't catch SIGTERM\n");
    }

    if (sigaction(SIGINT, &data.sigact, NULL) == -1)
    {
        printf("\ncan't catch SIGINT\n");
    }

    if (sigaction(SIGABRT, &data.sigact, NULL) == -1)
    {
        printf("\ncan't catch SIGABRT\n");
    }

    if (sigaction(SIGBUS, &data.sigact, NULL) == -1)
    {
        printf("\ncan't catch SIGBUS\n");
    }

    if (sigaction(SIGSEGV, &data.sigact, NULL) == -1)
    {
        printf("\ncan't catch SIGSEGV\n");
    }

    if (sigaction(SIGHUP, &data.sigact, NULL) == -1)
    {
        printf("\ncan't catch SIGHUP\n");
    }

    if (sigaction(SIGPIPE, &data.sigact, NULL) == -1)
    {
        printf("\ncan't catch SIGPIPE\n");
    }

    return 0;
}

int processinfo_ProcessSignals(PROCESSINFO *processinfo)
{
    int loopOK = 1;
    // process signals

    if (data.signal_TERM == 1)
    {
        loopOK = 0;
        processinfo_SIGexit(processinfo, SIGTERM);
    }

    if (data.signal_INT == 1)
    {
        loopOK = 0;
        processinfo_SIGexit(processinfo, SIGINT);
    }

    if (data.signal_ABRT == 1)
    {
        loopOK = 0;
        processinfo_SIGexit(processinfo, SIGABRT);
    }

    if (data.signal_BUS == 1)
    {
        loopOK = 0;
        processinfo_SIGexit(processinfo, SIGBUS);
    }

    if (data.signal_SEGV == 1)
    {
        loopOK = 0;
        processinfo_SIGexit(processinfo, SIGSEGV);
    }

    if (data.signal_HUP == 1)
    {
        loopOK = 0;
        processinfo_SIGexit(processinfo, SIGHUP);
    }

    if (data.signal_PIPE == 1)
    {
        loopOK = 0;
        processinfo_SIGexit(processinfo, SIGPIPE);
    }

    return loopOK;
}

/** @brief Update ouput stream at completion of processinfo-enabled loop iteration
 *
 */
errno_t processinfo_update_output_stream(PROCESSINFO *processinfo,
                                         imageID      outstreamID)
{
    if (data.image[outstreamID].md[0].shared == 1)
    {
        imageID IDin;

        DEBUG_TRACEPOINT(" ");

        if (processinfo != NULL)
        {
            IDin = processinfo->triggerstreamID;
            DEBUG_TRACEPOINT("trigger IDin = %ld", IDin);

            if (IDin > -1)
            {
                int sptisize = data.image[IDin].md[0].NBproctrace - 1;

                // copy streamproctrace from input to output
                memcpy(&data.image[outstreamID].streamproctrace[1],
                       &data.image[IDin].streamproctrace[0],
                       sizeof(STREAM_PROC_TRACE) * sptisize);
            }

            DEBUG_TRACEPOINT("timing");
            struct timespec ts;
            if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
            {
                perror("clock_gettime");
                exit(EXIT_FAILURE);
            }

            // write first streamproctrace entry
            DEBUG_TRACEPOINT("trigger info");
            data.image[outstreamID].streamproctrace[0].triggermode =
                processinfo->triggermode;

            data.image[outstreamID].streamproctrace[0].procwrite_PID = getpid();

            data.image[outstreamID].streamproctrace[0].trigger_inode =
                processinfo->triggerstreaminode;

            data.image[outstreamID].streamproctrace[0].ts_procstart =
                processinfo->texecstart[processinfo->timerindex];

            data.image[outstreamID].streamproctrace[0].ts_streamupdate = ts;

            data.image[outstreamID].streamproctrace[0].trigsemindex =
                processinfo->triggersem;

            data.image[outstreamID].streamproctrace[0].triggerstatus =
                processinfo->triggerstatus;

            if (IDin > -1)
            {
                data.image[outstreamID].streamproctrace[0].cnt0 =
                    data.image[IDin].md[0].cnt0;
            }
        }
        DEBUG_TRACEPOINT(" ");
    }

    ImageStreamIO_UpdateIm(&data.image[outstreamID]);

    return RETURN_SUCCESS;
}




// unused
/*


static long getTopOutput()
{
	long NBtop = 0;

    char outstring[200];
    char command[200];
    FILE * fpout;
	int ret;

	clock_gettime(CLOCK_REALTIME, &t1);

    sprintf(command, "top -H -b -n 1");
    fpout = popen (command, "r");
    if(fpout==NULL)
    {
        printf("WARNING: cannot run command \"%s\"\n", command);
    }
    else
    {
		int startScan = 0;
		ret = 12;
        while( (fgets(outstring, 100, fpout) != NULL) && (NBtop<NBtopMax) && (ret==12) )
           {
			   if(startScan == 1)
			   {
				   ret = sscanf(outstring, "%d %s %s %d %s %s %s %s %f %f %s %s\n",
						&toparray_PID[NBtop],
						toparray_USER[NBtop],
						toparray_PR[NBtop],
						&toparray_NI[NBtop],
						 toparray_VIRT[NBtop],
						 toparray_RES[NBtop],
						 toparray_SHR[NBtop],
						 toparray_S[NBtop],
						&toparray_CPU[NBtop],
						&toparray_MEM[NBtop],
						 toparray_TIME[NBtop],
						 toparray_COMMAND[NBtop]
						);
				   NBtop++;
			   }

				if(strstr(outstring, "USER")!=NULL)
					startScan = 1;
		   }
        pclose(fpout);
    }
    clock_gettime(CLOCK_REALTIME, &t2);
	tdiff = timespec_diff(t1, t2);
	scantime_top += 1.0*tdiff.tv_sec + 1.0e-9*tdiff.tv_nsec;

	return NBtop;
}



*/




/**
 * ## Purpose
 *
 * Creates list of CPU sets
 *
 * ## Description
 *
 * Uses command: cset set -l
 *
 *
 */

int processinfo_CPUsets_List(STRINGLISTENTRY *CPUsetList)
{
    char  line[200];
    FILE *fp;
    int   NBsetMax = 1000;
    int   setindex;
    char  word[200];
    char  word1[200];
    int   NBset = 0;

    char fname[STRINGMAXLEN_FILENAME];
    WRITE_FILENAME(fname, "%s/.csetlist.%ld", SHAREDPROCDIR, (long) getpid());

    EXECUTE_SYSTEM_COMMAND(
        "cset set -l | awk '/root/{stop=1} stop==1{print $0}' > %s",
        fname);

    // first scan: get number of entries
    fp = fopen(fname, "r");
    while (NBset < NBsetMax)
    {
        if (fgets(line, 199, fp) == NULL)
        {
            break;
        }
        NBset++;
        //		printf("%3d: %s", NBset, line);
    }
    fclose(fp);

    setindex = 0;
    fp       = fopen(fname, "r");
    while (1)
    {
        if (fgets(line, 199, fp) == NULL)
        {
            break;
        }
        sscanf(line, "%s %s", word, word1);
        strcpy(CPUsetList[setindex].name, word);
        strcpy(CPUsetList[setindex].description, word1);
        setindex++;
    }
    fclose(fp);

    remove(fname);

    return NBset;
}

int processinfo_SelectFromList(STRINGLISTENTRY *StringList, int NBelem)
{
    int   selected = 0;
    long  i;
    char  buff[100];
    int   inputOK;
    char *p;
    int   strlenmax = 20;

    printf("%d entries in list:\n", NBelem);
    fflush(stdout);
    for (i = 0; i < NBelem; i++)
    {
        printf("   %3ld   : %16s   %s\n",
               i,
               StringList[i].name,
               StringList[i].description);
        fflush(stdout);
    }

    inputOK = 0;

    while (inputOK == 0)
    {
        printf("\nEnter a number: ");
        fflush(stdout);

        int  stringindex = 0;
        char c;
        while (((c = getchar()) != 13) && (stringindex < strlenmax - 1))
        {
            buff[stringindex] = c;
            if (c == 127) // delete key
            {
                putchar(0x8);
                putchar(' ');
                putchar(0x8);
                stringindex--;
            }
            else
            {
                putchar(c); // echo on screen
                stringindex++;
            }
            if (stringindex < 0)
            {
                stringindex = 0;
            }
        }
        buff[stringindex] = '\0';

        selected = strtol(buff, &p, strlenmax);

        if ((selected < 0) || (selected > NBelem - 1))
        {
            printf("\nError: number not valid. Must be >= 0 and < %d\n",
                   NBelem);
            inputOK = 0;
        }
        else
        {
            inputOK = 1;
        }
    }

    printf("Selected entry : %s\n", StringList[selected].name);

    return selected;
}


void processinfo_CTRLscreen_atexit()
{
    //echo();
    //endwin();

    printf("EXIT from processinfo_CTRLscreen at line %d\n", CTRLscreenExitLine);
}


/**
 * ## Purpose
 *
 * Control screen for PROCESSINFO structures
 *
 * ## Description
 *
 * Relies on ncurses for display\n
 *
 *
 */
errno_t processinfo_CTRLscreen()
{
    long pindex, index;

    PROCINFOPROC
    procinfoproc; // Main structure - holds everything that needs to be shared with other functions and scan thread
    pthread_t threadscan;

    int cpusocket;

    char pselected_FILE[200];
    char pselected_FUNCTION[200];
    int  pselected_LINE;

    // timers
    struct timespec t1loop;
    struct timespec t2loop;
    //struct timespec tdiffloop;

    struct timespec t01loop;
    struct timespec t02loop;
    struct timespec t03loop;
    struct timespec t04loop;
    struct timespec t05loop;
    struct timespec t06loop;
    struct timespec t07loop;

    float frequ = 32.0; // Hz
    char  monstring[200];

    // list of active indices
    int pindexActiveSelected;
    int pindexSelected;

    int listindex;

    int ToggleValue;

    DEBUG_TRACEPOINT(" ");

    char procdname[200];
    processinfo_procdirname(procdname);

    processinfo_CatchSignals();

    /*
        struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = processinfo_CTRLscreen_handle_winch;
        if(sigaction(SIGWINCH, &sa, NULL) == -1)
        {
            printf("can't handle SIGWINCH");
            exit(EXIT_FAILURE);
        }


        setlocale(LC_ALL, "");
    */

    // initialize procinfoproc entries
    procinfoproc.loopcnt = 0;
    for (pindex = 0; pindex < PROCESSINFOLISTSIZE; pindex++)
    {
        procinfoproc.pinfoarray[pindex]   = NULL;
        procinfoproc.pinfommapped[pindex] = 0; // 1 if mmapped, 0 otherwise
        procinfoproc.PIDarray[pindex]     = 0; // used to track changes
        procinfoproc.updatearray[pindex]  = 1; // initialize: load all
        procinfoproc.fdarray[pindex]      = 0; // file descriptors
        procinfoproc.loopcntarray[pindex] = 0;
        procinfoproc.loopcntoffsetarray[pindex] = 0;
        procinfoproc.selectedarray[pindex]      = 0; // initially not selected
        procinfoproc.sorted_pindex_time[pindex] = pindex;

        procinfoproc.pindexActive[pindex]   = 0;
        procinfoproc.psysinfostatus[pindex] = 0;
    }
    procinfoproc.NBcpus      = 1;
    procinfoproc.NBcpusocket = 1;
    for (int cpu = 0; cpu < MAXNBCPU; cpu++)
    {

        procinfoproc.CPUload[cpu] = 0.0;

        procinfoproc.CPUcnt0[cpu] = 0;
        procinfoproc.CPUcnt1[cpu] = 0;
        procinfoproc.CPUcnt2[cpu] = 0;
        procinfoproc.CPUcnt3[cpu] = 0;
        procinfoproc.CPUcnt4[cpu] = 0;
        procinfoproc.CPUcnt5[cpu] = 0;
        procinfoproc.CPUcnt6[cpu] = 0;
        procinfoproc.CPUcnt7[cpu] = 0;
        procinfoproc.CPUcnt8[cpu] = 0;

        procinfoproc.CPUids[cpu]  = cpu;
        procinfoproc.CPUphys[cpu] = 0;
        procinfoproc.CPUpcnt[cpu] = 0;
    }



    STRINGLISTENTRY *CPUsetList;
    int              NBCPUset;
    CPUsetList = (STRINGLISTENTRY *) malloc(sizeof(STRINGLISTENTRY) * 1000);
    if (CPUsetList == NULL)
    {
        PRINT_ERROR("malloc returns NULL pointer");
        abort();
    }
    NBCPUset = processinfo_CPUsets_List(CPUsetList);
    DEBUG_TRACEPOINT("%d CPU set", NBCPUset);

    // Create / read process list
    //
    if (processinfo_shm_list_create() == 0)
    {
        printf("==== NO PROCESS TO DISPLAY ====\n");
        //return(0);
    }

    // copy pointer
    procinfoproc.pinfolist = pinfolist;



    procinfoproc.NBcpus = GetNumberCPUs(&procinfoproc);
    GetCPUloads(&procinfoproc);

    EXECUTE_SYSTEM_COMMAND("touch line.%s.%d.txt",
                           __func__,
                           __LINE__); //DEBUGTEST

    // default: use ncurses
    TUI_set_screenprintmode(SCREENPRINT_NCURSES);

    if (getenv("MILK_TUIPRINT_STDIO"))
    {
        // use stdio instead of ncurses
        TUI_set_screenprintmode(SCREENPRINT_STDIO);
    }

    if (getenv("MILK_TUIPRINT_NONE"))
    {
        TUI_set_screenprintmode(SCREENPRINT_NONE);
    }

    TUI_init_terminal(&wrow, &wcol);
    // INITIALIZE ncurses
    //initncurses();

    //atexit( processinfo_CTRLscreen_atexit );

    // set print string lengths
    // string to be printed. Used to keep track of total length
    char string[200];

    int pstrlen_status = 10;
    int pstrlen_pid    = 7;
    int pstrlen_pname  = 25;
    int pstrlen_state  = 5;
    // Clevel :  2
    // tstart : 12
    int pstrlen_tmux    = 16;
    int pstrlen_loopcnt = 10;
    int pstrlen_descr   = 25;

    int pstrlen_msg = 35;

    int pstrlen_cset = 10;

    int pstrlen_inode          = 10;
    int pstrlen_trigstreamname = 16;
    int pstrlen_missedfr       = 4;
    int pstrlen_missedfrc      = 12;
    int pstrlen_tocnt          = 10;

    clear();

    // redirect stderr to /dev/null

    int backstderr, newstderr;

    fflush(stderr);
    backstderr = dup(STDERR_FILENO);
    newstderr  = open("/dev/null", O_WRONLY);
    dup2(newstderr, STDERR_FILENO);
    close(newstderr);

    procinfoproc.NBpinfodisp = wrow - 5;
    procinfoproc.pinfodisp   = (PROCESSINFODISP *) malloc(
        sizeof(PROCESSINFODISP) * procinfoproc.NBpinfodisp);
    if (procinfoproc.pinfodisp == NULL)
    {
        PRINT_ERROR("malloc returns NULL pointer");
        abort();
    }

    for (pindex = 0; pindex < procinfoproc.NBpinfodisp; pindex++)
    {
        // by default, each process is assumed to be single-threaded
        procinfoproc.pinfodisp[pindex].NBsubprocesses = 1;

        procinfoproc.pinfodisp[pindex].active = 0;
        procinfoproc.pinfodisp[pindex].PID    = 0;
        strcpy(procinfoproc.pinfodisp[pindex].name, "null");
        procinfoproc.pinfodisp[pindex].updatecnt = 0;

        procinfoproc.pinfodisp[pindex].loopcnt  = 0;
        procinfoproc.pinfodisp[pindex].loopstat = 0;

        procinfoproc.pinfodisp[pindex].createtime_hr  = 0;
        procinfoproc.pinfodisp[pindex].createtime_min = 0;
        procinfoproc.pinfodisp[pindex].createtime_sec = 0;
        procinfoproc.pinfodisp[pindex].createtime_ns  = 0;

        strcpy(procinfoproc.pinfodisp[pindex].cpuset, "null");
        strcpy(procinfoproc.pinfodisp[pindex].cpusallowed, "null");
        for (int cpu = 0; cpu < MAXNBCPU; cpu++)
        {
            procinfoproc.pinfodisp[pindex].cpuOKarray[cpu] = 0;
        }
        procinfoproc.pinfodisp[pindex].threads = 0;

        procinfoproc.pinfodisp[pindex].rt_priority = 0;
        procinfoproc.pinfodisp[pindex].memload     = 0.0;

        strcpy(procinfoproc.pinfodisp[pindex].statusmsg, "");
        strcpy(procinfoproc.pinfodisp[pindex].tmuxname, "");

        procinfoproc.pinfodisp[pindex].NBsubprocesses = 1;
        for (int spi = 0; spi < MAXNBSUBPROCESS; spi++)
        {
            procinfoproc.pinfodisp[pindex].sampletimearray[spi]      = 0.0;
            procinfoproc.pinfodisp[pindex].sampletimearray_prev[spi] = 0.0;

            procinfoproc.pinfodisp[pindex].ctxtsw_voluntary[spi]         = 0;
            procinfoproc.pinfodisp[pindex].ctxtsw_nonvoluntary[spi]      = 0;
            procinfoproc.pinfodisp[pindex].ctxtsw_voluntary_prev[spi]    = 0;
            procinfoproc.pinfodisp[pindex].ctxtsw_nonvoluntary_prev[spi] = 0;

            procinfoproc.pinfodisp[pindex].cpuloadcntarray[spi]      = 0;
            procinfoproc.pinfodisp[pindex].cpuloadcntarray_prev[spi] = 0;
            procinfoproc.pinfodisp[pindex].subprocCPUloadarray[spi]  = 0.0;
            procinfoproc.pinfodisp[pindex]
                .subprocCPUloadarray_timeaveraged[spi] = 0.0;

            procinfoproc.pinfodisp[pindex].VmRSSarray[spi]     = 0;
            procinfoproc.pinfodisp[pindex].processorarray[spi] = 0;

            procinfoproc.pinfodisp[pindex].subprocPIDarray[spi] = 0;
        }
    }


    EXECUTE_SYSTEM_COMMAND("touch line.%s.%d.txt",
                           __func__,
                           __LINE__); //DEBUGTEST

    pindexActiveSelected = 0;
    procinfoproc.DisplayMode =
        PROCCTRL_DISPLAYMODE_CTRL; // default upon startup
    // display modes:
    // 2: overview
    // 3: CPU affinity

    // Start scan thread
    procinfoproc.loop    = 1;
    procinfoproc.twaitus = 1000000; // 1 sec

    procinfoproc.SCANBLOCK_requested = 0;
    procinfoproc.SCANBLOCK_OK        = 0;

    pthread_create(&threadscan, NULL, processinfo_scan, (void *) &procinfoproc);


    EXECUTE_SYSTEM_COMMAND("touch line.%s.%d.txt",
                           __func__,
                           __LINE__); //DEBUGTEST

    // wait for first scan to be completed
    procinfoproc.SCANBLOCK_OK = 1;
    while (procinfoproc.loopcnt < 1)
    {
        //printf("procinfoproc.loopcnt  = %ld\n", (long) procinfoproc.loopcnt);
        usleep(10000);
    }


    EXECUTE_SYSTEM_COMMAND("touch line.%s.%d.txt",
                           __func__,
                           __LINE__); //DEBUGTEST


    int  loopOK  = 1;
    int  freeze  = 0;
    long cnt     = 0;
    int  MonMode = 0;
    int  TimeSorted =
        1; // by default, sort processes by start time (most recent at top)
    int dispindexMax = 0;

    clear();
    int Xexit = 0; // toggles to 1 when users types x

    pindexSelected       = 0;
    int pindexSelectedOK = 0; // no process selected by cursor


    EXECUTE_SYSTEM_COMMAND("touch line.%s.%d.txt",
                           __func__,
                           __LINE__); //DEBUGTEST
    while (loopOK == 1)
    {
        int pid;
        //char command[200];

        DEBUG_TRACEPOINT(" ");

        EXECUTE_SYSTEM_COMMAND("touch line.%s.%d.txt",
                               __func__,
                               __LINE__); //DEBUGTEST

        if (procinfoproc.SCANBLOCK_requested == 1)
        {
            //system("echo \"scanblock request read 1\" > steplog.dQRr1.txt");//TEST

            procinfoproc.SCANBLOCK_OK = 1; // issue OK to scan thread
            //system("echo \"scanblock OK write 1\" > steplog.dOKw1.txt");//TEST

            // wait for scan thread to have completed scan
            while (procinfoproc.SCANBLOCK_OK == 1)
            {
                //system("echo \"scanblock OK read 1\" > steplog.dOKr1.txt");//TEST
                usleep(100);
            }
            //system("echo \"scanblock OK read 0\" > steplog.dOKr0.txt");//TEST
        }
        //		else
        //			system("echo \"scanblock request read 0\" > steplog.dQRr0.txt");//TEST

        usleep((long) (1000000.0 / frequ));
        int ch = getch();

        clock_gettime(CLOCK_REALTIME, &t1loop);

        scantime_cpuset  = 0.0;
        scantime_status  = 0.0;
        scantime_stat    = 0.0;
        scantime_pstree  = 0.0;
        scantime_top     = 0.0;
        scantime_CPUload = 0.0;
        scantime_CPUpcnt = 0.0;

        DEBUG_TRACEPOINT(" ");

        EXECUTE_SYSTEM_COMMAND("touch line.%s.%d.txt",
                               __func__,
                               __LINE__); //DEBUGTEST

        if (freeze == 0)
        {
            //attron(A_BOLD);
            sprintf(monstring, "Mode %d   PRESS x TO STOP MONITOR", MonMode);
            //processtools__print_header(monstring, '-');
            TUI_print_header(monstring, '-');
            //attroff(A_BOLD);
        }

        int selectedOK = 0; // goes to 1 if at least one process is selected
        switch (ch)
        {
        case 'f': // Freeze screen (toggle)
            if (freeze == 0)
            {
                freeze = 1;
            }
            else
            {
                freeze = 0;
            }
            break;

        case 'x': // Exit control screen
            loopOK = 0;
            Xexit  = 1;
            break;

        case ' ': // Mark current PID as selected (if none selected, other commands only apply to highlighted process)
            pindex = pindexSelected;
            if (procinfoproc.selectedarray[pindex] == 1)
            {
                procinfoproc.selectedarray[pindex] = 0;
            }
            else
            {
                procinfoproc.selectedarray[pindex] = 1;
            }
            break;

        case 'u': // undelect all
            for (index = 0; index < procinfoproc.NBpindexActive; index++)
            {
                pindex = procinfoproc.pindexActive[index];
                procinfoproc.selectedarray[pindex] = 0;
            }
            break;

        case KEY_UP:
            pindexActiveSelected--;
            if (pindexActiveSelected < 0)
            {
                pindexActiveSelected = 0;
            }
            if (TimeSorted == 0)
            {
                pindexSelected =
                    procinfoproc.pindexActive[pindexActiveSelected];
            }
            else
            {
                pindexSelected =
                    procinfoproc.sorted_pindex_time[pindexActiveSelected];
            }
            break;

        case KEY_DOWN:
            pindexActiveSelected++;
            if (pindexActiveSelected > procinfoproc.NBpindexActive - 1)
            {
                pindexActiveSelected = procinfoproc.NBpindexActive - 1;
            }
            if (TimeSorted == 0)
            {
                pindexSelected =
                    procinfoproc.pindexActive[pindexActiveSelected];
            }
            else
            {
                pindexSelected =
                    procinfoproc.sorted_pindex_time[pindexActiveSelected];
            }
            break;

        case KEY_RIGHT:
            procinfoproc.DisplayDetailedMode = 1;
            break;

        case KEY_LEFT:
            procinfoproc.DisplayDetailedMode = 0;
            break;

        case KEY_PPAGE:
            pindexActiveSelected -= 10;
            if (pindexActiveSelected < 0)
            {
                pindexActiveSelected = 0;
            }
            if (TimeSorted == 0)
            {
                pindexSelected =
                    procinfoproc.pindexActive[pindexActiveSelected];
            }
            else
            {
                pindexSelected =
                    procinfoproc.sorted_pindex_time[pindexActiveSelected];
            }
            break;

        case KEY_NPAGE:
            pindexActiveSelected += 10;
            if (pindexActiveSelected > procinfoproc.NBpindexActive - 1)
            {
                pindexActiveSelected = procinfoproc.NBpindexActive - 1;
            }
            if (TimeSorted == 0)
            {
                pindexSelected =
                    procinfoproc.pindexActive[pindexActiveSelected];
            }
            else
            {
                pindexSelected =
                    procinfoproc.sorted_pindex_time[pindexActiveSelected];
            }
            break;

        case 'T':
            for (index = 0; index < procinfoproc.NBpindexActive; index++)
            {
                pindex = procinfoproc.pindexActive[index];
                if (procinfoproc.selectedarray[pindex] == 1)
                {
                    selectedOK = 1;
                    pid        = pinfolist->PIDarray[pindex];
                    kill(pid, SIGTERM);
                }
            }
            if ((selectedOK == 0) && (pindexSelectedOK == 1))
            {
                pindex = pindexSelected;
                pid    = pinfolist->PIDarray[pindex];
                kill(pid, SIGTERM);
            }
            break;

        case 'K':
            for (index = 0; index < procinfoproc.NBpindexActive; index++)
            {
                pindex = procinfoproc.pindexActive[index];
                if (procinfoproc.selectedarray[pindex] == 1)
                {
                    selectedOK = 1;
                    pid        = pinfolist->PIDarray[pindex];
                    kill(pid, SIGKILL);
                }
            }
            if ((selectedOK == 0) && (pindexSelectedOK == 1))
            {
                pindex = pindexSelected;
                pid    = pinfolist->PIDarray[pindex];
                kill(pid, SIGKILL);
            }
            break;

        case 'I':
            for (index = 0; index < procinfoproc.NBpindexActive; index++)
            {
                pindex = procinfoproc.pindexActive[index];
                if (procinfoproc.selectedarray[pindex] == 1)
                {
                    selectedOK = 1;
                    pid        = pinfolist->PIDarray[pindex];
                    kill(pid, SIGINT);
                }
            }
            if ((selectedOK == 0) && (pindexSelectedOK == 1))
            {
                pindex = pindexSelected;
                pid    = pinfolist->PIDarray[pindex];
                kill(pid, SIGINT);
            }
            break;

        case 'r':
            for (index = 0; index < procinfoproc.NBpindexActive; index++)
            {
                pindex = procinfoproc.pindexActive[index];
                if (procinfoproc.selectedarray[pindex] == 1)
                {
                    selectedOK = 1;
                    if (pinfolist->active[pindex] != 1)
                    {
                        char SM_fname[STRINGMAXLEN_FULLFILENAME];
                        WRITE_FULLFILENAME(SM_fname,
                                           "%s/proc.%s.%06d.shm",
                                           procdname,
                                           pinfolist->pnamearray[pindex],
                                           (int) pinfolist->PIDarray[pindex]);
                        remove(SM_fname);
                    }
                }
            }
            if ((selectedOK == 0) && (pindexSelectedOK == 1))
            {
                pindex = pindexSelected;
                if (pinfolist->active[pindex] != 1)
                {
                    remove(procinfoproc.pinfoarray[pindex]->logfilename);

                    char SM_fname[STRINGMAXLEN_FULLFILENAME];
                    WRITE_FULLFILENAME(SM_fname,
                                       "%s/proc.%s.%06d.shm",
                                       procdname,
                                       pinfolist->pnamearray[pindex],
                                       (int) pinfolist->PIDarray[pindex]);
                    remove(SM_fname);
                }
            }
            break;

        case 'R':
            for (index = 0; index < procinfoproc.NBpindexActive; index++)
            {
                pindex = procinfoproc.pindexActive[index];
                if (pinfolist->active[pindex] != 1)
                {
                    remove(procinfoproc.pinfoarray[pindex]->logfilename);

                    char SM_fname[STRINGMAXLEN_FULLFILENAME];
                    WRITE_FULLFILENAME(SM_fname,
                                       "%s/proc.%s.%06d.shm",
                                       procdname,
                                       pinfolist->pnamearray[pindex],
                                       (int) pinfolist->PIDarray[pindex]);
                    remove(SM_fname);
                }
            }
            break;

        // loop controls
        case 'p': // pause toggle
            for (index = 0; index < procinfoproc.NBpindexActive; index++)
            {
                pindex = procinfoproc.pindexActive[index];
                if (procinfoproc.selectedarray[pindex] == 1)
                {
                    selectedOK = 1;
                    if (procinfoproc.pinfoarray[pindex]->CTRLval == 0)
                    {
                        procinfoproc.pinfoarray[pindex]->CTRLval = 1;
                    }
                    else
                    {
                        procinfoproc.pinfoarray[pindex]->CTRLval = 0;
                    }
                }
            }
            if ((selectedOK == 0) && (pindexSelectedOK == 1))
            {
                pindex = pindexSelected;
                if (procinfoproc.pinfoarray[pindex]->CTRLval == 0)
                {
                    procinfoproc.pinfoarray[pindex]->CTRLval = 1;
                }
                else
                {
                    procinfoproc.pinfoarray[pindex]->CTRLval = 0;
                }
            }
            break;

        case 'c': // compute toggle (toggles between 0-run and 5-run-without-compute)
            DEBUG_TRACEPOINT(" ");
            for (index = 0; index < procinfoproc.NBpindexActive; index++)
            {
                pindex = procinfoproc.pindexActive[index];
                if (procinfoproc.selectedarray[pindex] == 1)
                {
                    selectedOK = 1;
                    if (procinfoproc.pinfoarray[pindex]->CTRLval ==
                        0) // if running, turn compute to off
                    {
                        procinfoproc.pinfoarray[pindex]->CTRLval = 5;
                    }
                    else if (procinfoproc.pinfoarray[pindex]->CTRLval ==
                             5) // if compute off, turn compute back on
                    {
                        procinfoproc.pinfoarray[pindex]->CTRLval = 0;
                    }
                }
            }
            DEBUG_TRACEPOINT(" ");
            if ((selectedOK == 0) && (pindexSelectedOK == 1))
            {
                pindex = pindexSelected;
                if (procinfoproc.pinfoarray[pindex]->CTRLval ==
                    0) // if running, turn compute to off
                {
                    procinfoproc.pinfoarray[pindex]->CTRLval = 5;
                }
                else if (procinfoproc.pinfoarray[pindex]->CTRLval ==
                         5) // if procinfoproccompute off, turn compute back on
                {
                    procinfoproc.pinfoarray[pindex]->CTRLval = 0;
                }
            }
            DEBUG_TRACEPOINT(" ");
            break;

        case 's': // step
            for (index = 0; index < procinfoproc.NBpindexActive; index++)
            {
                pindex = procinfoproc.pindexActive[index];
                if (procinfoproc.selectedarray[pindex] == 1)
                {
                    selectedOK                               = 1;
                    procinfoproc.pinfoarray[pindex]->CTRLval = 2;
                }
            }
            if ((selectedOK == 0) && (pindexSelectedOK == 1))
            {
                pindex                                   = pindexSelected;
                procinfoproc.pinfoarray[pindex]->CTRLval = 2;
            }
            break;

        case '>': // move to other cpuset
            pindex = pindexSelected;
            if (pinfolist->active[pindex] == 1)
            {
                TUI_exit();
                if (system("clear") != 0) // clear screen
                {
                    PRINT_ERROR("system() returns non-zero value");
                }
                printf("CURRENT cpu set : %s\n",
                       procinfoproc.pinfodisp[pindex].cpuset);
                listindex = processinfo_SelectFromList(CPUsetList, NBCPUset);

                EXECUTE_SYSTEM_COMMAND("sudo cset proc -m %d %s",
                                       pinfolist->PIDarray[pindex],
                                       CPUsetList[listindex].name);

                TUI_init_terminal(&wrow, &wcol);
            }
            break;

        case '<': // move to same cpuset
            pindex = pindexSelected;
            if (pinfolist->active[pindex] == 1)
            {
                TUI_exit();

                EXECUTE_SYSTEM_COMMAND("sudo cset proc -m %d root &> /dev/null",
                                       pinfolist->PIDarray[pindex]);
                EXECUTE_SYSTEM_COMMAND(
                    "sudo cset proc --force -m %d %s &> /dev/null",
                    pinfolist->PIDarray[pindex],
                    procinfoproc.pinfodisp[pindex].cpuset);

                TUI_init_terminal(&wrow, &wcol);
            }
            break;

        case 'e': // exit
            for (index = 0; index < procinfoproc.NBpindexActive; index++)
            {
                pindex = procinfoproc.pindexActive[index];
                if (procinfoproc.selectedarray[pindex] == 1)
                {
                    selectedOK                               = 1;
                    procinfoproc.pinfoarray[pindex]->CTRLval = 3;
                }
            }
            if ((selectedOK == 0) && (pindexSelectedOK == 1))
            {
                pindex                                   = pindexSelected;
                procinfoproc.pinfoarray[pindex]->CTRLval = 3;
            }
            break;

        case 'z': // apply current value as offset (zero loop counter)
            selectedOK = 0;
            for (index = 0; index < procinfoproc.NBpindexActive; index++)
            {
                pindex = procinfoproc.pindexActive[index];
                if (procinfoproc.selectedarray[pindex] == 1)
                {
                    selectedOK = 1;
                    procinfoproc.loopcntoffsetarray[pindex] =
                        procinfoproc.pinfoarray[pindex]->loopcnt;
                }
            }
            if ((selectedOK == 0) && (pindexSelectedOK == 1))
            {
                pindex = pindexSelected;
                procinfoproc.loopcntoffsetarray[pindex] =
                    procinfoproc.pinfoarray[pindex]->loopcnt;
            }
            break;

        case 'Z': // revert to original counter value
            for (index = 0; index < procinfoproc.NBpindexActive; index++)
            {
                pindex = procinfoproc.pindexActive[index];
                if (procinfoproc.selectedarray[pindex] == 1)
                {
                    selectedOK                              = 1;
                    procinfoproc.loopcntoffsetarray[pindex] = 0;
                }
            }
            if ((selectedOK == 0) && (pindexSelectedOK == 1))
            {
                pindex                                  = pindexSelected;
                procinfoproc.loopcntoffsetarray[pindex] = 0;
            }
            break;

        case 't':
            TUI_exit();
            EXECUTE_SYSTEM_COMMAND(
                "tmux a -t %s",
                procinfoproc.pinfoarray[pindexSelected]->tmuxname);
            TUI_init_terminal(&wrow, &wcol);
            break;

        case 'a':
            pindex = pindexSelected;
            if (pinfolist->active[pindex] == 1)
            {
                TUI_exit();
                EXECUTE_SYSTEM_COMMAND("watch -n 0.1 cat /proc/%d/status",
                                       (int) pinfolist->PIDarray[pindex]);
                TUI_init_terminal(&wrow, &wcol);
            }
            break;

        case 'd':
            pindex = pindexSelected;
            if (pinfolist->active[pindex] == 1)
            {
                TUI_exit();
                EXECUTE_SYSTEM_COMMAND("watch -n 0.1 cat /proc/%d/sched",
                                       (int) pinfolist->PIDarray[pindex]);
                EXECUTE_SYSTEM_COMMAND("watch -n 0.1 cat /proc/%d/sched",
                                       (int) pinfolist->PIDarray[pindex]);
                TUI_init_terminal(&wrow, &wcol);
            }
            break;

        case 'o':
            if (TimeSorted == 1)
            {
                TimeSorted = 0;
            }
            else
            {
                TimeSorted = 1;
            }
            break;

        case 'L': // toggle time limit (iter)
            pindex      = pindexSelected;
            ToggleValue = procinfoproc.pinfoarray[pindex]->dtiter_limit_enable;
            if (ToggleValue == 0)
            {
                procinfoproc.pinfoarray[pindex]->dtiter_limit_enable = 1;
                procinfoproc.pinfoarray[pindex]->dtiter_limit_value =
                    (long) (1.5 *
                            procinfoproc.pinfoarray[pindex]->dtmedian_iter_ns);
                procinfoproc.pinfoarray[pindex]->dtiter_limit_cnt = 0;
            }
            else
            {
                ToggleValue++;
                if (ToggleValue == 3)
                {
                    ToggleValue = 0;
                }
                procinfoproc.pinfoarray[pindex]->dtiter_limit_enable =
                    ToggleValue;
            }
            break;
            ;

        case 'M': // toggle time limit (exec)
            pindex      = pindexSelected;
            ToggleValue = procinfoproc.pinfoarray[pindex]->dtexec_limit_enable;
            if (ToggleValue == 0)
            {
                procinfoproc.pinfoarray[pindex]->dtexec_limit_enable = 1;
                procinfoproc.pinfoarray[pindex]->dtexec_limit_value =
                    (long) (1.5 * procinfoproc.pinfoarray[pindex]
                                      ->dtmedian_exec_ns +
                            20000);
                procinfoproc.pinfoarray[pindex]->dtexec_limit_cnt = 0;
            }
            else
            {
                ToggleValue++;
                if (ToggleValue == 3)
                {
                    ToggleValue = 0;
                }
                procinfoproc.pinfoarray[pindex]->dtexec_limit_enable =
                    ToggleValue;
            }
            break;
            ;

        case 'm': // message
            pindex = pindexSelected;
            if (pinfolist->active[pindex] == 1)
            {
                TUI_exit();
                EXECUTE_SYSTEM_COMMAND(
                    "clear; tail -f %s",
                    procinfoproc.pinfoarray[pindex]->logfilename);
                TUI_init_terminal(&wrow, &wcol);
            }
            break;

            // ============ SCREENS

        case 'h': // help
            procinfoproc.DisplayMode = PROCCTRL_DISPLAYMODE_HELP;
            break;

        case KEY_F(2): // control
            procinfoproc.DisplayMode = PROCCTRL_DISPLAYMODE_CTRL;
            break;

        case KEY_F(3): // resources
            procinfoproc.DisplayMode = PROCCTRL_DISPLAYMODE_RESOURCES;
            break;

        case KEY_F(4): // triggering
            procinfoproc.DisplayMode = PROCCTRL_DISPLAYMODE_TRIGGER;
            break;

        case KEY_F(5): // timing
            procinfoproc.DisplayMode = PROCCTRL_DISPLAYMODE_TIMING;
            break;

        case KEY_F(6): // htop
            TUI_exit();
            if (system("htop") != 0)
            {
                PRINT_ERROR("system() returns non-zero value");
            }
            TUI_init_terminal(&wrow, &wcol);
            break;

        case KEY_F(7): // iotop
            TUI_exit();
            if (system("sudo iotop -o") != 0)
            {
                PRINT_ERROR("system() returns non-zero value");
            }
            TUI_init_terminal(&wrow, &wcol);
            break;

        case KEY_F(8): // atop
            TUI_exit();
            if (system("sudo atop") != 0)
            {
                PRINT_ERROR("system() returns non-zero value");
            }
            TUI_init_terminal(&wrow, &wcol);
            break;

            // ============ SCANNING

        case '{': // slower scan update
            procinfoproc.twaitus = (int) (1.2 * procinfoproc.twaitus);
            if (procinfoproc.twaitus > 1000000)
            {
                procinfoproc.twaitus = 1000000;
            }
            break;

        case '}': // faster scan update
            procinfoproc.twaitus =
                (int) (0.83333333333333333333 * procinfoproc.twaitus);
            if (procinfoproc.twaitus < 1000)
            {
                procinfoproc.twaitus = 1000;
            }
            break;

            // ============ DISPLAY

        case '-': // slower display update
            frequ *= 0.5;
            if (frequ < 1.0)
            {
                frequ = 1.0;
            }
            if (frequ > 64.0)
            {
                frequ = 64.0;
            }
            break;

        case '+': // faster display update
            frequ *= 2.0;
            if (frequ < 1.0)
            {
                frequ = 1.0;
            }
            if (frequ > 64.0)
            {
                frequ = 64.0;
            }
            break;
        }
        clock_gettime(CLOCK_REALTIME, &t01loop);

        DEBUG_TRACEPOINT(" ");

        if (freeze == 0)
        {
            erase();

            if (procinfoproc.DisplayMode == PROCCTRL_DISPLAYMODE_HELP)
            {
                int attrval = A_BOLD;

                attron(attrval);
                TUI_printfw("    x");
                attroff(attrval);
                TUI_printfw("    Exit");
                TUI_newline();

                TUI_printfw("============ SCREENS");
                TUI_newline();

                attron(attrval);
                TUI_printfw("     h");
                attroff(attrval);
                TUI_printfw("   Help screen");
                TUI_newline();

                attron(attrval);
                TUI_printfw("F2 F3 F4 F5");
                attroff(attrval);
                TUI_printfw(
                    "   control / resources / triggering / timing "
                    "screens");
                TUI_newline();

                attron(attrval);
                TUI_printfw("F6 F7 F8");
                attroff(attrval);
                TUI_printfw(
                    "   htop (F10 to exit) / iotop (q to exit) / "
                    "atop (q to exit)");
                TUI_newline();

                TUI_printfw("============ SCANNING");
                TUI_newline();

                attron(attrval);
                TUI_printfw("  } {");
                attroff(attrval);
                TUI_printfw("    Increase/decrease scan frequency");
                TUI_newline();

                TUI_printfw("============ DISPLAY");
                TUI_newline();

                attron(attrval);
                TUI_printfw("+ - f");
                attroff(attrval);
                TUI_printfw(
                    "    Increase/decrease display frequency, "
                    "(f)reeze display");
                TUI_newline();

                attron(attrval);
                TUI_printfw("  r R");
                attroff(attrval);
                TUI_printfw(
                    "    Remove selected (r) or all (R) inactive "
                    "process(es) log");
                TUI_newline();

                attron(attrval);
                TUI_printfw("    o");
                attroff(attrval);
                TUI_printfw("    sort processes (toggle)");
                TUI_newline();

                attron(attrval);
                TUI_printfw("SPACE; u");
                attroff(attrval);
                TUI_printfw(
                    "    Select this process; (u)nselect all "
                    "processes");
                TUI_newline();

                TUI_printfw("============ PROCESS DETAILS");
                TUI_newline();

                attron(attrval);
                TUI_printfw("    t");
                attroff(attrval);
                TUI_printfw("    Connect to tmux session");
                TUI_newline();

                attron(attrval);
                TUI_printfw("  a/d");
                attroff(attrval);
                TUI_printfw("    process st(a)t / sche(d)");
                TUI_newline();

                TUI_printfw("============ LOOP CONTROL");
                TUI_newline();

                attron(attrval);
                TUI_printfw("p c s");
                attroff(attrval);
                TUI_printfw(
                    "    (p)ause (toggle C0 - C1); (c)ompute "
                    "on/off (toggle C0 - C5); (s)tep");
                TUI_newline();

                attron(attrval);
                TUI_printfw("e T K I");
                attroff(attrval);
                TUI_printfw(
                    "    clean (e)xit; SIG(T)ERM; SIG(K)ILL; "
                    "SIG(I)NT");
                TUI_newline();

                TUI_printfw("============ COUNTERS, TIMERS");
                TUI_newline();

                attron(attrval);
                TUI_printfw(" z Z");
                attroff(attrval);
                TUI_printfw(
                    "    zero this (z) or all (Z) selected "
                    "counter");
                TUI_newline();

                attron(attrval);
                TUI_printfw(" L H ");
                attroff(attrval);
                TUI_printfw("    Enable iteration/execution time limit");
                TUI_newline();

                TUI_printfw("============ AFFINITY");
                TUI_newline();

                attron(attrval);
                TUI_printfw(" > <");
                attroff(attrval);
                TUI_printfw(
                    "    Move to other CPU set / back to same CPU "
                    "set");
                TUI_newline();
            }
            else
            {
                DEBUG_TRACEPOINT(" ");

                TUI_printfw("pindexSelected = %d    %d",
                            pindexSelected,
                            pindexSelectedOK);
                TUI_newline();

                TUI_printfw(
                    "[PID %d   SCAN TID %d]  %2d cpus   %2d "
                    "processes tracked    Display Mode %d",
                    CLIPID,
                    (int) procinfoproc.scanPID,
                    procinfoproc.NBcpus,
                    procinfoproc.NBpindexActive,
                    procinfoproc.DisplayMode);
                TUI_newline();

                if (procinfoproc.DisplayMode == PROCCTRL_DISPLAYMODE_HELP)
                {
                    attron(A_REVERSE);
                    TUI_printfw("[h] Help");
                    attroff(A_REVERSE);
                }
                else
                {
                    TUI_printfw("[h] Help");
                }
                TUI_printfw("   ");

                if (procinfoproc.DisplayMode == PROCCTRL_DISPLAYMODE_CTRL)
                {
                    attron(A_REVERSE);
                    TUI_printfw("[F2] CTRL");
                    attroff(A_REVERSE);
                }
                else
                {
                    TUI_printfw("[F2] CTRL");
                }
                TUI_printfw("   ");

                if (procinfoproc.DisplayMode == PROCCTRL_DISPLAYMODE_RESOURCES)
                {
                    attron(A_REVERSE);
                    TUI_printfw("[F3] Resources");
                    attroff(A_REVERSE);
                }
                else
                {
                    TUI_printfw("[F3] Resources");
                }
                TUI_printfw("   ");

                if (procinfoproc.DisplayMode == PROCCTRL_DISPLAYMODE_TRIGGER)
                {
                    attron(A_REVERSE);
                    TUI_printfw("[F4] Triggering");
                    attroff(A_REVERSE);
                }
                else
                {
                    TUI_printfw("[F4] Triggering");
                }
                TUI_printfw("   ");

                if (procinfoproc.DisplayMode == PROCCTRL_DISPLAYMODE_TIMING)
                {
                    attron(A_REVERSE);
                    TUI_printfw("[F5] Timing");
                    attroff(A_REVERSE);
                }
                else
                {
                    TUI_printfw("[F5] Timing");
                }
                TUI_printfw("   ");

                if (procinfoproc.DisplayMode == PROCCTRL_DISPLAYMODE_HTOP)
                {
                    attron(A_REVERSE);
                    TUI_printfw("[F6] htop (F10 to exit)");
                    attroff(A_REVERSE);
                }
                else
                {
                    TUI_printfw("[F6] htop (F10 to exit)");
                }
                TUI_printfw("   ");

                if (procinfoproc.DisplayMode == PROCCTRL_DISPLAYMODE_IOTOP)
                {
                    attron(A_REVERSE);
                    TUI_printfw("[F7] iotop (q to exit)");
                    attroff(A_REVERSE);
                }
                else
                {
                    TUI_printfw("[F7] iotop (q to exit)");
                }
                TUI_printfw("   ");

                if (procinfoproc.DisplayMode == PROCCTRL_DISPLAYMODE_ATOP)
                {
                    attron(A_REVERSE);
                    TUI_printfw("[F8] atop (q to exit)");
                    attroff(A_REVERSE);
                }
                else
                {
                    TUI_printfw("[F8] atop (q to exit)");
                }
                TUI_printfw("   ");

                TUI_newline();

                DEBUG_TRACEPOINT(" ");

                TUI_printfw(
                    "Display frequ = %2d Hz  [%ld] fscan=%5.2f Hz "
                    "( %5.2f Hz %5.2f %% busy )",
                    (int) (frequ + 0.5),
                    procinfoproc.loopcnt,
                    1.0 / procinfoproc.dtscan,
                    1000000.0 / procinfoproc.twaitus,
                    100.0 *
                        (procinfoproc.dtscan - 1.0e-6 * procinfoproc.twaitus) /
                        procinfoproc.dtscan);
                TUI_newline();

                DEBUG_TRACEPOINT(" ");

                if ((pindexSelected >= 0) &&
                    (pindexSelected < PROCESSINFOLISTSIZE))
                {
                    if (procinfoproc.pinfommapped[pindexSelected] == 1)
                    {

                        strcpy(pselected_FILE,
                               procinfoproc.pinfoarray[pindexSelected]
                                   ->source_FILE);
                        strcpy(pselected_FUNCTION,
                               procinfoproc.pinfoarray[pindexSelected]
                                   ->source_FUNCTION);
                        pselected_LINE =
                            procinfoproc.pinfoarray[pindexSelected]
                                ->source_LINE;

                        TUI_printfw(
                            "Source Code: %s line %d "
                            "(function %s)",
                            pselected_FILE,
                            pselected_LINE,
                            pselected_FUNCTION);
                        TUI_newline();
                    }
                    else
                    {
                        sprintf(pselected_FILE, "?");
                        sprintf(pselected_FUNCTION, "?");
                        pselected_LINE = 0;
                        TUI_newline();
                    }
                }
                else
                {
                    TUI_printfw("---");
                    TUI_newline();
                }

                TUI_newline();

                clock_gettime(CLOCK_REALTIME, &t02loop);

                DEBUG_TRACEPOINT(" ");

                clock_gettime(CLOCK_REALTIME, &t03loop);

                clock_gettime(CLOCK_REALTIME, &t04loop);

                /** ### Display
                 *
                 *
                 *
                 */

                int dispindex;
                if (TimeSorted == 0)
                {
                    dispindexMax = wrow - 4;
                }
                else
                {
                    dispindexMax = procinfoproc.NBpindexActive;
                }

                DEBUG_TRACEPOINT(" ");

                if (procinfoproc.DisplayMode == PROCCTRL_DISPLAYMODE_RESOURCES)
                {
                    int cpu;
                    DEBUG_TRACEPOINT(" ");

                    // List CPUs

                    // Measure CPU loads, Display
                    int ColorCode;

                    // color limits for load
                    int CPUloadLim0 = 3;
                    int CPUloadLim1 = 40;
                    int CPUloadLim2 = 60;
                    int CPUloadLim3 = 80;

                    // color limits for # processes
                    int CPUpcntLim0 = 1;
                    int CPUpcntLim1 = 2;
                    int CPUpcntLim2 = 4;
                    int CPUpcntLim3 = 8;

                    DEBUG_TRACEPOINT(" ");

                    // List CPUs
                    TUI_printfw(
                        " %*.*s %*.*s %-*.*s    %-*.*s         "
                        "     ",
                        pstrlen_status,
                        pstrlen_status,
                        " ",
                        pstrlen_pid,
                        pstrlen_pid,
                        " ",
                        pstrlen_pname,
                        pstrlen_pname,
                        " ",
                        pstrlen_cset,
                        pstrlen_cset,
                        " ");

                    for (cpusocket = 0; cpusocket < procinfoproc.NBcpusocket;
                         cpusocket++)
                    {
                        if (cpusocket > 0)
                        {
                            TUI_printfw("    ");
                        }
                        for (cpu = 0; cpu < procinfoproc.NBcpus; cpu++)
                            if (procinfoproc.CPUphys[cpu] == cpusocket)
                            {
                                TUI_printfw("|%02d", procinfoproc.CPUids[cpu]);
                            }
                        TUI_printfw("|");
                    }
                    TUI_printfw(" <- %2d sockets %2d CPUs",
                                procinfoproc.NBcpusocket,
                                procinfoproc.NBcpus);
                    TUI_newline();

                    // List CPU # processes
                    TUI_printfw(
                        " %*.*s %*.*s %-*.*s    %-*.*s         "
                        "     ",
                        pstrlen_status,
                        pstrlen_status,
                        " ",
                        pstrlen_pid,
                        pstrlen_pid,
                        " ",
                        pstrlen_pname,
                        pstrlen_pname,
                        " ",
                        pstrlen_cset,
                        pstrlen_cset,
                        " ");

                    for (cpusocket = 0; cpusocket < procinfoproc.NBcpusocket;
                         cpusocket++)
                    {
                        if (cpusocket > 0)
                        {
                            TUI_printfw("    ");
                        }

                        for (cpu = 0; cpu < procinfoproc.NBcpus; cpu++)
                            if (procinfoproc.CPUphys[cpu] == cpusocket)
                            {
                                int vint =
                                    procinfoproc
                                        .CPUpcnt[procinfoproc.CPUids[cpu]];
                                if (vint > 99)
                                {
                                    vint = 99;
                                }

                                ColorCode = 0;
                                if (vint > CPUpcntLim1)
                                {
                                    ColorCode = 2;
                                }
                                if (vint > CPUpcntLim2)
                                {
                                    ColorCode = 3;
                                }
                                if (vint > CPUpcntLim3)
                                {
                                    ColorCode = 4;
                                }
                                if (vint < CPUpcntLim0)
                                {
                                    ColorCode = 5;
                                }

                                TUI_printfw("|");
                                if (ColorCode != 0)
                                {
                                    attron(COLOR_PAIR(ColorCode));
                                }
                                TUI_printfw("%02d", vint);
                                if (ColorCode != 0)
                                {
                                    attroff(COLOR_PAIR(ColorCode));
                                }
                            }
                        TUI_printfw("|");
                    }

                    TUI_printfw(" <- PROCESSES");
                    TUI_newline();

                    DEBUG_TRACEPOINT(" ");

                    // Print CPU LOAD
                    TUI_printfw(
                        " %*.*s %*.*s %-*.*s PR %-*.*s  #T  "
                        "ctxsw   ",
                        pstrlen_status,
                        pstrlen_status,
                        "STATUS",
                        pstrlen_pid,
                        pstrlen_pid,
                        "PID",
                        pstrlen_pname,
                        pstrlen_pname,
                        "pname",
                        pstrlen_cset,
                        pstrlen_cset,
                        "cset",
                        procinfoproc.NBcpus);

                    for (cpusocket = 0; cpusocket < procinfoproc.NBcpusocket;
                         cpusocket++)
                    {
                        if (cpusocket > 0)
                        {
                            TUI_printfw("    ");
                        }
                        for (cpu = 0; cpu < procinfoproc.NBcpus; cpu++)
                            if (procinfoproc.CPUphys[cpu] == cpusocket)
                            {
                                int vint =
                                    (int) (100.0 *
                                           procinfoproc.CPUload
                                               [procinfoproc.CPUids[cpu]]);
                                if (vint > 99)
                                {
                                    vint = 99;
                                }

                                ColorCode = 0;
                                if (vint > CPUloadLim1)
                                {
                                    ColorCode = 2;
                                }
                                if (vint > CPUloadLim2)
                                {
                                    ColorCode = 3;
                                }
                                if (vint > CPUloadLim3)
                                {
                                    ColorCode = 4;
                                }
                                if (vint < CPUloadLim0)
                                {
                                    ColorCode = 5;
                                }

                                TUI_printfw("|");
                                if (ColorCode != 0)
                                {
                                    attron(COLOR_PAIR(ColorCode));
                                }
                                TUI_printfw("%02d", vint);
                                if (ColorCode != 0)
                                {
                                    attroff(COLOR_PAIR(ColorCode));
                                }
                            }
                        TUI_printfw("|");
                    }

                    TUI_printfw(" <- CPU LOAD");
                    TUI_newline();
                    TUI_newline();
                }

                // print header for display mode PROCCTRL_DISPLAYMODE_CTRL
                if (procinfoproc.DisplayMode == PROCCTRL_DISPLAYMODE_CTRL)
                {
                    DEBUG_TRACEPOINT(" ");
                    TUI_newline();
                    TUI_newline();
                    TUI_printfw(
                        " %*.*s %*.*s %-*.*s %-*.*s C# tstart  "
                        "     %-*.*s %-*.*s   %-*.*s   %-*.*s",
                        pstrlen_status,
                        pstrlen_status,
                        "STATUS",
                        pstrlen_pid,
                        pstrlen_pid,
                        "PID",
                        pstrlen_pname,
                        pstrlen_pname,
                        "pname",
                        pstrlen_state,
                        pstrlen_state,
                        "state",
                        pstrlen_tmux,
                        pstrlen_tmux,
                        "tmux sess",
                        pstrlen_loopcnt,
                        pstrlen_loopcnt,
                        "loopcnt",
                        pstrlen_descr,
                        pstrlen_descr,
                        "Description",
                        pstrlen_msg,
                        pstrlen_msg,
                        "Message");
                    TUI_newline();
                    TUI_newline();
                    DEBUG_TRACEPOINT(" ");
                }

                // print header for display mode PROCCTRL_DISPLAYMODE_TRIGGER
                if (procinfoproc.DisplayMode == PROCCTRL_DISPLAYMODE_TRIGGER)
                {
                    DEBUG_TRACEPOINT(" ");
                    TUI_newline();
                    TUI_newline();
                    TUI_printfw(
                        "%*.*s %*.*s %-*.*s %*.*s %*.*s mode "
                        "sem %*.*s  %*.*s  %*.*s",
                        pstrlen_status,
                        pstrlen_status,
                        "STATUS",
                        pstrlen_pid,
                        pstrlen_pid,
                        "PID",
                        pstrlen_pname,
                        pstrlen_pname,
                        "pname",
                        pstrlen_inode,
                        pstrlen_inode,
                        "inode",
                        pstrlen_trigstreamname,
                        pstrlen_trigstreamname,
                        "stream",
                        pstrlen_missedfr,
                        pstrlen_missedfr,
                        "miss",
                        pstrlen_missedfrc,
                        pstrlen_missedfrc,
                        "misscumul",
                        pstrlen_tocnt,
                        pstrlen_tocnt,
                        "timeouts");
                    TUI_newline();
                    TUI_newline();
                    DEBUG_TRACEPOINT(" ");
                }

                // print header for display mode PROCCTRL_DISPLAYMODE_TIMING
                if (procinfoproc.DisplayMode == PROCCTRL_DISPLAYMODE_TIMING)
                {
                    DEBUG_TRACEPOINT(" ");
                    TUI_newline();
                    TUI_newline();
                    TUI_printfw(
                        "   STATUS    PID   process name       "
                        "             ");
                    TUI_newline();
                    TUI_newline();
                    DEBUG_TRACEPOINT(" ");
                }

                DEBUG_TRACEPOINT(" ");

                clock_gettime(CLOCK_REALTIME, &t05loop);

                // ===========================================================================
                // ============== PRINT INFORMATION FOR EACH PROCESS =========================
                // ===========================================================================
                pindexSelectedOK = 0;

                for (dispindex = 0; dispindex < dispindexMax; dispindex++)
                {
                    if (TimeSorted == 0)
                    {
                        pindex = dispindex;
                    }
                    else
                    {
                        pindex = procinfoproc.sorted_pindex_time[dispindex];
                    }

                    if (pindex < procinfoproc.NBpinfodisp)
                    {
                        DEBUG_TRACEPOINT("%d %d   %ld %ld",
                                         dispindex,
                                         dispindexMax,
                                         pindex,
                                         procinfoproc.NBpinfodisp);

                        if (pindex == pindexSelected)
                        {
                            attron(A_REVERSE);
                            pindexSelectedOK = 1;
                        }

                        if (procinfoproc.selectedarray[pindex] == 1)
                        {
                            TUI_printfw("*");
                        }
                        else
                        {
                            TUI_printfw(" ");
                        }

                        DEBUG_TRACEPOINT(
                            "procinfoproc.selectedarray["
                            "pindex] = %d",
                            procinfoproc.selectedarray[pindex]);

                        if (pinfolist->active[pindex] == 1)
                        {
                            sprintf(string,
                                    "%-*.*s",
                                    pstrlen_status,
                                    pstrlen_status,
                                    "ACTIVE");
                            attron(COLOR_PAIR(2));
                            TUI_printfw("%s", string);
                            attroff(COLOR_PAIR(2));
                        }

                        DEBUG_TRACEPOINT(
                            "pinfolist->active[pindex] = "
                            "%d",
                            pinfolist->active[pindex]);

                        if (pinfolist->active[pindex] ==
                            2) // not active: error, crashed or terminated
                        {
                            switch (procinfoproc.pinfoarray[pindex]->loopstat)
                            {
                            case 3: // clean exit
                                sprintf(string,
                                        "%-*.*s",
                                        pstrlen_status,
                                        pstrlen_status,
                                        "STOPPED");
                                attron(COLOR_PAIR(3));
                                TUI_printfw("%s", string);
                                attroff(COLOR_PAIR(3));
                                break;

                            case 4: // error
                                sprintf(string,
                                        "%-*.*s",
                                        pstrlen_status,
                                        pstrlen_status,
                                        "ERROR");
                                attron(COLOR_PAIR(3));
                                TUI_printfw("%s", string);
                                attroff(COLOR_PAIR(3));
                                break;

                            default: // crashed
                                sprintf(string,
                                        "%-*.*s",
                                        pstrlen_status,
                                        pstrlen_status,
                                        "CRASHED");
                                attron(COLOR_PAIR(4));
                                TUI_printfw("%s", string);
                                attroff(COLOR_PAIR(4));
                                break;
                            }
                        }

                        DEBUG_TRACEPOINT("%d %d   %ld %ld",
                                         dispindex,
                                         dispindexMax,
                                         pindex,
                                         procinfoproc.NBpinfodisp);

                        if (pinfolist->active[pindex] != 0)
                        {
                            if (pindex == pindexSelected)
                            {
                                attron(A_REVERSE);
                            }

                            sprintf(string,
                                    " %-*.*d",
                                    pstrlen_pid,
                                    pstrlen_pid,
                                    pinfolist->PIDarray[pindex]);
                            TUI_printfw("%s", string);

                            attron(A_BOLD);

                            sprintf(string,
                                    " %-*.*s",
                                    pstrlen_pname,
                                    pstrlen_pname,
                                    procinfoproc.pinfodisp[pindex].name);
                            TUI_printfw("%s", string);
                            attroff(A_BOLD);

                            // ================ DISPLAY MODE PROCCTRL_DISPLAYMODE_CTRL ==================
                            if (procinfoproc.DisplayMode ==
                                PROCCTRL_DISPLAYMODE_CTRL)
                            {
                                switch (
                                    procinfoproc.pinfoarray[pindex]->loopstat)
                                {
                                case 0:
                                    sprintf(string,
                                            " %-*.*"
                                            "s",
                                            pstrlen_state,
                                            pstrlen_state,
                                            "INIT");
                                    break;

                                case 1:
                                    sprintf(string,
                                            " %-*.*"
                                            "s",
                                            pstrlen_state,
                                            pstrlen_state,
                                            "RUN");
                                    break;

                                case 2:
                                    sprintf(string,
                                            " %-*.*"
                                            "s",
                                            pstrlen_state,
                                            pstrlen_state,
                                            "PAUS");
                                    break;

                                case 3:
                                    sprintf(string,
                                            " %-*.*"
                                            "s",
                                            pstrlen_state,
                                            pstrlen_state,
                                            "TERM");
                                    break;

                                case 4:
                                    sprintf(string,
                                            " %-*.*"
                                            "s",
                                            pstrlen_state,
                                            pstrlen_state,
                                            "ERR");
                                    break;

                                case 5:
                                    sprintf(string,
                                            " %-*.*"
                                            "s",
                                            pstrlen_state,
                                            pstrlen_state,
                                            "OFF");
                                    break;

                                case 6:
                                    sprintf(string,
                                            " %-*.*"
                                            "s",
                                            pstrlen_state,
                                            pstrlen_state,
                                            "CRAS"
                                            "H");
                                    break;

                                default:
                                    sprintf(string,
                                            " %-*.*"
                                            "s",
                                            pstrlen_state,
                                            pstrlen_state,
                                            "??");
                                }
                                TUI_printfw("%s", string);

                                if (procinfoproc.pinfoarray[pindex]->CTRLval ==
                                    0)
                                {
                                    attron(COLOR_PAIR(2));
                                    TUI_printfw(" C%d",
                                                procinfoproc.pinfoarray[pindex]
                                                    ->CTRLval);
                                    attroff(COLOR_PAIR(2));
                                }
                                else
                                {
                                    TUI_printfw(" C%d",
                                                procinfoproc.pinfoarray[pindex]
                                                    ->CTRLval);
                                }

                                sprintf(string,
                                        " %02d:%02d:%"
                                        "02d.%03d",
                                        procinfoproc.pinfodisp[pindex]
                                            .createtime_hr,
                                        procinfoproc.pinfodisp[pindex]
                                            .createtime_min,
                                        procinfoproc.pinfodisp[pindex]
                                            .createtime_sec,
                                        (int) (0.000001 *
                                               (procinfoproc.pinfodisp[pindex]
                                                    .createtime_ns)));
                                TUI_printfw("%s", string);

                                sprintf(
                                    string,
                                    " %-*.*s",
                                    pstrlen_tmux,
                                    pstrlen_tmux,
                                    procinfoproc.pinfoarray[pindex]->tmuxname);
                                TUI_printfw("%s", string);

                                sprintf(
                                    string,
                                    " %- *.*ld",
                                    pstrlen_loopcnt,
                                    pstrlen_loopcnt,
                                    procinfoproc.pinfoarray[pindex]->loopcnt -
                                        procinfoproc
                                            .loopcntoffsetarray[pindex]);
                                //if(procinfoproc.pinfoarray[pindex]->loopcnt == procinfoproc.loopcntarray[pindex])
                                if (procinfoproc.pinfoarray[pindex]->loopcnt ==
                                    procinfoproc.loopcntarray[pindex])
                                {
                                    // loopcnt has not changed
                                    TUI_printfw("%s", string);
                                }
                                else
                                {
                                    // loopcnt has changed
                                    attron(COLOR_PAIR(2));
                                    TUI_printfw("%s", string);
                                    attroff(COLOR_PAIR(2));
                                }

                                procinfoproc.loopcntarray[pindex] =
                                    procinfoproc.pinfoarray[pindex]->loopcnt;

                                TUI_printfw(" | ");

                                sprintf(string,
                                        "%-*.*s",
                                        pstrlen_descr,
                                        pstrlen_descr,
                                        procinfoproc.pinfoarray[pindex]
                                            ->description);
                                TUI_printfw("%s", string);

                                TUI_printfw(" | ");

                                if ((procinfoproc.pinfoarray[pindex]
                                         ->loopstat == 4) ||
                                    (procinfoproc.pinfoarray[pindex]
                                         ->loopstat == 6)) // ERROR or CRASH
                                {
                                    attron(COLOR_PAIR(4));
                                }

                                sprintf(
                                    string,
                                    "%-*.*s",
                                    pstrlen_msg,
                                    pstrlen_msg,
                                    procinfoproc.pinfoarray[pindex]->statusmsg);
                                TUI_printfw("%s", string);

                                if ((procinfoproc.pinfoarray[pindex]
                                         ->loopstat == 4) ||
                                    (procinfoproc.pinfoarray[pindex]
                                         ->loopstat == 6)) // ERROR
                                {
                                    attroff(COLOR_PAIR(4));
                                }
                            }

                            // ================ DISPLAY MODE PROCCTRL_DISPLAYMODE_RESOURCES ==================
                            if (procinfoproc.DisplayMode ==
                                PROCCTRL_DISPLAYMODE_RESOURCES)
                            {
                                int cpu;

                                if (procinfoproc.psysinfostatus[pindex] == -1)
                                {
                                    sprintf(string,
                                            " no "
                                            "proces"
                                            "s "
                                            "info "
                                            "availa"
                                            "ble");
                                    TUI_printfw("%s", string);
                                    TUI_newline();
                                }
                                else
                                {

                                    int spindex; // sub process index, 0 for main
                                    for (spindex = 0;
                                         spindex <
                                         procinfoproc.pinfodisp[pindex]
                                             .NBsubprocesses;
                                         spindex++)
                                    {
                                        //int TID; // thread ID

                                        if (spindex > 0)
                                        {
                                            //TID = procinfoproc.pinfodisp[pindex].subprocPIDarray[spindex];
                                            sprintf(
                                                string,
                                                " %*.*s %-*.*d %-*.*s",
                                                pstrlen_status,
                                                pstrlen_status,
                                                "|---",
                                                pstrlen_pid,
                                                pstrlen_pid,
                                                procinfoproc.pinfodisp[pindex]
                                                    .subprocPIDarray[spindex],
                                                pstrlen_pname,
                                                pstrlen_pname,
                                                procinfoproc.pinfodisp[pindex]
                                                    .name);
                                            TUI_printfw("%s", string);
                                        }
                                        else
                                        {
                                            //TID = procinfoproc.pinfodisp[pindex].PID;
                                            procinfoproc.pinfodisp[pindex]
                                                .subprocPIDarray[0] =
                                                procinfoproc.pinfodisp[pindex]
                                                    .PID;
                                        }

                                        sprintf(string,
                                                " %2d",
                                                procinfoproc.pinfodisp[pindex]
                                                    .rt_priority);
                                        TUI_printfw("%s", string);

                                        sprintf(string,
                                                " %-*.*s",
                                                pstrlen_cset,
                                                pstrlen_cset,
                                                procinfoproc.pinfodisp[pindex]
                                                    .cpuset);
                                        TUI_printfw("%s", string);

                                        sprintf(string,
                                                " %2dx ",
                                                procinfoproc.pinfodisp[pindex]
                                                    .threads);
                                        TUI_printfw("%s", string);

                                        // Context Switches
#ifdef CMDPROC_CONTEXTSWITCH
                                        if (procinfoproc.pinfodisp[pindex]
                                                .ctxtsw_nonvoluntary_prev
                                                    [spindex] !=
                                            procinfoproc.pinfodisp[pindex]
                                                .ctxtsw_nonvoluntary[spindex])
                                        {
                                            attron(COLOR_PAIR(4));
                                        }
                                        else if (procinfoproc.pinfodisp[pindex]
                                                     .ctxtsw_voluntary_prev
                                                         [spindex] !=
                                                 procinfoproc.pinfodisp[pindex]
                                                     .ctxtsw_voluntary[spindex])
                                        {
                                            attron(COLOR_PAIR(3));
                                        }

                                        sprintf(
                                            string,
                                            " +%02ld +%02ld",
                                            labs(
                                                procinfoproc.pinfodisp[pindex]
                                                    .ctxtsw_voluntary[spindex] -
                                                procinfoproc.pinfodisp[pindex]
                                                    .ctxtsw_voluntary_prev
                                                        [spindex]) %
                                                100,
                                            labs(procinfoproc.pinfodisp[pindex]
                                                     .ctxtsw_nonvoluntary
                                                         [spindex] -
                                                 procinfoproc.pinfodisp[pindex]
                                                     .ctxtsw_nonvoluntary_prev
                                                         [spindex]) %
                                                100);
                                        TUI_printfw("%s", string);

                                        if (procinfoproc.pinfodisp[pindex]
                                                .ctxtsw_nonvoluntary_prev
                                                    [spindex] !=
                                            procinfoproc.pinfodisp[pindex]
                                                .ctxtsw_nonvoluntary[spindex])
                                        {
                                            attroff(COLOR_PAIR(4));
                                        }
                                        else if (procinfoproc.pinfodisp[pindex]
                                                     .ctxtsw_voluntary_prev
                                                         [spindex] !=
                                                 procinfoproc.pinfodisp[pindex]
                                                     .ctxtsw_voluntary[spindex])
                                        {
                                            attroff(COLOR_PAIR(3));
                                        }
                                        TUI_printfw(" ");
#endif

                                        // CPU use
#ifdef CMDPROC_CPUUSE
                                        int cpuColor = 0;

                                        //					if(pinfodisp[pindex].subprocCPUloadarray[spindex]>5.0)
                                        cpuColor = 1;
                                        if (procinfoproc.pinfodisp[pindex]
                                                .subprocCPUloadarray_timeaveraged
                                                    [spindex] > 10.0)
                                        {
                                            cpuColor = 2;
                                        }
                                        if (procinfoproc.pinfodisp[pindex]
                                                .subprocCPUloadarray_timeaveraged
                                                    [spindex] > 20.0)
                                        {
                                            cpuColor = 3;
                                        }
                                        if (procinfoproc.pinfodisp[pindex]
                                                .subprocCPUloadarray_timeaveraged
                                                    [spindex] > 40.0)
                                        {
                                            cpuColor = 4;
                                        }
                                        if (procinfoproc.pinfodisp[pindex]
                                                .subprocCPUloadarray_timeaveraged
                                                    [spindex] < 1.0)
                                        {
                                            cpuColor = 5;
                                        }

                                        // First group of cores (physical CPU 0)
                                        for (cpu = 0;
                                             cpu < procinfoproc.NBcpus /
                                                       procinfoproc.NBcpusocket;
                                             cpu++)
                                        {
                                            TUI_printfw("|");

                                            if (procinfoproc.CPUids[cpu] ==
                                                procinfoproc.pinfodisp[pindex]
                                                    .processorarray[spindex])
                                            {
                                                attron(COLOR_PAIR(cpuColor));
                                            }

                                            if (procinfoproc.pinfodisp[pindex]
                                                    .cpuOKarray[cpu] == 1)
                                            {
                                                TUI_printfw(
                                                    "%2d",
                                                    procinfoproc.CPUids[cpu]);
                                            }
                                            else
                                            {
                                                TUI_printfw("  ");
                                            }

                                            if (procinfoproc.CPUids[cpu] ==
                                                procinfoproc.pinfodisp[pindex]
                                                    .processorarray[spindex])
                                            {
                                                attroff(COLOR_PAIR(cpuColor));
                                            }
                                        }

                                        sprintf(string, "|    ");
                                        TUI_printfw("%s", string);

                                        // Second group of cores (physical CPU 0)
                                        for (cpu = procinfoproc.NBcpus /
                                                   procinfoproc.NBcpusocket;
                                             cpu < procinfoproc.NBcpus;
                                             cpu++)
                                        {
                                            TUI_printfw("|");

                                            if (procinfoproc.CPUids[cpu] ==
                                                procinfoproc.pinfodisp[pindex]
                                                    .processorarray[spindex])
                                            {
                                                attron(COLOR_PAIR(cpuColor));
                                            }

                                            if (procinfoproc.pinfodisp[pindex]
                                                    .cpuOKarray[cpu] == 1)
                                            {
                                                TUI_printfw(
                                                    "%2d",
                                                    procinfoproc.CPUids[cpu]);
                                            }
                                            else
                                            {
                                                TUI_printfw("  ");
                                            }

                                            if (procinfoproc.CPUids[cpu] ==
                                                procinfoproc.pinfodisp[pindex]
                                                    .processorarray[spindex])
                                            {
                                                attroff(COLOR_PAIR(cpuColor));
                                            }
                                        }
                                        TUI_printfw("| ");

                                        attron(COLOR_PAIR(cpuColor));
                                        sprintf(
                                            string,
                                            "%5.1f %6.2f",
                                            procinfoproc.pinfodisp[pindex]
                                                .subprocCPUloadarray[spindex],
                                            procinfoproc.pinfodisp[pindex]
                                                .subprocCPUloadarray_timeaveraged
                                                    [spindex]);
                                        TUI_printfw("%s", string);
                                        attroff(COLOR_PAIR(cpuColor));
#endif

                                        // Memory use
#ifdef CMDPROC_MEMUSE
                                        int memColor = 0;

                                        int kBcnt, MBcnt, GBcnt;

                                        kBcnt = procinfoproc.pinfodisp[pindex]
                                                    .VmRSSarray[spindex];
                                        MBcnt = kBcnt / 1024;
                                        kBcnt = kBcnt - MBcnt * 1024;

                                        GBcnt = MBcnt / 1024;
                                        MBcnt = MBcnt - GBcnt * 1024;

                                        //if(pinfodisp[pindex].subprocMEMloadarray[spindex]>0.5)
                                        memColor = 1;
                                        if (procinfoproc.pinfodisp[pindex]
                                                .VmRSSarray[spindex] >
                                            10 * 1024) // 10 MB
                                        {
                                            memColor = 2;
                                        }
                                        if (procinfoproc.pinfodisp[pindex]
                                                .VmRSSarray[spindex] >
                                            100 * 1024) // 100 MB
                                        {
                                            memColor = 3;
                                        }
                                        if (procinfoproc.pinfodisp[pindex]
                                                .VmRSSarray[spindex] >
                                            1024 * 1024) // 1 GB
                                        {
                                            memColor = 4;
                                        }
                                        if (procinfoproc.pinfodisp[pindex]
                                                .VmRSSarray[spindex] <
                                            1024) // 1 MB
                                        {
                                            memColor = 5;
                                        }

                                        TUI_printfw(" ");

                                        attron(COLOR_PAIR(memColor));
                                        if (GBcnt > 0)
                                        {
                                            sprintf(string, "%3d GB ", GBcnt);
                                            TUI_printfw("%s", string);
                                        }
                                        else
                                        {
                                            sprintf(string, "       ");
                                            TUI_printfw("%s", string);
                                        }

                                        if (MBcnt > 0)
                                        {
                                            sprintf(string, "%4d MB ", MBcnt);
                                            TUI_printfw("%s", string);
                                        }
                                        else
                                        {
                                            sprintf(string, "       ");
                                            TUI_printfw("%s", string);
                                        }

                                        if (kBcnt > 0)
                                        {
                                            sprintf(string, "%4d kB ", kBcnt);
                                            TUI_printfw("%s", string);
                                        }
                                        else
                                        {
                                            sprintf(string, "       ");
                                            TUI_printfw("%s", string);
                                        }
                                        attroff(COLOR_PAIR(memColor));
#endif

                                        if (pindex == pindexSelected)
                                        {
                                            attroff(A_REVERSE);
                                        }

                                        TUI_newline();
                                    }
                                    if (procinfoproc.pinfodisp[pindex]
                                            .NBsubprocesses == 0)
                                    {
                                        TUI_printfw(
                                            "  ERROR: "
                                            "procinfoproc.pinfodisp[pindex]."
                                            "NBsubprocesses = %d",
                                            (int) procinfoproc.pinfodisp[pindex]
                                                .NBsubprocesses);
                                        TUI_newline();

                                        if (pindex == pindexSelected)
                                        {
                                            attroff(A_REVERSE);
                                        }
                                    }
                                }
                            }

                            // ================ DISPLAY MODE PROCCTRL_DISPLAYMODE_TRIGGER ==================
                            if (procinfoproc.DisplayMode ==
                                PROCCTRL_DISPLAYMODE_TRIGGER)
                            {
                                TUI_printfw("%*d ",
                                            pstrlen_inode,
                                            procinfoproc.pinfoarray[pindex]
                                                ->triggerstreaminode);
                                TUI_printfw("%*s ",
                                            pstrlen_trigstreamname,
                                            procinfoproc.pinfoarray[pindex]
                                                ->triggerstreamname);

                                switch (procinfoproc.pinfoarray[pindex]
                                            ->triggermode)
                                {

                                case PROCESSINFO_TRIGGERMODE_IMMEDIATE:
                                    TUI_printfw(
                                        "%2d:"
                                        "IMME ",
                                        procinfoproc.pinfoarray[pindex]
                                            ->triggermode);
                                    break;

                                case PROCESSINFO_TRIGGERMODE_CNT0:
                                    TUI_printfw(
                                        "%2d:"
                                        "CNT0 ",
                                        procinfoproc.pinfoarray[pindex]
                                            ->triggermode);
                                    break;

                                case PROCESSINFO_TRIGGERMODE_CNT1:
                                    TUI_printfw(
                                        "%2d:"
                                        "CNT1 ",
                                        procinfoproc.pinfoarray[pindex]
                                            ->triggermode);
                                    break;

                                case PROCESSINFO_TRIGGERMODE_SEMAPHORE:
                                    TUI_printfw(
                                        "%2d:"
                                        "sem%"
                                        "01d ",
                                        procinfoproc.pinfoarray[pindex]
                                            ->triggermode,
                                        procinfoproc.pinfoarray[pindex]
                                            ->triggersem);

                                    break;

                                case PROCESSINFO_TRIGGERMODE_DELAY:
                                    TUI_printfw(
                                        "%2d:"
                                        "DELA ",
                                        procinfoproc.pinfoarray[pindex]
                                            ->triggermode);
                                    break;

                                default:
                                    TUI_printfw(
                                        "%2d:"
                                        "UNKN ",
                                        procinfoproc.pinfoarray[pindex]
                                            ->triggermode);
                                }

                                TUI_printfw("  %*d ",
                                            pstrlen_missedfr,
                                            procinfoproc.pinfoarray[pindex]
                                                ->triggermissedframe);
                                TUI_printfw("  %*llu ",
                                            pstrlen_missedfrc,
                                            procinfoproc.pinfoarray[pindex]
                                                ->triggermissedframe_cumul);
                                TUI_printfw("  %*llu ",
                                            pstrlen_tocnt,
                                            procinfoproc.pinfoarray[pindex]
                                                ->trigggertimeoutcnt);
                            }

                            // ================ DISPLAY MODE PROCCTRL_DISPLAYMODE_TIMING ==================
                            if (procinfoproc.DisplayMode ==
                                PROCCTRL_DISPLAYMODE_TIMING)
                            {
                                if (procinfoproc.pinfoarray[pindex]
                                        ->MeasureTiming == 1)
                                {
                                    TUI_printfw(" ON");
                                }
                                else
                                {
                                    TUI_printfw("OFF");
                                }

                                if (procinfoproc.pinfoarray[pindex]
                                        ->MeasureTiming == 1)
                                {
                                    long *dtiter_array;
                                    long *dtexec_array;
                                    //int dtindex;

                                    TUI_printfw(
                                        " %3d "
                                        "..%"
                                        "02ld "
                                        " ",
                                        procinfoproc.pinfoarray[pindex]
                                            ->timerindex,
                                        procinfoproc.pinfoarray[pindex]
                                                ->timingbuffercnt %
                                            100);

                                    // compute timing stat
                                    dtiter_array = (long *) malloc(
                                        sizeof(long) *
                                        (PROCESSINFO_NBtimer - 1));
                                    if (dtiter_array == NULL)
                                    {
                                        PRINT_ERROR(
                                            "malloc returns NULL pointer");
                                        abort();
                                    }
                                    dtexec_array = (long *) malloc(
                                        sizeof(long) *
                                        (PROCESSINFO_NBtimer - 1));
                                    if (dtexec_array == NULL)
                                    {
                                        PRINT_ERROR(
                                            "malloc returns NULL pointer");
                                        abort();
                                    }

                                    int tindex;
                                    //dtindex = 0;

                                    // we exclude the current timerindex, as timers may not all be written
                                    for (tindex = 0;
                                         tindex < PROCESSINFO_NBtimer - 1;
                                         tindex++)
                                    {
                                        int ti0, ti1;

                                        ti1 = procinfoproc.pinfoarray[pindex]
                                                  ->timerindex -
                                              tindex;
                                        ti0 = ti1 - 1;

                                        if (ti0 < 0)
                                        {
                                            ti0 += PROCESSINFO_NBtimer;
                                        }

                                        if (ti1 < 0)
                                        {
                                            ti1 += PROCESSINFO_NBtimer;
                                        }

                                        dtiter_array[tindex] =
                                            (procinfoproc.pinfoarray[pindex]
                                                 ->texecstart[ti1]
                                                 .tv_nsec -
                                             procinfoproc.pinfoarray[pindex]
                                                 ->texecstart[ti0]
                                                 .tv_nsec) +
                                            1000000000 *
                                                (procinfoproc
                                                     .pinfoarray[pindex]
                                                     ->texecstart[ti1]
                                                     .tv_sec -
                                                 procinfoproc
                                                     .pinfoarray[pindex]
                                                     ->texecstart[ti0]
                                                     .tv_sec);

                                        dtexec_array[tindex] =
                                            (procinfoproc.pinfoarray[pindex]
                                                 ->texecend[ti0]
                                                 .tv_nsec -
                                             procinfoproc.pinfoarray[pindex]
                                                 ->texecstart[ti0]
                                                 .tv_nsec) +
                                            1000000000 *
                                                (procinfoproc
                                                     .pinfoarray[pindex]
                                                     ->texecend[ti0]
                                                     .tv_sec -
                                                 procinfoproc
                                                     .pinfoarray[pindex]
                                                     ->texecstart[ti0]
                                                     .tv_sec);
                                    }

                                    quick_sort_long(dtiter_array,
                                                    PROCESSINFO_NBtimer - 1);
                                    quick_sort_long(dtexec_array,
                                                    PROCESSINFO_NBtimer - 1);

                                    int colorcode;

                                    if (procinfoproc.pinfoarray[pindex]
                                            ->dtiter_limit_enable != 0)
                                    {
                                        if (procinfoproc.pinfoarray[pindex]
                                                ->dtiter_limit_cnt == 0)
                                        {
                                            colorcode = COLOR_PAIR(2);
                                        }
                                        else
                                        {
                                            colorcode = COLOR_PAIR(4);
                                        }
                                        attron(colorcode);
                                    }
                                    TUI_printfw(
                                        "ITERli"
                                        "m "
                                        "%d/"
                                        "%5ld/"
                                        "%4ld",
                                        procinfoproc.pinfoarray[pindex]
                                            ->dtiter_limit_enable,
                                        (long) (0.001 *
                                                procinfoproc.pinfoarray[pindex]
                                                    ->dtiter_limit_value),
                                        procinfoproc.pinfoarray[pindex]
                                            ->dtiter_limit_cnt);
                                    if (procinfoproc.pinfoarray[pindex]
                                            ->dtiter_limit_enable != 0)
                                    {
                                        attroff(colorcode);
                                    }

                                    TUI_printfw("  ");

                                    if (procinfoproc.pinfoarray[pindex]
                                            ->dtexec_limit_enable != 0)
                                    {
                                        if (procinfoproc.pinfoarray[pindex]
                                                ->dtexec_limit_cnt == 0)
                                        {
                                            colorcode = COLOR_PAIR(2);
                                        }
                                        else
                                        {
                                            colorcode = COLOR_PAIR(4);
                                        }
                                        attron(colorcode);
                                    }

                                    TUI_printfw(
                                        "EXECli"
                                        "m "
                                        "%d/"
                                        "%5ld/"
                                        "%4ld ",
                                        procinfoproc.pinfoarray[pindex]
                                            ->dtexec_limit_enable,
                                        (long) (0.001 *
                                                procinfoproc.pinfoarray[pindex]
                                                    ->dtexec_limit_value),
                                        procinfoproc.pinfoarray[pindex]
                                            ->dtexec_limit_cnt);
                                    if (procinfoproc.pinfoarray[pindex]
                                            ->dtexec_limit_enable != 0)
                                    {
                                        attroff(colorcode);
                                    }

                                    float tval;

                                    tval =
                                        0.001 *
                                        dtiter_array[(
                                            long) (0.5 * PROCESSINFO_NBtimer)];
                                    procinfoproc.pinfoarray[pindex]
                                        ->dtmedian_iter_ns = dtiter_array[(
                                        long) (0.5 * PROCESSINFO_NBtimer)];
                                    if (tval > 9999.9)
                                    {
                                        TUI_printfw(" ITER    >10ms ");
                                    }
                                    else
                                    {
                                        TUI_printfw(" ITER %6.1fus ", tval);
                                    }

                                    tval = 0.001 * dtiter_array[0];
                                    if (tval > 9999.9)
                                    {
                                        TUI_printfw("[   >10ms -");
                                    }
                                    else
                                    {
                                        TUI_printfw("[%6.1fus -", tval);
                                    }

                                    tval =
                                        0.001 *
                                        dtiter_array[PROCESSINFO_NBtimer - 2];
                                    if (tval > 9999.9)
                                    {
                                        TUI_printfw("    >10ms ]");
                                    }
                                    else
                                    {
                                        TUI_printfw(" %6.1fus ]", tval);
                                    }

                                    tval =
                                        0.001 *
                                        dtexec_array[(
                                            long) (0.5 * PROCESSINFO_NBtimer)];
                                    procinfoproc.pinfoarray[pindex]
                                        ->dtmedian_exec_ns = dtexec_array[(
                                        long) (0.5 * PROCESSINFO_NBtimer)];
                                    if (tval > 9999.9)
                                    {
                                        TUI_printfw(" EXEC    >10ms ");
                                    }
                                    else
                                    {
                                        TUI_printfw(" EXEC %6.1fus ", tval);
                                    }

                                    tval = 0.001 * dtexec_array[0];
                                    if (tval > 9999.9)
                                    {
                                        TUI_printfw("[   >10ms -");
                                    }
                                    else
                                    {
                                        TUI_printfw("[%6.1fus -", tval);
                                    }

                                    tval =
                                        0.001 *
                                        dtexec_array[PROCESSINFO_NBtimer - 2];
                                    if (tval > 9999.9)
                                    {
                                        TUI_printfw("    >10ms ]");
                                    }
                                    else
                                    {
                                        TUI_printfw(" %6.1fus ]", tval);
                                    }

                                    //	TUI_printfw(" ITER %9.3fus [%9.3f - %9.3f] ", 0.001*dtiter_array[(long) (0.5*PROCESSINFO_NBtimer)], 0.001*dtiter_array[0], 0.001*dtiter_array[PROCESSINFO_NBtimer-2]);

                                    //	TUI_printfw(" EXEC %9.3fus [%9.3f - %9.3f] ", 0.001*dtexec_array[(long) (0.5*PROCESSINFO_NBtimer)], 0.001*dtexec_array[0], 0.001*dtexec_array[PROCESSINFO_NBtimer-2]);

                                    TUI_printfw(
                                        "  "
                                        "busy "
                                        "= "
                                        "%6.2f "
                                        "%%",
                                        100.0 *
                                            dtexec_array[(
                                                long) (0.5 *
                                                       PROCESSINFO_NBtimer)] /
                                            (dtiter_array[(
                                                 long) (0.5 *
                                                        PROCESSINFO_NBtimer)] +
                                             1));

                                    free(dtiter_array);
                                    free(dtexec_array);
                                }
                            }

                            if (pindex == pindexSelected)
                            {
                                attroff(A_REVERSE);
                            }
                        }
                    }

                    if ((procinfoproc.DisplayMode ==
                         PROCCTRL_DISPLAYMODE_CTRL) ||
                        (procinfoproc.DisplayMode ==
                         PROCCTRL_DISPLAYMODE_TRIGGER) ||
                        (procinfoproc.DisplayMode ==
                         PROCCTRL_DISPLAYMODE_TIMING))
                    {
                        TUI_newline();
                    }
                }
            }

            clock_gettime(CLOCK_REALTIME, &t06loop);

            DEBUG_TRACEPOINT(" ");

            clock_gettime(CLOCK_REALTIME, &t07loop);

            cnt++;

            clock_gettime(CLOCK_REALTIME, &t2loop);

            tdiff             = timespec_diff(t1loop, t2loop);
            double tdiffvloop = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

            TUI_newline();
            TUI_printfw("Loop time = %9.8f s  ( max rate = %7.2f Hz)",
                        tdiffvloop,
                        1.0 / tdiffvloop);
            TUI_newline();

            refresh();
        }

        if ((data.signal_TERM == 1) || (data.signal_INT == 1) ||
            (data.signal_ABRT == 1) || (data.signal_BUS == 1) ||
            (data.signal_SEGV == 1) || (data.signal_HUP == 1) ||
            (data.signal_PIPE == 1))
        {
            loopOK = 0;
        }
    }
    endwin();

    // Why did we exit ?

    printf("loopOK = 0 -> exit\n");

    if (Xexit == 1) // normal exit
    {
        printf("[%4d] User typed x -> exiting\n", __LINE__);
    }
    else if (data.signal_TERM == 1)
    {
        printf("[%4d] Received signal TERM\n", __LINE__);
    }
    else if (data.signal_INT == 1)
    {
        printf("[%4d] Received signal INT\n", __LINE__);
    }
    else if (data.signal_ABRT == 1)
    {
        printf("[%4d] Received signal ABRT\n", __LINE__);
    }
    else if (data.signal_BUS == 1)
    {
        printf("[%4d] Received signal BUS\n", __LINE__);
    }
    else if (data.signal_SEGV == 1)
    {
        printf("[%4d] Received signal SEGV\n", __LINE__);
    }
    else if (data.signal_HUP == 1)
    {
        printf("[%4d] Received signal HUP\n", __LINE__);
    }
    else if (data.signal_PIPE == 1)
    {
        printf("[%4d] Received signal PIPE\n", __LINE__);
    }

    procinfoproc.loop = 0;

    int *line;
    int  ret = -1;

    while (ret != 0)
    {
        ret = pthread_tryjoin_np(threadscan, (void **) &line);
        /*
                if(ret==EBUSY){
                    printf("Waiting for thread to complete - currently at line %d\n", procinfoproc.scandebugline);
                }
                */
        usleep(10000);
    }

    // cleanup
    for (pindex = 0; pindex < procinfoproc.NBpinfodisp; pindex++)
    {
        if (procinfoproc.pinfommapped[pindex] == 1)
        {
            processinfo_shm_close(procinfoproc.pinfoarray[pindex],
                                  procinfoproc.fdarray[pindex]);
            procinfoproc.pinfommapped[pindex] = 0;
        }
    }

    free(procinfoproc.pinfodisp);

    free(CPUsetList);

    fflush(stderr);
    dup2(backstderr, STDERR_FILENO);
    close(backstderr);

    return RETURN_SUCCESS;
}
