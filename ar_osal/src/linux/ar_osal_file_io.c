/**
 * \file ar_osal_file_io.c
 *
 * \brief
 *      This file has implementation of file input/output related
 *      helper functions.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#define AR_OSAL_FILE_IO_LOG_TAG    "COFI"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include "ar_osal_file_io.h"
#include "ar_osal_log.h"
#include "ar_osal_error.h"

#ifdef AR_OSAL_USE_CUTILS
#include <cutils/properties.h>

const char* gsl_log_paths[] = {"/data/vendor/audio/gsl_log.bin",
                               "/data/vendor/etc/audio/gsl_log.bin"};
#endif

#define AR_FILE_WRITE_MAX_SIZE ( (256)*(1024)*(1024) ) //256 MB

_IRQL_requires_max_(PASSIVE_LEVEL)
int32_t ar_fopen(_Out_ ar_fhandle *handle,
                   _In_  const char_t *path,
                   _In_  uint32_t access)
{
    int32_t rc = 0;
    char *file_mode = NULL;
    FILE *file_ptr;
    FILE *fileptr = NULL;

    if (NULL == path || NULL == handle) {
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s: File path or handle is NULL\n", __func__);
        rc = AR_EBADPARAM;
        goto done;
    }

#ifdef AR_OSAL_USE_CUTILS
    // ignore gpr packet log open if logging disabled
    if(!property_get_bool("vendor.audio.gpr.enable.packet.logs", 0))
        for(int i = 0; i < sizeof(gsl_log_paths) / sizeof(gsl_log_paths[0]); ++i)
            if(!strcmp(path, gsl_log_paths[i])){
                *handle = NULL;
                return AR_EBADPARAM;
            }
#endif

    switch (access) {
    case AR_FOPEN_READ_ONLY:
        file_mode = "r";
        break;
    case AR_FOPEN_WRITE_ONLY:
        file_mode = "wb";
        break;
    case AR_FOPEN_READ_WRITE:
        file_mode = "r+";
        break;
    case AR_FOPEN_APPEND:
        file_mode = "a";
        break;
    case AR_FOPEN_WRITE_ONLY_APPEND:
        file_mode = "a";
        break;
    case AR_FOPEN_READ_WRITE_APPEND:
        file_mode = "a+";
        break;
    case AR_FOPEN_READ_ONLY_WRITE:
        file_mode = "w+";
        fileptr = fopen(path, "r");
        if(fileptr){
            fclose(fileptr);
        }
        else {
            rc = AR_EFAILED;
            goto done;
        }
        break;
    default:
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s not a valid file open access specifier %d  \n",__func__, access);
        rc = AR_EBADPARAM;
        goto done;
    }
    file_ptr = fopen(path, file_mode);

    if (NULL == file_ptr) {
        rc = AR_EBADPARAM;
        /*
         *TODO: Add a util api to print the errno with custom
         * string to avoid 80 char overflow
         */
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s fail for %s err:%d %s\n",__func__, path, rc, strerror(errno));
    }

	if(access == AR_FOPEN_WRITE_ONLY || access == AR_FOPEN_WRITE_ONLY_APPEND)
	{
		if(AR_FILE_WRITE_MAX_SIZE <= ar_fsize(file_ptr))
		{
			AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,
                "%s max size detected - truncating to zero size \n", __func__ );
			if(0 !=  ftruncate(fileno(file_ptr), 0))
			{
				AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s fail to truncate %s err: %s\n",
					__func__, path, strerror(errno));
			}
		}
	}
   *handle = file_ptr;

done:
    return rc;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
size_t ar_fsize(_In_ ar_fhandle handle)
{
    int32_t rc = 0;
    FILE *file_ptr = (FILE *)handle;
    struct stat file_info;
    size_t file_size = 0;
    int fd = 0;

    if (NULL == handle) {
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s Invalid file handle\n",__func__);
        rc = AR_EBADPARAM;
        goto done;
    }

    fd = fileno(file_ptr);

    if (fd < 0) {
        rc = AR_EFAILED;
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%sinvalid file handle %d %s\n", __func__, rc, strerror(errno));
        goto done;
    }

    rc = fstat(fd, &file_info);
    if (0 != rc) {
        rc = AR_EFAILED;
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%sfstat for file failed with error %d %s\n", __func__, rc, strerror(errno));
        goto done;
    }

    file_size = file_info.st_size;
    return file_size;
done:
    return rc;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
int32_t ar_fmap(ar_fhandle handle,
                const void **fbuffer)
{
    return AR_EUNSUPPORTED;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
int32_t ar_funmap(const void *fbuffer)
{
    return AR_EUNSUPPORTED;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
int32_t ar_fseek(_In_ ar_fhandle handle,
                   _In_ size_t offset, 
                   _In_ ar_fseek_reference_t ref)
{
    int32_t rc = 0;
    ar_fseek_reference_t fseek_ref;
    FILE *file_ptr = (FILE *)handle;

    if (NULL == handle) {
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s Invalid file handle\n",__func__);
        rc = AR_EBADPARAM;
        return rc;
    }

    switch (ref) {
        case AR_FSEEK_BEGIN:
            fseek_ref = SEEK_SET;
            break;
        case AR_FSEEK_END:
            fseek_ref = SEEK_END;
            break;
        case AR_FSEEK_CURRENT:
            fseek_ref = SEEK_CUR;
        default :
            AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"Invalid reference id %d\n", fseek_ref);
			return AR_EFAILED;
            break;
    }

    rc = fseek(file_ptr, offset, fseek_ref);
    if (0 != rc) {
        rc = AR_EFAILED;
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s failed %d: %s\n",__func__, rc, strerror(errno));
    }
    return rc;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
int32_t ar_fread( _In_     ar_fhandle handle,
                    _Inout_  void *buf_ptr,
                    _In_     size_t read_size,
                    _Inout_    size_t *bytes_read)
{
    int32_t rc = 0;
    FILE *file_ptr = (FILE *)handle;

    if (NULL == handle) {
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s Invalid file handle\n",__func__);
        rc = AR_EBADPARAM;
        goto done;
    }

    if (NULL == buf_ptr || NULL == bytes_read) {
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s input buf is NULL or bytes_read ptr is NULL\n", __func__);
        rc = AR_EBADPARAM;
        goto done;
    }

    /*
     *Having size of object as 1 helps ensure that the fread
     *gives correct number of bytes if a failure occurs reading
     *for read_size number of bytes.
     */
     *bytes_read = fread(buf_ptr, 1, read_size, file_ptr);
     if (ferror(file_ptr)) {
         rc = AR_EFAILED;
         AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s failed with error %d %s\n", __func__, rc, strerror(errno));
     }
done:
    return rc;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
int32_t ar_fwrite(_In_         ar_fhandle handle,
                    _Inout_      void *buf_ptr,
                    _In_         size_t write_size,
                    _Inout_        size_t *bytes_written)
{
    int32_t rc = 0;
    FILE *file_ptr = (FILE *)handle;

    if (NULL == handle) {
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s Invalid file handle\n",__func__);
        rc = AR_EBADPARAM;
        goto done;
    }

    if (NULL == buf_ptr || NULL == bytes_written) {
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s input buf is NULL or bytes_written ptr is NULL\n", __func__);
        rc = AR_EBADPARAM;
        goto done;
    }
    *bytes_written = fwrite(buf_ptr, 1, write_size, file_ptr);
    if (ferror(file_ptr)) {
        rc = AR_EFAILED;
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s failed with error %d %s\n", __func__, rc, strerror(errno));
    }
    if(EOF == fflush(file_ptr))
    {
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s error in fwrite\n",__func__);
        rc = AR_EFAILED;
    }
done:
    return rc;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
int32_t ar_fclose(_In_ ar_fhandle handle)
{
    int32_t rc = 0;
    FILE *file_ptr = (FILE *)handle;

    if (NULL == handle) {
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s Invalid file handle\n",__func__);
        rc = AR_EBADPARAM;
        goto done;
    }

    rc = fclose(file_ptr);
    if (0 != rc) {
        rc = AR_EFAILED;
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s failed %d %s\n", __func__, rc, strerror(errno));
    }
done:
    return rc;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
int32_t ar_fdelete(_In_ const char_t *path)
{
    int32_t rc = 0;

    if (NULL == path) {
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s Invalid path\n",__func__);
        rc = AR_EBADPARAM;
        goto done;
    }
    rc = remove(path);
    if (0 != rc) {
        rc = AR_EFAILED;
        AR_LOG_ERR(AR_OSAL_FILE_IO_LOG_TAG,"%s failed %d %s\n", __func__, rc, strerror(errno));
    }
done:
    return rc;
}
