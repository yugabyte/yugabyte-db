/*
 * For PostgreSQL Database Management System:
 * (formerly known as Postgres, then as Postgres95)
 *
 * Portions Copyright (c) 1996-2010, The PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 * Permission to use, copy, modify, and distribute this software and its documentation for any purpose,
 * without fee, and without a written agreement is hereby granted, provided that the above copyright notice
 * and this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR DIRECT,
 * INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY
 * OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA
 * HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_proc.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#include "catalog/ag_namespace.h"
#include "utils/ag_func.h"

/* checks that func_oid is of func_name function in ag_catalog */
bool is_oid_ag_func(Oid func_oid, const char *func_name)
{
    HeapTuple proctup;
    Form_pg_proc proc;
    Oid nspid;
    const char *nspname;

    AssertArg(OidIsValid(func_oid));
    AssertArg(func_name);

    proctup = SearchSysCache1(PROCOID, ObjectIdGetDatum(func_oid));
    Assert(HeapTupleIsValid(proctup));
    proc = (Form_pg_proc)GETSTRUCT(proctup);
    if (strncmp(NameStr(proc->proname), func_name, NAMEDATALEN) != 0)
    {
        ReleaseSysCache(proctup);
        return false;
    }

    nspid = proc->pronamespace;
    ReleaseSysCache(proctup);

    nspname = get_namespace_name_or_temp(nspid);
    Assert(nspname);
    return (strcmp(nspname, "ag_catalog") == 0);
}

/* gets the function OID that matches with func_name and argument types */
Oid get_ag_func_oid(const char *func_name, const int nargs, ...)
{
    Oid oids[FUNC_MAX_ARGS];
    va_list ap;
    int i;
    oidvector *arg_types;
    Oid func_oid;

    AssertArg(func_name);
    AssertArg(nargs >= 0 && nargs <= FUNC_MAX_ARGS);

    va_start(ap, nargs);
    for (i = 0; i < nargs; i++)
        oids[i] = va_arg(ap, Oid);
    va_end(ap);

    arg_types = buildoidvector(oids, nargs);

    func_oid = GetSysCacheOid3(PROCNAMEARGSNSP, Anum_pg_proc_oid,
                               CStringGetDatum(func_name),
                               PointerGetDatum(arg_types),
                               ObjectIdGetDatum(ag_catalog_namespace_id()));
    if (!OidIsValid(func_oid))
    {
        ereport(ERROR, (errmsg_internal("ag function does not exist"),
                        errdetail_internal("%s(%d)", func_name, nargs)));
    }

    return func_oid;
}

Oid get_pg_func_oid(const char *func_name, const int nargs, ...)
{
    Oid oids[FUNC_MAX_ARGS];
    va_list ap;
    int i;
    oidvector *arg_types;
    Oid func_oid;

    AssertArg(func_name);
    AssertArg(nargs >= 0 && nargs <= FUNC_MAX_ARGS);

    va_start(ap, nargs);
    for (i = 0; i < nargs; i++)
        oids[i] = va_arg(ap, Oid);
    va_end(ap);

    arg_types = buildoidvector(oids, nargs);

    func_oid = GetSysCacheOid3(PROCNAMEARGSNSP, Anum_pg_proc_oid,
                               CStringGetDatum(func_name),
                               PointerGetDatum(arg_types),
                               ObjectIdGetDatum(pg_catalog_namespace_id()));
    if (!OidIsValid(func_oid))
    {
        ereport(ERROR, (errmsg_internal("pg function does not exist"),
                        errdetail_internal("%s(%d)", func_name, nargs)));
    }

    return func_oid;
}
