/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { useState } from 'react';
import { useQuery, useQueryClient } from 'react-query';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Dropdown, DropdownButton, MenuItem } from 'react-bootstrap';
import { Link } from 'react-router';
import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { useInterval } from 'react-use';

import { api, providerQueryKey, universeQueryKey } from '../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import {
  ProviderLabel,
  CloudVendorProviders,
  ProviderCode,
  KubernetesProviderType,
  KUBERNETES_PROVIDERS_MAP,
  PROVIDER_ROUTE_PREFIX,
  KubernetesProviderTypeLabel,
  ProviderStatus,
  ProviderStatusLabel as ProviderStatusTextLabel,
  PROVIDER_CONFIG_REFETCH_INTERVAL_MS,
  TRANSITORY_PROVIDER_STATUSES
} from './constants';
import { EmptyListPlaceholder } from './EmptyListPlaceholder';
import { ProviderDashboardView } from './InfraProvider';
import { RegionsCell } from './RegionsCell';
import { YBLabelWithIcon } from '../../common/descriptors';
import EllipsisIcon from '../../common/media/more.svg?img';
import { DeleteProviderConfigModal } from './DeleteProviderConfigModal';
import { UniverseItem } from './providerView/providerDetails/UniverseTable';
import { getLinkedUniverses } from './utils';
import { usePillStyles } from '../../../redesign/styles/styles';
import { YBButton } from '../../../redesign/components';
import { ProviderStatusLabel } from './components/ProviderStatusLabel';
import { SortOrder } from '../../../redesign/helpers/constants';
import {
  RbacValidator,
  hasNecessaryPerm
} from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { isRbacEnabled } from '../../../redesign/features/rbac/common/RbacUtils';
import { useDropdownButtonStyles } from './useSplitButtonStyles';

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

const useStyles = makeStyles((theme) => ({
  menuItemHeader: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1)
  },
  menuItem: {
    height: 'fit-content',

    '& a': {
      height: 'fit-content !important'
    }
  },
  gradientTitle: {
    padding: theme.spacing(0.25, 0.75),

    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: '4px',
    background: 'linear-gradient(273deg, #ED35EC 10%, #ED35C5 50%, #7879F1 85.17%, #5E60F0 99.9%)',
    WebkitBackgroundClip: 'text',
    WebkitTextFillColor: 'transparent',
    backgroundClip: 'text',
    color: 'transparent'
  }
}));

const AUTOMATIC_PROVISIONING_DOC_URL =
  'https://docs.yugabyte.com/preview/yugabyte-platform/prepare/server-nodes-software/software-on-prem';
const TABLE_MIN_PAGE_SIZE = 10;

export const ProviderList = (props: ProviderListProps) => {
  const { providerCode, setCurrentView } = props;
  const [isDeleteProviderModalOpen, setIsDeleteProviderModalOpen] = useState<boolean>(false);
  const [deleteProviderConfigSelection, setDeleteProviderConfigSelection] = useState<YBProvider>();
  const splitButtonClasses = useDropdownButtonStyles();
  const pillClasses = usePillStyles();
  const classes = useStyles();
  const theme = useTheme();
  const providerListQuery = useQuery(providerQueryKey.ALL, () => api.fetchProviderList());
  const universeListQuery = useQuery(universeQueryKey.ALL, () => api.fetchUniverseList());
  const queryClient = useQueryClient();

  useInterval(() => {
    if (
      providerListQuery.data?.some((provider) =>
        (TRANSITORY_PROVIDER_STATUSES as readonly ProviderStatus[]).includes(
          provider.usabilityState
        )
      )
    ) {
      queryClient.invalidateQueries(providerQueryKey.ALL, { exact: true });
    }
  }, PROVIDER_CONFIG_REFETCH_INTERVAL_MS);

  if (
    providerListQuery.isLoading ||
    providerListQuery.isIdle ||
    universeListQuery.isLoading ||
    universeListQuery.isIdle
  ) {
    return <YBLoading />;
  }
  if (providerListQuery.isError) {
    return <YBErrorIndicator customErrorMessage="Error fetching provider list." />;
  }
  if (universeListQuery.isError && !isRbacEnabled()) {
    return <YBErrorIndicator customErrorMessage="Error fetching universe list." />;
  }
  const providerList = providerListQuery.data;
  const universeList = universeListQuery.data ?? [];

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
        <Typography variant="body2">{providerName}</Typography>
      </Link>
    );
  };
  const formatProviderStatus = (usabilityState: ProviderStatus) => (
    <ProviderStatusLabel providerStatus={usabilityState} variant="body2" />
  );
  const sortProviderStatus = (rowA: ProviderListItem, rowB: ProviderListItem, order: SortOrder) => {
    let comparison = 0;
    if (
      ProviderStatusTextLabel[rowA.usabilityState] === ProviderStatusTextLabel[rowB.usabilityState]
    ) {
      // Break ties with provider name.
      comparison = rowA.name > rowB.name ? 1 : -1;
    } else {
      comparison =
        ProviderStatusTextLabel[rowA.usabilityState] > ProviderStatusTextLabel[rowB.usabilityState]
          ? 1
          : -1;
    }

    return order === SortOrder.DESCENDING ? comparison * -1 : comparison;
  };
  const formatRegions = (regions: YBRegion[]) => <RegionsCell regions={regions} />;
  const formatProviderActions = (_: unknown, row: ProviderListItem) => {
    return (
      <Dropdown id="table-actions-dropdown" pullRight>
        <Dropdown.Toggle noCaret>
          <img src={EllipsisIcon} alt="more" className="ellipsis-icon" />
        </Dropdown.Toggle>
        <Dropdown.Menu>
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.DELETE_PROVIDER}
            isControl
            overrideStyle={{ display: 'block' }}
          >
            <MenuItem
              eventKey="1"
              onSelect={() => handleDeleteProviderConfig(row)}
              data-testid="DeleteConfiguration-button"
              disabled={
                row.linkedUniverses.length > 0 ||
                !hasNecessaryPerm(ApiPermissionMap.DELETE_PROVIDER)
              }
            >
              <YBLabelWithIcon icon="fa fa-trash">Delete Configuration</YBLabelWithIcon>
            </MenuItem>
          </RbacValidator>
        </Dropdown.Menu>
      </Dropdown>
    );
  };
  const formatUsage = (_: unknown, row: ProviderListItem) => {
    return row.linkedUniverses.length ? (
      <Box display="flex" gridGap="5px" alignItems="center">
        <Typography variant="body2">In Use</Typography>
        <div className={pillClasses.pill}>{row.linkedUniverses.length}</div>
      </Box>
    ) : (
      <Typography variant="body2">Not in Use</Typography>
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

  const createOnPremProviderDropdown = (
    <DropdownButton
      className={splitButtonClasses.splitButton}
      title="Create Config"
      id="ProviderListView-CreateConfigDropdownMenu"
      data-testid="ProviderListView-CreateConfigDropdownMenu"
      pullRight={filteredProviderList.length > 0}
    >
      <MenuItem
        eventKey="1"
        onSelect={() => window.open(AUTOMATIC_PROVISIONING_DOC_URL, '_black', 'noopener')}
        className={classes.menuItem}
      >
        <Box
          display="flex"
          flexDirection="column"
          component="span"
          height="fit-content"
          gridGap={theme.spacing(1)}
        >
          <span className={classes.menuItemHeader}>
            <Typography variant="body1">Automatic Provisioning</Typography>
            <Typography variant="subtitle2" className={classes.gradientTitle}>
              New
            </Typography>
          </span>
          <span>
            <Typography variant="subtitle1">
              Create a provider automatically. <u>Learn More</u>
            </Typography>
          </span>
        </Box>
      </MenuItem>
      <MenuItem divider />
      <RbacValidator
        accessRequiredOn={ApiPermissionMap.CREATE_PROVIDER}
        overrideStyle={{ display: 'block' }}
        isControl
      >
        <MenuItem eventKey="2" onSelect={() => setCurrentView(ProviderDashboardView.CREATE)}>
          <Typography variant="body1">Legacy Provisioning</Typography>
        </MenuItem>
      </RbacValidator>
    </DropdownButton>
  );
  const providerLabel =
    providerCode === ProviderCode.KUBERNETES && props.kubernetesProviderType
      ? KubernetesProviderTypeLabel[props.kubernetesProviderType]
      : ProviderLabel[providerCode];
  return (
    <>
      <Box display="flex" marginBottom="35px" justifyContent="space-between">
        <Typography variant="h4">{`${providerLabel} Configs`}</Typography>
        {filteredProviderList.length > 0 &&
          (providerCode !== ProviderCode.ON_PREM ? (
            <RbacValidator accessRequiredOn={ApiPermissionMap.CREATE_PROVIDER} isControl>
              <YBButton
                style={{ marginLeft: 'auto', width: 'fit-content' }}
                variant="primary"
                onClick={() => setCurrentView(ProviderDashboardView.CREATE)}
                data-testid="ProviderListView-CreateConfigButton"
              >
                <i className="fa fa-plus" />
                Create Config
              </YBButton>
            </RbacValidator>
          ) : (
            createOnPremProviderDropdown
          ))}
      </Box>
      {filteredProviderList.length === 0 ? (
        providerCode === ProviderCode.ON_PREM ? (
          <EmptyListPlaceholder
            variant="primary"
            isCustomPrimaryAction
            customPrimaryAction={createOnPremProviderDropdown}
            descriptionText={`No ${providerLabel} config to show`}
          />
        ) : (
          <EmptyListPlaceholder
            variant="primary"
            accessRequiredOn={ApiPermissionMap.CREATE_PROVIDER}
            actionButtonText={`Create ${providerLabel} Config`}
            descriptionText={`No ${providerLabel} config to show`}
            onActionButtonClick={handleCreateProviderAction}
            dataTestIdPrefix="ProviderEmptyList"
            isCustomPrimaryAction={false}
          />
        )
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
              <TableHeaderColumn
                dataField="usabilityState"
                dataSort={true}
                sortFunc={sortProviderStatus}
                dataFormat={formatProviderStatus}
              >
                Status
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
