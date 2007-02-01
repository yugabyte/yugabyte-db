/*
 * file.c 
 * PostgreSQL implementation of Oracle API UTL_FILE
 * @ Pavel Stehule 2007-02-01
 *
 */

/*
 ToDo:
    Security: can work only in direcories specified utl_file_dir
    Safety: can close all opened file's descriptor
            can dynamicly enhance local buffer size
 */

