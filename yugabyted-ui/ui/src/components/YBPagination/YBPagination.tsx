import React, { FC, useEffect, useState } from 'react';
import { makeStyles } from '@material-ui/core';
import { Pagination } from '@material-ui/lab';

const useStyles = makeStyles(() => ({
  root: {
    maxWidth: 300
  }
}));

interface PaginationProps {
  onPageSelect: (pageNo: number) => void;
  pageCount?: number;
  currentPage?: number;
}

export const YBPagination: FC<PaginationProps> = ({ pageCount = 4, onPageSelect, currentPage }) => {
  const classes = useStyles();
  const [page, setPage] = useState(1);

  useEffect(() => {
    if (currentPage) {
      setPage(currentPage);
    }
  }, [currentPage]);

  return (
    <Pagination
      className={classes.root}
      siblingCount={0}
      page={page}
      boundaryCount={1}
      shape="rounded"
      count={pageCount}
      onChange={(_e, newpage) => {
        setPage(newpage);
        onPageSelect(newpage);
      }}
    />
  );
};
