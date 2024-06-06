/**
 * @file    savefits.c
 * @brief   save FITS format files
 */

#include "CommandLineInterface/CLIcore.h"

#include <pthread.h>

// Handle old fitsios
#ifndef ULONGLONG_IMG
#define ULONGLONG_IMG (80)
#endif

#include "COREMOD_iofits_common.h"
#include "COREMOD_memory/COREMOD_memory.h"
#include "check_fitsio_status.h"
#include "file_exists.h"
#include "is_fits_file.h"

extern COREMOD_IOFITS_DATA COREMOD_iofits_data;

// variables local to this translation unit
static char *inimname;
static char *outfname;
static int  *outbitpix;
static char *inheader; // import header from this file



// CLI function arguments and parameters
static CLICMDARGDEF farg[] = {{
        CLIARG_IMG,
        ".in_name",
        "input image",
        "im1",
        CLIARG_VISIBLE_DEFAULT,
        (void **) &inimname,
        NULL
    },
    {
        CLIARG_STR,
        ".out_fname",
        "output FITS file name",
        "out.fits",
        CLIARG_VISIBLE_DEFAULT,
        (void **) &outfname,
        NULL
    },
    {
        // non-CLI parameter
        CLIARG_INT64,
        ".bitpix",
        "0: auto\n"
        "8 /(10) : (un)sig   8-b int\n"
        "16/(20) 32/(40) 64/(80) : (un)sig int\n"
        "-32/-64 : 32/64-b flt\n",
        "0",
        CLIARG_HIDDEN_DEFAULT,
        (void **) &outbitpix,
        NULL
    },
    {
        CLIARG_STR,
        ".in_header",
        "header import from this FITS file",
        "",
        CLIARG_HIDDEN_DEFAULT,
        (void **) &inheader,
        NULL
    }
};



// CLI function initialization data
static CLICMDDATA CLIcmddata =
{
    "saveFITS", "save image as FITS", CLICMD_FIELDS_DEFAULTS
};

// detailed help
static errno_t help_function()
{
    return RETURN_SUCCESS;
}








/**
 * @brief Write FITS file - wrapper kept for backwards compatibility before introducing
 * optional input image truncation
 *
 * @param inputimname       input image name
 * @param truncate          truncate input image to truncate first slices - -1 to ignore
 * @param outputFITSname    output FITS file name
 * @param outputbitpix      bitpix of output image. 0 if match input
 * @param importheaderfile  optional FITS file from which to read keywords
 * @param kwarray           optional keyword array. Set to NULL if unused
 * @param kwarraysize       number of keywords in optional keyword array. Set to 0 if unused.
 * @param FITSIOext         extension to pass instructions to FITSIO
 * @return errno_t
 */
errno_t saveFITS_opt_trunc(
    const char *__restrict inputimname,
    int truncate,
    const char *__restrict outputFITSname,
    int outputbitpix,
    const char *__restrict importheaderfile,
    IMAGE_KEYWORD *kwarray,
    int            kwarraysize,
    const char *__restrict FITSIOext
)
{


    DEBUG_TRACE_FSTART();
    DEBUG_TRACEPOINT("Saving image %s to file %s, bitpix = %d, slice truncation %d\n",
                     inputimname,
                     outputFITSname,
                     outputbitpix,
                     truncate);

    COREMOD_iofits_data.FITSIO_status = 0;

    // get PID to include in file name, so that file name is unique
    pthread_t self_id = pthread_self();

    char fnametmp[STRINGMAXLEN_FILENAME];

    DEBUG_TRACEPOINT(">> saving %s to %s\n", inputimname, outputFITSname);
    /*
        WRITE_FILENAME(fnametmp,
                       "_savefits_atomic_%s_%d_%ld.tmp.fits",
                       inputimname,
                       (int) getpid(),
                       (long) self_id);
    */

    WRITE_FILENAME(fnametmp,
                   "%s.%d.%ld.tmp",
                   outputFITSname,
                   (int) getpid(),
                   (long) self_id);
    DEBUG_TRACEPOINT("temp name : %s\n", fnametmp);

    // extended filename to pass instructions to FITSIO
    // For example, FITSIOext = [compress R 1,1,10000]
    char fnametmpext[STRINGMAXLEN_FILENAME];
    WRITE_FILENAME(fnametmpext,
                   "%s%s",
                   fnametmp,
                   FITSIOext
                  );

    IMGID imgin = mkIMGID_from_name(inputimname);
    resolveIMGID(&imgin, ERRMODE_WARN);
    if(imgin.ID == -1)
    {
        PRINT_WARNING("Image %s does not exist in memory - cannot save to FITS",
                      inputimname);
        DEBUG_TRACE_FEXIT();
        return RETURN_SUCCESS;
    }

    // data types
    uint8_t datatype       = imgin.md->datatype;
    char *datainptr = (char *) imgin.im->array.raw;

    // default
    int     bitpix = FLOAT_IMG;

    if(outputbitpix == BYTE_IMG || outputbitpix == SBYTE_IMG ||
            outputbitpix == SHORT_IMG || outputbitpix == USHORT_IMG ||
            outputbitpix == LONG_IMG || outputbitpix == ULONG_IMG ||
            outputbitpix == LONGLONG_IMG || outputbitpix == ULONGLONG_IMG ||
            outputbitpix == FLOAT_IMG || outputbitpix == DOUBLE_IMG)
    {
        bitpix = outputbitpix;
        DEBUG_TRACEPOINT("    output data type: %d\n", outputbitpix);
    }

    if(outputbitpix == 0)
    {
        int bitpix_from_datatype = ImageStreamIO_FITSIObitpix(datatype);
        if(bitpix_from_datatype != -1) // in-band error -1
        {
            bitpix = bitpix_from_datatype;
        }
    }

    DEBUG_TRACEPOINT("%d -> bitpix = %d\n", outputbitpix, bitpix);
    fflush(stdout);

    fitsfile *fptr;
    COREMOD_iofits_data.FITSIO_status = 0;
    DEBUG_TRACEPOINT("creating FITS file %s", fnametmpext);
    fits_create_file(&fptr, fnametmpext, &COREMOD_iofits_data.FITSIO_status);
    DEBUG_TRACEPOINT(" ");

    if(check_FITSIO_status(__FILE__, __func__, __LINE__, 1) != 0)
    {
        char errstring[200];
        if(access(fnametmp, F_OK) == 0)
        {
            snprintf(errstring, 200, "File already exists");
        }
        PRINT_ERROR("fits_create_file error %d on file %s %s",
                    COREMOD_iofits_data.FITSIO_status,
                    fnametmpext,
                    errstring);
        abort();
    }

    int  naxis = imgin.md->naxis;
    long nelements = 1;
    long naxesl[3];
    for(int i = 0; i < naxis; i++)
    {
        naxesl[i] = (long) imgin.md->size[i];
        if (truncate >= 0 && i == naxis -1) {
            naxesl[naxis - 1] = truncate;
            DEBUG_TRACEPOINT("-------------- TRUNCATE TO %d\n", truncate);
        }
        nelements *= naxesl[i];
        DEBUG_TRACEPOINT("-------------- SIZE %d = %ld\n", i, naxesl[i]);
    }

    //printf(">>>>>>>> bitpix = %d\n", bitpix);
    COREMOD_iofits_data.FITSIO_status = 0;
    fits_create_img(fptr,
                    bitpix,
                    naxis,
                    naxesl,
                    &COREMOD_iofits_data.FITSIO_status);
    if(check_FITSIO_status(__FILE__, __func__, __LINE__, 1) != 0)
    {
        PRINT_ERROR("fits_create_img error on file %s", fnametmpext);
        EXECUTE_SYSTEM_COMMAND("rm %s", fnametmp);
        FUNC_RETURN_FAILURE(" ");
    }


    DEBUG_TRACEPOINT("Adding optional header");
    // HEADER

    // Add FITS keywords from importheaderfile (optional)
    if(strlen(importheaderfile) > 0)
    {
        if(is_fits_file(importheaderfile) == 1)
        {
            DEBUG_TRACEPOINT("Importing FITS header entries from : %s\n",
                             importheaderfile);

            fitsfile *fptr_header = NULL;
            int       nkeys;

            char *header;

            COREMOD_iofits_data.FITSIO_status = 0;
            fits_open_file(&fptr_header,
                           importheaderfile,
                           READONLY,
                           &COREMOD_iofits_data.FITSIO_status);
            if(check_FITSIO_status(__FILE__, __func__, __LINE__, 1) != 0)
            {
                PRINT_ERROR("fits_open_file error on file %s",
                            importheaderfile);
                abort();
            }

            COREMOD_iofits_data.FITSIO_status = 0;
            fits_hdr2str(fptr_header,
                         1,
                         NULL,
                         0,
                         &header,
                         &nkeys,
                         &COREMOD_iofits_data.FITSIO_status);
            if(check_FITSIO_status(__FILE__, __func__, __LINE__, 1) != 0)
            {
                PRINT_ERROR("fits_hdr2str erroron file %s", importheaderfile);
                abort();
            }
            DEBUG_TRACEPOINT("imported %d header cards\n", nkeys);

            for (char* hptr = header; strncmp(hptr, "END ", 4) != 0; hptr += 80)
            {
                char fitscard[81];
                snprintf(fitscard, 81, "%.80s", hptr);

                // keywords to not overwrite
                int   writecard = 1;
                char *keyexcl[] = {"BITPIX",
                                   "NAXIS",
                                   "SIMPLE",
                                   "EXTEND",
                                   "BSCALE",
                                   "BZERO",
                                   0
                                  };
                int   ki        = 0;
                while(keyexcl[ki])
                {
                    if(strncmp(keyexcl[ki], fitscard, strlen(keyexcl[ki])) ==
                            0)
                    {
                        DEBUG_TRACEPOINT("EXCLUDING %s\n", fitscard);
                        writecard = 0;
                        break;
                    }
                    ki++;
                }

                if(writecard == 1)
                {
                    COREMOD_iofits_data.FITSIO_status = 0;
                    fits_write_record(fptr,
                                      fitscard,
                                      &COREMOD_iofits_data.FITSIO_status);
                    if(check_FITSIO_status(__FILE__, __func__, __LINE__, 1) !=
                            0)
                    {
                        PRINT_ERROR(
                            "fits_write_record error on "
                            "file %s",
                            importheaderfile);
                        abort();
                    }
                }
            }

            COREMOD_iofits_data.FITSIO_status = 0;
            fits_free_memory(header, &COREMOD_iofits_data.FITSIO_status);
            if(check_FITSIO_status(__FILE__, __func__, __LINE__, 1) != 0)
            {
                PRINT_ERROR("fits_free_memory error on file %s",
                            importheaderfile);
                abort();
            }

            COREMOD_iofits_data.FITSIO_status = 0;
            fits_close_file(fptr_header, &COREMOD_iofits_data.FITSIO_status);
            if(check_FITSIO_status(__FILE__, __func__, __LINE__, 1) != 0)
            {
                PRINT_ERROR("fits_close_file error on file %s",
                            importheaderfile);
                abort();
            }
        }
    }



    DEBUG_TRACEPOINT("Add FITS keywords from image keywords");
    // Skip keywords that start with a "_"
    // These are technical keywords that shouldn't be propagated to FITS.

    {
        int NBkw  = imgin.md->NBkw;
        int kwcnt = 0;
        DEBUG_TRACEPOINT("----------- NUMBER KW = %d ---------------\n", NBkw);
        for(int kw = 0; kw < NBkw; kw++)
        {
            if(imgin.im->kw[kw].name[0] == '_')
            {
                // Skip keywords that start with a "_"
                continue;
            }
            if(imgin.im->kw[kw].name[0] == ' ')
            {
                // Abort when we hit an empty keyword.
                break;
            }

            char tmpkwvalstr[81];
            COREMOD_iofits_data.FITSIO_status = 0;
            switch(imgin.im->kw[kw].type)
            {
                case 'L':
                    DEBUG_TRACEPOINT("writing keyword [L] %-8s= %20ld / %s\n",
                                     imgin.im->kw[kw].name,
                                     imgin.im->kw[kw].value.numl,
                                     imgin.im->kw[kw].comment);
                    fits_update_key(fptr,
                                    TLONG,
                                    imgin.im->kw[kw].name,
                                    &imgin.im->kw[kw].value.numl,
                                    imgin.im->kw[kw].comment,
                                    &COREMOD_iofits_data.FITSIO_status);
                    kwcnt++;
                    break;

                case 'D':
                    DEBUG_TRACEPOINT("writing keyword [D] %-8s= %20g / %s\n",
                                     imgin.im->kw[kw].name,
                                     imgin.im->kw[kw].value.numf,
                                     imgin.im->kw[kw].comment);
                    fits_update_key(fptr,
                                    TDOUBLE,
                                    imgin.im->kw[kw].name,
                                    &imgin.im->kw[kw].value.numf,
                                    imgin.im->kw[kw].comment,
                                    &COREMOD_iofits_data.FITSIO_status);
                    kwcnt++;
                    break;

                case 'S':
                    snprintf(tmpkwvalstr, 81, "'%s'", imgin.im->kw[kw].value.valstr);
                    DEBUG_TRACEPOINT("writing keyword [S] %-8s= %20s / %s\n",
                                     imgin.im->kw[kw].name,
                                     tmpkwvalstr,
                                     imgin.im->kw[kw].comment);
                    // MIND THAT WE ADDED SINGLE QUOTES JUST ABOVE IN snprintf!!
                    if((strncmp("'#TRUE#'", tmpkwvalstr, 8) == 0) ||
                            (strncmp("'#FALSE#'", tmpkwvalstr, 9) == 0))
                    {
                        // Booleans through magic strings
                        int tmpval_is_true =
                            strncmp("'#TRUE#'", tmpkwvalstr, 6) == 0;
                        fits_update_key(fptr,
                                        TLOGICAL,
                                        imgin.im->kw[kw].name,
                                        &tmpval_is_true,
                                        imgin.im->kw[kw].comment,
                                        &COREMOD_iofits_data.FITSIO_status);
                    }
                    else
                    {
                        // Normal string
                        fits_update_key(fptr,
                                        TSTRING,
                                        imgin.im->kw[kw].name,
                                        imgin.im->kw[kw].value.valstr,
                                        imgin.im->kw[kw].comment,
                                        &COREMOD_iofits_data.FITSIO_status);
                    }
                    kwcnt++;
                    break;
                case 'F':
                    snprintf(tmpkwfmtstr, 21, "%%-8s= %s / %%s", kwarray[kwi].format)
                    snprintf(tmpkwvalstr, 81, tmpkwfmtstr, 
                                     kwarray[kwi].name,
                                     kwarray[kwi].value.numf,
                                     kwarray[kwi].comment);
                    
                    DEBUG_TRACEPOINT("writing keyword [F] %-8s= %s %20g / %s\n",
                                     kwarray[kwi].name,
                                     kwarray[kwi].format,
                                     kwarray[kwi].value.numf,
                                     kwarray[kwi].comment);
                    
                    fits_update_card(fptr,
                                     kwarray[kwi].name,
                                     tmpkwvalstr,
                                     &COREMOD_iofits_data.FITSIO_status);

                default:
                    break;
            }

            if(check_FITSIO_status(__FILE__, __func__, __LINE__, 1) != 0)
            {
                PRINT_ERROR("fits_update_key error on keyword %s",
                            imgin.im->kw[kw].name);
                abort();
            }
        }
    }

    DEBUG_TRACEPOINT("add custom keywords");

    if((kwarraysize > 0) && (kwarray != NULL))
    {
        DEBUG_TRACEPOINT("----------- NUMBER CUSTOM KW = %d ---------------\n",
                         kwarraysize);
        for(int kwi = 0; kwi < kwarraysize; kwi++)
        {
            char tmpkwvalstr[81];
            char tmpkwfmtstr[21];
            COREMOD_iofits_data.FITSIO_status = 0;
            switch(kwarray[kwi].type)
            {
                case 'L':
                    fits_update_key(fptr,
                                    TLONG,
                                    kwarray[kwi].name,
                                    &kwarray[kwi].value.numl,
                                    kwarray[kwi].comment,
                                    &COREMOD_iofits_data.FITSIO_status);
                    break;

                case 'D':
                    DEBUG_TRACEPOINT("writing keyword [D] %-8s= %20g / %s\n",
                                     kwarray[kwi].name,
                                     kwarray[kwi].value.numf,
                                     kwarray[kwi].comment);
                    fits_update_key(fptr,
                                    TDOUBLE,
                                    kwarray[kwi].name,
                                    &kwarray[kwi].value.numf,
                                    kwarray[kwi].comment,
                                    &COREMOD_iofits_data.FITSIO_status);
                    break;
                case 'F':
                    snprintf(tmpkwfmtstr, 21, "%%-8s= %s / %%s", kwarray[kwi].format)
                    snprintf(tmpkwvalstr, 81, tmpkwfmtstr, 
                                     kwarray[kwi].name,
                                     kwarray[kwi].value.numf,
                                     kwarray[kwi].comment);
                    
                    DEBUG_TRACEPOINT("writing keyword [F] %-8s= %s %20g / %s\n",
                                     kwarray[kwi].name,
                                     kwarray[kwi].format,
                                     kwarray[kwi].value.numf,
                                     kwarray[kwi].comment);
                    
                    fits_update_card(fptr,
                                     kwarray[kwi].name,
                                     tmpkwvalstr,
                                     &COREMOD_iofits_data.FITSIO_status);
                case 'S':
                    snprintf(tmpkwvalstr, 81, "'%s'", kwarray[kwi].value.valstr);
                    DEBUG_TRACEPOINT("writing keyword [S] %-8s= %20s / %s\n",
                                     kwarray[kwi].name,
                                     tmpkwvalstr,
                                     kwarray[kwi].comment);
                    fits_update_key(fptr,
                                    TSTRING,
                                    kwarray[kwi].name,
                                    kwarray[kwi].value.valstr,
                                    kwarray[kwi].comment,
                                    &COREMOD_iofits_data.FITSIO_status);
                    break;
                default:
                    break;
            }

            if(check_FITSIO_status(__FILE__, __func__, __LINE__, 1) != 0)
            {
                PRINT_ERROR("fits_write_record error on keyword %s",
                            kwarray[kwi].name);
                abort();
            }
        }
    }

    // default (for floats, signed)
    double bscaleval_d = 1.0;
    int64_t bzeroval_l = 0L;
    uint64_t bzeroval_ul = 0UL;

    int     FITSIOdatatype = ImageStreamIO_FITSIOdatatype(datatype);
    switch(FITSIOdatatype)
    {
        // The standard specifies TSBYTE to be bzero-offset
        // and TBYTE to be bzero = 0
        case TSBYTE :
            // This would underflow in a uint64_t
            bzeroval_l  = -128L;
            break;

        case TUSHORT :
            bzeroval_ul  = 32768UL;
            break;

        case TUINT :
            bzeroval_ul  = 2147483648UL;
            break;

        case TULONG :
            // This would overflow in a int64_t. We need UL here.
            bzeroval_ul  = 4294967296UL * 2147483648UL;
            break;
    }

    fits_update_key(fptr,
                    TDOUBLE,
                    "BSCALE",
                    &bscaleval_d,
                    "Real=fits-value*BSCALE+BZERO",
                    &COREMOD_iofits_data.FITSIO_status);
    // We must write BZERO as some integer type
    // If float, the addition may cause an auto-cast into
    // a floating type on the file-reader side.
    fits_update_key(fptr,
                    FITSIOdatatype == TSBYTE ? TLONG : TULONG,
                    "BZERO",
                    FITSIOdatatype == TSBYTE ? (void *)&bzeroval_l : (void *)&bzeroval_ul,
                    "Real=fits-value*BSCALE+BZERO",
                    &COREMOD_iofits_data.FITSIO_status);

    long fpixel                       = 1;
    COREMOD_iofits_data.FITSIO_status = 0;
    fits_write_img(fptr,
                   FITSIOdatatype,
                   fpixel,
                   nelements,
                   datainptr,
                   &COREMOD_iofits_data.FITSIO_status);
    int errcode = check_FITSIO_status(__FILE__, __func__, __LINE__, 1);
    if(errcode != 0)
    {
        if(errcode == 412)
        {
            PRINT_WARNING("data trucated");
        }
        else
        {
            PRINT_ERROR("fits_write_img error %d on file %s",
                        errcode,
                        fnametmpext);
            EXECUTE_SYSTEM_COMMAND("rm %s", fnametmp);
            FUNC_RETURN_FAILURE(" ");
        }
    }

    DEBUG_TRACEPOINT(" ");

    COREMOD_iofits_data.FITSIO_status = 0;
    fits_write_date(fptr, &COREMOD_iofits_data.FITSIO_status);

    COREMOD_iofits_data.FITSIO_status = 0;
    fits_close_file(fptr, &COREMOD_iofits_data.FITSIO_status);
    if(check_FITSIO_status(__FILE__, __func__, __LINE__, 1) != 0)
    {
        PRINT_ERROR("fits_close_file error on file %s", fnametmpext);
        EXECUTE_SYSTEM_COMMAND("rm %s", fnametmp);
        FUNC_RETURN_FAILURE(" ");
    }


    EXECUTE_SYSTEM_COMMAND_ERRCHECK("mv %s %s", fnametmp, outputFITSname);

    DEBUG_TRACE_FEXIT();
    return RETURN_SUCCESS;
}



/**
 * @brief Write FITS file - wrapper kept for backwards compatibility before introducing
 * optional input image truncation
 *
 * @param inputimname       input image name
 * @param outputFITSname    output FITS file name
 * @param outputbitpix      bitpix of output image. 0 if match input
 * @param importheaderfile  optional FITS file from which to read keywords
 * @param kwarray           optional keyword array. Set to NULL if unused
 * @param kwarraysize       number of keywords in optional keyword array. Set to 0 if unused.
 * @return errno_t
 */
errno_t saveFITS(
    const char *__restrict inputimname,
    const char *__restrict outputFITSname,
    int outputbitpix,
    const char *__restrict importheaderfile,
    IMAGE_KEYWORD *kwarray,
    int            kwarraysize)
{
    return saveFITS_opt_trunc(
               inputimname,
               -1,
               outputFITSname,
               outputbitpix,
               importheaderfile,
               kwarray,
               kwarraysize,
               "");
}




errno_t saveall_fits(
    const char *__restrict savedirname
)
{
    DEBUG_TRACE_FSTART();
    char fname[STRINGMAXLEN_FULLFILENAME];

    EXECUTE_SYSTEM_COMMAND("mkdir -p %s", savedirname);

    for(long i = 0; i < data.NB_MAX_IMAGE; i++)
        if(data.image[i].used == 1)
        {

            WRITE_FULLFILENAME(fname,
                               "./%s/%s.fits",
                               savedirname,
                               data.image[i].name);
            saveFITS(data.image[i].name, fname, 0, "", NULL, 0);
        }

    DEBUG_TRACE_FEXIT();
    return RETURN_SUCCESS;
}



errno_t save_fits(
    const char *__restrict savedirname,
    const char *__restrict outputFITSname
)
{
    DEBUG_TRACE_FSTART();

    FUNC_CHECK_RETURN(saveFITS(savedirname, outputFITSname, 0, "", NULL, 0));

    DEBUG_TRACE_FEXIT();
    return RETURN_SUCCESS;
}


errno_t save_fl_fits(
    const char *__restrict savedirname,
    const char *__restrict outputFITSname
)
{
    DEBUG_TRACE_FSTART();

    FUNC_CHECK_RETURN(saveFITS(savedirname, outputFITSname, -32, "", NULL, 0));

    DEBUG_TRACE_FEXIT();
    return RETURN_SUCCESS;
}


errno_t save_db_fits(
    const char *__restrict savedirname,
    const char *__restrict outputFITSname
)
{
    DEBUG_TRACE_FSTART();

    FUNC_CHECK_RETURN(saveFITS(savedirname, outputFITSname, -64, "", NULL, 0));

    DEBUG_TRACE_FEXIT();
    return RETURN_SUCCESS;
}


static errno_t compute_function()
{
    DEBUG_TRACE_FSTART();

    INSERT_STD_PROCINFO_COMPUTEFUNC_START

    saveFITS(inimname, outfname, *outbitpix, inheader, NULL, 0);

    INSERT_STD_PROCINFO_COMPUTEFUNC_END

    DEBUG_TRACE_FEXIT();
    return RETURN_SUCCESS;
}

INSERT_STD_FPSCLIfunctions

// Register function in CLI
errno_t
CLIADDCMD_COREMOD_iofits__saveFITS()
{
    INSERT_STD_CLIREGISTERFUNC

    return RETURN_SUCCESS;
}
