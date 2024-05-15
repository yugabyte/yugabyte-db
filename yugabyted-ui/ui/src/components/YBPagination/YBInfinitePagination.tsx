import React, { useEffect, useState, useCallback } from 'react';
import { Box, Grid, makeStyles, MenuItem } from '@material-ui/core';
import { useQuery, useQueryClient } from 'react-query';
import { YBSelect } from '@app/components/YBSelect/YBSelect';
import { YBPagination } from './YBPagination';
import { useMount } from 'react-use';

const useStyles = makeStyles(() => ({
  root: {
    display: 'grid',
    gridTemplateColumns: '1fr auto',
    gridColumnGap: '5px',
    justifyItems: 'center'
  },
  dropdownContainer: {
    display: 'flex',
    alignItems: 'center',
    flexWrap: 'wrap'
  }
}));

interface YBInfinitePaginationProps<T> {
  dataPrefetchFn: (skip: number, limit: number) => Promise<T[]>;
  render?: (item: T, index: number) => React.ReactNode;
  prefetchRecordSize?: number;
  queryKey: string;
  recordsPerPage?: number[];
  showRecordsPerPage?: boolean;
  recordsPerPageCaption?: string | React.ReactNode;
  setDataSource?: (items: T[]) => void;
  staleTime?: number;
}

export const YBInfinitePagination = <T,>(props: YBInfinitePaginationProps<T>): React.ReactElement => {
  const classes = useStyles();
  const [currentPage, setCurrentPage] = useState(1);
  const [totalPages, setTotalPages] = useState(1);
  const [hasMoreData, setHasMoreData] = useState(true);
  const {
    prefetchRecordSize = 0,
    dataPrefetchFn,
    render,
    queryKey,
    recordsPerPage = [10, 20, 50],
    showRecordsPerPage,
    recordsPerPageCaption = '',
    setDataSource,
    staleTime = 5000 // default 5 sec to cache the results
  } = props;

  const [limit, setLimit] = useState(recordsPerPage[0]);

  const queryClient = useQueryClient();

  const { data } = useQuery(
    [queryKey, currentPage, limit],
    () => {
      return dataPrefetchFn(currentPage, limit);
    },
    {
      keepPreviousData: true,
      staleTime
    }
  );

  const prefetchPages = useCallback(
    async (prefetchSize: number, tlimit: number) => {
      for (let count = 1; count <= prefetchSize; count++) {
        await queryClient.prefetchQuery([queryKey, count, tlimit], () => dataPrefetchFn(count, tlimit));
        const prefetchedData = await queryClient.getQueryData([queryKey, count, tlimit]);

        setTotalPages(count);

        if (prefetchedData === undefined || (Array.isArray(prefetchedData) && prefetchedData.length !== tlimit)) {
          //We don't have any Data further
          setHasMoreData(false);
          break;
        }
      }
    },
    [dataPrefetchFn, queryClient, queryKey]
  );

  useMount(() => {
    void prefetchPages(prefetchRecordSize, limit);
  });

  useEffect(() => {
    if (typeof setDataSource === 'function' && data !== undefined) {
      setDataSource(data);
    }
  }, [setDataSource, data]);

  const handleOnPageSelect = useCallback(
    async (page) => {
      setCurrentPage(page);

      if (hasMoreData && page === totalPages) {
        setTotalPages(totalPages + 1);
        await queryClient.prefetchQuery([queryKey, totalPages + 1, limit], () => {
          return dataPrefetchFn(totalPages + 1, limit);
        });
        const prefetchedData = await queryClient.getQueryData([queryKey, totalPages + 1, limit]);
        if (prefetchedData === undefined || (Array.isArray(prefetchedData) && prefetchedData.length !== limit)) {
          //We don't have any Data further
          setHasMoreData(false);
        }
      }
    },
    [dataPrefetchFn, hasMoreData, limit, queryClient, queryKey, totalPages]
  );

  return (
    <>
      {data && typeof render === 'function' ? data.map((item, index) => render(item, index)) : null}
      <Grid container direction="row" alignItems="center" justify="space-between" className={classes.root}>
        <Grid item xs={12}>
          <YBPagination currentPage={currentPage} pageCount={totalPages} onPageSelect={handleOnPageSelect} />
        </Grid>
        {showRecordsPerPage && (
          <Grid item xs={12} className={classes.dropdownContainer}>
            <Box mr={1}>{recordsPerPageCaption} </Box>
            <YBSelect
              value={limit}
              onChange={(event) => {
                const newLimit = parseInt(event.target.value);
                setLimit(newLimit);
                setHasMoreData(true);
                setCurrentPage(1);
                setTotalPages(1);
                void prefetchPages(prefetchRecordSize, newLimit);
              }}
            >
              {recordsPerPage.map((recordSize) => (
                <MenuItem value={recordSize} key={recordSize}>
                  {recordSize}
                </MenuItem>
              ))}
            </YBSelect>
          </Grid>
        )}
      </Grid>
    </>
  );
};
