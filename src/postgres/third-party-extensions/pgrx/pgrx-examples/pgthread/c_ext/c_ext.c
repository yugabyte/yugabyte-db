#include "postgres.h"

#include "fmgr.h"
#include "executor/spi.h"
#include "miscadmin.h"
#include "utils/memutils.h"

#include <pthread.h>

PG_FUNCTION_INFO_V1(start_thread);

typedef struct ThreadResult
{
    int  spi_rc;
    int  exec_rc;
    bool ok;
} ThreadResult;

static void *spi_thread_main(void *arg)
{
    ThreadResult *res = (ThreadResult *) arg;

    /*
     * IMPORTANT: This is not how Postgres is meant to be used.
     * We do *some* “make SPI less immediately explode” setup anyway.
     */

    /* Create a private memory context for the thread's work */
    MemoryContext thread_mcxt =
        AllocSetContextCreate(TopMemoryContext,
                              "pthread SPI context",
                              ALLOCSET_DEFAULT_SIZES);

    /* Create a private resource owner */
    ResourceOwner thread_owner = ResourceOwnerCreate(NULL, "pthread SPI owner");

    /* Switch global pointers (this is where things get truly unsafe) */
    MemoryContext old_mcxt = CurrentMemoryContext;
    ResourceOwner old_owner = CurrentResourceOwner;

    CurrentMemoryContext = thread_mcxt;
    CurrentResourceOwner = thread_owner;

    res->ok = false;
    res->spi_rc = -1;
    res->exec_rc = -1;

    PG_TRY();
    {
        res->spi_rc = SPI_connect();
        if (res->spi_rc != SPI_OK_CONNECT)
            ereport(ERROR, (errmsg("SPI_connect failed: %d", res->spi_rc)));

        res->exec_rc = SPI_execute("SELECT 1;", true, 0);
        if (res->exec_rc < 0)
            ereport(ERROR, (errmsg("SPI_execute failed: %d", res->exec_rc)));

        (void) SPI_finish();
        res->ok = true;
    }
    PG_CATCH();
    {
        /*
         * Don't rethrow; swallow so the main thread can return normally.
         * (Error state is global; this is still not “clean”.)
         */
        FlushErrorState();
        res->ok = false;

        /* Best effort to unwind SPI if we got that far */
        (void) SPI_finish();
    }
    PG_END_TRY();

    /* Restore globals */
    CurrentResourceOwner = old_owner;
    CurrentMemoryContext = old_mcxt;

    /* Cleanup thread-local allocations */
    ResourceOwnerRelease(thread_owner, RESOURCE_RELEASE_BEFORE_LOCKS, false, false);
    ResourceOwnerRelease(thread_owner, RESOURCE_RELEASE_LOCKS, false, true);
    ResourceOwnerRelease(thread_owner, RESOURCE_RELEASE_AFTER_LOCKS, false, false);
    ResourceOwnerDelete(thread_owner);

    MemoryContextDelete(thread_mcxt);

    return NULL;
}

PGDLLEXPORT Datum
start_thread(PG_FUNCTION_ARGS)
{
    pthread_t tid;
    ThreadResult res;

    int err = pthread_create(&tid, NULL, spi_thread_main, &res);
    if (err != 0)
        ereport(ERROR, (errmsg("pthread_create failed: %d", err)));

    err = pthread_join(tid, NULL);
    if (err != 0)
        ereport(ERROR, (errmsg("pthread_join failed: %d", err)));

    if (!res.ok)
        ereport(ERROR, (errmsg("pthread SPI work failed (spi_rc=%d exec_rc=%d)",
                               res.spi_rc, res.exec_rc)));

    PG_RETURN_VOID();
}
