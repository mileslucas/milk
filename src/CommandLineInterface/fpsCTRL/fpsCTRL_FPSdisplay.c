#include <math.h>

#include "CommandLineInterface/CLIcore.h"


#include "TUItools.h"

#include "print_nodeinfo.h"
#include "level0node_summary.h"


errno_t fpsCTRL_FPSdisplay(KEYWORD_TREE_NODE    *keywnode,
                           FPSCTRL_PROCESS_VARS *fpsCTRLvar)
{

    DEBUG_TRACEPOINT("Check that selected node is OK : %d",
                     fpsCTRLvar->nodeSelected);

    long       icnt = 0;
    static int nodechain[MAXNBLEVELS];


    if (fpsCTRLvar->NBfps > 0)
    {

        if (strlen(keywnode[fpsCTRLvar->nodeSelected].keywordfull) < 1)
        {
            // if not OK, set to last valid entry
            fpsCTRLvar->nodeSelected = 1;
            while (
                (strlen(keywnode[fpsCTRLvar->nodeSelected].keywordfull) < 1) &&
                (fpsCTRLvar->nodeSelected < NB_KEYWNODE_MAX))
            {
                fpsCTRLvar->nodeSelected++;
            }
        }

        DEBUG_TRACEPOINT("Get info from selected node");
        fpsCTRLvar->fpsindexSelected =
            keywnode[fpsCTRLvar->nodeSelected].fpsindex;
        fpsCTRLvar->pindexSelected = keywnode[fpsCTRLvar->nodeSelected].pindex;

        if (fpsCTRLvar->fpsCTRL_DisplayVerbose == 1)
        {
            fpsCTRLscreen_print_nodeinfo(data.fpsarray,
                                         keywnode,
                                         fpsCTRLvar->nodeSelected,
                                         fpsCTRLvar->fpsindexSelected,
                                         fpsCTRLvar->pindexSelected);
        }

        DEBUG_TRACEPOINT("trace back node chain");
        nodechain[fpsCTRLvar->currentlevel] = fpsCTRLvar->directorynodeSelected;


        {
            int level = fpsCTRLvar->currentlevel - 1;
            while (level > 0)
            {
                nodechain[level] = keywnode[nodechain[level + 1]].parent_index;
                level--;
            }
        }
        TUI_newline();
        nodechain[0] = 0; // root

        DEBUG_TRACEPOINT("Get number of lines to be displayed");
        fpsCTRLvar->currentlevel =
            keywnode[fpsCTRLvar->directorynodeSelected].keywordlevel;
        int GUIlineMax = keywnode[fpsCTRLvar->directorynodeSelected].NBchild;
        for (int level = 0; level < fpsCTRLvar->currentlevel; level++)
        {
            DEBUG_TRACEPOINT(
                "update GUIlineMax, the "
                "maximum number of lines");
            if (keywnode[nodechain[level]].NBchild > GUIlineMax)
            {
                GUIlineMax = keywnode[nodechain[level]].NBchild;
            }
        }


        if (fpsCTRLvar->fpsCTRL_DisplayVerbose == 1)
        {
            TUI_printfw(
                "[node %d] level = %d   [%d] NB child "
                "= %d",
                fpsCTRLvar->nodeSelected,
                fpsCTRLvar->currentlevel,
                fpsCTRLvar->directorynodeSelected,
                keywnode[fpsCTRLvar->directorynodeSelected].NBchild);

            TUI_printfw("   fps %d", fpsCTRLvar->fpsindexSelected);

            TUI_printfw("   pindex %d ",
                        keywnode[fpsCTRLvar->nodeSelected].pindex);

            TUI_newline();
        }

        /*      TUI_printfw("SELECTED DIR = %3d    SELECTED = %3d   GUIlineMax= %3d",
                     fpsCTRLvar.directorynodeSelected,
                     fpsCTRLvar.nodeSelected,
                     GUIlineMax);
            TUI_newline();
            TUI_newline();
              TUI_printfw("LINE: %d / %d",
                     fpsCTRLvar.GUIlineSelected[fpsCTRLvar.currentlevel],
                     keywnode[fpsCTRLvar.directorynodeSelected].NBchild);
                     TUI_newline();
                     TUI_newline();
        	*/

        //while(!(fps[fpsindexSelected].parray[pindexSelected].fpflag & FPFLAG_VISIBLE)) { // if invisible
        //		fpsCTRLvar.GUIlineSelected[fpsCTRLvar.currentlevel]++;
        //}

        //if(!(fps[fpsindex].parray[pindex].fpflag & FPFLAG_VISIBLE)) { // if invisible

        //              if( !(  fps[keywnode[fpsCTRLvar.nodeSelected].fpsindex].parray[keywnode[fpsCTRLvar.nodeSelected].pindex].fpflag & FPFLAG_VISIBLE)) { // if invisible
        //				if( !(  fps[fpsCTRLvar.fpsindexSelected].parray[fpsCTRLvar.pindexSelected].fpflag & FPFLAG_VISIBLE)) { // if invisible
        if (!(data.fpsarray[fpsCTRLvar->fpsindexSelected].parray[0].fpflag &
              FPFLAG_VISIBLE)) // if invisible
        {
            if (fpsCTRLvar->direction > 0)
            {
                fpsCTRLvar->GUIlineSelected[fpsCTRLvar->currentlevel]++;
            }
            else
            {
                fpsCTRLvar->GUIlineSelected[fpsCTRLvar->currentlevel]--;
            }
        }

        while (fpsCTRLvar->GUIlineSelected[fpsCTRLvar->currentlevel] >
               keywnode[fpsCTRLvar->directorynodeSelected].NBchild - 1)
        {
            fpsCTRLvar->GUIlineSelected[fpsCTRLvar->currentlevel]--;
        }

        int child_index[MAXNBLEVELS];
        for (int level = 0; level < MAXNBLEVELS; level++)
        {
            child_index[level] = 0;
        }


        for (int GUIline = 0; GUIline < GUIlineMax; GUIline++)
        {
            // GUIline is the line number on GUI display

            for (int level = 0; level < fpsCTRLvar->currentlevel; level++)
            {

                if (GUIline < keywnode[nodechain[level]].NBchild)
                {
                    int snode = 0; // selected node
                    int knodeindex;

                    knodeindex = keywnode[nodechain[level]].child[GUIline];

                    //TODO: adjust len to string
                    char pword[100];

                    if (level == 0)
                    {
                        DEBUG_TRACEPOINT(
                            "provid"
                            "e a "
                            "fps "
                            "status"
                            " summa"
                            "ry if "
                            "at "
                            "root");
                        int fpsindex = keywnode[knodeindex].fpsindex;
                        fpsCTRLscreen_level0node_summary(data.fpsarray,
                                                         fpsindex);
                    }

                    // toggle highlight if node is in the chain
                    int v1 = keywnode[nodechain[level]].child[GUIline];
                    int v2 = nodechain[level + 1];

                    // TEST
                    // TUI_printfw("[[%d %d %d %d]] ", level, v1, nodechain[level], v2);

                    if (v1 == v2)
                    {
                        snode = 1;
                        screenprint_setreverse();
                    }

                    // color node if directory
                    if (keywnode[knodeindex].leaf == 0)
                    {
                        screenprint_setcolor(5);
                    }

                    // print keyword
                    if (snprintf(
                            pword,
                            10,
                            "%s",
                            keywnode[keywnode[nodechain[level]].child[GUIline]]
                                .keyword[level]) < 0)
                    {
                        PRINT_ERROR(
                            "snprin"
                            "tf "
                            "erro"
                            "r");
                    }
                    TUI_printfw("%-10s ", pword);

                    if (keywnode[knodeindex].leaf == 0) // directory
                    {
                        screenprint_unsetcolor(5);
                    }

                    screenprint_setreverse();
                    if (snode == 1)
                    {
                        TUI_printfw(">");
                    }
                    else
                    {
                        TUI_printfw(" ");
                    }
                    screenprint_unsetreverse();
                    screenprint_setnormal();
                }
                else // blank space
                {
                    if (level == 0)
                    {
                        TUI_printfw(
                            "      "
                            "      "
                            "      "
                            "  ");
                    }
                    TUI_printfw("            ");
                }
            }

            int knodeindex;
            knodeindex = keywnode[fpsCTRLvar->directorynodeSelected]
                             .child[child_index[fpsCTRLvar->currentlevel]];
            if (knodeindex < fpsCTRLvar->NBkwn)
            {
                int fpsindex = keywnode[knodeindex].fpsindex;
                int pindex   = keywnode[knodeindex].pindex;

                if (child_index[fpsCTRLvar->currentlevel] >
                    keywnode[fpsCTRLvar->directorynodeSelected].NBchild - 1)
                {
                    child_index[fpsCTRLvar->currentlevel] =
                        keywnode[fpsCTRLvar->directorynodeSelected].NBchild - 1;
                }


                DEBUG_TRACEPOINT(" ");

                if (child_index[fpsCTRLvar->currentlevel] <
                    keywnode[fpsCTRLvar->directorynodeSelected].NBchild)
                {

                    if (fpsCTRLvar->currentlevel > 0)
                    {
                        screenprint_setreverse();
                        TUI_printfw(" ");
                        screenprint_unsetreverse();
                    }

                    DEBUG_TRACEPOINT(" ");

                    if (keywnode[knodeindex].leaf == 0)
                    { // If this is a directory
                        DEBUG_TRACEPOINT(" ");
                        if (fpsCTRLvar->currentlevel ==
                            0) // provide a status summary if at root
                        {
                            DEBUG_TRACEPOINT(" ");

                            fpsindex = keywnode[knodeindex].fpsindex;
                            pid_t pid;

                            pid = data.fpsarray[fpsindex].md->confpid;
                            if ((getpgid(pid) >= 0) && (pid > 0))
                            {
                                screenprint_setcolor(2);
                                TUI_printfw("%07d ", (int) pid);
                                screenprint_unsetcolor(2);
                            }
                            else // PID not active
                            {
                                if (data.fpsarray[fpsindex].md->status &
                                    FUNCTION_PARAMETER_STRUCT_STATUS_CMDCONF)
                                {
                                    // not clean exit
                                    screenprint_setcolor(4);
                                    TUI_printfw("%07d ", (int) pid);
                                    screenprint_unsetcolor(4);
                                }
                                else
                                {
                                    // All OK
                                    TUI_printfw("%07d ", (int) pid);
                                }
                            }

                            if (data.fpsarray[fpsindex].md->conferrcnt > 99)
                            {
                                screenprint_setcolor(4);
                                TUI_printfw("[XX]");
                                screenprint_unsetcolor(4);
                            }
                            if (data.fpsarray[fpsindex].md->conferrcnt > 0)
                            {
                                screenprint_setcolor(4);
                                TUI_printfw(
                                    "[%02d]",
                                    data.fpsarray[fpsindex].md->conferrcnt);
                                screenprint_unsetcolor(4);
                            }
                            if (data.fpsarray[fpsindex].md->conferrcnt == 0)
                            {
                                screenprint_setcolor(2);
                                TUI_printfw(
                                    "[%02d]",
                                    data.fpsarray[fpsindex].md->conferrcnt);
                                screenprint_unsetcolor(2);
                            }

                            pid = data.fpsarray[fpsindex].md->runpid;
                            if ((getpgid(pid) >= 0) && (pid > 0))
                            {
                                screenprint_setcolor(2);
                                TUI_printfw("%07d ", (int) pid);
                                screenprint_unsetcolor(2);
                            }
                            else
                            {
                                if (data.fpsarray[fpsindex].md->status &
                                    FUNCTION_PARAMETER_STRUCT_STATUS_CMDRUN)
                                {
                                    // not clean exit
                                    screenprint_setcolor(4);
                                    TUI_printfw("%07d ", (int) pid);
                                    screenprint_unsetcolor(4);
                                }
                                else
                                {
                                    // All OK
                                    TUI_printfw("%07d ", (int) pid);
                                }
                            }
                        }

                        if (GUIline ==
                            fpsCTRLvar
                                ->GUIlineSelected[fpsCTRLvar->currentlevel])
                        {
                            screenprint_setreverse();
                            fpsCTRLvar->nodeSelected = knodeindex;
                            fpsCTRLvar->fpsindexSelected =
                                keywnode[knodeindex].fpsindex;
                        }

                        if (child_index[fpsCTRLvar->currentlevel + 1] <
                            keywnode[fpsCTRLvar->directorynodeSelected].NBchild)
                        {
                            screenprint_setcolor(5);

                            TUI_printfw(
                                "%-16s",
                                keywnode[knodeindex]
                                    .keyword[keywnode[knodeindex].keywordlevel -
                                             1]);
                            screenprint_unsetcolor(5);

                            if (GUIline ==
                                fpsCTRLvar
                                    ->GUIlineSelected[fpsCTRLvar->currentlevel])
                            {
                                screenprint_unsetreverse();
                            }
                        }
                        else
                        {
                            TUI_printfw("%-16s", " ");
                        }

                        DEBUG_TRACEPOINT(" ");
                    }
                    else // If this is a parameter
                    {
                        DEBUG_TRACEPOINT(" ");
                        fpsindex = keywnode[knodeindex].fpsindex;
                        pindex   = keywnode[knodeindex].pindex;

                        DEBUG_TRACEPOINT(" ");
                        int isVISIBLE = 1;
                        if (!(data.fpsarray[fpsindex].parray[pindex].fpflag &
                              FPFLAG_VISIBLE)) // if invisible
                        {
                            isVISIBLE = 0;
                            screenprint_setdim();
                            screenprint_setblink();
                        }

                        //int kl;

                        if (GUIline ==
                            fpsCTRLvar
                                ->GUIlineSelected[fpsCTRLvar->currentlevel])
                        {
                            fpsCTRLvar->pindexSelected =
                                keywnode[knodeindex].pindex;
                            fpsCTRLvar->fpsindexSelected =
                                keywnode[knodeindex].fpsindex;
                            fpsCTRLvar->nodeSelected = knodeindex;

                            if (isVISIBLE == 1)
                            {
                                screenprint_setcolor(10);
                                screenprint_setbold();
                            }
                        }
                        DEBUG_TRACEPOINT(" ");

                        if (isVISIBLE == 1)
                        {
                            if (data.fpsarray[fpsindex].parray[pindex].fpflag &
                                FPFLAG_WRITESTATUS)
                            {
                                screenprint_setcolor(10);
                                screenprint_setblink();
                                TUI_printfw("W "); // writable
                                screenprint_unsetcolor(10);
                                screenprint_unsetblink();
                            }
                            else
                            {
                                screenprint_setcolor(4);
                                screenprint_setblink();
                                TUI_printfw("NW"); // non writable
                                screenprint_unsetcolor(4);
                                screenprint_unsetblink();
                            }
                        }
                        else
                        {
                            TUI_printfw("  ");
                        }

                        DEBUG_TRACEPOINT(" ");
                        //level = keywnode[knodeindex].keywordlevel;

                        if (GUIline ==
                            fpsCTRLvar
                                ->GUIlineSelected[fpsCTRLvar->currentlevel])
                        {
                            screenprint_setreverse();
                        }

                        TUI_printfw(
                            " %-"
                            "20s",
                            data.fpsarray[fpsindex]
                                .parray[pindex]
                                .keyword[keywnode[knodeindex].keywordlevel -
                                         1]);

                        if (GUIline ==
                            fpsCTRLvar
                                ->GUIlineSelected[fpsCTRLvar->currentlevel])
                        {
                            screenprint_unsetcolor(10);
                            screenprint_unsetreverse();
                        }
                        DEBUG_TRACEPOINT(" ");
                        TUI_printfw("   ");

                        // VALUE

                        int paramsync = 1; // parameter is synchronized

                        if (data.fpsarray[fpsindex].parray[pindex].fpflag &
                            FPFLAG_ERROR) // parameter setting error
                        {
                            if (isVISIBLE == 1)
                            {
                                screenprint_setcolor(4);
                            }
                        }

                        if (data.fpsarray[fpsindex].parray[pindex].type ==
                            FPTYPE_UNDEF)
                        {
                            TUI_printfw("  %s", "-undef-");
                        }

                        DEBUG_TRACEPOINT(" ");

                        // integer types
                        {
                            long val0    = 0;
                            long val3    = 0;
                            int  intflag = 0; // toggles to 1 if int type
                            switch (data.fpsarray[fpsindex].parray[pindex].type)
                            {
                            case FPTYPE_INT32:
                                val0 = data.fpsarray[fpsindex]
                                           .parray[pindex]
                                           .val.i32[0];
                                val3 = data.fpsarray[fpsindex]
                                           .parray[pindex]
                                           .val.i32[3];
                                intflag = 1;
                                break;
                            case FPTYPE_UINT32:
                                val0 = data.fpsarray[fpsindex]
                                           .parray[pindex]
                                           .val.ui32[0];
                                val3 = data.fpsarray[fpsindex]
                                           .parray[pindex]
                                           .val.ui32[3];
                                intflag = 1;
                                break;
                            case FPTYPE_INT64:
                                val0 = data.fpsarray[fpsindex]
                                           .parray[pindex]
                                           .val.i64[0];
                                val3 = data.fpsarray[fpsindex]
                                           .parray[pindex]
                                           .val.i64[3];
                                intflag = 1;
                                break;
                            case FPTYPE_UINT64:
                                val0 = data.fpsarray[fpsindex]
                                           .parray[pindex]
                                           .val.ui64[0];
                                val3 = data.fpsarray[fpsindex]
                                           .parray[pindex]
                                           .val.ui64[3];
                                intflag = 1;
                                break;
                            }

                            if (intflag == 1)
                            {
                                if (data.fpsarray[fpsindex]
                                        .parray[pindex]
                                        .fpflag &
                                    FPFLAG_FEEDBACK)
                                {
                                    // Check value feedback if available
                                    if (!(data.fpsarray[fpsindex]
                                              .parray[pindex]
                                              .fpflag &
                                          FPFLAG_ERROR))
                                        if (val0 != val3)
                                        {
                                            paramsync = 0;
                                        }
                                }

                                if (paramsync == 0)
                                {
                                    if (isVISIBLE == 1)
                                    {
                                        screenprint_setcolor(3);
                                    }
                                }

                                TUI_printfw("  %10d", (int) val0);

                                if (paramsync == 0)
                                {
                                    if (isVISIBLE == 1)
                                    {
                                        screenprint_unsetcolor(3);
                                    }
                                }
                            }
                        }

                        DEBUG_TRACEPOINT(" ");

                        // float types
                        {
                            double val0      = 0.0;
                            double val3      = 0.0;
                            int    floatflag = 0; // toggles to 1 if int type
                            switch (data.fpsarray[fpsindex].parray[pindex].type)
                            {
                            case FPTYPE_FLOAT32:
                                val0 = data.fpsarray[fpsindex]
                                           .parray[pindex]
                                           .val.f32[0];
                                val3 = data.fpsarray[fpsindex]
                                           .parray[pindex]
                                           .val.f32[3];
                                floatflag = 1;
                                break;
                            case FPTYPE_FLOAT64:
                                val0 = data.fpsarray[fpsindex]
                                           .parray[pindex]
                                           .val.f64[0];
                                val3 = data.fpsarray[fpsindex]
                                           .parray[pindex]
                                           .val.f64[3];
                                floatflag = 1;
                                break;
                            }

                            if (floatflag == 1)
                            {
                                if (data.fpsarray[fpsindex]
                                        .parray[pindex]
                                        .fpflag &
                                    FPFLAG_FEEDBACK) // Check value feedback if available
                                    if (!(data.fpsarray[fpsindex]
                                              .parray[pindex]
                                              .fpflag &
                                          FPFLAG_ERROR))
                                    {
                                        double absdiff;
                                        double abssum;
                                        double epsrel = 1.0e-6;
                                        double epsabs = 1.0e-10;

                                        absdiff = fabs(val0 - val3);
                                        abssum  = fabs(val0) + fabs(val3);

                                        if ((absdiff < epsrel * abssum) ||
                                            (absdiff < epsabs))
                                        {
                                            paramsync = 1;
                                        }
                                        else
                                        {
                                            paramsync = 0;
                                        }
                                    }

                                if (paramsync == 0)
                                {
                                    if (isVISIBLE == 1)
                                    {
                                        screenprint_setcolor(3);
                                    }
                                }

                                TUI_printfw("  %10f", (float) val0);

                                if (paramsync == 0)
                                {
                                    if (isVISIBLE == 1)
                                    {
                                        screenprint_unsetcolor(3);
                                    }
                                }
                            }
                        }

                        DEBUG_TRACEPOINT(" ");
                        if (data.fpsarray[fpsindex].parray[pindex].type ==
                            FPTYPE_PID)
                        {
                            if (data.fpsarray[fpsindex].parray[pindex].fpflag &
                                FPFLAG_FEEDBACK) // Check value feedback if available
                                if (!(data.fpsarray[fpsindex]
                                          .parray[pindex]
                                          .fpflag &
                                      FPFLAG_ERROR))
                                    if (data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .val.pid[0] !=
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .val.pid[1])
                                    {
                                        paramsync = 0;
                                    }

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_setcolor(3);
                                }
                            }

                            TUI_printfw("  %10d",
                                        (float) data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .val.pid[0]);

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_unsetcolor(3);
                                }
                            }

                            TUI_printfw("  %10d",
                                        (int) data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .val.pid[0]);
                        }

                        DEBUG_TRACEPOINT(" ");

                        if (data.fpsarray[fpsindex].parray[pindex].type ==
                            FPTYPE_TIMESPEC)
                        {
                            TUI_printfw("  %10f",
                                        1.0 * data.fpsarray[fpsindex]
                                                    .parray[pindex]
                                                    .val.ts[0]
                                                    .tv_sec +
                                            1e-9 * data.fpsarray[fpsindex]
                                                       .parray[pindex]
                                                       .val.ts[0]
                                                       .tv_nsec);
                        }

                        if (data.fpsarray[fpsindex].parray[pindex].type ==
                            FPTYPE_FILENAME)
                        {
                            if (data.fpsarray[fpsindex].parray[pindex].fpflag &
                                FPFLAG_FEEDBACK) // Check value feedback if available
                                if (!(data.fpsarray[fpsindex]
                                          .parray[pindex]
                                          .fpflag &
                                      FPFLAG_ERROR))
                                    if (strcmp(data.fpsarray[fpsindex]
                                                   .parray[pindex]
                                                   .val.string[0],
                                               data.fpsarray[fpsindex]
                                                   .parray[pindex]
                                                   .val.string[1]))
                                    {
                                        paramsync = 0;
                                    }

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_setcolor(3);
                                }
                            }

                            TUI_printfw("  %10s",
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .val.string[0]);

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_unsetcolor(3);
                                }
                            }
                        }
                        DEBUG_TRACEPOINT(" ");

                        if (data.fpsarray[fpsindex].parray[pindex].type ==
                            FPTYPE_FITSFILENAME)
                        {
                            if (data.fpsarray[fpsindex].parray[pindex].fpflag &
                                FPFLAG_FEEDBACK) // Check value feedback if available
                                if (!(data.fpsarray[fpsindex]
                                          .parray[pindex]
                                          .fpflag &
                                      FPFLAG_ERROR))
                                    if (strcmp(data.fpsarray[fpsindex]
                                                   .parray[pindex]
                                                   .val.string[0],
                                               data.fpsarray[fpsindex]
                                                   .parray[pindex]
                                                   .val.string[1]))
                                    {
                                        paramsync = 0;
                                    }

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_setcolor(3);
                                }
                            }

                            TUI_printfw("  %10s",
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .val.string[0]);

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_unsetcolor(3);
                                }
                            }
                        }
                        DEBUG_TRACEPOINT(" ");
                        if (data.fpsarray[fpsindex].parray[pindex].type ==
                            FPTYPE_EXECFILENAME)
                        {
                            if (data.fpsarray[fpsindex].parray[pindex].fpflag &
                                FPFLAG_FEEDBACK) // Check value feedback if available
                                if (!(data.fpsarray[fpsindex]
                                          .parray[pindex]
                                          .fpflag &
                                      FPFLAG_ERROR))
                                    if (strcmp(data.fpsarray[fpsindex]
                                                   .parray[pindex]
                                                   .val.string[0],
                                               data.fpsarray[fpsindex]
                                                   .parray[pindex]
                                                   .val.string[1]))
                                    {
                                        paramsync = 0;
                                    }

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_setcolor(3);
                                }
                            }

                            TUI_printfw("  %10s",
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .val.string[0]);

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_unsetcolor(3);
                                }
                            }
                        }
                        DEBUG_TRACEPOINT(" ");
                        if (data.fpsarray[fpsindex].parray[pindex].type ==
                            FPTYPE_DIRNAME)
                        {
                            if (data.fpsarray[fpsindex].parray[pindex].fpflag &
                                FPFLAG_FEEDBACK) // Check value feedback if available
                                if (!(data.fpsarray[fpsindex]
                                          .parray[pindex]
                                          .fpflag &
                                      FPFLAG_ERROR))
                                    if (strcmp(data.fpsarray[fpsindex]
                                                   .parray[pindex]
                                                   .val.string[0],
                                               data.fpsarray[fpsindex]
                                                   .parray[pindex]
                                                   .val.string[1]))
                                    {
                                        paramsync = 0;
                                    }

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_setcolor(3);
                                }
                            }

                            TUI_printfw("  %10s",
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .val.string[0]);

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_unsetcolor(3);
                                }
                            }
                        }

                        DEBUG_TRACEPOINT(" ");
                        if (data.fpsarray[fpsindex].parray[pindex].type ==
                            FPTYPE_STREAMNAME)
                        {
                            if (data.fpsarray[fpsindex].parray[pindex].fpflag &
                                FPFLAG_FEEDBACK) // Check value feedback if available
                                if (!(data.fpsarray[fpsindex]
                                          .parray[pindex]
                                          .fpflag &
                                      FPFLAG_ERROR))
                                    //  if(strcmp(fps[fpsindex].parray[pindex].val.string[0], fps[fpsindex].parray[pindex].val.string[1])) {
                                    //      paramsync = 0;
                                    //  }

                                    if (data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .info.stream.streamID > -1)
                                    {
                                        if (isVISIBLE == 1)
                                        {
                                            screenprint_setcolor(2);
                                        }
                                    }

                            TUI_printfw("[LOC %d]  %6s",
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .info.stream.stream_sourceLocation,
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .val.string[0]);

                            if (data.fpsarray[fpsindex]
                                    .parray[pindex]
                                    .info.stream.streamID > -1)
                            {

                                TUI_printfw(" [ %d",
                                            data.fpsarray[fpsindex]
                                                .parray[pindex]
                                                .info.stream.stream_xsize[0]);
                                if (data.fpsarray[fpsindex]
                                        .parray[pindex]
                                        .info.stream.stream_naxis[0] > 1)
                                {
                                    TUI_printfw(
                                        "x%d",
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .info.stream.stream_ysize[0]);
                                }
                                if (data.fpsarray[fpsindex]
                                        .parray[pindex]
                                        .info.stream.stream_naxis[0] > 2)
                                {
                                    TUI_printfw(
                                        "x%d",
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .info.stream.stream_zsize[0]);
                                }

                                TUI_printfw(" ]");
                                if (isVISIBLE == 1)
                                {
                                    screenprint_unsetcolor(2);
                                }
                            }
                        }
                        DEBUG_TRACEPOINT(" ");

                        if (data.fpsarray[fpsindex].parray[pindex].type ==
                            FPTYPE_STRING)
                        {
                            if (data.fpsarray[fpsindex].parray[pindex].fpflag &
                                FPFLAG_FEEDBACK) // Check value feedback if available
                                if (!(data.fpsarray[fpsindex]
                                          .parray[pindex]
                                          .fpflag &
                                      FPFLAG_ERROR))
                                    if (strcmp(data.fpsarray[fpsindex]
                                                   .parray[pindex]
                                                   .val.string[0],
                                               data.fpsarray[fpsindex]
                                                   .parray[pindex]
                                                   .val.string[1]))
                                    {
                                        paramsync = 0;
                                    }

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_setcolor(3);
                                }
                            }

                            TUI_printfw("  %10s",
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .val.string[0]);

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_unsetcolor(3);
                                }
                            }
                        }
                        DEBUG_TRACEPOINT(" ");

                        if (data.fpsarray[fpsindex].parray[pindex].type ==
                            FPTYPE_ONOFF)
                        {
                            if (data.fpsarray[fpsindex].parray[pindex].fpflag &
                                FPFLAG_ONOFF)
                            {
                                screenprint_setcolor(2);
                                TUI_printfw("  ON ");
                                screenprint_unsetcolor(2);
                                //TUI_printfw(" [ON  :%3s]", data.fpsarray[fpsindex].parray[pindex].val.string[0]);
                            }
                            else
                            {
                                screenprint_setcolor(1);
                                TUI_printfw(" OFF ");
                                screenprint_unsetcolor(1);
                                //TUI_printfw(" [OFF :%3s]", data.fpsarray[fpsindex].parray[pindex].val.string[1]);
                            }
                        }

                        if (data.fpsarray[fpsindex].parray[pindex].type ==
                            FPTYPE_FPSNAME)
                        {
                            if (data.fpsarray[fpsindex].parray[pindex].fpflag &
                                FPFLAG_FEEDBACK) // Check value feedback if available
                                if (!(data.fpsarray[fpsindex]
                                          .parray[pindex]
                                          .fpflag &
                                      FPFLAG_ERROR))
                                    if (strcmp(data.fpsarray[fpsindex]
                                                   .parray[pindex]
                                                   .val.string[0],
                                               data.fpsarray[fpsindex]
                                                   .parray[pindex]
                                                   .val.string[1]))
                                    {
                                        paramsync = 0;
                                    }

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_setcolor(2);
                                }
                            }
                            else
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_setcolor(4);
                                }
                            }

                            TUI_printfw(" %10s [%ld %ld %ld]",
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .val.string[0],
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .info.fps.FPSNBparamMAX,
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .info.fps.FPSNBparamActive,
                                        data.fpsarray[fpsindex]
                                            .parray[pindex]
                                            .info.fps.FPSNBparamUsed);

                            if (paramsync == 0)
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_unsetcolor(2);
                                }
                            }
                            else
                            {
                                if (isVISIBLE == 1)
                                {
                                    screenprint_unsetcolor(4);
                                }
                            }
                        }

                        DEBUG_TRACEPOINT(" ");

                        if (data.fpsarray[fpsindex].parray[pindex].fpflag &
                            FPFLAG_ERROR) // parameter setting error
                        {
                            if (isVISIBLE == 1)
                            {
                                screenprint_unsetcolor(4);
                            }
                        }

                        TUI_printfw(
                            "    "
                            "%s",
                            data.fpsarray[fpsindex].parray[pindex].description);

                        if (GUIline ==
                            fpsCTRLvar
                                ->GUIlineSelected[fpsCTRLvar->currentlevel])
                        {
                            if (isVISIBLE == 1)
                            {
                                screenprint_unsetbold();
                            }
                        }

                        if (isVISIBLE == 0)
                        {
                            screenprint_unsetblink();
                            screenprint_unsetdim();
                        }
                        // END LOOP
                    }

                    DEBUG_TRACEPOINT(" ");
                    icnt++;

                    for (int level = 0; level < MAXNBLEVELS; level++)
                    {
                        child_index[level]++;
                    }
                }
            }

            TUI_newline();
        }

        DEBUG_TRACEPOINT(" ");

        fpsCTRLvar->NBindex = icnt;

        if (fpsCTRLvar->GUIlineSelected[fpsCTRLvar->currentlevel] >
            fpsCTRLvar->NBindex - 1)
        {
            fpsCTRLvar->GUIlineSelected[fpsCTRLvar->currentlevel] =
                fpsCTRLvar->NBindex - 1;
        }

        DEBUG_TRACEPOINT(" ");

        TUI_newline();

        if (data.fpsarray[fpsCTRLvar->fpsindexSelected].md->status &
            FUNCTION_PARAMETER_STRUCT_STATUS_CHECKOK)
        {
            screenprint_setcolor(2);
            TUI_printfw(
                "[%ld] PARAMETERS OK - RUN "
                "function good to go",
                data.fpsarray[fpsCTRLvar->fpsindexSelected].md->msgcnt);
            screenprint_unsetcolor(2);
            TUI_newline();
        }
        else
        {
            int msgi;

            screenprint_setcolor(4);
            TUI_printfw(
                "[%ld] %d PARAMETER SETTINGS "
                "ERROR(s) :",
                data.fpsarray[fpsCTRLvar->fpsindexSelected].md->msgcnt,
                data.fpsarray[fpsCTRLvar->fpsindexSelected].md->conferrcnt);
            screenprint_unsetcolor(4);
            TUI_newline();

            screenprint_setbold();

            for (msgi = 0;
                 msgi < data.fpsarray[fpsCTRLvar->fpsindexSelected].md->msgcnt;
                 msgi++)
            {
                int pindex = data.fpsarray[fpsCTRLvar->fpsindexSelected]
                                 .md->msgpindex[msgi];
                TUI_printfw("%-40s %s",
                            data.fpsarray[fpsCTRLvar->fpsindexSelected]
                                .parray[pindex]
                                .keywordfull,
                            data.fpsarray[fpsCTRLvar->fpsindexSelected]
                                .md->message[msgi]);
                TUI_newline();
            }

            screenprint_unsetbold();
        }

        DEBUG_TRACEPOINT(" ");
    }

    return RETURN_SUCCESS;
}
