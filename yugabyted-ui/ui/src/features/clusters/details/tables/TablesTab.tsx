import React, { FC, useMemo } from 'react';
import { makeStyles, Box, LinearProgress, Breadcrumbs, Link, Typography, MenuItem } from '@material-ui/core';
import { AlertVariant, YBDropdown } from '@app/components';
import { ApiError, useGetClusterTablesQuery, GetClusterTablesApiEnum, ClusterTable, 
  useGetClusterHealthCheckQuery, useGetClusterNodesQuery, useGetClusterTabletsQuery } from '@app/api/src';
import { useToast } from '@app/helpers';
import { DatabaseList } from './DatabaseList';
import TriangleDownIcon from '@app/assets/caret-down.svg';
import { TableList } from './TableList';
import { TabletList } from './TabletList';
import { useTranslation } from 'react-i18next';

export type DatabaseListType = Array<{ name: string, tableCount: number, size: number }>
export type TableListType = Array<ClusterTable & { isIndexTable?: boolean }>;

const useStyles = makeStyles((theme) => ({
  dropdown: {
    cursor: 'pointer',
    marginRight: theme.spacing(1)
  },
  link: {
    '&:link, &:focus, &:active, &:visited, &:hover': {
      textDecoration: 'none',
      color: theme.palette.text.primary
    }
  },
  breadcrumbs: {
    marginTop: theme.spacing(3),
    marginBottom: theme.spacing(4),
  },
  dropdownContent: {
    color: "black"
  },
  dropdownHeader: {
    fontWeight: 500,
    color: theme.palette.grey[500],
    marginLeft: theme.spacing(2),
    marginRight: theme.spacing(2),
    fontSize: '11.5px',
    textTransform: 'uppercase'
  }
}));

export const TablesTab: FC<{ dbApi: GetClusterTablesApiEnum }> = ({ dbApi }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [selectedDB, setSelectedDB] = React.useState<string>();

  type SelectedTable = {
    name: string,
    uuid: string
  } | undefined

  const [selectedTable, setSelectedTable] = React.useState<SelectedTable>();

  React.useEffect(() => {
    setSelectedDB(undefined);
    setSelectedTable(undefined);
  }, [dbApi]);

  const { addToast } = useToast();

  const { data: clusterTablesResponseYsql, isFetching: isFetchingYsql, refetch: refetchYsql } =
    useGetClusterTablesQuery({
      api: GetClusterTablesApiEnum.Ysql
    },
      {
        query: {
          onError: (error: ApiError) => {
            const message = error?.error?.detail ?? '';
            addToast(AlertVariant.Error, message);
          }
        }
      }
    );
  const { data: clusterTablesResponseYcql, isFetching: isFetchingYcql, refetch: refetchYcql } =
    useGetClusterTablesQuery({
      api: GetClusterTablesApiEnum.Ycql
    },
      {
        query: {
          onError: (error: ApiError) => {
            const message = error?.error?.detail ?? '';
            addToast(AlertVariant.Error, message);
          }
        }
      }
    );

  const { refetch: refetchTablets } = useGetClusterTabletsQuery({ query: { enabled: false }});
  const { refetch: refetchNodes } = useGetClusterNodesQuery({ query: { enabled: false }});
  const { refetch: refetchHealth } = useGetClusterHealthCheckQuery({ query: { enabled: false }});

  const refetchData = () => {
    if (dbApi === GetClusterTablesApiEnum.Ycql) {
      refetchYcql()
    } else {
      refetchYsql()
    }

    refetchTablets();
    refetchNodes();
    refetchHealth();
  };

  const ysqlTableData = useMemo<TableListType>(
    () =>
      clusterTablesResponseYsql
        ? [
            ...clusterTablesResponseYsql.tables,
            ...clusterTablesResponseYsql.indexes.map((indexTable) => ({
              ...indexTable,
              isIndexTable: true,
            })),
          ]
        : [],
    [clusterTablesResponseYsql?.tables, clusterTablesResponseYsql?.indexes]
  );
  const ycqlTableData = useMemo<TableListType>(
    () => clusterTablesResponseYcql?.tables ?? [],
    [clusterTablesResponseYcql?.tables]
  );

  let isFetching = false;
  let data: TableListType;
  switch (dbApi) {
    case GetClusterTablesApiEnum.Ysql:
      isFetching = isFetchingYsql;
      data = ysqlTableData;
      break;
    case GetClusterTablesApiEnum.Ycql:
      isFetching = isFetchingYcql;
      data = ycqlTableData;
      break;
  }

  const dbKeyframeList = React.useMemo<DatabaseListType>(() => 
    Array.from(data.reduce((prev, curr) => prev.add(curr.keyspace), new Set<string>()))
      .map(dbName => {
        const filteredDb = data.filter(d => d.keyspace === dbName);
        return {
          name: dbName,
          tableCount: filteredDb.length,
          size: filteredDb.reduce((prev, curr) => prev + curr.size_bytes, 0)
        }
      }), [data]);

  const tableList = React.useMemo(() => data.filter(d => d.keyspace === selectedDB), [selectedDB, data])

  if (isFetching) {
    return (
      <Box textAlign="center" mt={2.5}>
        <LinearProgress />
      </Box>
    );
  }

  return (
    <Box>
      <Box className={classes.breadcrumbs}>
        {selectedDB &&
          <Breadcrumbs aria-label="breadcrumb">
            <Link className={classes.link} 
              onClick={() => { setSelectedTable(undefined); setSelectedDB(undefined); }}>
              <Typography variant="body2" color="primary">{dbApi}</Typography>
            </Link>
            {selectedTable ?
              <Link className={classes.link} 
                onClick={() => { setSelectedTable(undefined); }}>
                <Typography variant="body2" color="primary">
                  {selectedDB}
                </Typography>
              </Link>
              :
              <YBDropdown
                origin={
                  <Box display="flex" alignItems="center" className={classes.dropdownContent}>
                    {selectedDB}
                    <TriangleDownIcon />
                  </Box>
                }
                position={'bottom'}
                growDirection={'right'}
                className={classes.dropdown}
              >
                <Box className={classes.dropdownHeader}>{t('clusterDetail.databases.database')}</Box>
                {dbKeyframeList.map(item => (
                  <MenuItem
                    key={`keyspaces-${item.name.replace(' ', '-')}`}
                    selected={item.name === selectedDB}
                    onClick={() => setSelectedDB(item.name)}
                  >
                    {item.name}
                  </MenuItem>
                ))}
              </YBDropdown>
            }
            {selectedTable &&
              <YBDropdown
              origin={
                <Box display="flex" alignItems="center" className={classes.dropdownContent}>
                  {selectedTable.name}
                  <TriangleDownIcon />
                </Box>
              }
              position={'bottom'}
              growDirection={'right'}
              className={classes.dropdown}
            >
              <Box className={classes.dropdownHeader}>{t('clusterDetail.databases.table')}</Box>
              {tableList.filter(table => !table.isIndexTable).map(item => (
                <MenuItem
                  key={`keyspaces-${item.name.replace(' ', '-')}`}
                  selected={item.uuid === selectedTable.uuid}
                  onClick={() => setSelectedTable({ name: item.name, uuid: item.uuid }) }
                >
                  {item.name}
                </MenuItem>
              ))}
            </YBDropdown>
          }
          </Breadcrumbs>
        }
      </Box>
      {selectedDB && selectedTable ? 
        <TabletList
          selectedTableUuid={selectedTable.uuid}
          onRefetch={refetchData} />
        :
        (selectedDB ? 
          <TableList
            tableList={tableList}
            onSelect={(name: string, uuid: string) => {
                setSelectedTable({ name: name, uuid: uuid })
            }}
            onRefetch={refetchData}
          />
          :
          <DatabaseList 
            dbList={dbKeyframeList} 
            isYcql={dbApi === GetClusterTablesApiEnum.Ycql}
            onSelect={setSelectedDB}
            onRefetch={refetchData}
          />
        )
      }
    </Box>
  );
};
