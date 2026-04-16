import React, { useState, useMemo, FC, Fragment, useCallback } from 'react';
import { useForm } from 'react-hook-form';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import type { ClassNameMap } from '@material-ui/styles';
import type { MUIDataTableColumn } from 'mui-datatables';
import { TableRow, TableCell, makeStyles, Typography, Box, IconButton, LinearProgress } from '@material-ui/core';
import { useDebounce } from 'react-use';
import { useQueryParams, ArrayParam, StringParam, withDefault } from 'use-query-params';
import { AlertVariant, YBInput, YBTable, YBModal, YBCodeBlock, YBButton, YBCheckboxField } from '@app/components';
import { roundDecimal, useToast } from '@app/helpers';
import { QueryDetailsPanel, QueryRowEntry } from './QueryDetailsPanel';
import { ApiError, useGetSlowQueriesQuery, SlowQueryResponseYSQLQueryItem } from '@app/api/src';
import RefreshIcon from '@app/assets/refresh.svg';
import SearchIcon from '@app/assets/search.svg';
import EditIcon from '@app/assets/edit.svg';
import Drilldown from '@app/assets/Drilldown.svg';

const useStyles = makeStyles((theme) => ({
  searchBox: {
    maxWidth: 520,
    flexGrow: 1,
    marginRight: 'auto'
  },
  refreshBtn: {
    marginRight: theme.spacing(1)
  },
  rowTableCell: {
    borderBottom: 'unset',
    padding: theme.spacing(2, 2, 0.5, 2)
  },
  queryTableCell: {
    padding: theme.spacing(0, 2, 1, 2),
    borderBottom: '0px'
  },
  queryCodeBlock: {
    lineHeight: 1.5,
    whiteSpace: 'nowrap',
    overflow: 'hidden',
    textOverflow: 'ellipsis',
    paddingRight: theme.spacing(0)
  },
  queryContainerCode: {
    maxWidth: 900,
    cursor: 'pointer'
  },
  actionsCell: {
    width: theme.spacing(8)
  }
}));

const getRowCellComponent = (
  displayedRows: SlowQueryResponseYSQLQueryItem[],
  classes: ClassNameMap,
  showQuery: boolean,
  tableColumns: MUIDataTableColumn[],
  onSelect: (value: SlowQueryResponseYSQLQueryItem) => void
) => {
  const rowCellComponent = (data: (string | number | undefined)[], dataIndex: number) => {
    return (
      <Fragment>
        <TableRow key={`slow-queries-row-${String(data[0])}`}>
          {data.map((val: string | number | undefined, index: number) => {
            // Skip id field
            if (index) {
              if (val !== undefined && index < tableColumns.length - 1) {
                return (
                  <TableCell
                    key={`row-${dataIndex}-body-cell-${index}`} // eslint-disable-line react/no-array-index-key
                    className={showQuery ? classes.rowTableCell : ''}
                  >
                    {val}
                  </TableCell>
                );
              }
              if (index === tableColumns.length - 1) {
                // table row actions
                return (
                  <TableCell
                    key={`row-${dataIndex}-body-cell-actions`}
                    className={showQuery ? clsx(classes.rowTableCell, classes.actionsCell) : ''}
                  >
                    <IconButton aria-label="delete" onClick={() => onSelect(displayedRows[dataIndex])}>
                      <Drilldown />
                    </IconButton>
                  </TableCell>
                );
              }
            }
            return null;
          })}
        </TableRow>
        {showQuery && (
          <TableRow key={`slow-queries-query-row-${dataIndex}`}>
            <TableCell
              colSpan={tableColumns.length - 1}
              className={classes.queryTableCell}
              onClick={() => onSelect(displayedRows[dataIndex])}
            >
              <YBCodeBlock
                text={displayedRows[dataIndex].query ?? ''}
                codeClassName={classes.queryContainerCode}
                preClassName={classes.queryCodeBlock}
              />
            </TableCell>
          </TableRow>
        )}
      </Fragment>
    );
  };
  return rowCellComponent;
};

export const SlowQueries: FC = () => {
  const [showQueryOptions, setShowQueryOptions] = useState(false);
  const classes = useStyles();
  const { t } = useTranslation();
  const { addToast } = useToast();
  const [selectedQuery, setSelectedQuery] = useState<QueryRowEntry | undefined>();

  const [queryParams, setQueryParams] = useQueryParams({
    columns: withDefault(ArrayParam, []),
    search: withDefault(StringParam, '')
  });
  const [searchInput, setSearchInput] = useState(queryParams.search);

  // Change me!
  const { data: queryData, refetch, isLoading } = useGetSlowQueriesQuery(
    {
      query: {
        staleTime: Infinity,
        onError: (error: ApiError) => {
          const message = error?.error?.detail ?? '';
          addToast(AlertVariant.Error, message);
        }
      }
    }
  );

  const displayedRows = useMemo(() => {
    const data =
      queryData?.data?.ysql?.queries?.sort((rowA, rowB) => (rowB.total_time ?? 0) - (rowA.total_time ?? 0)) ?? [];
    return data.map((item) => {
      item.total_time = roundDecimal(item.total_time ?? 0);
      item.max_time = roundDecimal(item.max_time ?? 0);
      item.min_time = roundDecimal(item.min_time ?? 0);
      item.mean_time = roundDecimal(item.mean_time ?? 0);
      item.stddev_time = roundDecimal(item.stddev_time ?? 0);
      return item;
    });
  }, [queryData]);

  const getDefaultValues = useCallback(() => {
    const values: Record<string, boolean> = {
      query: true,
      user: true,
      totalTime: true,
      count: true,
      rows: true,
      database: true
    };

    if (queryParams.columns.length > 0) {
      const filters = queryParams.columns as string[];

      Object.keys(values).forEach((filter) => {
        if (filters.includes(filter)) {
          values[filter] = true;
        } else {
          values[filter] = false;
        }
      });
    }

    return values;
  }, [queryParams.columns]);

  const [columns, setColumns] = useState(getDefaultValues());
  const { control, handleSubmit, reset } = useForm({
    mode: 'onChange',
    defaultValues: columns
  });

  const queriesTableColumns = [
    {
      name: 'queryid',
      options: {
        display: false,
        filter: false
      }
    },
    columns.user && {
      name: 'rolname',
      label: t('clusterDetail.performance.slowQueries.columns.user'),
      options: {
        filter: true,
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } })
      }
    },
    columns.totalTime && {
      name: 'total_time',
      label: t('clusterDetail.performance.slowQueries.columns.totalTime'),
      options: {
        filter: true,
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } })
      }
    },
    columns.count && {
      name: 'calls',
      label: t('clusterDetail.performance.slowQueries.columns.count'),
      options: {
        filter: true,
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } })
      }
    },
    columns.rows && {
      name: 'rows',
      label: t('clusterDetail.performance.slowQueries.columns.rows'),
      options: {
        filter: true,
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } })
      }
    },
    columns.database && {
      name: 'datname',
      label: t('clusterDetail.performance.slowQueries.columns.database'),
      options: {
        filter: true,
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } })
      }
    },
    {
      name: 'actions',
      options: {
        filter: true,
        hideHeader: true,
        setCellProps: () => ({ style: { paddingLeft: 20 } })
      }
    },
    {
      name: 'query',
      options: {
        display: false,
        filter: false
      }
    }
  ].filter(Boolean) as MUIDataTableColumn[];

  const applyColumnChanges = handleSubmit((formData) => {
    setColumns(formData);
    closeQueryOptionsModal();
  });

  useDebounce(
    () => {
      const col: string[] = [];
      Object.keys(columns)
        .filter((key) => columns[key])
        .forEach((key) => col.push(key));

      setQueryParams({
        search: searchInput,
        columns: col
      });
    },
    1000,
    [columns, searchInput]
  );
  const modalTitle = t('clusterDetail.performance.queryDetailsHeader', { db: 'YSQL' });
  const closeQueryOptionsModal = () => setShowQueryOptions(false);
  const handleSelectQuery = (value: SlowQueryResponseYSQLQueryItem) => setSelectedQuery(value as QueryRowEntry);
  const handleClose = () => {
    reset(columns);
    closeQueryOptionsModal();
  };

  if (isLoading) {
    return <LinearProgress />;
  }

  return (
    <>
      <Box display="flex" alignItems="center" justifyContent="space-between" mb={2}>
        <YBInput
          placeholder={t('clusterDetail.performance.filterPlaceholder')}
          InputProps={{
            startAdornment: <SearchIcon />
          }}
          className={classes.searchBox}
          onChange={(ev) => setSearchInput(ev.target.value)}
          value={searchInput}
        />
        <YBButton variant="ghost" startIcon={<RefreshIcon />} className={classes.refreshBtn} onClick={() => refetch()}>
          {t('clusterDetail.performance.actions.refresh')}
        </YBButton>
        <YBButton variant="ghost" startIcon={<EditIcon />} onClick={() => setShowQueryOptions(true)}>
          {t('clusterDetail.performance.actions.options')}
        </YBButton>
      </Box>
      <YBTable
        data={displayedRows}
        columns={queriesTableColumns}
        options={{
          customRowRender: getRowCellComponent(
            displayedRows,
            classes,
            columns.query,
            queriesTableColumns,
            handleSelectQuery
          ),
          rowsSelected: [],
          searchText: searchInput,
          customSearch: (searchQuery, currentRow) => {
            for (let index = 1; index < currentRow.length; index++) {
              const currentDataEntry = String(currentRow[index]).toLocaleLowerCase();
              if (currentDataEntry.includes(searchQuery.toLocaleLowerCase())) {
                return true;
              }
              // Also check using search terms separated by space
              if (index === currentRow.length - 1) {
                const searchTerms = searchQuery.split(' ').filter(Boolean);
                let lastPosition = 0;
                return searchTerms.every((term) => {
                  const termIndex = currentDataEntry.indexOf(term.toLocaleLowerCase(), lastPosition);
                  if (termIndex > -1) {
                    lastPosition = termIndex + term.length;
                    return true;
                  }
                  return false;
                });
              }
            }
            return false;
          }
        }}
      />
      <YBModal
        open={showQueryOptions}
        title={t('clusterDetail.performance.queryOptions')}
        onClose={handleClose}
        onSubmit={applyColumnChanges}
        enableBackdropDismiss
        titleSeparator
        submitLabel={t('common.apply')}
        cancelLabel={t('common.cancel')}
      >
        <Box mt={1} mb={1}>
          <Typography variant="subtitle1" color="textSecondary">
            {t('clusterDetail.performance.columnsSection')}
          </Typography>
        </Box>
        <div>
          <YBCheckboxField
            name="query"
            label={t('clusterDetail.performance.slowQueries.columns.query')}
            control={control}
          />
        </div>
        <div>
          <YBCheckboxField
            name="user"
            label={t('clusterDetail.performance.slowQueries.columns.user')}
            control={control}
          />
        </div>
        <div>
          <YBCheckboxField
            name="totalTime"
            label={t('clusterDetail.performance.slowQueries.columns.totalTime')}
            control={control}
          />
        </div>
        <div>
          <YBCheckboxField
            name="count"
            label={t('clusterDetail.performance.slowQueries.columns.count')}
            control={control}
          />
        </div>
        <div>
          <YBCheckboxField
            name="rows"
            label={t('clusterDetail.performance.slowQueries.columns.rows')}
            control={control}
          />
        </div>
        <div>
          <YBCheckboxField
            name="database"
            label={t('clusterDetail.performance.slowQueries.columns.database')}
            control={control}
          />
        </div>
      </YBModal>
      <QueryDetailsPanel
        open={!!selectedQuery}
        title={modalTitle}
        currentEntry={selectedQuery}
        onClose={() => setSelectedQuery(undefined)}
      />
    </>
  );
};
