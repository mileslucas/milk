#include <sys/stat.h>
#include <pthread.h>
#include <sys/mman.h> // mmap()

#include <ncurses.h>

#include "CLIcore.h"
#include <processtools.h>

#include "CommandLineInterface/timeutils.h"
#include "COREMOD_tools/COREMOD_tools.h"

#include "procCTRL_PIDcollectSystemInfo.h"
#include "procCTRL_GetCPUloads.h"

#include "processinfo/processinfo_procdirname.h"


#include "procCTRL_TUI.h"


extern PROCESSINFOLIST *pinfolist;



static FILE *fpdebuglog                = NULL;
static int   processinfo_scan_debuglog = 0;

#define PROCESSINFO_SCAN_DEBUGLOG(...)                                         \
    do                                                                         \
    {                                                                          \
        if (processinfo_scan_debuglog == 1)                                    \
        {                                                                      \
            fprintf(fpdebuglog, "%5d  : ", __LINE__);                          \
            fprintf(fpdebuglog, __VA_ARGS__);                                  \
        }                                                                      \
    } while (0)


/**
 * ## Purpose
 *
 * Scan function for processinfo CTRL
 *
 * ## Description
 *
 * Runs in background loop as thread initiated by processinfo_CTRL
 *
 *
 * ## ENV variables
 *
 *  MILK_DEBUGLOG_PROCESSINFO_SCAN : write debug log
 *
 */
void *processinfo_scan(void *thptr)
{
    PROCINFOPROC *pinfop;

    pinfop = (PROCINFOPROC *) thptr;

    pinfop->loopcnt = 0;

    // timing
    static int             firstIter = 1;
    static struct timespec t0;
    struct timespec        t1;
    double                 tdiffv;
    struct timespec        tdiff;

    char procdname[STRINGMAXLEN_DIRNAME];
    processinfo_procdirname(procdname);

    pinfop->scanPID = getpid();

    pinfop->scandebugline = __LINE__;


    // DEBUG LOG
    //
    if (getenv("MILK_DEBUGLOG_PROCESSINFO_SCAN"))
    {
        processinfo_scan_debuglog = 1;
    }

    if (processinfo_scan_debuglog == 1)
    {
        fpdebuglog = fopen("processinfo_scan.debuglog", "w");
    }

    PROCESSINFO_SCAN_DEBUGLOG("START\n");


    long loopcnt = 0;
    while (pinfop->loop == 1)
    {

        DEBUG_TRACEPOINT(" ");

        pinfop->scandebugline = __LINE__;

        DEBUG_TRACEPOINT(" ");

        // timing measurement
        clock_gettime(CLOCK_REALTIME, &t1);
        if (firstIter == 1)
        {
            tdiffv    = 0.1;
            firstIter = 0;
        }
        else
        {
            tdiff  = timespec_diff(t0, t1);
            tdiffv = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;
        }
        clock_gettime(CLOCK_REALTIME, &t0);
        pinfop->dtscan = tdiffv;

        DEBUG_TRACEPOINT(" ");

        pinfop->scandebugline = __LINE__;

        pinfop->SCANBLOCK_requested = 1; // request scan

        // wait for display to OK scan
        while (pinfop->SCANBLOCK_OK == 0)
        {
            usleep(100);
            pinfop->scandebugline = __LINE__;
            if (pinfop->loop == 0)
            {
                int line = __LINE__;
                pthread_exit(&line);
            }
        }


        pinfop->SCANBLOCK_requested = 0;
        // acknowledge that request has been granted
        //system("echo \"scanblock request write 0\" > steplog.sRQw0.txt");//TEST

        DEBUG_TRACEPOINT(" ");

        // LOAD / UPDATE process information
        // This step re-mmaps pinfo and rebuilds list, so we need to ensure it is run exclusively of the dislpay
        //
        pinfop->scandebugline = __LINE__;

        DEBUG_TRACEPOINT(" ");




        // pinfolistindex is index in PROCESSINFOLIST
        {
            long pinfolistindex = 0;
            while (pinfolistindex < PROCESSINFOLISTSIZE)
            {

                if (pinfop->loop == 1)
                {
                    DEBUG_TRACEPOINT("pinfolistindex %ld / %ld",
                                     pinfolistindex,
                                     PROCESSINFOLISTSIZE);


                    PROCESSINFO_SCAN_DEBUGLOG("pinfolistindex %ld / %d\n",
                                              pinfolistindex,
                                              PROCESSINFOLISTSIZE);

                    // shared memory file name
                    char        SM_fname[STRINGMAXLEN_FULLFILENAME];
                    struct stat file_stat;

                    pinfop->scandebugline = __LINE__;
                    pinfop->PIDarray[pinfolistindex] =
                        pinfolist->PIDarray[pinfolistindex];
                    DEBUG_TRACEPOINT("pinfolistindex %ld / %ld",
                                     pinfolistindex,
                                     PROCESSINFOLISTSIZE);

                    // SHOULD WE (RE)LOAD ?
                    if (pinfolist->active[pinfolistindex] == 0) // inactive
                    {
                        pinfop->updatearray[pinfolistindex] = 0;
                    }

                    if ((pinfolist->active[pinfolistindex] == 1) ||
                        (pinfolist->active[pinfolistindex] ==
                         2)) // active or crashed
                    {
                        pinfop->updatearray[pinfolistindex] = 1;
                    }
                    //    if(pinfolist->active[pindex] == 2) // mmap crashed, file may still be present
                    //        updatearray[pindex] = 1;

                    if (pinfolist->active[pinfolistindex] ==
                        3) // file has gone away
                    {
                        pinfop->updatearray[pinfolistindex] = 0;
                    }

                    DEBUG_TRACEPOINT(" ");

                    pinfop->scandebugline = __LINE__;




                    // check if process info file exists
                    //
                    WRITE_FULLFILENAME(
                        SM_fname,
                        "%s/proc.%s.%06d.shm",
                        procdname,
                        pinfolist->pnamearray[pinfolistindex],
                        (int) pinfolist->PIDarray[pinfolistindex]);

                    // DEBUGLOG
                    PROCESSINFO_SCAN_DEBUGLOG("     SM_fname %s\n", SM_fname);

                    // Does file exist ?
                    //
                    if (stat(SM_fname, &file_stat) == -1 && errno == ENOENT)
                    {
                        // if not, don't (re)load and remove from process info list
                        pinfolist->active[pinfolistindex]   = 0;
                        pinfop->updatearray[pinfolistindex] = 0;

                        PROCESSINFO_SCAN_DEBUGLOG(
                            "     process does not exist\n");
                    }



                    DEBUG_TRACEPOINT(" ");

                    if (pinfolist->active[pinfolistindex] == 1)
                    {
                        // check if process still exists
                        struct stat sts;
                        char        procfname[STRINGMAXLEN_FULLFILENAME];

                        WRITE_FULLFILENAME(
                            procfname,
                            "/proc/%d",
                            (int) pinfolist->PIDarray[pinfolistindex]);
                        if (stat(procfname, &sts) == -1 && errno == ENOENT)
                        {
                            // process doesn't exist -> flag as inactive
                            pinfolist->active[pinfolistindex] = 2;
                        }
                    }


                    PROCESSINFO_SCAN_DEBUGLOG(
                        "     pinfop->updatearray %ld : %d\n",
                        pinfolistindex,
                        pinfop->updatearray[pinfolistindex]);

                    DEBUG_TRACEPOINT(" ");

                    pinfop->scandebugline = __LINE__;

                    if (pinfop->updatearray[pinfolistindex] == 1)
                    {
                        // (RE)LOAD


                        DEBUG_TRACEPOINT(" ");

                        // if already mmapped, first unmap
                        if (pinfop->pinfommapped[pinfolistindex] == 1)
                        {
                            PROCESSINFO_SCAN_DEBUGLOG(
                                "     already mmapped, first unmap\n");
                            processinfo_shm_close(
                                pinfop->pinfoarray[pinfolistindex],
                                pinfop->fdarray[pinfolistindex]);
                            pinfop->pinfommapped[pinfolistindex] = 0;
                        }

                        DEBUG_TRACEPOINT(" ");

                        // COLLECT INFORMATION FROM PROCESSINFO FILE
                        pinfop->pinfoarray[pinfolistindex] =
                            processinfo_shm_link(
                                SM_fname,
                                &pinfop->fdarray[pinfolistindex]);

                        if (pinfop->pinfoarray[pinfolistindex] == MAP_FAILED)
                        {
                            PROCESSINFO_SCAN_DEBUGLOG("     MAP_FAILED\n");
                            close(pinfop->fdarray[pinfolistindex]);
                            endwin();
                            fprintf(stderr,
                                    "[%d] Error mapping file %s\n",
                                    __LINE__,
                                    SM_fname);
                            pinfolist->active[pinfolistindex]    = 3;
                            pinfop->pinfommapped[pinfolistindex] = 0;
                        }
                        else
                        {
                            PROCESSINFO_SCAN_DEBUGLOG("     shm linked\n");
                            pinfop->pinfommapped[pinfolistindex] = 1;
                            strncpy(pinfop->pinfodisp[pinfolistindex].name,
                                    pinfop->pinfoarray[pinfolistindex]->name,
                                    40 - 1);

                            struct tm *createtm;
                            createtm =
                                gmtime(&pinfop->pinfoarray[pinfolistindex]
                                            ->createtime.tv_sec);
                            pinfop->pinfodisp[pinfolistindex].createtime_hr =
                                createtm->tm_hour;
                            pinfop->pinfodisp[pinfolistindex].createtime_min =
                                createtm->tm_min;
                            pinfop->pinfodisp[pinfolistindex].createtime_sec =
                                createtm->tm_sec;
                            pinfop->pinfodisp[pinfolistindex].createtime_ns =
                                pinfop->pinfoarray[pinfolistindex]
                                    ->createtime.tv_nsec;

                            pinfop->pinfodisp[pinfolistindex].loopcnt =
                                pinfop->pinfoarray[pinfolistindex]->loopcnt;
                        }

                        DEBUG_TRACEPOINT(" ");

                        pinfop->pinfodisp[pinfolistindex].active =
                            pinfolist->active[pinfolistindex];
                        pinfop->pinfodisp[pinfolistindex].PID =
                            pinfolist->PIDarray[pinfolistindex];

                        pinfop->pinfodisp[pinfolistindex].updatecnt++;

                        // pinfop->updatearray[pindex] == 0; // by default, no need to re-connect


                        PROCESSINFO_SCAN_DEBUGLOG("     \n");

                        DEBUG_TRACEPOINT(" ");
                    }

                    pinfop->scandebugline = __LINE__;
                }
                else
                {
                    int line = __LINE__;
                    pthread_exit(&line);
                }

                pinfolistindex++;
            }
        }




        /** ### Build a time-sorted list of processes
          *
          *
          *
          */
        DEBUG_TRACEPOINT(" ");

        // Idenfity active processes
        //
        pinfop->NBpindexActive = 0;
        for (long pindex = 0; pindex < PROCESSINFOLISTSIZE; pindex++)
            if (pinfolist->active[pindex] != 0)
            {
                pinfop->pindexActive[pinfop->NBpindexActive] = pindex;
                pinfop->NBpindexActive++;
            }



        if (pinfop->NBpindexActive > 0)
        {

            double *timearray;
            long   *indexarray;
            timearray =
                (double *) malloc(sizeof(double) * pinfop->NBpindexActive);
            if (timearray == NULL)
            {
                PRINT_ERROR("malloc returns NULL pointer");
                abort();
            }
            indexarray = (long *) malloc(sizeof(long) * pinfop->NBpindexActive);
            if (indexarray == NULL)
            {
                PRINT_ERROR("malloc returns NULL pointer");
                abort();
            }


            int listcnt = 0;
            for (int index = 0; index < pinfop->NBpindexActive; index++)
            {
                long pindex = pinfop->pindexActive[index];
                if (pinfop->pinfommapped[pindex] == 1)
                {
                    indexarray[index] = pindex;
                    // minus sign for most recent first
                    //TUI_printfw("index  %ld  ->  pindex  %ld\n", index, pindex);
                    timearray[index] =
                        -1.0 * pinfop->pinfoarray[pindex]->createtime.tv_sec -
                        1.0e-9 * pinfop->pinfoarray[pindex]->createtime.tv_nsec;
                    listcnt++;
                }
            }
            DEBUG_TRACEPOINT(" ");



            pinfop->NBpindexActive = listcnt;



            if (pinfop->NBpindexActive > 0)
            {
                quick_sort2l_double(timearray,
                                    indexarray,
                                    pinfop->NBpindexActive);
            }


            for (int index = 0; index < pinfop->NBpindexActive; index++)
            {
                pinfop->sorted_pindex_time[index] = indexarray[index];
            }

            DEBUG_TRACEPOINT(" ");

            free(timearray);
            free(indexarray);
        }



        pinfop->scandebugline = __LINE__;

        pinfop->SCANBLOCK_OK = 0; // let display thread we're done
        //system("echo \"scanblock OK write 0\" > steplog.sOKw0.txt");//TEST

        pinfop->scandebugline = __LINE__;

        DEBUG_TRACEPOINT(" ");



        // only compute of displayed processes
        //
        if (pinfop->DisplayMode == PROCCTRL_DISPLAYMODE_RESOURCES)
        {
            DEBUG_TRACEPOINT(" ");
            pinfop->scandebugline = __LINE__;
            GetCPUloads(pinfop);
            pinfop->scandebugline = __LINE__;

            // collect required info for display
            {
                long pindex = 0;
                while (pindex < PROCESSINFOLISTSIZE)
                {
                    if (pinfop->loop == 1)
                    {
                        DEBUG_TRACEPOINT(" ");

                        if (pinfolist->active[pindex] != 0)
                        {
                            pinfop->scandebugline = __LINE__;

                            // pinfop->pinfodisp[pindex].NBsubprocesses should never be zero - should be at least 1 (for main process)
                            if (pinfop->pinfodisp[pindex].NBsubprocesses != 0)
                            {

                                int spindex; // sub process index, 0 for main

                                if (pinfop->psysinfostatus[pindex] != -1)
                                {
                                    for (spindex = 0;
                                         spindex < pinfop->pinfodisp[pindex]
                                                       .NBsubprocesses;
                                         spindex++)
                                    {
                                        // place info in subprocess arrays
                                        pinfop->pinfodisp[pindex]
                                            .sampletimearray_prev[spindex] =
                                            pinfop->pinfodisp[pindex]
                                                .sampletimearray[spindex];
                                        // Context Switches

                                        pinfop->pinfodisp[pindex]
                                            .ctxtsw_voluntary_prev[spindex] =
                                            pinfop->pinfodisp[pindex]
                                                .ctxtsw_voluntary[spindex];
                                        pinfop->pinfodisp[pindex]
                                            .ctxtsw_nonvoluntary_prev[spindex] =
                                            pinfop->pinfodisp[pindex]
                                                .ctxtsw_nonvoluntary[spindex];

                                        // CPU use
                                        pinfop->pinfodisp[pindex]
                                            .cpuloadcntarray_prev[spindex] =
                                            pinfop->pinfodisp[pindex]
                                                .cpuloadcntarray[spindex];
                                    }
                                }

                                pinfop->scandebugline = __LINE__;

                                pinfop->psysinfostatus[pindex] =
                                    PIDcollectSystemInfo(
                                        &(pinfop->pinfodisp[pindex]),
                                        0);

                                if (pinfop->psysinfostatus[pindex] != -1)
                                {
                                    char cpuliststring[200];
                                    char cpustring[16];

                                    for (spindex = 0;
                                         spindex < pinfop->pinfodisp[pindex]
                                                       .NBsubprocesses;
                                         spindex++)
                                    {
                                        if (pinfop->pinfodisp[pindex]
                                                .sampletimearray[spindex] !=
                                            pinfop->pinfodisp[pindex]
                                                .sampletimearray_prev[spindex])
                                        {
                                            // get CPU and MEM load

                                            // THIS DOES NOT WORK ON TICKLESS KERNEL
                                            pinfop->pinfodisp[pindex]
                                                .subprocCPUloadarray[spindex] =
                                                100.0 *
                                                ((1.0 *
                                                      pinfop->pinfodisp[pindex]
                                                          .cpuloadcntarray
                                                              [spindex] -
                                                  pinfop->pinfodisp[pindex]
                                                      .cpuloadcntarray_prev
                                                          [spindex]) /
                                                 sysconf(_SC_CLK_TCK)) /
                                                (pinfop->pinfodisp[pindex]
                                                     .sampletimearray[spindex] -
                                                 pinfop->pinfodisp[pindex]
                                                     .sampletimearray_prev
                                                         [spindex]);

                                            pinfop->pinfodisp[pindex]
                                                .subprocCPUloadarray_timeaveraged
                                                    [spindex] =
                                                0.9 *
                                                    pinfop->pinfodisp[pindex]
                                                        .subprocCPUloadarray_timeaveraged
                                                            [spindex] +
                                                0.1 * pinfop->pinfodisp[pindex]
                                                          .subprocCPUloadarray
                                                              [spindex];
                                        }
                                    }

                                    sprintf(
                                        cpuliststring,
                                        ",%s,",
                                        pinfop->pinfodisp[pindex].cpusallowed);

                                    pinfop->scandebugline = __LINE__;

                                    int cpu;
                                    for (cpu = 0; cpu < pinfop->NBcpus; cpu++)
                                    {
                                        int cpuOK = 0;
                                        int cpumin, cpumax;

                                        sprintf(cpustring,
                                                ",%d,",
                                                pinfop->CPUids[cpu]);
                                        if (strstr(cpuliststring, cpustring) !=
                                            NULL)
                                        {
                                            cpuOK = 1;
                                        }

                                        for (cpumin = 0;
                                             cpumin <= pinfop->CPUids[cpu];
                                             cpumin++)
                                            for (cpumax = pinfop->CPUids[cpu];
                                                 cpumax < pinfop->NBcpus;
                                                 cpumax++)
                                            {
                                                sprintf(cpustring,
                                                        ",%d-%d,",
                                                        cpumin,
                                                        cpumax);
                                                if (strstr(cpuliststring,
                                                           cpustring) != NULL)
                                                {
                                                    cpuOK = 1;
                                                }
                                            }
                                        pinfop->pinfodisp[pindex]
                                            .cpuOKarray[cpu] = cpuOK;
                                    }
                                }
                            }
                            pindex++;
                        }
                        pindex++;
                    }
                    else
                    {
                        DEBUG_TRACEPOINT(" ");
                        int line = __LINE__;
                        pthread_exit(&line);
                    }
                }
            }

            pinfop->scandebugline = __LINE__;

        } // end of DisplayMode PROCCTRL_DISPLAYMODE_RESOURCES

        DEBUG_TRACEPOINT(" ");

        pinfop->loopcnt++;

        int loopcntiter   = 0;
        int NBloopcntiter = 10;
        while ((pinfop->loop == 1) && (loopcntiter < NBloopcntiter))
        {
            usleep(pinfop->twaitus / NBloopcntiter);
            loopcntiter++;
        }

        if (pinfop->loop == 0)
        {
            int line = __LINE__;
            pthread_exit(&line);
        }
    }

    if (pinfop->loop == 0)
    {
        int line = __LINE__;
        pthread_exit(&line);
    }


    if (processinfo_scan_debuglog == 1)
    {
        fclose(fpdebuglog);
    }


    return NULL;
}
