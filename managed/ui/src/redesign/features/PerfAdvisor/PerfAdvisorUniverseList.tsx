import { useState } from 'react';
import { Link } from 'react-router';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { RemoteObjSpec, SortOrder, TableHeaderColumn } from 'react-bootstrap-table';
import { Box, Button, Typography } from '@material-ui/core';
import { YBErrorIndicator, YBLoading } from '../../../components/common/indicators';
import { YBTable } from '../../../components/common/YBTable/YBTable';
import { QUERY_KEY, PerfAdvisorAPI } from './api';
import { toast } from 'react-toastify';

interface PerfAdvisorUniverseListProps {
  paUuid: string;
  isNewPerfAdvisorUIEnabled: boolean;
}

interface PaUniverseInfo {
  universeUuid: string;
  universeName: string | null;
  dataMountPoints: string[];
  otherMountPoints: string[];
  advancedObservability: boolean;
}

interface PagedResponse {
  entities: PaUniverseInfo[];
  hasNext: boolean;
  hasPrev: boolean;
  totalCount: number;
}

const DEFAULT_LIMIT = 10;
const DEFAULT_SORT_COLUMN = 'universeName';
const DEFAULT_SORT_DIRECTION = 'ASC';

export const PerfAdvisorUniverseList = ({ paUuid, isNewPerfAdvisorUIEnabled }: PerfAdvisorUniverseListProps) => {
  const [page, setPage] = useState(1);
  const [sizePerPage, setSizePerPage] = useState(DEFAULT_LIMIT);
  const [sortColumn] = useState(DEFAULT_SORT_COLUMN);
  const [sortDirection, setSortDirection] = useState(DEFAULT_SORT_DIRECTION);
  const queryClient = useQueryClient();

  const offset = (page - 1) * sizePerPage;

  const { data, isLoading, isError } = useQuery<PagedResponse>(
    [QUERY_KEY.pageRegisteredUniverses, paUuid, offset, sizePerPage, sortColumn, sortDirection],
    () =>
      PerfAdvisorAPI.pageRegisteredUniverses(
        paUuid,
        offset,
        sizePerPage,
        sortColumn,
        sortDirection
      ),
    { keepPreviousData: true }
  );

  const unregisterMutation = useMutation(
    (universeUuid: string) => PerfAdvisorAPI.deleteUniverseRegistration(universeUuid),
    {
      onSuccess: (resp: any) => {
        const msg = resp?.taskUUID
          ? 'Universe unregistration initiated'
          : 'Universe unregistered successfully';
        toast.success(msg);
        queryClient.invalidateQueries(QUERY_KEY.pageRegisteredUniverses);
      },
      onError: () => {
        toast.error('Failed to unregister universe');
      }
    }
  );

  if (isError) {
    return <YBErrorIndicator customErrorMessage="Failed to load registered universes" />;
  }
  if (isLoading && !data) {
    return <YBLoading />;
  }

  const universes = data?.entities ?? [];
  const totalCount = data?.totalCount ?? 0;

  const formatMountPoints = (_cell: any, row: PaUniverseInfo, field: keyof PaUniverseInfo) => {
    const points = row[field] as string[];
    return points?.join(', ') ?? '';
  };

  const formatAdvancedObservability = (_cell: any, row: PaUniverseInfo) => {
    return row.advancedObservability ? 'Enabled' : 'Disabled';
  };

  const formatActions = (_cell: any, row: PaUniverseInfo) => {
    return (
      <Button
        variant="outlined"
        size="small"
        color="secondary"
        onClick={(e) => {
          e.stopPropagation();
          unregisterMutation.mutate(row.universeUuid);
        }}
        disabled={unregisterMutation.isLoading}
      >
        {'Unregister'}
      </Button>
    );
  };

  const formatUniverseName = (_cell: any, row: PaUniverseInfo) => {
    const displayName = row.universeName ?? `${row.universeUuid} (deleted)`;
    if (!row.universeName) {
      return displayName;
    }
    const path = row.advancedObservability && isNewPerfAdvisorUIEnabled
      ? `/universes/${row.universeUuid}/perfAdvisor`
      : `/universes/${row.universeUuid}`;
    return <Link to={path}>{displayName}</Link>;
  };

  return (
    <Box mt={2}>
      {totalCount === 0 && !isLoading ? (
        <Typography variant="body1">No universes registered with this PA collector.</Typography>
      ) : (
        <YBTable
          data={universes}
          pagination={true}
          remote={(remoteObj: RemoteObjSpec) => {
            return { ...remoteObj, pagination: true };
          }}
          fetchInfo={{ dataTotalSize: totalCount }}
          options={{
            sizePerPage,
            onSizePerPageList: setSizePerPage,
            page,
            onPageChange: (newPage: number) => setPage(newPage),
            defaultSortOrder: DEFAULT_SORT_DIRECTION.toLowerCase() as SortOrder,
            defaultSortName: DEFAULT_SORT_COLUMN,
            onSortChange: (_: any, order: SortOrder) =>
              setSortDirection(order.toUpperCase())
          }}
        >
          <TableHeaderColumn dataField="universeUuid" isKey={true} width="20%">
            Universe UUID
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="universeName"
            dataFormat={formatUniverseName}
            dataSort
            width="18%"
          >
            {'Universe Name'}
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="dataMountPoints"
            dataFormat={(cell: any, row: PaUniverseInfo) =>
              formatMountPoints(cell, row, 'dataMountPoints')
            }
            width="17%"
          >
            {'Data Mount Points'}
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="otherMountPoints"
            dataFormat={(cell: any, row: PaUniverseInfo) =>
              formatMountPoints(cell, row, 'otherMountPoints')
            }
            width="17%"
          >
            Other Mount Points
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="advancedObservability"
            dataFormat={formatAdvancedObservability}
            width="13%"
          >
            {'Advanced Observability'}
          </TableHeaderColumn>
          <TableHeaderColumn dataField="actions" dataFormat={formatActions} width="10%">
            {'Actions'}
          </TableHeaderColumn>
        </YBTable>
      )}
    </Box>
  );
};
