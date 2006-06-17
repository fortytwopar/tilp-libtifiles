/* Hey EMACS -*- linux-c -*- */
/* $Id: grouped.c 1737 2006-01-23 12:54:47Z roms $ */

/*  libtifiles - Ti File Format library, a part of the TiLP project
 *  Copyright (C) 1999-2005  Romain Lievin
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	TiGroup (*.tig) management
	A TiGroup file is in fact a ZIP archive with no compression (stored).

  Please note that I don't use USEWIN32IOAPI.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBZ
#include "minizip/zip.h"
#include "minizip/unzip.h"
#endif

#include "tifiles.h"
#include "logging.h"
#include "error.h"
#include "rwfile.h"

#define WRITEBUFFERSIZE (8192)

#ifdef HAVE_LIBZ
extern uLong filetime(char *f, tm_zip *tmzip, uLong *dt);
extern int do_list(unzFile uf);
#endif

#if !GLIB_CHECK_VERSION(2, 8, 0)
#include <errno.h>
// Code taken from Glib
int g_chdir (const gchar *path)
{
#ifdef __WIN32__
  if (G_WIN32_HAVE_WIDECHAR_API ())
    {
      wchar_t *wpath = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
      int retval;
      int save_errno;

      if (wpath == NULL)
	{
	  errno = EINVAL;
	  return -1;
	}

      retval = _wchdir (wpath);
      save_errno = errno;

      g_free (wpath);
      
      errno = save_errno;
      return retval;
    }
  else
    {
      gchar *cp_path = g_locale_from_utf8 (path, -1, NULL, NULL, NULL);
      int retval;
      int save_errno;

      if (cp_path == NULL)
	{
	  errno = EINVAL;
	  return -1;
	}

      retval = chdir (cp_path);
      save_errno = errno;

      g_free (cp_path);

      errno = save_errno;
      return retval;
    }
#else
  return chdir (path);
#endif
}
#endif

/**
 * tifiles_content_create_tigroup:
 * @model: a calculator model or CALC_NONE.
 * @r: number of #FileContent entries
 * @f: number of #FlashContent entries
 *
 * Allocates a TigContent structure. Note: the calculator model is not required
 * if the content is used for file reading but is compulsory for file writing.
 *
 * Return value: the allocated block.
 **/
TIEXPORT TigContent* TICALL tifiles_content_create_tigroup(CalcModel model, int r, int f)
{
	TigContent* content = calloc(1, sizeof(FileContent));

	content->model = model;
	content->comment = strdup(tifiles_comment_set_tigroup());

	content->regular_files = (char **)calloc(r+1, sizeof(char *));
	content->flash_files = (char **)calloc(f+1, sizeof(char *));

	content->regular = (FileContent **)calloc(r+1, sizeof(FileContent *));
	content->flash = (FlashContent **)calloc(f+1, sizeof(FileContent *));

	return content;
}

/**
 * tifiles_content_delete_tigroup:
 *
 * Free the whole content of a @TigContent structure and the content itself.
 *
 * Return value: none.
 **/
TIEXPORT int TICALL tifiles_content_delete_tigroup(TigContent *content)
{
	int i, r, f;
	
	// counter number of files to group
	for (r = 0; content->regular[r] != NULL; r++);
	for (f = 0; content->flash[f] != NULL; f++);

	// release allocated memory in structures
	for (i = 0; i < r; i++) 
	{
		//tifiles_content_delete_regular(content->regular[i]);
	}

	for (i = 0; i < f; i++) 
	{
		tifiles_content_delete_flash(content->flash[i]);
	}

	for(i = 0; i < r; i++)
		free(content->regular_files[i]);
	free(content->regular_files);

	for(i = 0; i < f; i++)
		free(content->flash_files[i]);
	free(content->flash_files);

	free(content);

  return 0;
}

/**
 * tifiles_file_read_tigroup:
 * @filename: the name of file to load.
 * @content: where to store content (may be re-allocated).
 *
 * This function load & TiGroup and place its content into content.
 *
 * Return value: an error code if unsuccessful, 0 otherwise.
 **/
TIEXPORT int TICALL tifiles_file_read_tigroup(const char *filename, TigContent *content)
{
#ifdef HAVE_LIBZ
	unzFile uf = NULL;
	unz_global_info gi;
	unz_file_info file_info;
	int cnt, err = UNZ_OK;
	char filename_inzip[256];
	unsigned i;
	void* buf = NULL;
	const char *password = NULL;
	int ri =0, fi = 0;

	// Open ZIP archive
	uf = unzOpen(filename);
	if (uf == NULL)
    {
		printf("Can't open this file: <%s>", filename);
		return ERR_FILE_ZIP;
	}

	// Allocate
	buf = (void*)malloc(WRITEBUFFERSIZE);
    if (buf==NULL)
    {
        printf("Error allocating memory\n");
		goto tfrt_exit;
    }
	
	// Size of comment and number of files in archive
	err = unzGetGlobalInfo (uf,&gi);
    if (err!=UNZ_OK)
	{
		printf("error %d with zipfile in unzGetGlobalInfo \n",err);
		goto tfrt_exit;
	}        
	printf("# entries: %lu\n", gi.number_entry);

	// Get comment
	free(content->comment);
	content->comment = (char *)malloc((gi.size_comment+1) * sizeof(char));
	err = unzGetGlobalComment(uf, content->comment, gi.size_comment);

	// Parse archive for files
	for (i = 0; i < gi.number_entry; i++)
    {
		FILE *f;
		gchar *filename;	// beware: mask global 'filename' !
		gchar *utf8;
		gchar *gfe;

		// get infos
		err = unzGetCurrentFileInfo(uf,&file_info,filename_inzip,sizeof(filename_inzip),NULL,0,NULL,0);
		if (err!=UNZ_OK)
		{
			printf("error %d with zipfile in unzGetCurrentFileInfo\n",err);
			goto tfrt_exit;
		}
		printf("Extracting <%s> with %lu bytes\n", filename_inzip, file_info.uncompressed_size);

		err = unzOpenCurrentFilePassword(uf,password);
        if (err!=UNZ_OK)
        {
            printf("error %d with zipfile in unzOpenCurrentFilePassword\n",err);
			goto tfrt_exit;
        }

		// extract/uncompress into temporary file
		utf8 = g_locale_to_utf8(filename_inzip, -1, NULL, NULL, NULL);
		gfe = g_filename_from_utf8(utf8, -1, NULL, NULL, NULL);		
		filename = g_strconcat(g_get_tmp_dir(), G_DIR_SEPARATOR_S, gfe, NULL);
		g_free(utf8);
		g_free(gfe);

		f = gfopen(filename, "wb");
		if(f == NULL)
		{
			err = ERR_FILE_OPEN;
			goto tfrt_exit;
		}

		do
        {
            err = unzReadCurrentFile(uf,buf,WRITEBUFFERSIZE);
            if (err<0)
            {
                printf("error %d with zipfile in unzReadCurrentFile\n",err);
				fclose(f);
                goto tfrt_exit;
            }
            if (err>0)
			{
				cnt = fwrite(buf, 1, err, f);
				if(cnt == -1)
                {
                    printf("error in writing extracted file\n");
                    err=UNZ_ERRNO;
					fclose(f);
                    goto tfrt_exit;
                }
            }
        }
		while (err>0);
		fclose(f);

		// add to TigContent
		{
			content->model = tifiles_file_get_model(filename);

			if(tifiles_file_is_regular(filename))
			{
				content->regular_files = (char **)realloc(content->regular_files, (ri+1)*sizeof(char *));
				content->regular = (FileContent**)realloc(content->regular, (ri+1)*sizeof(FileContent*));

				content->regular_files[ri] = strdup(filename_inzip);
				content->regular[ri] = tifiles_content_create_regular(CALC_NONE);
				tifiles_file_read_regular(filename, content->regular[ri]);

				ri++;
				content->regular[ri] = NULL;
				content->regular_files[ri] = NULL;
			}
			else if(tifiles_file_is_flash(filename))
			{
				content->flash_files = (char **)realloc(content->flash_files, (fi+1)*sizeof(char *));
				content->flash = (FlashContent**)realloc(content->flash, (fi+1)*sizeof(FlashContent*));

				content->flash_files[fi] = strdup(filename_inzip);
				content->flash[fi] = tifiles_content_create_flash(CALC_NONE);
				tifiles_file_read_flash(filename, content->flash[fi]);

				fi++;
				content->flash[fi] = NULL;
				content->flash_files[fi] = NULL;
			}
			else
			{
				// skip
			}
		}
		unlink(filename);
		g_free(filename);

		// next file
		if ((i+1) < gi.number_entry)
		{
			err = unzGoToNextFile(uf);
			if (err!=UNZ_OK)
			{
				printf("error %d with zipfile in unzGoToNextFile\n",err);
				goto tfrt_exit;
			}
		}

	}	

	// Close
tfrt_exit:
	free(buf);
	unzCloseCurrentFile(uf);
	return err ? ERR_FILE_ZIP : 0;
#else
	return ERR_UNSUPPORTED;
#endif
}

/**
 * tifiles_file_write_tigroup:
 * @filename: the name of file to load.
 * @content: where to store content.
 *
 * This function load & TiGroup and place its content into content.
 *
 * Return value: an error code if unsuccessful, 0 otherwise.
 **/
TIEXPORT int TICALL tifiles_file_write_tigroup(const char *filename, TigContent *content)
{
#ifdef HAVE_LIBZ
	zipFile zf;
	zip_fileinfo zi;
	int err = ZIP_OK;
	int i;
    void* buf=NULL;
	gchar *old_dir = g_get_current_dir();
	
	FileContent** ptr1;
	FlashContent** ptr2;
	int ri = 0, rf = 0;

	g_chdir(g_get_tmp_dir());

	// Open ZIP archive
	zf = zipOpen(filename,APPEND_STATUS_CREATE);
	if (zf == NULL)
    {
		printf("Can't open this file: <%s>", filename);
		return ERR_FILE_ZIP;
	}

	// Allocate buffer
	buf = (void*)malloc(WRITEBUFFERSIZE);
	if (buf==NULL)
	{
		printf("Error allocating memory\n");
		goto tfwt_exit;
	}

	for(ptr1 = content->regular, ri=0; ptr1; ri++)
	{
		FILE *f;
		char filenameinzip[256];
		unsigned long crcFile=0;
		char *filename;
		int size_read;

		// write TI file into tmp folder
		TRYC(tifiles_file_write_regular(content->regular_files[ri], ptr1[ri], NULL));
		f = gfopen(filename, "rb");
		if(f == NULL)
		{
			err = ERR_FILE_OPEN;
			goto tfwt_exit;
		}
		strcpy(filenameinzip, filename);

		// update time stamp (to do ?)
		zi.tmz_date.tm_sec = zi.tmz_date.tm_min = zi.tmz_date.tm_hour =
        zi.tmz_date.tm_mday = zi.tmz_date.tm_mon = zi.tmz_date.tm_year = 0;
        zi.dosDate = 0;
        zi.internal_fa = 0;
        zi.external_fa = 0;
        filetime(filenameinzip,&zi.tmz_date,&zi.dosDate);

		err = zipOpenNewFileInZip3(zf,filenameinzip,&zi,
                                 NULL,0,NULL,0,content->comment /* comment*/,
                                 0 /* store, no comp */,
                                 0 /*comp level */,0,
                                 -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
                                 NULL /* no pwd*/,crcFile);
        if (err != ZIP_OK)
		{
            printf("error in opening %s in zipfile\n",filenameinzip);
			return ERR_FILE_ZIP;
		}		

		do
        {
			// feed with our data
            err = ZIP_OK;
			size_read = fread(buf, 1, WRITEBUFFERSIZE, f);

            if (size_read < WRITEBUFFERSIZE)
			{
				if (!feof(f))
				{
					printf("error in reading %s\n",filenameinzip);
					err = ZIP_ERRNO;
					goto tfwt_exit;
				}
			}
            if (size_read > 0)
            {
                err = zipWriteInFileInZip (zf,buf,size_read);
                if (err<0)
                {
                    printf("error in writing %s in the zipfile\n", filenameinzip);
					goto tfwt_exit;
                }

            }
        } while ((err == ZIP_OK) && (size_read>0));

		// close file
		err = zipCloseFileInZip(zf);
        if (err!=ZIP_OK)
		{
            printf("error in closing %s in the zipfile\n", filenameinzip);
			goto tfwt_exit;
		}

		fclose(f);
		unlink(filename);
		free(filename);
	}

	// close archive
tfwt_exit:
	//tifiles_content_delete_group(contents);

	err = zipClose(zf,NULL);
    if (err != ZIP_OK)
        printf("error in closing %s\n",filename);
	free(buf);
	g_chdir(old_dir);
	return err;
#else
	return ERR_UNSUPPORTED;
#endif
}

/**
 * tifiles_file_display_tigroup:
 * @filename: the name of file to load.
 *
 * This function shows file conten ( = "unzip -l filename.tig").
 *
 * Return value: an error code if unsuccessful, 0 otherwise.
 **/
TIEXPORT int TICALL tifiles_file_display_tigroup(const char *filename)
{
#ifdef HAVE_LIBZ
	unzFile uf = NULL;

	uf = unzOpen(filename);
	if (uf==NULL)
    {
		tifiles_warning("Can't open this file: <%s>", filename);
		return -1;
	}

	do_list(uf);
	unzCloseCurrentFile(uf);

	return 0;
#else
	return ERR_UNSUPPORTED;
#endif
}
