import React, { useState, useMemo, FC, Fragment, useCallback } from 'react';
import clsx from 'clsx';
import { useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import type { ClassNameMap } from '@material-ui/styles';
import type { MUIDataTableColumn } from 'mui-datatables';
import {
  TableRow,
  TableCell,
  makeStyles,
  Typography,
  Box,
  MenuItem,
  IconButton,
  LinearProgress
} from '@material-ui/core';
import { roundDecimal, useToast } from '@app/helpers';
import {
  AlertVariant,
  YBSelect,
  YBInput,
  YBTable,
  YBModal,
  YBCodeBlock,
  YBButton,
  YBCheckboxField
} from '@app/components';
import {
  ApiError,
  useGetLiveQueriesQuery,
  GetLiveQueriesApiEnum,
  LiveQueryResponseYCQLQueryItem,
  LiveQueryResponseYSQLQueryItem
} from '@app/api/src';
import { QueryDetailsPanel, QueryRowEntry } from './QueryDetailsPanel';
import RefreshIcon from '@app/assets/refresh.svg';
import SearchIcon from '@app/assets/search.svg';
import EditIcon from '@app/assets/edit.svg';
import DrillDownIcon from '@app/assets/Drilldown.svg';
import { useDebounce } from 'react-use';
import { useQueryParams, ArrayParam, StringParam, withDefault } from 'use-query-params';

const useStyles = makeStyles((theme) => ({
  dropdown: {
    width: 105,
    marginRight: theme.spacing(1),
    flexGrow: 0
  },
  searchBox: {
    maxWidth: 520,
    flexGrow: 1,
    marginRight: 'auto'
  },
  refreshBtn: {
    marginRight: theme.spacing(1)
  },
  queryTableRow: {
    display: 'flex',
    flexWrap: 'wrap'
  },
  rowTableCell: {
    borderBottom: 'unset'
  },
  actionsCell: {
    width: theme.spacing(8)
  },
  queryTableCell: {
    padding: theme.spacing(0, 2, 1, 2)
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
  }
}));

const TABLE_ACTIONS_INDEX = 7;

const getRowCellComponent = (
  displayedRows: (LiveQueryResponseYCQLQueryItem | LiveQueryResponseYSQLQueryItem)[],
  classes: ClassNameMap,
  showQuery: boolean,
  onSelect: (value: LiveQueryResponseYCQLQueryItem | LiveQueryResponseYSQLQueryItem) => void
) => {
  const rowCellComponent = (data: (string | number | undefined)[], dataIndex: number) => {
    return (
      <Fragment>
        <TableRow key={`live-queries-row-${String(data[0])}`}>
          {data.map((val: string | number | undefined, index: number) => {
            if (index) {
              // Skip id field
              if (val !== undefined && index < TABLE_ACTIONS_INDEX) {
                return (
                  <TableCell
                    key={`row-${dataIndex}-body-cell-${index}`} // eslint-disable-line react/no-array-index-key
                    className={showQuery ? classes.rowTableCell : ''}
                  >
                    {val}
                  </TableCell>
                );
              }
              if (index === TABLE_ACTIONS_INDEX) {
                // table row actions
                return (
                  <TableCell
                    key={`row-${dataIndex}-body-cell-actions`}
                    className={clsx(classes.rowTableCell, classes.actionsCell)}
                  >
                    <IconButton onClick={() => onSelect(displayedRows[dataIndex])}>
                      <DrillDownIcon />
                    </IconButton>
                  </TableCell>
                );
              }
            }
            return null;
          })}
        </TableRow>
        {showQuery && (
          <TableRow>
            <TableCell
              colSpan={7}
              className={classes.queryTableCell}
              onClick={() => onSelect(displayedRows[dataIndex])}
            >
              <YBCodeBlock
                text={displayedRows[dataIndex].query || 'No query data'}
                preClassName={classes.queryCodeBlock}
                codeClassName={classes.queryContainerCode}
              />
            </TableCell>
          </TableRow>
        )}
      </Fragment>
    );
  };
  return rowCellComponent;
};

export const LiveQueries: FC = () => {
  const [selectedQuery, setSelectedQuery] = useState<QueryRowEntry | undefined>();
  const [showQueryOptions, setShowQueryOptions] = useState(false);
  const classes = useStyles();
  const { addToast } = useToast();
  const { t } = useTranslation();

  const [queryParams, setQueryParams] = useQueryParams({
    api: withDefault(StringParam, GetLiveQueriesApiEnum.Ysql),
    columns: withDefault(ArrayParam, []),
    search: withDefault(StringParam, '')
  });

  const [dbApi, setDbApi] = useState(
    Object.values(GetLiveQueriesApiEnum).includes(queryParams.api as GetLiveQueriesApiEnum)
      ? queryParams.api
      : GetLiveQueriesApiEnum.Ysql
  );
  const [searchInput, setSearchInput] = useState(queryParams.search);

  const { data: queryData, refetch, isLoading } = useGetLiveQueriesQuery(
    {
      api: (dbApi as GetLiveQueriesApiEnum) ?? GetLiveQueriesApiEnum.Ysql
    },
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
    let tableData: (LiveQueryResponseYSQLQueryItem | LiveQueryResponseYCQLQueryItem)[];
    if (dbApi === GetLiveQueriesApiEnum.Ysql) {
      tableData = queryData?.data?.ysql?.queries ?? [];
    } else {
      tableData = queryData?.data?.ycql?.queries ?? [];
    }
    return tableData.map((item: LiveQueryResponseYSQLQueryItem | LiveQueryResponseYCQLQueryItem) => {
      item.elapsed_millis = roundDecimal(item.elapsed_millis ?? 0);
      return item;
    });
  }, [queryData, dbApi]);

  const getDefaultValues = useCallback(() => {
    const values: Record<string, boolean> = {
      query: true,
      nodeName: true,
      elapsedTime: true,
      type: true,
      clientHost: true,
      clientPort: true
    };

    if (dbApi === GetLiveQueriesApiEnum.Ysql) {
      values.database = true;
    } else {
      values.keyspace = true;
    }

    if (queryParams.columns.length > 0) {
      const filters = queryParams.columns as string[];

      Object.keys(values).forEach((filter) => {
        if (filters.includes(filter)) {
          values[filter] = true;
        } else {
          values[filter] = false;
        }
      });

      /**
       * when we switch between ysql and ycql, we should map keyspace to database and vice versa
       * else it will be removed from the query param and it'll never show
       */
      if (dbApi === GetLiveQueriesApiEnum.Ysql && filters.includes('keyspace')) {
        values.database = true;
      }

      if (dbApi === GetLiveQueriesApiEnum.Ysql && filters.includes('database')) {
        values.keyspace = true;
      }
    }

    return values;
  }, [dbApi, queryParams.columns]);

  const [columns, setColumns] = useState(getDefaultValues());
  const { control, handleSubmit, reset } = useForm({
    mode: 'onChange',
    defaultValues: columns
  });

  const queriesTableColumns = [
    {
      name: 'id',
      options: {
        display: false,
        filter: false
      }
    },
    columns.nodeName && {
      name: 'node_name',
      label: t('clusterDetail.performance.liveQueries.columns.nodeName'),
      options: {
        filter: true,
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } })
      }
    },
    columns.database && {
      name: dbApi === GetLiveQueriesApiEnum.Ysql ? 'db_name' : 'keyspace',
      label:
        dbApi === GetLiveQueriesApiEnum.Ysql
          ? t('clusterDetail.performance.liveQueries.columns.database')
          : t('clusterDetail.performance.liveQueries.columns.keyspace'),
      options: {
        filter: true,
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } })
      }
    },
    columns.elapsedTime && {
      name: 'elapsed_millis',
      label: t('clusterDetail.performance.liveQueries.columns.elapsedTime'),
      options: {
        filter: true,
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } })
      }
    },
    columns.type && {
      name: dbApi === GetLiveQueriesApiEnum.Ysql ? 'session_status' : 'type',
      label:
        dbApi === GetLiveQueriesApiEnum.Ysql
          ? t('clusterDetail.performance.liveQueries.columns.status')
          : t('clusterDetail.performance.liveQueries.columns.type'),
      options: {
        filter: true,
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } })
      }
    },
    columns.clientHost && {
      name: 'client_host',
      label: t('clusterDetail.performance.liveQueries.columns.clientHost'),
      options: {
        filter: true,
        setCellHeaderProps: () => ({ style: { padding: '8px 16px' } })
      }
    },
    columns.clientPort && {
      name: 'client_port',
      label: t('clusterDetail.performance.liveQueries.columns.clientPort'),
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
        columns: col,
        api: dbApi
      });
    },
    1000,
    [columns, searchInput, dbApi]
  );

  const closeQueryOptionsModal = () => {
    setShowQueryOptions(false);
  };

  const handleClose = () => {
    reset(columns);
    closeQueryOptionsModal();
  };
  const handleSelectQuery = (value: LiveQueryResponseYCQLQueryItem | LiveQueryResponseYSQLQueryItem) => {
    setSelectedQuery(value as QueryRowEntry);
  };
  if (isLoading) {
    return <LinearProgress />;
  }

  return (
    <>
      <Box display="flex" alignItems="center" justifyContent="space-between" mb={2}>
        <YBSelect
          value={dbApi}
          onChange={(ev) => setDbApi(ev.target.value as GetLiveQueriesApiEnum)}
          className={classes.dropdown}
        >
          <MenuItem value={GetLiveQueriesApiEnum.Ysql}>{'YSQL'}</MenuItem>
          <MenuItem value={GetLiveQueriesApiEnum.Ycql}>{'YCQL'}</MenuItem>
        </YBSelect>
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
          pagination: false,
          customRowRender: getRowCellComponent(displayedRows, classes, columns.query, handleSelectQuery),
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
        <Typography variant="subtitle1" color="textSecondary">
          {t('clusterDetail.performance.columnsSection')}
        </Typography>
        <Box mt={1}>
          <YBCheckboxField
            name="query"
            label={t('clusterDetail.performance.liveQueries.columns.query')}
            control={control}
          />
        </Box>
        <div>
          <YBCheckboxField
            name="nodeName"
            label={t('clusterDetail.performance.liveQueries.columns.nodeName')}
            control={control}
          />
        </div>
        <div>
          <YBCheckboxField
            name="database"
            label={
              dbApi === GetLiveQueriesApiEnum.Ysql
                ? t('clusterDetail.performance.liveQueries.columns.database')
                : t('clusterDetail.performance.liveQueries.columns.keyspace')
            }
            control={control}
          />
        </div>
        <div>
          <YBCheckboxField
            name="elapsedTime"
            label={t('clusterDetail.performance.liveQueries.columns.elapsedTime')}
            control={control}
          />
        </div>
        <div>
          <YBCheckboxField
            name="type"
            label={
              dbApi === GetLiveQueriesApiEnum.Ysql
                ? t('clusterDetail.performance.liveQueries.columns.status')
                : t('clusterDetail.performance.liveQueries.columns.type')
            }
            control={control}
          />
        </div>
        <div>
          <YBCheckboxField
            name="clientHost"
            label={t('clusterDetail.performance.liveQueries.columns.clientHost')}
            control={control}
          />
        </div>
        <div>
          <YBCheckboxField
            name="clientPort"
            label={t('clusterDetail.performance.liveQueries.columns.clientPort')}
            control={control}
          />
        </div>
      </YBModal>
      <QueryDetailsPanel
        open={!!selectedQuery}
        title={t('clusterDetail.performance.queryDetailsHeader', { db: dbApi })}
        currentEntry={selectedQuery}
        onClose={() => setSelectedQuery(undefined)}
      />
    </>
  );
};
