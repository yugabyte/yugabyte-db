/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React, { useState } from 'react';
import { useQuery } from 'react-query';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { Link } from 'react-router';
import { Box, Typography } from '@material-ui/core';

import { api, providerQueryKey, universeQueryKey } from '../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import {
  ProviderLabel,
  CloudVendorProviders,
  ProviderCode,
  KubernetesProviderType,
  KUBERNETES_PROVIDERS_MAP,
  PROVIDER_ROUTE_PREFIX,
  KubernetesProviderTypeLabel
} from './constants';
import { EmptyListPlaceholder } from './EmptyListPlaceholder';
import { ProviderDashboardView } from './InfraProvider';
import { RegionsCell } from './RegionsCell';
import { YBLabelWithIcon } from '../../common/descriptors';
import ellipsisIcon from '../../common/media/more.svg';
import { DeleteProviderConfigModal } from './DeleteProviderConfigModal';
import { UniverseItem } from './providerView/providerDetails/UniverseTable';
import { getLinkedUniverses, usePillStyles } from './utils';
import { YBButton } from '../../../redesign/components';

import { YBProvider, YBRegion } from './types';

import styles from './ProviderList.module.scss';

interface ProviderListCommonProps {
  setCurrentView: (newView: ProviderDashboardView) => void;
}
interface GenericProviderListProps extends ProviderListCommonProps {
  providerCode: typeof CloudVendorProviders[number] | typeof ProviderCode.ON_PREM;
}
interface K8sProviderListProps extends ProviderListCommonProps {
  providerCode: typeof ProviderCode.KUBERNETES;
  kubernetesProviderType: KubernetesProviderType;
}
type ProviderListProps = GenericProviderListProps | K8sProviderListProps;
type ProviderListItem = YBProvider & {
  linkedUniverses: UniverseItem[];
};

const TABLE_MIN_PAGE_SIZE = 10;

export const ProviderList = (props: ProviderListProps) => {
  const { providerCode, setCurrentView } = props;
  const [isDeleteProviderModalOpen, setIsDeleteProviderModalOpen] = useState<boolean>(false);
  const [deleteProviderConfigSelection, setDeleteProviderConfigSelection] = useState<YBProvider>();
  const classes = usePillStyles();
  const {
    data: providerList,
    isLoading: isProviderListQueryLoading,
    isError: isProviderListQueryError,
    isIdle: isProviderListQueryIdle
  } = useQuery(providerQueryKey.ALL, () => api.fetchProviderList());
  const {
    data: universeList,
    isLoading: isUniverseListQueryLoading,
    isError: isUniverseListQueryError,
    isIdle: isUniverseListQueryIdle
  } = useQuery(universeQueryKey.ALL, () => api.fetchUniverseList());

  if (
    isProviderListQueryLoading ||
    isProviderListQueryIdle ||
    isUniverseListQueryLoading ||
    isUniverseListQueryIdle
  ) {
    return <YBLoading />;
  }
  if (isProviderListQueryError) {
    return <YBErrorIndicator customErrorMessage="Error fetching provider list." />;
  }
  if (isUniverseListQueryError) {
    return <YBErrorIndicator customErrorMessage="Error fetching universe list." />;
  }

  const showDeleteProviderModal = () => {
    setIsDeleteProviderModalOpen(true);
  };
  const hideDeleteProviderModal = () => {
    setIsDeleteProviderModalOpen(false);
  };
  const handleDeleteProviderConfig = (providerConfig: ProviderListItem) => {
    setDeleteProviderConfigSelection(providerConfig);
    showDeleteProviderModal();
  };
  const handleCreateProviderAction = () => {
    setCurrentView(ProviderDashboardView.CREATE);
  };

  const formatProviderName = (providerName: string, row: ProviderListItem) => {
    return (
      <Link
        to={`/${PROVIDER_ROUTE_PREFIX}/${
          providerCode === ProviderCode.KUBERNETES ? props.kubernetesProviderType : providerCode
        }/${row.uuid}`}
      >
        {providerName}
      </Link>
    );
  };
  const formatRegions = (regions: YBRegion[]) => <RegionsCell regions={regions} />;
  const formatProviderActions = (_: unknown, row: ProviderListItem) => {
    return (
      <Dropdown id="table-actions-dropdown" pullRight>
        <Dropdown.Toggle noCaret>
          <img src={ellipsisIcon} alt="more" className="ellipsis-icon" />
        </Dropdown.Toggle>
        <Dropdown.Menu>
          <MenuItem
            eventKey="1"
            onSelect={() => handleDeleteProviderConfig(row)}
            data-testid="DeleteConfiguration-button"
            disabled={row.linkedUniverses.length > 0}
          >
            <YBLabelWithIcon icon="fa fa-trash">Delete Configuration</YBLabelWithIcon>
          </MenuItem>
        </Dropdown.Menu>
      </Dropdown>
    );
  };

  const formatUsage = (_: unknown, row: ProviderListItem) => {
    return row.linkedUniverses.length ? (
      <Box display="flex" gridGap="5px">
        <div>In Use</div>
        <div className={classes.pill}>{row.linkedUniverses.length}</div>
      </Box>
    ) : (
      'Not in Use'
    );
  };

  const filteredProviderList: ProviderListItem[] = providerList
    .filter((provider) =>
      providerCode === ProviderCode.KUBERNETES
        ? provider.code === providerCode &&
          (KUBERNETES_PROVIDERS_MAP[props.kubernetesProviderType] as readonly string[]).includes(
            provider.details.cloudInfo.kubernetes.kubernetesProvider
          )
        : provider.code === providerCode
    )
    .map((provider) => {
      const linkedUniverses = getLinkedUniverses(provider.uuid, universeList);
      return { ...provider, linkedUniverses: linkedUniverses };
    });
  return (
    <>
      <Box display="flex" marginBottom="35px">
        <Typography variant="h4">{`${
          providerCode === ProviderCode.KUBERNETES && props.kubernetesProviderType
            ? KubernetesProviderTypeLabel[props.kubernetesProviderType]
            : ProviderLabel[providerCode]
        } Configs`}</Typography>
        {filteredProviderList.length > 0 && (
          <YBButton
            style={{ marginLeft: 'auto', width: '200px' }}
            variant="primary"
            onClick={() => setCurrentView(ProviderDashboardView.CREATE)}
            data-testid="ProviderListView-CreateConfigButton"
          >
            <i className="fa fa-plus" />
            Create Config
          </YBButton>
        )}
      </Box>
      {filteredProviderList.length === 0 ? (
        <EmptyListPlaceholder
          actionButtonText={`Create ${ProviderLabel[providerCode]} Config`}
          descriptionText={`No ${ProviderLabel[providerCode]} config to show`}
          onActionButtonClick={handleCreateProviderAction}
          dataTestIdPrefix="ProviderEmptyList"
        />
      ) : (
        <>
          <div className={styles.bootstrapTableContainer}>
            <BootstrapTable
              tableContainerClass={styles.bootstrapTable}
              data={filteredProviderList}
              pagination={filteredProviderList.length > TABLE_MIN_PAGE_SIZE}
              hover
            >
              <TableHeaderColumn
                dataField="name"
                isKey={true}
                dataSort={true}
                dataFormat={formatProviderName}
              >
                Configuration Name
              </TableHeaderColumn>
              <TableHeaderColumn dataField="regions" dataFormat={formatRegions}>
                Regions
              </TableHeaderColumn>
              <TableHeaderColumn dataFormat={formatUsage}>Usage</TableHeaderColumn>
              <TableHeaderColumn
                columnClassName={styles.providerActionsColumn}
                dataFormat={formatProviderActions}
                width="50"
              />
            </BootstrapTable>
          </div>
          <DeleteProviderConfigModal
            open={isDeleteProviderModalOpen}
            onClose={hideDeleteProviderModal}
            providerConfig={deleteProviderConfigSelection}
          />
        </>
      )}
    </>
  );
};
