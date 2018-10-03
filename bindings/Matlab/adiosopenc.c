/*
 * ADIOS is freely available under the terms of the BSD license described
 * in the COPYING file in the top level directory of this source distribution.
 *
 * Copyright (c) 2008 - 2009.  UT-BATTELLE, LLC. All rights reserved.
 */

/*=================================================================
 * adiosopenc.c - Open an ADIOS file (Requires ADIOS 2.x)
 *
 * Input: File, Verbose
 *    File:     string (name) or int64 (handler)
 *    Verbose:  numeric (double)
 *
 * Output: Information structure
 *     Name        file path
 *     Handlers    object handlers to pass on to ADIOS functions
 *           FileHandler   int64 file handler
 *           GroupHandler  int64 IO group object handler
 *           ADIOSHandler  int64 ADIOS object handler

 *     Variables     structure array of variables
 *           Name          path of variable
 *           Type          Matlab type class of data
 *           Dims          Array of dimensions
 *           StepsStart    First step's index for this variable in file, always
 at least 1
 *           StepsCount    Number of steps for this variable in file, always at
 least 1
 *           GlobalMin     global minimum  of the variable (1-by-1 mxArray)
 *           GlobalMax     global maximum of the variable
 *
 *     Attributes  structure array of attributes
 *           Name          path of attribute
 *           Type          Matlab type class of data
 *           Value         attribute value (mxArray)
 *
 *
 *
 * Date: 2018/09/07
 * Author: Norbert Podhorszki <pnorbert@ornl.gov>
 *=================================================================*/

#include "adios2_c.h"
#include "mex.h"
#include <stdint.h> /* uint64_t and other int types */
#include <string.h> /* memcpy */

static int verbose = 0;

mxClassID adiostypeToMatlabClass(int adiostype, mxComplexity *complexity);
mxClassID adiostypestringToMatlabClass(const char *type,
                                       mxComplexity *complexity);
mxArray *valueToMatlabValue(const void *data, mxClassID mxtype,
                            mxComplexity complexFlag);
void errorCheck(int nlhs, int nrhs, const mxArray *prhs[]);
char *getString(const mxArray *mxstr);
static size_t *swap_order(size_t n, const size_t *array);

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    char *fname; /* file name */
    int status;
    char msg[512]; /* error messages from function calls */
    int32_t *int32p;

    adios2_adios *adiosobj;         /* ADIOS object pointer */
    adios2_io *group;               /* IO group object pointer */
    adios2_engine *fp;              /* File handler pointer  */
    size_t nvars, nattrs;           /* Number of variables and attributes */
    adios2_variable **adios_vars;   /* List of variable objects */
    adios2_attribute **adios_attrs; /* List of attribute objects */
    int mpi_comm_dummy; /* ADIOS read API needs an MPI communicator */
    void *data;         /* Attributes return their values */

    size_t vi, ai, i;                 /* loop variables for vars and attrs */
    mxArray *arr;                     /* temp array for constructions */
    mxArray *handlers, *vars, *attrs; /* struct arrays under top struct */
    mxClassID mxtype;                 /* matlab type (of vars and attrs) */
    mxComplexity complexFlag;

    /* Output structure definition */
    const char *top_field_names[] = {
        "Name", "Handlers", "Variables",
        "Attributes"}; /* top level struct fields */
    mwSize ntopfields = 4;
    mwSize top_struct_dims[] = {
        1, 1}; /* dimensions for top level struct array: 1-by-1 */
    int top_field_Name;
    int top_field_Handlers;
    int top_field_Variables;
    int top_field_Attributes;

    /* Handlers structure definition */
    const char *handler_field_names[] = {
        "FileHandler", "GroupHandler",
        "ADIOSHandler"}; /* handler level struct fields */
    mwSize nhandlerfields = 3;
    mwSize handler_struct_dims[] = {
        1, 1}; /* dimensions for handler level struct array: 1-by-1 */
    int handler_field_FileHandler;
    int handler_field_GroupHandler;
    int handler_field_ADIOSHandler;

    const char
        *var_field_names[] = {"Name",       "Type",       "Dims",
                              "StepsStart", "StepsCount", "GlobalMin",
                              "GlobalMax"}; /* variable level struct fields */
    mwSize nvarfields = 7;
    mwSize var_struct_dims[2]; /* dimensions for variable level struct array:
                                  1-by-sth */
    int var_field_Name;
    int var_field_Type;
    int var_field_Dims;
    int var_field_StepsStart;
    int var_field_StepsCount;
    int var_field_GlobalMin;
    int var_field_GlobalMax;

    const char *attr_field_names[] = {
        "Name", "Type", "Value"}; /* attribute level struct fields */
    mwSize nattrfields = 3;
    mwSize attr_struct_dims
        [2]; /* dimensions for attribute level struct array: 1-by-sth */
    int attr_field_Name;
    int attr_field_Type;
    int attr_field_Value;

    errorCheck(nlhs, nrhs, prhs);

    /*mexPrintf("nrhs=%d  nlhs=%d\n", nrhs, nlhs);*/

    /***********************/
    /* 0. get verbose parameter first */
    verbose = (int)mxGetScalar(prhs[1]);
    if (verbose)
        mexPrintf("Verbosity level: %d\n", verbose);

    /* 1. get file handler */
    if (mxIsChar(prhs[0]))
    {
        fname = getString((mxArray *)prhs[0]);
        if (verbose)
            mexPrintf("File name: \"%s\"\n", fname);
    }

    /********************************************************/
    /* Open ADIOS file now and get variables and attributes */
    adiosobj = adios2_init_nompi(adios2_debug_mode_on);
    group = adios2_declare_io(adiosobj, "matlabiogroup"); // name is arbitrary
    fp = adios2_open(group, fname, adios2_mode_read);
    if (fp == NULL)
    {
        mexErrMsgIdAndTxt("MATLAB:adiosopenc:open",
                          "Opening the file failed\n");
    }

    adios2_inquire_all_variables(group, &nvars, &adios_vars);
    adios2_inquire_all_attributes(group, &nattrs, &adios_attrs);
    if (verbose)
        mexPrintf("Opened file fp=%lld nvars=%zu nattrs=%zu\n", (int64_t)fp,
                  nvars, nattrs);

    /******************************/
    /* Create top level structure */
    if (verbose)
        mexPrintf("Create top struct array, 1-by-1\n");
    plhs[0] =
        mxCreateStructArray(2, top_struct_dims, ntopfields, top_field_names);
    top_field_Name = mxGetFieldNumber(plhs[0], "Name");
    top_field_Handlers = mxGetFieldNumber(plhs[0], "Handlers");
    top_field_Variables = mxGetFieldNumber(plhs[0], "Variables");
    top_field_Attributes = mxGetFieldNumber(plhs[0], "Attributes");
    mxSetFieldByNumber(plhs[0], 0, top_field_Name, mxCreateString(fname));

    /* Create top.Handlers structure array */
    if (verbose)
        mexPrintf("Create top.Handlers struct array, 1-by-1\n");
    handlers = mxCreateStructArray(2, handler_struct_dims, nhandlerfields,
                                   handler_field_names);
    mxSetFieldByNumber(plhs[0], 0, top_field_Handlers, handlers);

    /* Create top.Variables structure array */
    if (verbose)
        mexPrintf("Create top.Variables struct array, 1-by-%zu\n", nvars);
    var_struct_dims[0] = 1;
    var_struct_dims[1] = nvars;
    vars = mxCreateStructArray(2, var_struct_dims, nvarfields, var_field_names);
    mxSetFieldByNumber(plhs[0], 0, top_field_Variables, vars);

    /* Create top.Attributes structure array */
    if (verbose)
        mexPrintf("Create top.Attributes struct array, 1-by-%zu\n", nattrs);
    attr_struct_dims[0] = 1;
    attr_struct_dims[1] = nattrs;
    attrs =
        mxCreateStructArray(2, attr_struct_dims, nattrfields, attr_field_names);
    mxSetFieldByNumber(plhs[0], 0, top_field_Attributes, attrs);

    /****************************/
    /* Fill in Handlers structure */
    handler_field_FileHandler = mxGetFieldNumber(handlers, "FileHandler");
    handler_field_GroupHandler = mxGetFieldNumber(handlers, "GroupHandler");
    handler_field_ADIOSHandler = mxGetFieldNumber(handlers, "ADIOSHandler");
    arr = valueToMatlabValue(&fp, mxUINT64_CLASS, mxREAL);
    mxSetFieldByNumber(handlers, 0, handler_field_FileHandler, arr);
    arr = valueToMatlabValue(&group, mxUINT64_CLASS, mxREAL);
    mxSetFieldByNumber(handlers, 0, handler_field_GroupHandler, arr);
    arr = valueToMatlabValue(&adiosobj, mxUINT64_CLASS, mxREAL);
    mxSetFieldByNumber(handlers, 0, handler_field_ADIOSHandler, arr);

    /***********************************/
    /* Fill in top.Variables structure */
    var_field_Name = mxGetFieldNumber(vars, "Name");
    var_field_Type = mxGetFieldNumber(vars, "Type");
    var_field_Dims = mxGetFieldNumber(vars, "Dims");
    var_field_StepsStart = mxGetFieldNumber(vars, "StepsStart");
    var_field_StepsCount = mxGetFieldNumber(vars, "StepsCount");
    var_field_GlobalMin = mxGetFieldNumber(vars, "GlobalMin");
    var_field_GlobalMax = mxGetFieldNumber(vars, "GlobalMax");

    /************************************/
    /* Fill in top.Attributes structure */
    attr_field_Name = mxGetFieldNumber(attrs, "Name");
    attr_field_Type = mxGetFieldNumber(attrs, "Type");
    attr_field_Value = mxGetFieldNumber(attrs, "Value");

    /******************************/
    /* Add variables to the array */

    if (verbose > 1)
        mexPrintf("    Variables\n");
    for (vi = 0; vi < nvars; vi++)
    {
        const adios2_variable *avar = adios_vars[vi];
        /* field NAME */
        size_t namelen;
        const char *varname = adios2_variable_name(avar, &namelen);
        mxSetFieldByNumber(vars, vi, var_field_Name, mxCreateString(varname));
        /* field TYPE */
        const adios2_type adiostype = adios2_variable_type(avar);
        mxtype = adiostypeToMatlabClass(adiostype, &complexFlag);
        arr = mxCreateNumericMatrix(1, 1, mxtype, complexFlag);
        mxSetFieldByNumber(vars, vi, var_field_Type,
                           mxCreateString(mxGetClassName(arr)));
        mxDestroyArray(arr);
        /* field DIMS */
        size_t ndim = adios2_variable_ndims(avar);
        const size_t *dims = adios2_variable_shape(avar);
        /* Flip dimensions from ADIOS-read-api/C/row-major order to
         * Matlab/Fortran/column-major order */
        size_t *mxdims = swap_order(ndim, dims);
        if (verbose > 1)
        {
            mexPrintf("      %s: ndims=%d, adios type=%d, dimensions [",
                      varname, ndim, adiostype);
            for (i = 0; i < ndim; i++)
                mexPrintf("%lld ", mxdims[i]);
            mexPrintf("]\n");
        }
        if (ndim > 0)
        {
            arr = mxCreateNumericMatrix(1, ndim, mxINT32_CLASS, mxREAL);
            int32p = (int32_t *)mxGetData(arr);
            for (i = 0; i < ndim; i++)
            {
                int32p[i] = (int32_t)mxdims[i];
            }
            free(mxdims);
        }
        else
        {
            arr = mxCreateNumericMatrix(0, 0, mxINT32_CLASS, mxREAL);
        }
        mxSetFieldByNumber(vars, vi, var_field_Dims, arr);

        /* field STEPSSTART */
        size_t stepsStart = adios2_variable_steps_start(avar);
        arr = valueToMatlabValue((void *)(&stepsStart), mxINT64_CLASS, mxREAL);
        mxSetFieldByNumber(vars, vi, var_field_StepsStart, arr);

        /* field STEPSCOUNT */
        size_t stepsCount = adios2_variable_steps(avar);
        arr = valueToMatlabValue((void *)(&stepsCount), mxINT64_CLASS, mxREAL);
        mxSetFieldByNumber(vars, vi, var_field_StepsCount, arr);

        /* field GLOBALMIN */
        // FIXME arr = valueToMatlabValue(vinfo->gmin, mxtype, complexFlag);
        double fakemin = 0.0;
        arr = valueToMatlabValue(&fakemin, mxDOUBLE_CLASS, complexFlag);
        mxSetFieldByNumber(vars, vi, var_field_GlobalMin, arr);

        /* field GLOBALMAX */
        // FIXME arr = valueToMatlabValue(vinfo->gmax, mxtype, complexFlag);
        arr = valueToMatlabValue(&fakemin, mxDOUBLE_CLASS, complexFlag);
        mxSetFieldByNumber(vars, vi, var_field_GlobalMax, arr);
    }

    /******************************/
    /* Add attributes to the group */

    if (verbose > 1)
        mexPrintf("    Attributes\n");
    for (ai = 0; ai < nattrs; ai++)
    {
        const adios2_attribute *aa = adios_attrs[ai];
        /* field NAME */
        size_t namelen;
        const char *attrname = adios2_attribute_name(aa, &namelen);
        mxSetFieldByNumber(attrs, ai, attr_field_Name,
                           mxCreateString(attrname));
        /* field TYPE */
        size_t typelen;
        const char *atype = adios2_attribute_type(aa, &typelen);
        mxtype = adiostypestringToMatlabClass(atype, &complexFlag);
        // mxtype = adiostypeToMatlabClass(adiostype, &complexFlag);
        arr = mxCreateNumericMatrix(1, 1, mxtype, complexFlag);
        mxSetFieldByNumber(attrs, ai, attr_field_Type,
                           mxCreateString(mxGetClassName(arr)));
        mxDestroyArray(arr);
        /* field VALUE */
        size_t asize;
        const void *data = adios2_attribute_data(aa, &asize);
        arr = valueToMatlabValue(data, mxtype, complexFlag);
        mxSetFieldByNumber(attrs, ai, attr_field_Value, arr);
        if (verbose > 1)
            mexPrintf("      %s: adios type=%s size=%zu\n", attrname, atype,
                      asize);
    }

    if (verbose)
        mexPrintf("Lock the function call to not loose the ADIOS handlers\n");
    // mexLock();
    if (verbose)
        mexPrintf("return from adiosopenc\n");
}

mxArray *valueToMatlabValue(const void *data, mxClassID mxtype,
                            mxComplexity complexFlag)
{
    /* copies values in all cases, so one can free(data) later */
    mxArray *arr;
    if (data == NULL)
    {
        arr = mxCreateString("undefined");
    }
    else if (mxtype == mxCHAR_CLASS)
    {
        arr = mxCreateString((const char *)data);
    }
    else if (complexFlag == mxCOMPLEX)
    {
        arr = mxCreateDoubleMatrix(1, 1, mxCOMPLEX);
        if (mxtype == mxSINGLE_CLASS)
        {
            *(float *)mxGetPr(arr) = ((const float *)data)[0];
            *(float *)mxGetPi(arr) = ((const float *)data)[1];
        }
        else
        {
            *(double *)mxGetPr(arr) = ((const double *)data)[0];
            *(double *)mxGetPi(arr) = ((const double *)data)[1];
        }
    }
    else
    {
        arr = mxCreateNumericMatrix(1, 1, mxtype, mxREAL);
        memcpy(mxGetData(arr), data, mxGetElementSize(arr));
    }
    return arr;
}

void errorCheck(int nlhs, int nrhs, const mxArray *prhs[])
{
    /* Assume that we are called from adiosread.m which checks the arguments
     * already */
    /* Check for proper number of arguments. */

    if (nrhs != 2)
    {
        mexErrMsgIdAndTxt(
            "MATLAB:adiosopenc:rhs",
            "This function needs exactly 2 arguments: File, Verbose");
    }

    if (!mxIsChar(prhs[0]))
    {
        mexErrMsgIdAndTxt("MATLAB:adiosopenc:rhs",
                          "First arg must be a string.");
    }

    if (!mxIsNumeric(prhs[1]))
    {
        mexErrMsgIdAndTxt("MATLAB:adiosopenc:rhs",
                          "Second arg must be a number.");
    }

    if (nlhs > 1)
    {
        mexErrMsgIdAndTxt("MATLAB:adiosopenc:lhs",
                          "Too many output arguments.");
    }
}

/** Make a C char* string from a Matlab string */
char *getString(const mxArray *mxstr)
{
    mwSize buflen;
    char *str;
    /* Allocate enough memory to hold the converted string. */
    buflen = mxGetNumberOfElements(mxstr) + 1;
    str = mxCalloc(buflen, sizeof(char));
    /* Copy the string data from string_array_ptr and place it into buf. */
    if (mxGetString(mxstr, str, buflen) != 0)
        mexErrMsgTxt("Could not convert string data from the file name.");
    return str;
}

/** return the appropriate class for an adios type (and complexity too) */
mxClassID adiostypeToMatlabClass(adios2_type adiostype,
                                 mxComplexity *complexity)
{
    *complexity = mxREAL;
    switch (adiostype)
    {
    case adios2_type_unsigned_char:
    case adios2_type_uint8_t:
        return mxUINT8_CLASS;
    case adios2_type_char:
    case adios2_type_signed_char:
    case adios2_type_int8_t:
        return mxINT8_CLASS;

    case adios2_type_string:
    case adios2_type_string_array:
        return mxCHAR_CLASS;

    case adios2_type_unsigned_short:
    case adios2_type_uint16_t:
        return mxUINT16_CLASS;
    case adios2_type_short:
    case adios2_type_int16_t:
        return mxINT16_CLASS;

    case adios2_type_unsigned_int:
    case adios2_type_uint32_t:
        return mxUINT32_CLASS;
    case adios2_type_int:
    case adios2_type_int32_t:
        return mxINT32_CLASS;

    case adios2_type_unsigned_long_int:
        if (sizeof(long int) == 4)
            return mxUINT32_CLASS;
        else
            return mxUINT64_CLASS;
    case adios2_type_long_int:
        if (sizeof(long int) == 4)
            return mxINT32_CLASS;
        else
            return mxINT64_CLASS;

    case adios2_type_unsigned_long_long_int:
    case adios2_type_uint64_t:
        return mxUINT64_CLASS;
    case adios2_type_long_long_int:
    case adios2_type_int64_t:
        return mxINT64_CLASS;

    case adios2_type_float:
        return mxSINGLE_CLASS;
    case adios2_type_double:
        return mxDOUBLE_CLASS;

    case adios2_type_float_complex: /* 8 bytes */
        *complexity = mxCOMPLEX;
        return mxSINGLE_CLASS;
    case adios2_type_double_complex: /*  16 bytes */
        *complexity = mxCOMPLEX;
        return mxDOUBLE_CLASS;

    default:
        mexErrMsgIdAndTxt("MATLAB:adiosopenc.c:dimensionTooLarge",
                          "Adios type id=%d not supported in matlab.\n",
                          adiostype);
        break;
    }
    return 0; /* just to avoid warnings. never executed */
}

/** return the appropriate class for an adios type (and complexity too) */
mxClassID adiostypestringToMatlabClass(const char *type,
                                       mxComplexity *complexity)
{
    *complexity = mxREAL;
    if (!strcmp(type, "char"))
        return mxINT8_CLASS;
    else if (!strcmp(type, "unsigned char"))
        return mxUINT8_CLASS;
    else if (!strcmp(type, "short"))
        return mxINT16_CLASS;
    else if (!strcmp(type, "unsigned short"))
        return mxUINT16_CLASS;
    else if (!strcmp(type, "int"))
        return mxINT32_CLASS;
    else if (!strcmp(type, "unsigned int"))
        return mxUINT32_CLASS;
    else if (!strcmp(type, "long int"))
    {
        if (sizeof(long int) == 4)
            return mxINT32_CLASS;
        else
            return mxINT64_CLASS;
    }
    else if (!strcmp(type, "unsigned long int"))
    {
        if (sizeof(long int) == 4)
            return mxUINT32_CLASS;
        else
            return mxUINT64_CLASS;
    }
    else if (!strcmp(type, "long long int"))
        return mxINT64_CLASS;
    else if (!strcmp(type, "unsigned long long int"))
        return mxUINT64_CLASS;
    else if (!strcmp(type, "float"))
        return mxSINGLE_CLASS;
    else if (!strcmp(type, "double"))
        return mxDOUBLE_CLASS;
    else if (!strcmp(type, "float complex"))
    {
        *complexity = mxCOMPLEX;
        return mxSINGLE_CLASS;
    }
    else if (!strcmp(type, "double complex"))
    {
        *complexity = mxCOMPLEX;
        return mxDOUBLE_CLASS;
    }
    else if (!strcmp(type, "string"))
        return mxCHAR_CLASS;
    else if (!strcmp(type, "string array"))
        return mxCHAR_CLASS;

    return 0; /* just to avoid warnings. never executed */
}

/* Reverse the order in an array in place.
   use swapping from Matlab/Fortran/column-major order to
   ADIOS-read-api/C/row-major order and back
*/
static size_t *swap_order(size_t n, const size_t *array)
{
    size_t i;
    size_t *out = (size_t *)malloc(sizeof(size_t) * n);
    size_t *o = out;
    const size_t *a = &array[n - 1];
    for (i = 0; i < n; i++)
    {
        // out[i] = array[n-i];
        *o = *a;
        ++o;
        --a;
    }
    return out;
}