/*
  This code implements one part of functonality of 
  free available library PL/Vision. Please look www.quest.com

  Original author: Steven Feuerstein, 1996 - 2002
  PostgreSQL implementation author: Pavel Stehule, 2006

  This module is under BSD Licence

  History:
    1.0. first public version 13. March 2006
*/


#include "postgres.h"
#include "utils/builtins.h"
#include "utils/numeric.h"
#include "string.h"
#include "stdlib.h"
#include "utils/pg_locale.h"
#include "mb/pg_wchar.h"


#include "catalog/pg_type.h"
#include "libpq/pqformat.h"
#include "orafunc.h"

#include "plvstr.h"

PG_FUNCTION_INFO_V1(plvstr_rvrs);
PG_FUNCTION_INFO_V1(plvstr_normalize);
PG_FUNCTION_INFO_V1(plvstr_is_prefix);
PG_FUNCTION_INFO_V1(plvstr_is_prefix_text);
PG_FUNCTION_INFO_V1(plvstr_is_prefix_int);
PG_FUNCTION_INFO_V1(plvstr_is_prefix_int64);
PG_FUNCTION_INFO_V1(plvstr_lpart);
PG_FUNCTION_INFO_V1(plvstr_rpart);
PG_FUNCTION_INFO_V1(plvstr_lstrip);
PG_FUNCTION_INFO_V1(plvstr_rstrip);
PG_FUNCTION_INFO_V1(plvstr_left);
PG_FUNCTION_INFO_V1(plvstr_right);
PG_FUNCTION_INFO_V1(plvstr_substr2);
PG_FUNCTION_INFO_V1(plvstr_substr3);
PG_FUNCTION_INFO_V1(plvstr_instr2);
PG_FUNCTION_INFO_V1(plvstr_instr3);
PG_FUNCTION_INFO_V1(plvstr_instr4);
PG_FUNCTION_INFO_V1(plvstr_betwn_i);
PG_FUNCTION_INFO_V1(plvstr_betwn_c);
PG_FUNCTION_INFO_V1(plvstr_swap);

PG_FUNCTION_INFO_V1(plvchr_nth);
PG_FUNCTION_INFO_V1(plvchr_first);
PG_FUNCTION_INFO_V1(plvchr_last);
PG_FUNCTION_INFO_V1(plvchr_is_kind_i);
PG_FUNCTION_INFO_V1(plvchr_is_kind_a);
PG_FUNCTION_INFO_V1(plvchr_char_name);

PG_FUNCTION_INFO_V1(oracle_substr2);
PG_FUNCTION_INFO_V1(oracle_substr3);

char* char_names[] = {
	"NULL","SOH","STX","ETX","EOT","ENQ","ACK","DEL",
	"BS",  "HT", "NL", "VT", "NP", "CR", "SO", "SI",
	"DLE", "DC1","DC2","DC3","DC4","NAK","SYN","ETB",
	"CAN", "EM","SUB","ESC","FS","GS","RS","US","SP"
};

#define NON_EMPTY_CHECK(str) \
if (VARSIZE(str) - VARHDRSZ == 0) \
	ereport(ERROR, \
    		(errcode(ERRCODE_INVALID_PARAMETER_VALUE), \
    		 errmsg("invalid parameter"), \
		 errdetail("Not allowed empty string.")));

#define PARAMETER_ERROR(detail) \
	ereport(ERROR, \
		(errcode(ERRCODE_INVALID_PARAMETER_VALUE), \
		 errmsg("invalid parameter"), \
		 errdetail(detail))); 


#ifndef _pg_mblen
#define _pg_mblen	pg_mblen
#endif

typedef enum
{
	POSITION,
	FIRST,
	LAST
}  position_mode;


text*
ora_clone_text(text *t)
{
	text *result;
	
	result = palloc(VARSIZE(t));
	SET_VARSIZE(result, VARSIZE(t));
	memcpy(VARDATA(result), VARDATA(t), VARSIZE(t) - VARHDRSZ);

	return result;
}

text *
ora_make_text_fix(char *c, int n)
{
	text *result;
	result = palloc(n + VARHDRSZ);
	SET_VARSIZE(result, n + VARHDRSZ);
	memcpy(VARDATA(result), c, n);

	return result;
}

/*
 * Make substring, can handle negative start
 * 
 */


int
ora_mb_strlen(text *str, char **sizes, int **positions)
{
	int r_len;
	int cur_size = 0;
	int sz;
	char *p;
	int cur = 0;
	
	p = (char*)VARDATA(str);
	r_len = VARSIZE(str) - VARHDRSZ;

    if (NULL != sizes)
		*sizes = palloc(r_len * sizeof(char));
	if (NULL != positions)
		*positions = palloc(r_len * sizeof(int));

	while (cur < r_len)
	{
		sz = _pg_mblen(p);
		if (sizes)
			(*sizes)[cur_size] = sz;
		if (positions)
			(*positions)[cur_size] = cur;
		cur += sz;
		p += sz;
		cur_size += 1;
	}

	return cur_size;
}


int
ora_mb_strlen1(text *str)
{
	int r_len;
	int c;
	char *p;

	r_len = VARSIZE(str) - VARHDRSZ;
	
	if (pg_database_encoding_max_length() == 1)
		return r_len;

	p = (char*)VARDATA(str);
	c = 0;
	while (r_len > 0)
	{
		int sz;
		
		sz = _pg_mblen(p);
		p += sz;
		r_len -= sz;
		c += 1;
	}

	return c;
}

text*
ora_substr(text *str, int start, int len, bool valid_length)
{
	text *result;
	int l;
	bool mb_encode;
	char *sizes = NULL;
	int *positions = NULL;

	if (start == 0)
		return CStringGetTextP("");

	if (len < 0 && valid_length)
		PARAMETER_ERROR("Third parameter is negative.");


	mb_encode = pg_database_encoding_max_length() > 1;
	if (!mb_encode)
		l = VARSIZE(str) - VARHDRSZ;
	else
		l = ora_mb_strlen(str, &sizes, &positions);

	start = start > 0 ? start : l + start + 1;
	len = valid_length ? len : l - start + 1;
	len = len + start - 1> l ? l - start + 1 : len;
	len = len < 0 ? 0 : len;

	if (!mb_encode)
	{
		result = palloc(len + VARHDRSZ);
		SET_VARSIZE(result, len + VARHDRSZ);
		memcpy(VARDATA(result), ((char*)VARDATA(str))+start - 1, len);
	}
	else
	{
		int r_len;
		int m_len;
		int c_len;
		int i;
		int j;
		char *p, *d;

		r_len = VARSIZE(str) - VARHDRSZ;
		m_len = len * pg_database_encoding_max_length();

		if (r_len > m_len)
			result = palloc(m_len + VARHDRSZ);
		else
			result = palloc(r_len + VARHDRSZ);
		d =(char*) VARDATA(result);
		
		c_len = 0;
		p = &((char*)VARDATA(str))[positions[start-1]];
		for (i = start - 1; i < start+len-1; i++)
		{
			for (j = 0; j < sizes[i]; j++)
				*d++ = *p++;
			c_len += sizes[i]; 
		}

		SET_VARSIZE(result, c_len + VARHDRSZ);

	}

	return result;
}

/* simply search algorhitm - can be better */

static int 
ora_instr_mb(text *txt, text *pattern, int start, int nth)
{
	int c_len_txt, c_len_pat;
	char *sizes_txt,  *sizes_pat;
	int *pos_txt, *pos_pat;
	char *str , *patt_f, *txt_p, *patt_p;
	int dx, i, j;
	int fzs_pat, fzs_txt;
	int pos;

	c_len_txt = ora_mb_strlen(txt, &sizes_txt, &pos_txt);
	c_len_pat = ora_mb_strlen(pattern, &sizes_pat, &pos_pat);
	fzs_txt = VARSIZE(txt) - VARHDRSZ;
	fzs_pat = VARSIZE(pattern) - VARHDRSZ;

	if (start > 0)
	{
		dx = 1; pos = 1;
		str = &((char*)VARDATA(txt))[pos_txt[start-1]]; 
		patt_f = (char*)VARDATA(pattern);
		
	}
	else
	{
		dx = -1; pos = c_len_txt+start+1;
		str = &((char*)VARDATA(txt))[pos_txt[c_len_txt+ start]+sizes_txt[c_len_txt+ start]-1]; 
		patt_f = ((char*)VARDATA(pattern)) + fzs_pat - 1;
	}


	for(i = 0; i < fzs_txt; i++)
	{
		patt_p = patt_f;
		txt_p = str;
		for (j = 0; j < fzs_pat; j++)
		{
			if (*txt_p != *patt_p)
				break;
			txt_p += dx;
			patt_p += dx;
		}
		if (j < fzs_pat)
		{
			str += dx*sizes_txt[pos-1];
			pos += dx;
		}
		else
		{
			if (--nth == 0)
			{
				return dx < 0 ? pos - c_len_pat + 1: pos + start-1;
			}
			else
			{
				str += (fzs_pat)*dx;
				pos += dx*c_len_pat;
			}
		}
	}			

	return 0;
}


int 
ora_instr(text *txt, text *pattern, int start, int nth)
{
	int i, j, len, len_p, dx;
	char *str, *txt_p, *patt_p, *patt_f;

	if (nth <= 0)
		PARAMETER_ERROR("Four parameter isn't positive.");
	
    /*
     * Forward for multibyte strings
     */

	if (pg_database_encoding_max_length() > 1)
		return ora_instr_mb(txt, pattern, start, nth);

	len = VARSIZE(txt) - VARHDRSZ;
	len_p = VARSIZE(pattern) - VARHDRSZ;	


	if (start > 0)
	{
		dx = 1;
		str = (char*)VARDATA(txt) + start - 1;
		patt_f = (char*)VARDATA(pattern);
		
	}
	else
	{
		dx = -1;
		str = ((char*)VARDATA(txt)) + len + start;
		patt_f = ((char*)VARDATA(pattern)) + len_p - 1;
	}

	for(i = 0; i < len; i++)
	{
		patt_p = patt_f;
		txt_p = str;
		for (j = 0; j < len_p; j++)
		{
			if (*txt_p != *patt_p)
				break;
			txt_p += dx;
			patt_p += dx;
		}
		if (j < len_p)
			str += dx;
		else
		{
			if (--nth == 0)
			{
				int off = str - VARDATA(txt) + 1;
				return dx < 0 ? off - len_p + 1: off;
			}
			else
				str += (len_p)*dx;
		}
	}			
	return 0;
}


/****************************************************************
 * PLVstr.normalize
 *
 * Syntax:
 *   FUNCTION plvstr.normalize (string_in IN VARCHAR)
 *  	RETURN VARCHAR;
 *
 * Purpouse:
 *   Normalize string - replace white chars by space, replace 
 * spaces by space
 *
 ****************************************************************/

Datum 
plvstr_normalize(PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	text *result;
	char *aux, *aux_cur;
	int i, l;
	char c, *cur;
	bool write_spc = false;
	bool ignore_stsp = true;
	bool mb_encode;
	int sz;
	
	mb_encode = pg_database_encoding_max_length() > 1;

	l = VARSIZE(str) - VARHDRSZ;
	aux_cur = aux = palloc(l);
	
	write_spc = false;
	cur = VARDATA(str);

	for (i = 0; i < l; i++)
	{
		switch ((c = *cur))
		{
			case '\t':
			case '\n':
			case '\r':
			case ' ':
				write_spc = ignore_stsp ? false : true;
				break;
			default:
				/* ignore all other unvisible chars */
				
				if (mb_encode)
				{
					sz = _pg_mblen(cur);
					if (sz > 1 || (sz == 1 && c > 32))
					{
					    int j;
					    
					    if (write_spc)
					    {
						    *aux_cur++ = ' ';
						    write_spc = false;

					    }
					    for (j = 0; j < sz; j++)
					    {
						    *aux_cur++ = *cur++;
					    }
					    ignore_stsp = false;
					    i += sz - 1;
					}
					continue;
				
				}
				else
					if (c > 32)
					{
						if (write_spc)
						{
							*aux_cur++ = ' ';
							write_spc = false;
						}
						*aux_cur++ = c;
						ignore_stsp = false;
						continue;
					}
		
		}
		cur += 1;
	}

	l = aux_cur - aux;
	result = palloc(l+VARHDRSZ);
	SET_VARSIZE(result, l + VARHDRSZ);
	memcpy(VARDATA(result), aux, l);
	
	PG_RETURN_TEXT_P(result);
}


/****************************************************************
 * PLVstr.instr
 *
 * Syntax:
 *   FUNCTION plvstr.instr (string_in VARCHAR, pattern VARCHAR)
 *   FUNCTION plvstr.instr (string_in VARCHAR, pattern VARCHAR,
 *            start_in INTEGER)
 *   FUNCTION plvstr.instr (string_in VARCHAR, pattern VARCHAR,
 *            start_in INTEGER, nth INTEGER)
 *  	RETURN INT;
 *
 * Purpouse:
 *   Search pattern in string. 
 *
 ****************************************************************/

Datum
plvstr_instr2 (PG_FUNCTION_ARGS)
{
	text *arg1 = PG_GETARG_TEXT_P(0);
	text *arg2 = PG_GETARG_TEXT_P(1);
	
	PG_RETURN_INT32(ora_instr(arg1, arg2, 1, 1));
}

Datum
plvstr_instr3 (PG_FUNCTION_ARGS)
{
	text *arg1 = PG_GETARG_TEXT_P(0);
	text *arg2 = PG_GETARG_TEXT_P(1);
	int arg3 = PG_GETARG_INT32(2);

	PG_RETURN_INT32(ora_instr(arg1, arg2, arg3, 1));
}

Datum
plvstr_instr4 (PG_FUNCTION_ARGS)
{
	text *arg1 = PG_GETARG_TEXT_P(0);
	text *arg2 = PG_GETARG_TEXT_P(1);
	int arg3 = PG_GETARG_INT32(2);
	int arg4 = PG_GETARG_INT32(3);

	PG_RETURN_INT32(ora_instr(arg1, arg2, arg3, arg4));
}


/****************************************************************
 * PLVstr.is_prefix
 *
 * Syntax:
 *   FUNCTION plvstr.is_prefix (string_in IN VARCHAR,
 *                    prefix_in VARCHAR, 
 *                    case_sensitive BOOL := true)
 *      RETURN bool;
 *   FUNCTION plvstr.is_prefix (num_in IN NUMERIC,
 *                    prefix_in NUMERIC) RETURN bool;
 *   FUNCTION plvstr.is_prefix (int_in IN INT,
 *                    prefix_in INT)  RETURN bool;
 *
 * Purpouse:
 *   Returns true, if prefix_in is prefix of string_in
 *
 ****************************************************************/


Datum 
plvstr_is_prefix_text (PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	text *prefix = PG_GETARG_TEXT_P(1);
	bool case_sens = PG_GETARG_BOOL(2);
	bool mb_encode;

	int str_len = VARSIZE(str) - VARHDRSZ;
	int pref_len = VARSIZE(prefix) - VARHDRSZ;

	int i;
	char *ap, *bp;

	
	mb_encode = pg_database_encoding_max_length() > 1;

	if (mb_encode && !case_sens)
	{
		str = (text*)DatumGetPointer(DirectFunctionCall1(lower, PointerGetDatum(str)));
		prefix = (text*)DatumGetPointer(DirectFunctionCall1(lower, PointerGetDatum(prefix)));
	}

	ap = (char*)VARDATA(str);
	bp = (char*)VARDATA(prefix);

	for (i = 0; i < pref_len; i++)
	{
		if (i >= str_len)
			break;
		if (case_sens || mb_encode)
		{
			if (*ap++ != *bp++)
				break;
		}
		else if (!mb_encode)
		{
			if (pg_toupper((unsigned char) *ap++) != pg_toupper((unsigned char) *bp++))
				break;
		}
	}

	PG_RETURN_BOOL(i == pref_len);
}

Datum
plvstr_is_prefix_int (PG_FUNCTION_ARGS)
{
	int n = PG_GETARG_INT32(0);
	int prefix = PG_GETARG_INT32(1);
	bool result = false;

	do
	{
		if (n == prefix)
		{
			result = true;
			break;
		}
		n = n / 10;
		
	} while (n >= prefix);
	
	PG_RETURN_BOOL(result);
}

Datum
plvstr_is_prefix_int64 (PG_FUNCTION_ARGS)
{
	int64 n = PG_GETARG_INT64(0);
	int64 prefix = PG_GETARG_INT64(1);
	bool result = false;

	do
	{
		if (n == prefix)
		{
			result = true;
			break;
		}
		n = n / 10;
		
	} while (n >= prefix);
	
	PG_RETURN_BOOL(result);
}


/****************************************************************
 * PLVstr.rvrs
 *
 * Syntax:
 *   FUNCTION plvstr.rvrs (string_in IN VARCHAR,
 *					  start_in IN INTEGER := 1, 
 *					  end_in IN INTEGER := NULL) 
 *  	RETURN VARCHAR2;
 *
 * Purpouse:
 *   Reverse string or part of string
 *
 ****************************************************************/

Datum
plvstr_rvrs(PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	int start = PG_GETARG_INT32(1);
	int end = PG_GETARG_INT32(2);
	int len, aux;
	int i;
	int new_len;
	text *result;
	char *data;
	char *sizes = NULL;
	int *positions = NULL;
	bool mb_encode;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	mb_encode = pg_database_encoding_max_length() > 1;
	
	if (!mb_encode)
		len = VARSIZE(str) - VARHDRSZ;
	else
		len = ora_mb_strlen(str, &sizes, &positions);
	
		

	start = PG_ARGISNULL(1) ? 1 : start;
	end = PG_ARGISNULL(2) ? (start < 0 ? -len : len) : end;

	if ((start > end && start > 0) || (start < end && start < 0))
		PARAMETER_ERROR("Second parameter is biger than third.");

	if (start < 0)
	{
		aux = len + end + 1;
		end = len + start + 1;
		start = end;
	}

	new_len = end - start + 1;

	if (mb_encode)
	{
		int max_size;
		int cur_size;
		char *p;
		int j;
		int fz_size;
		
		fz_size = VARSIZE(str) - VARHDRSZ;
		
		if ((max_size = (new_len*pg_database_encoding_max_length())) > fz_size)
		    result = palloc(fz_size + VARHDRSZ);
		else
		    result = palloc(max_size + VARHDRSZ);    
		data = (char*) VARDATA(result);
		
		cur_size = 0; 
		p = VARDATA(str);
		for (i = end - 1; i>= start - 1; i--)
		{
		    for(j=0;j<sizes[i];j++)
		        *data++ = *(p+positions[i]+j);
		    cur_size += sizes[i];
		}
		SET_VARSIZE(result, cur_size + VARHDRSZ);
		
	}
	else
	{
		result = palloc(new_len + VARHDRSZ);
    		data = (char*) VARDATA(result);
		SET_VARSIZE(result, new_len + VARHDRSZ);

		for (i = end - 1; i >= start - 1; i--)
			*data++ = ((char*)VARDATA(str))[i];
			
	}
	
	PG_RETURN_TEXT_P(result);
}


/****************************************************************
 * PLVstr.lpart
 *
 * Syntax:
 *   FUNCTION PLVstr.lpart (string_in IN VARCHAR, 
 *					   divider_in IN VARCHAR,
 *					   start_in IN INTEGER := 1,
 *					   nth_in IN INTEGER := 1,
 *					   all_if_notfound_in IN BOOLEAN := FALSE)
 *	RETURN VARCHAR2;
 *
 * Purpouse:
 *   Call this function to return the left part of a string.
 *
 ****************************************************************/

Datum 
plvstr_lpart (PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	text *div = PG_GETARG_TEXT_P(1);
	int start = PG_GETARG_INT32(2);
	int nth   = PG_GETARG_INT32(3);
	bool all_if_notfound  = PG_GETARG_BOOL(4);
	int loc;

	loc = ora_instr(str, div, start, nth);
	if (loc == 0)
	{
		if (all_if_notfound)
			PG_RETURN_TEXT_P(ora_clone_text(str));
		else
			PG_RETURN_NULL();
	}
	else
		PG_RETURN_TEXT_P(ora_substr(str,1,loc-1,true));
}


/****************************************************************
 * PLVstr.rpart
 *
 * Syntax:
 *   FUNCTION PLVstr.rpart (string_in IN VARCHAR, 
 *					   divider_in IN VARCHAR,
 *					   start_in IN INTEGER := 1,
 *					   nth_in IN INTEGER := 1,
 *					   all_if_notfound_in IN BOOLEAN := FALSE)
 *	RETURN VARCHAR2;
 *
 * Purpouse:
 *   Call this function to return the right part of a string.
 *
 ****************************************************************/

Datum 
plvstr_rpart (PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	text *div = PG_GETARG_TEXT_P(1);
	int start = PG_GETARG_INT32(2);
	int nth   = PG_GETARG_INT32(3);
	bool all_if_notfound  = PG_GETARG_BOOL(4);
	int loc;

	loc = ora_instr(str, div, start, nth);
	if (loc == 0)
	{
		if (all_if_notfound)
			PG_RETURN_TEXT_P(ora_clone_text(str));
		else
			PG_RETURN_NULL();
	}
	else
		PG_RETURN_TEXT_P(ora_substr(str,loc+1,0,false));
}


/****************************************************************
 * PLVstr.lstrip
 *
 * Syntax:
 *   FUNCTION plvstr.lstrip (string_in IN VARCHAR, 
 *							substring_in IN VARCHAR,
 *							num_in IN INTEGER := 1)
 *  	RETURN VARCHAR;
 *
 * Purpouse:
 *   Call this function to remove characters from the beginning 
 * (left) of a string.
 *
 ****************************************************************/

Datum 
plvstr_lstrip (PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	text *pat = PG_GETARG_TEXT_P(1);
	int num = PG_GETARG_INT32(2);
	int count = 0;
	int len_p, len_s, i;

	char *str_p, *aux_str_p, *pat_p;
	len_p = VARSIZE(pat) - VARHDRSZ;
	len_s = VARSIZE(str) - VARHDRSZ;

	str_p = VARDATA(str);
	while (count < num)
	{
		pat_p = VARDATA(pat);
		aux_str_p = str_p;
		
		if (len_s < len_p)
			break;

		for(i = 0; i < len_p; i++)
			if (*aux_str_p++ != *pat_p++)
				break;
		
		if (i >= len_p)
		{
			count++;
			/* found */
			str_p = aux_str_p;
			len_s -= len_p;
			continue;
		}
		break;
	}
	
	PG_RETURN_TEXT_P(ora_make_text_fix(str_p,len_s));
}


/****************************************************************
 * PLVstr.rstrip
 *
 * Syntax:
 *   FUNCTION plvstr.rstrip (string_in IN VARCHAR, 
 *							substring_in IN VARCHAR,
 *							num_in IN INTEGER := 1)
 *  	RETURN VARCHAR;
 *
 * Purpouse:
 *   Call this function to remove characters from the end 
 * (right) of a string.
 *
 ****************************************************************/

Datum 
plvstr_rstrip (PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	text *pat = PG_GETARG_TEXT_P(1);
	int num = PG_GETARG_INT32(2);
	int count = 0;
	int len_p, len_s, i;

	char *str_p, *aux_str_p, *pat_p;
	len_p = VARSIZE(pat) - VARHDRSZ;
	len_s = VARSIZE(str) - VARHDRSZ;

	str_p = ((char*)VARDATA(str)) + len_s - 1;

	while (count < num)
	{
		pat_p = VARDATA(pat) + len_p - 1;
		aux_str_p = str_p;
		
		if (len_s < len_p)
			break;

		for(i = 0; i < len_p; i++)
			if (*aux_str_p-- != *pat_p--)
				break;
		
		if (i >= len_p)
		{
			count++;
			/* found */
			str_p = aux_str_p;
			len_s -= len_p;
			continue;
		}
		break;
	}
	
	PG_RETURN_TEXT_P(ora_make_text_fix((char*)VARDATA(str),len_s));
}


/****************************************************************
 * PLVstr.left
 *
 * Syntax:
 *   FUNCTION plvstr.left (string_in IN VARCHAR, 
 *							num_in INTEGER)
 *  	RETURN VARCHAR;
 *
 * Purpouse:
 *   Returns firs num_in charaters. You can use negative num_in
 *   left('abcde', -2) -> abc
 *
 ****************************************************************/


Datum 
plvstr_left (PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	int n = PG_GETARG_INT32(1);
	if (n < 0)
		n = ora_mb_strlen1(str) + n;
	n = n < 0 ? 0 : n;
	
	PG_RETURN_TEXT_P(ora_substr(str,1,n, true));
}


/****************************************************************
 * PLVstr.right
 *
 * Syntax:
 *   FUNCTION plvstr.right (string_in IN VARCHAR, 
 *							num_in INTEGER)
 *  	RETURN VARCHAR;
 *
 * Purpouse:
 *   Returns last (right) num_in characters.
 *
 ****************************************************************/

Datum 
plvstr_right (PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	int n = PG_GETARG_INT32(1);
	if (n < 0)
		n = ora_mb_strlen1(str) + n;
	n = (n < 0) ? 0 : n;

	PG_RETURN_TEXT_P(ora_substr(str,-n, 0, false));
}

/****************************************************************
 * PLVstr.substr2
 *
 * Syntax:
 *   FUNCTION plvstr.substr (string_in IN VARCHAR, 
 *							start INTEGER)
 *  	RETURN VARCHAR;
 *
 * Purpouse:
 *   Returns substring started on start_in to end
 *
 ****************************************************************/

Datum 
plvstr_substr2 (PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	int start = PG_GETARG_INT32(1);

	PG_RETURN_TEXT_P(ora_substr(str,start, 0, false));
}


/****************************************************************
 * PLVstr.substr3
 *
 * Syntax:
 *   FUNCTION plvstr.substr (string_in IN VARCHAR, 
 *							start INTEGER, len INTEGER)
 *  	RETURN VARCHAR;
 *
 * Purpouse:
 *   Returns len chars from start_in position 
 *
 ****************************************************************/

Datum 
plvstr_substr3 (PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	int start = PG_GETARG_INT32(1);
	int len = PG_GETARG_INT32(2);

	PG_RETURN_TEXT_P(ora_substr(str,start, len, true));
}


/****************************************************************
 * PLVchr.nth
 *
 * Syntax:
 *   FUNCTION plvchr.nth (string_in IN VARCHAR, 
 * 					 nth_in IN INTEGER)
 *  	RETURN VARCHAR;
 *
 * Purpouse:
 *   Call this function to return the Nth character in a string. 
 *
 ****************************************************************/

Datum
plvchr_nth (PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(ora_substr(PG_GETARG_TEXT_P(0), 
								PG_GETARG_INT32(1), 1, true));
}


/****************************************************************
 * PLVchr.first
 *
 * Syntax:
 *   FUNCTION plvchr.first (string_in IN VARCHAR, 
 *  	RETURN VARCHAR;
 *
 * Purpouse:
 *   Call this function to return the first character in a string.
 *
 ****************************************************************/

Datum
plvchr_first (PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(ora_substr(PG_GETARG_TEXT_P(0), 1, 1, true));
}


/****************************************************************
 * PLVchr.last
 *
 * Syntax:
 *   FUNCTION plvchr.last (string_in IN VARCHAR, 
 *  	RETURN VARCHAR;
 *
 * Purpouse:
 *   Call this function to return the last character in a string.
 *
 ****************************************************************/

Datum
plvchr_last (PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(ora_substr(PG_GETARG_TEXT_P(0), -1, 1, true));
}


/****************************************************************
 * PLVchr.is_blank, plvchr.is_digit, ...
 *
 * Syntax:
 *   FUNCTION plvchr.is_kind (string_in IN VARCHAR, 
 *      kind INT)
 *  	RETURN VARCHAR;
 *
 * Purpouse:
 *   Call this function to see if a character is blank, ...
 *   1 blank, 2 digit, 3 quote, 4 other, 5 letter
 *
 ****************************************************************/

static bool
is_kind(char c, int kind)
{
	switch (kind)
	{
		case 1:
			return c == ' ';
		case 2:
			return '0' <= c && c <= '9';
		case 3:
			return c == '\'';
		case 4:
			return 
				(32 <= c && c <= 47) ||
				(58 <= c && c <= 64) ||
				(91 <= c && c <= 96) || (123 <= c && c <= 126);
		case 5:
			return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z');
		default:
			PARAMETER_ERROR("Second parametr isn't in enum {1,2,3,4,5}");
			return false; 
	}
}

Datum
plvchr_is_kind_i (PG_FUNCTION_ARGS)
{
	int32 c = PG_GETARG_INT32(0);
	int32 k = PG_GETARG_INT32(1);

	PG_RETURN_INT32(is_kind((char)c,k));
}

Datum
plvchr_is_kind_a (PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	int32 k = PG_GETARG_INT32(1);
	char c;

	NON_EMPTY_CHECK(str);	
	if (pg_database_encoding_max_length() > 1)
	{
		if (_pg_mblen(((char*)VARDATA(str))) > 1)
			PG_RETURN_INT32( (k == 5) );
	}

	c = *((char*)VARDATA(str));
	PG_RETURN_INT32(is_kind((char)c,k));
}


/****************************************************************
 * PLVchr.char_name
 *
 * Syntax:
 *   FUNCTION plvchr.char_name (letter_in IN VARCHAR) 
 *   	RETURN VARCHAR;
 *
 * Purpouse:
 *   Returns the name of the character to ascii code as a VARCHAR.
 *
 ****************************************************************/

Datum
plvchr_char_name(PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	text *result; 
	char c;

	NON_EMPTY_CHECK(str);
	c = *((char*)VARDATA(str));


	if (c > 32 && _pg_mblen(((char*)VARDATA(str))) == 1)
		result = ora_substr(str,1, 1, true);
	else
		result = CStringGetTextP(char_names[(int)c]);
	
	PG_RETURN_TEXT_P(result);
}


/****************************************************************
 * substr
 *
 * Syntax:
 *   FUNCTION substr (string, start_position, [length]) 
 *   	RETURN VARCHAR;
 *
 * Purpouse:
 *   Returns len chars from start_in position, compatible with Oracle
 *
 ****************************************************************/

Datum
oracle_substr3(PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	int start = PG_GETARG_INT32(1);
	int len = PG_GETARG_INT32(2);

	if (len < 0)
		PG_RETURN_NULL();

	PG_RETURN_TEXT_P(ora_substr(str, start != 0 ? start : 1, len, true));
}

Datum
oracle_substr2(PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
	int start = PG_GETARG_INT32(1);

	PG_RETURN_TEXT_P(ora_substr(str, start != 0 ? start : 1, 0, false));
}




static text*
ora_concat2(text *str1, text *str2)
{
	int l1;                                                                                                                                        
	int l2;                                                                                                                                        
	text *result;                                                                                                                                  
                                                
	l1 = VARSIZE(str1) - VARHDRSZ;                                                                                                                   
	l2 = VARSIZE(str2) - VARHDRSZ;                                                                                                                   
                                                                                                                                                   
	result = palloc(l1+l2+VARHDRSZ);                                                                                                               
	memcpy(VARDATA(result), VARDATA(str1), l1);                                                                                                      
	memcpy(VARDATA(result) + l1, VARDATA(str2), l2);                                                                                                 
	SET_VARSIZE(result, l1 + l2 + VARHDRSZ);
                            
	return result;                                                                                                                       
}


static text*
ora_concat3(text *str1, text *str2, text *str3)
{
	int l1;                                                                                                                                        
	int l2;                                                                                                                                        
	int l3;
	text *result;                                                                                                                                  
                                                
	l1 = VARSIZE(str1) - VARHDRSZ;
	l2 = VARSIZE(str2) - VARHDRSZ;
	l3 = VARSIZE(str3) - VARHDRSZ;
                                                                                                                                                   
	result = palloc(l1+l2+l3+VARHDRSZ);                                                                                                               
	memcpy(VARDATA(result), VARDATA(str1), l1);
	memcpy(VARDATA(result) + l1, VARDATA(str2), l2);
	memcpy(VARDATA(result) + l1+l2, VARDATA(str3), l3);
	SET_VARSIZE(result, l1 + l2 + l3 + VARHDRSZ);
                            
	return result;                                                                                                                       
}


/****************************************************************
 * PLVchr.swap
 *
 * Syntax:
 *    FUNCTION swap
 *      (string_in IN VARCHAR2,
 *       replace_in IN VARCHAR2,
 *       start_in IN INTEGER := 1,
 *       oldlen_in IN INTEGER := NULL)
 *  RETURN VARCHAR2
 *
 * Purpouse:
 *   Replace a substring in a string with a specified string.
 *
 ****************************************************************/

Datum 
plvstr_swap(PG_FUNCTION_ARGS)
{
	text *string_in;
	text *replace_in;
	int start_in = 1;
	int oldlen_in;
	int v_len;

	if (PG_ARGISNULL(0))
	        PG_RETURN_NULL();
	else
	        string_in = PG_GETARG_TEXT_P(0);

	if (PG_ARGISNULL(1))
	        PG_RETURN_NULL();
	else
	        replace_in = PG_GETARG_TEXT_P(1);

	if (!PG_ARGISNULL(2))
	        start_in = PG_GETARG_INT32(2);

	if (PG_ARGISNULL(3))
	        oldlen_in = ora_mb_strlen1(replace_in);
        else
		oldlen_in = PG_GETARG_INT32(3);

	v_len =  ora_mb_strlen1(string_in);

	start_in = start_in > 0 ? start_in : v_len + start_in + 1;

	if (start_in == 0 || start_in > v_len)
	        PG_RETURN_TEXT_P(ora_clone_text(string_in));
	else if (start_in == 1)
	        PG_RETURN_TEXT_P(ora_concat2(replace_in, 
					      ora_substr(string_in, oldlen_in+1,0, false)));
	else
	        PG_RETURN_TEXT_P(ora_concat3(ora_substr(string_in, 1, start_in-1, true),
					       replace_in,
					       ora_substr(string_in, start_in + oldlen_in, 0, false)));

}

/****************************************************************
 * PLVchr.betwn
 *
 * Find the Substring Between Start and End Locations
 *
 * Syntax:
 *     FUNCTION plvstr.betwn (string_in IN VARCHAR2, 
 *       start_in IN INTEGER, 
 *       end_in IN INTEGER,
 *       inclusive IN BOOLEAN := TRUE)
 *      RETURN VARCHAR2;
 *
 *     FUNCTION plvstr.betwn (string_in IN VARCHAR2, 
 *       start_in IN VARCHAR2, 
 *       end_in IN VARCHAR2 := NULL,
 *       startnth_in IN INTEGER := 1,
 *       endnth_in IN INTEGER := 1,
 *       inclusive IN BOOLEAN := TRUE,
 *       gotoend IN BOOLEAN := FALSE)
 *      RETURN VARCHAR2;
 *
 * Purpouse:
 *   Call this function to extract a sub-string from a string. This 
 * function is overloaded. You can either provide the start and end 
 * locations or you can provide start and end substrings.
 *
 ****************************************************************/


Datum 
plvstr_betwn_i(PG_FUNCTION_ARGS)
{
	text *string_in = PG_GETARG_TEXT_P(0);
	int start_in = PG_GETARG_INT32(1);
	int end_in = PG_GETARG_INT32(2);
	bool inclusive = PG_GETARG_BOOL(3);

	if ((start_in < 0 && end_in > 0) ||   
	    (start_in > 0 && end_in < 0) ||
	    (start_in > end_in))
		PARAMETER_ERROR("Wrong positions.");

	if (start_in < 0)
	{
		int v_len =  ora_mb_strlen1(string_in);
		start_in = v_len + start_in + 1;
		end_in = v_len + start_in + 1;
	}

	if (!inclusive)
	{
		start_in += 1;
		end_in -= 1;

		if (start_in > end_in)
			PG_RETURN_TEXT_P(CStringGetTextP(""));
	}    

	PG_RETURN_TEXT_P(ora_substr(string_in, 
				    start_in, 
				    end_in - start_in + 1, true));
}


Datum 
plvstr_betwn_c(PG_FUNCTION_ARGS)
{
	text *string_in;
	text *start_in;
	text *end_in;
	int startnth_in;
	int endnth_in;
	bool inclusive;
	bool gotoend;

	int v_start;
	int v_end;

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1) ||
	    PG_ARGISNULL(3) || PG_ARGISNULL(4) ||
	    PG_ARGISNULL(5) || PG_ARGISNULL(6))
		PG_RETURN_NULL();
	

	string_in = PG_GETARG_TEXT_P(0);
	start_in = PG_GETARG_TEXT_P(1);
	end_in = PG_ARGISNULL(2) ? start_in : PG_GETARG_TEXT_P(2);
	startnth_in = PG_GETARG_INT32(3);
	endnth_in = PG_GETARG_INT32(4);
	inclusive = PG_GETARG_BOOL(5);
	gotoend = PG_GETARG_BOOL(6);
	
	if (startnth_in == 0)
	{
		v_start = 1;
		v_end = ora_instr(string_in, end_in, 1, endnth_in);
	}	
	else
	{
		v_start = ora_instr(string_in, start_in, 1, startnth_in);
		v_end = ora_instr(string_in, end_in, v_start + 1, endnth_in);
	}

	if (v_start == 0)
		PG_RETURN_NULL();

	if (!inclusive)
	{
		if (startnth_in > 0)
			v_start += ora_mb_strlen1(start_in);

		v_end -= 1;
	}
	else
		v_end += (ora_mb_strlen1(end_in) - 1);
	
	if (((v_start > v_end) && (v_end > 0)) || 
	    (v_end <= 0 && !gotoend))
		PG_RETURN_NULL();

	if (v_end <= 0)
		v_end = ora_mb_strlen1(string_in);

	PG_RETURN_TEXT_P(ora_substr(string_in, 
				    v_start, 
				    v_end - v_start + 1, true));
}
