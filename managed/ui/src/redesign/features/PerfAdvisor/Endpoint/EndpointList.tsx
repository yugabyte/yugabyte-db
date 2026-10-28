import { useState } from 'react';
import { isEmpty } from 'lodash';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { TableHeaderColumn } from 'react-bootstrap-table';
import { Box, Typography } from '@material-ui/core';
import { useQuery, useQueryClient } from 'react-query';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import clsx from 'clsx';

import { YBButton, YBModal, YBTooltip } from '../../../components';
import { YBTable } from '../../../../components/common/YBTable';
import { YBErrorIndicator, YBLoading } from '../../../../components/common/indicators';
import { YBLabelWithIcon } from '../../../../components/common/descriptors';
import { AddEditEndpointModal } from './AddEditEndpointModal';
import { createErrorMessage } from '../../universe/universe-form/utils/helpers';
import { api, universeQueryKey } from '../../../helpers/api';
import {
  getListPerfAdvisorEndpointsQueryKey,
  useDeletePerfAdvisorEndpoint,
  useListPerfAdvisorEndpoints
} from '../../../../v2/api/perf-advisor-endpoint/perf-advisor-endpoint';
import {
  PerfAdvisorEndpoint,
  PerfAdvisorEndpointMetricsType
} from '../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';

//styles
import { usePillStyles } from '../../../styles/styles';
import { useExportTelemetryStyles } from '../../export-telemetry/styles';
import styles from '../../../../components/configRedesign/providerRedesign/ProviderList.module.scss';

//icons
import AuditBackupIcon from '../../../assets/backup.svg?img';
import EllipsisIcon from '../../../assets/ellipsis.svg?img';

/** One flat row per endpoint: the table keys and sorts on primitives, not on the nested spec. */
interface EndpointRow {
  uuid: string;
  name: string;
  collectionEndpoint: string;
  metricsType: string;
  universeUuids: string[];
  universeNames: string[];
  endpoint: PerfAdvisorEndpoint;
}

const TRANSLATION_KEY_PREFIX = 'clusterDetail.perfAdvisorEndpoint';

/** Enough to identify the endpoint's users at a glance; the pill still carries the exact count. */
const MAX_UNIVERSES_IN_TOOLTIP = 10;

const METRICS_TYPE_LABEL: Record<PerfAdvisorEndpointMetricsType, string> = {
  otlphttp: 'OTLP/HTTP',
  remotewrite: 'Prometheus remote write'
};

export const EndpointList = () => {
  const classes = useExportTelemetryStyles();
  const pillClasses = usePillStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  // Unprefixed, for the shared button labels.
  const { t: tCommon } = useTranslation();
  const queryClient = useQueryClient();

  const [editedEndpoint, setEditedEndpoint] = useState<PerfAdvisorEndpoint | null>(null);
  const [showEditDialog, setShowEditDialog] = useState(false);
  const [deletedEndpoint, setDeletedEndpoint] = useState<PerfAdvisorEndpoint | null>(null);

  const { data: endpoints = [], isLoading, isError } = useListPerfAdvisorEndpoints();
  const { data: universeList = [], isLoading: isUniverseListLoading } = useQuery(
    universeQueryKey.ALL,
    () => api.fetchUniverseList()
  );

  const deleteEndpoint = useDeletePerfAdvisorEndpoint({
    mutation: {
      onSuccess: () => {
        queryClient.invalidateQueries(getListPerfAdvisorEndpointsQueryKey());
        toast.success(t('deleted'));
        setDeletedEndpoint(null);
      },
      onError: (e: any) => {
        // Refused while a universe still sends here, and the message names those universes. The
        // body carries it as a field-to-messages object, so it needs flattening rather than being
        // handed to the toast as-is.
        toast.error(createErrorMessage(e) ?? t('deleteFailed'));
      }
    }
  });

  if (isError) {
    return <YBErrorIndicator customErrorMessage={t('fetchFailed')} />;
  }
  if (isLoading || isUniverseListLoading) {
    return <YBLoading />;
  }

  const universeNameByUuid = new Map(universeList.map((u) => [u.universeUUID, u.name]));

  const endpointRows: EndpointRow[] = endpoints.map((endpoint) => ({
    uuid: endpoint.info?.uuid ?? '',
    name: endpoint.spec?.name ?? '',
    collectionEndpoint: endpoint.spec?.collection_endpoint ?? '',
    metricsType: endpoint.spec?.metrics_type ? METRICS_TYPE_LABEL[endpoint.spec.metrics_type] : '',
    universeUuids: endpoint.info?.universe_uuids ?? [],
    // A universe deleted while still referenced has no name to show, so it falls back to its uuid
    // rather than rendering an empty row.
    universeNames: (endpoint.info?.universe_uuids ?? [])
      .map((uuid) => universeNameByUuid.get(uuid) ?? uuid)
      .sort((a, b) => a.localeCompare(b)),
    endpoint
  }));

  const openEditDialog = (endpoint: PerfAdvisorEndpoint | null) => {
    setEditedEndpoint(endpoint);
    setShowEditDialog(true);
  };

  const formatUsage = (_: unknown, row: EndpointRow) => {
    const names = row.universeNames;
    const shown = names.slice(0, MAX_UNIVERSES_IN_TOOLTIP);
    const remaining = names.length - shown.length;
    return names.length ? (
      <Box display="flex" gridGap="5px" alignItems="center">
        <Typography variant="body2">{t('inUse')}</Typography>
        <YBTooltip
          title={
            <ul className={classes.universeList}>
              {shown.map((name) => (
                <li key={name}>{name}</li>
              ))}
              {remaining > 0 && <li>{t('andMoreUniverses', { count: remaining })}</li>}
            </ul>
          }
        >
          <div className={clsx(pillClasses.pill, pillClasses.small, pillClasses.metadataWhite)}>
            {names.length}
          </div>
        </YBTooltip>
      </Box>
    ) : (
      <Typography variant="body2">{t('notInUse')}</Typography>
    );
  };

  const formatActions = (_: unknown, row: EndpointRow) => (
    <Dropdown id="table-actions-dropdown" pullRight onClick={(e) => e.stopPropagation()}>
      <Dropdown.Toggle noCaret>
        <img src={EllipsisIcon} alt="more" className="ellipsis-icon" />
      </Dropdown.Toggle>
      <Dropdown.Menu>
        <MenuItem
          eventKey="1"
          onSelect={() => openEditDialog(row.endpoint)}
          data-testid="EndpointList-EditConfiguration"
        >
          <YBLabelWithIcon icon="fa fa-pencil">{t('editEndpoint')}</YBLabelWithIcon>
        </MenuItem>
        <MenuItem
          eventKey="2"
          onSelect={() => setDeletedEndpoint(row.endpoint)}
          data-testid="EndpointList-DeleteConfiguration"
          // YBA refuses while a universe still forwards here, so the row says so up front rather
          // than surfacing it as a failed request.
          disabled={row.universeUuids.length > 0}
        >
          <YBLabelWithIcon icon="fa fa-trash">{t('deleteEndpoint')}</YBLabelWithIcon>
        </MenuItem>
      </Dropdown.Menu>
    </Dropdown>
  );

  const inUseCount = (deletedEndpoint?.info?.universe_uuids ?? []).length;

  return (
    <Box display="flex" flexDirection="column" width="100%" p={0.25} mt={2}>
      <Box mb={4}>
        <Typography className={classes.mainTitle}>{t('heading')}</Typography>
      </Box>

      {!isEmpty(endpointRows) ? (
        <Box className={classes.exportListContainer}>
          <Box display={'flex'} flexDirection={'row'} justifyContent={'flex-end'}>
            <YBButton
              variant="primary"
              size="large"
              onClick={() => openEditDialog(null)}
              data-testid="EndpointList-AddEndpoint"
            >
              <i className="fa fa-plus" />
              {t('addButton')}
            </YBButton>
          </Box>
          <Box mt={4} width="100%" height="100%">
            <YBTable
              data={endpointRows}
              options={{ onRowClick: (row: EndpointRow) => openEditDialog(row.endpoint) }}
              hover
              pagination
            >
              <TableHeaderColumn width="0" dataField="uuid" isKey hidden />
              <TableHeaderColumn
                width="220"
                dataField="name"
                dataSort
                dataFormat={(cell) => <span>{cell}</span>}
              >
                <span>{t('name')}</span>
              </TableHeaderColumn>
              <TableHeaderColumn
                width="280"
                dataField="collectionEndpoint"
                dataSort
                dataFormat={(cell) => <span>{cell}</span>}
              >
                <span>{t('collectionEndpoint')}</span>
              </TableHeaderColumn>
              <TableHeaderColumn
                width="180"
                dataField="metricsType"
                dataSort
                dataFormat={(cell) => <span>{cell}</span>}
              >
                <span>{t('metricsType')}</span>
              </TableHeaderColumn>
              <TableHeaderColumn width="200" dataFormat={formatUsage}>
                <span>{t('universesUsing')}</span>
              </TableHeaderColumn>
              <TableHeaderColumn
                columnClassName={styles.exportActionsColumn}
                dataFormat={formatActions}
                width="70"
              ></TableHeaderColumn>
            </YBTable>
          </Box>
        </Box>
      ) : (
        <Box className={classes.emptyContainer}>
          <Box display={'flex'} flexDirection={'column'} alignItems={'center'}>
            <img src={AuditBackupIcon} alt="--" height={48} width={48} />
            <Box mt={3} mb={2}>
              <YBButton
                variant="primary"
                size="large"
                onClick={() => openEditDialog(null)}
                data-testid="EndpointList-CreateEndpoint"
              >
                {t('addButton')}
              </YBButton>
            </Box>
            <Typography variant="body2">{t('emptyList')}</Typography>
          </Box>
        </Box>
      )}

      {showEditDialog && (
        <AddEditEndpointModal
          open={showEditDialog}
          endpoint={editedEndpoint}
          onClose={() => {
            setShowEditDialog(false);
            setEditedEndpoint(null);
          }}
        />
      )}

      {deletedEndpoint && (
        <YBModal
          open={true}
          title={t('deleteTitle')}
          onClose={() => setDeletedEndpoint(null)}
          onSubmit={() => deleteEndpoint.mutate({ peUUID: deletedEndpoint.info!.uuid! })}
          cancelLabel={tCommon('common.cancel')}
          submitLabel={tCommon('common.delete')}
          isSubmitting={deleteEndpoint.isLoading}
          size="sm"
          titleSeparator
          overrideHeight="fit-content"
          submitTestId="EndpointList-DeleteConfirm"
          cancelTestId="EndpointList-DeleteCancel"
        >
          <Box pt={2} pb={2}>
            <Typography variant="body2">
              {inUseCount > 0
                ? t('deleteBlocked', { name: deletedEndpoint.spec?.name, count: inUseCount })
                : t('deleteConfirm', { name: deletedEndpoint.spec?.name })}
            </Typography>
          </Box>
        </YBModal>
      )}
    </Box>
  );
};
