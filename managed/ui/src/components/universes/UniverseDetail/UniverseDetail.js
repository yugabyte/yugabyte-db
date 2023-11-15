// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { Link, withRouter, browserHistory } from 'react-router';
import { Grid, DropdownButton, MenuItem, Tab, Alert } from 'react-bootstrap';
import Measure from 'react-measure';
import { mouseTrap } from 'react-mousetrap';

import { CustomerMetricsPanel } from '../../metrics';
import { RollingUpgradeFormContainer } from '../../../components/common/forms';
import {
  UniverseStatusContainer,
  NodeDetailsContainer,
  DeleteUniverseContainer,
  UniverseAppsModal,
  UniverseConnectModal,
  UniverseOverviewContainerNew,
  EncryptionKeyModalContainer,
  ToggleUniverseStateContainer,
  ToggleBackupStateContainer
} from '../../universes';
import { YBLabelWithIcon } from '../../common/descriptors';
import { YBTabsWithLinksPanel } from '../../panels';
import { ListTablesContainer, ListBackupsContainer, ReplicationContainer } from '../../tables';
import { QueriesViewer } from '../../queries';
import { isEmptyObject, isNonEmptyObject } from '../../../utils/ObjectUtils';
import {
  isKubernetesUniverse,
  isPausableUniverse,
  getPrimaryCluster,
  hasLiveNodes
} from '../../../utils/UniverseUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';

import { YBLoading, YBErrorIndicator } from '../../common/indicators';
import { UniverseHealthCheckList } from './compounds/UniverseHealthCheckList';
import { UniverseTaskList } from './compounds/UniverseTaskList';
import { YBMenuItem } from './compounds/YBMenuItem';
import { MenuItemsContainer } from './compounds/MenuItemsContainer';
import {
  isNonAvailable,
  isDisabled,
  isNotHidden,
  getFeatureState
} from '../../../utils/LayoutUtils';
import { SecurityMenu } from '../SecurityModal/SecurityMenu';
import { UniverseLevelBackup } from '../../backupv2/Universe/UniverseLevelBackup';
import { UniverseSupportBundle } from '../UniverseSupportBundle/UniverseSupportBundle';
import { XClusterReplication } from '../../xcluster/XClusterReplication';
import { EncryptionAtRest } from '../../../redesign/features/universe/universe-actions/encryption-at-rest/EncryptionAtRest';
import { EncryptionInTransit } from '../../../redesign/features/universe/universe-actions/encryption-in-transit/EncryptionInTransit';
import { EnableYSQLModal } from '../../../redesign/features/universe/universe-actions/edit-ysql-ycql/EnableYSQLModal';
import { EnableYCQLModal } from '../../../redesign/features/universe/universe-actions/edit-ysql-ycql/EnableYCQLModal';
import { EditGflagsModal } from '../../../redesign/features/universe/universe-actions/edit-gflags/EditGflags';
import { RuntimeConfigKey } from '../../../redesign/helpers/constants';
import { UniverseState, getUniverseStatus } from '../helpers/universeHelpers';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';

import './UniverseDetail.scss';

const INSTANCE_WITH_EPHEMERAL_STORAGE_ONLY = ['i3', 'c5d', 'c6gd'];

export const isEphemeralAwsStorageInstance = (instanceType) => {
  return INSTANCE_WITH_EPHEMERAL_STORAGE_ONLY.includes(instanceType?.split?.('.')[0]);
};

class UniverseDetail extends Component {
  constructor(props) {
    super(props);

    this.showUpgradeMarker = this.showUpgradeMarker.bind(this);
    this.onEditUniverseButtonClick = this.onEditUniverseButtonClick.bind(this);
    this.state = {
      dimensions: {},
      showAlert: false,
      actionsDropdownOpen: false
    };
  }

  hasReadReplica = (universeInfo) => {
    const clusters = universeInfo.universeDetails.clusters;
    return clusters.some((cluster) => cluster.clusterType === 'ASYNC');
  };

  componentWillUnmount() {
    this.props.resetUniverseInfo();
    this.props.resetTablesList();
  }

  isNewUIEnabled = () => {
    const { featureFlags } = this.props;
    return featureFlags.test.enableNewUI || featureFlags.released.enableNewUI;
  };

  isRRFlagsEnabled = () => {
    const { featureFlags } = this.props;
    return featureFlags.test.enableRRGflags || featureFlags.released.enableRRGflags;
  };

  componentDidMount() {
    const {
      customer: { currentCustomer }
    } = this.props;
    if (isNonAvailable(currentCustomer.data.features, 'universes.details')) {
      if (isNonAvailable(currentCustomer.data.features, 'universes')) {
        browserHistory.push('/');
      } else {
        browserHistory.push('/universes/');
      }
    }

    this.props.bindShortcut(['ctrl+e'], this.onEditUniverseButtonClick);

    if (this.props.location.pathname !== '/universes/create') {
      let uuid = this.props.uuid;
      if (typeof this.props.universeSelectionId !== 'undefined') {
        uuid = this.props.universeUUID;
      }
      this.props.getUniverseInfo(uuid).then((response) => {
        const primaryCluster = getPrimaryCluster(response.payload.data?.universeDetails?.clusters);
        const providerUUID = primaryCluster?.userIntent?.provider;
        this.props.fetchSupportedReleases(providerUUID);
      });

      if (isDisabled(currentCustomer.data.features, 'universes.details.health')) {
        // Get alerts instead of Health
        this.props.getAlertsList();
      } else {
        this.props.getHealthCheck(uuid);
      }
    }
    // Runtime config should by default be called at customer scope
    // If a specific universe is selected then fall back to universe scope
    const runTimeConfigUUID = this.props.uuid ?? this.props.customer?.currentCustomer?.data?.uuid;
    this.props.fetchRunTimeConfigs(runTimeConfigUUID);
  }

  componentDidUpdate(prevProps) {
    const {
      universe: { currentUniverse },
      universeTables
    } = this.props;
    if (
      getPromiseState(currentUniverse).isSuccess() &&
      !getPromiseState(prevProps.universe.currentUniverse).isSuccess()
    ) {
      if (hasLiveNodes(currentUniverse.data) && !universeTables.length) {
        this.props.fetchUniverseTables(currentUniverse.data.universeUUID);
      }
    }
  }

  onResize(dimensions) {
    this.setState({ dimensions });
  }

  onEditUniverseButtonClick = () => {
    const location = Object.assign({}, browserHistory.getCurrentLocation());
    Object.assign(location, { pathname: `/universes/${this.props.uuid}/edit/primary` });
    browserHistory.push(location);
  };

  isCurrentUniverseDeleteTask = (uuid) => {
    return this.props.tasks.customerTaskList.filter(
      (task) => task.targetUUID === uuid && task.type === 'Delete' && task.target === 'Universe'
    );
  };

  getUniverseInfo = () => {
    const universeUUID = this.props.universe.currentUniverse.data.universeUUID;
    const currentUniverseTasks = this.isCurrentUniverseDeleteTask(universeUUID);
    if (currentUniverseTasks.length > 0) {
      browserHistory.push('/');
    } else {
      this.props.getUniverseInfo(universeUUID);
    }
  };

  showUpgradeMarker = () => {
    const {
      updateAvailable,
      universe: { rollingUpgrade },
      modal: { showModal, visibleModal }
    } = this.props;

    return (
      !getPromiseState(rollingUpgrade).isLoading() &&
      updateAvailable !== 0 &&
      !(showModal && visibleModal === 'softwareUpgradesModal')
    );
  };

  stripQueryParams = () => {
    browserHistory.replace(browserHistory.getCurrentLocation().pathname);
  };

  handleSubmitManageKey = (res) => {
    if (res.payload.isAxiosError) {
      this.setState({
        showAlert: true,
        alertType: 'danger',
        alertMessage: res.payload.message
      });
    } else {
      this.setState({
        showAlert: true,
        alertType: 'success',
        alertMessage:
          JSON.parse(res.payload.config.data).key_op === 'ENABLE'
            ? 'Encryption key has been set!'
            : 'Encryption-at-Rest has been disabled!'
      });
    }
    setTimeout(() => this.setState({ showAlert: false }), 3000);

    this.props.closeModal();
  };

  closeAlert = () => {
    this.setState({ showAlert: false });
  };

  render() {
    const {
      uuid,
      updateAvailable,
      modal,
      modal: { showModal, visibleModal },
      universe,
      tasks,
      universe: { currentUniverse, supportedReleases },
      showSoftwareUpgradesModal,
      showVMImageUpgradeModal,
      showTLSConfigurationModal,
      showRollingRestartModal,
      showUpgradeSystemdModal,
      showThirdpartyUpgradeModal,
      showRunSampleAppsModal,
      showSupportBundleModal,
      showGFlagsModal,
      showGFlagsNewModal,
      showHelmOverridesModal,
      showManageKeyModal,
      showDeleteUniverseModal,
      showToggleUniverseStateModal,
      showToggleBackupModal,
      showEnableYSQLModal,
      showEnableYCQLModal,
      updateBackupState,
      closeModal,
      customer,
      customer: { currentCustomer, currentUser, runtimeConfigs },
      params: { tab },
      featureFlags,
      providers,
      accessKeys,
      graph
    } = this.props;
    const { showAlert, alertType, alertMessage } = this.state;
    const universePaused = universe?.currentUniverse?.data?.universeDetails?.universePaused;
    const updateInProgress = universe?.currentUniverse?.data?.universeDetails?.updateInProgress;
    const backupRestoreInProgress =
      updateInProgress &&
      ['BackupTable', 'MultiTableBackup', 'CreateBackup', 'RestoreBackup'].includes(
        universe?.currentUniverse?.data?.universeDetails?.updatingTask
      );
    const primaryCluster = getPrimaryCluster(
      universe?.currentUniverse?.data?.universeDetails?.clusters
    );
    const useSystemd = primaryCluster?.userIntent?.useSystemd;
    const isReadOnlyUniverse =
      getPromiseState(currentUniverse).isSuccess() &&
      currentUniverse.data.universeDetails.capability === 'READ_ONLY';
    const universeStatus =
      getPromiseState(currentUniverse).isSuccess() && getUniverseStatus(currentUniverse?.data);
    const isUniverseStatusPending = universeStatus.state === UniverseState.PENDING;

    const providerUUID = primaryCluster?.userIntent?.provider;
    const provider = providers.data.find((provider) => provider.uuid === providerUUID);
    let onPremSkipProvisioning = false;
    if (provider && provider.code === 'onprem') {
      const onPremKey = accessKeys.data.find(
        (accessKey) => accessKey.idKey.providerUUID === provider.uuid
      );
      onPremSkipProvisioning = onPremKey?.keyInfo.skipProvisioning;
    }

    const isPerfAdvisorEnabled =
      runtimeConfigs?.data?.configEntries?.find((c) => c.key === 'yb.ui.feature_flags.perf_advisor')
        ?.value === 'true';

    const isAuthEnforced =
      runtimeConfigs?.data?.configEntries?.find((c) => c.key === 'yb.universe.auth.is_enforced')
        ?.value === 'true';

    const isConfigureYSQLEnabled =
      runtimeConfigs?.data?.configEntries?.find((c) => c.key === 'yb.configure_db_api.ysql')
        ?.value === 'true';

    const isGFlagMultilineConfEnabled =
      runtimeConfigs?.data?.configEntries?.find(
        (config) => config.key === RuntimeConfigKey.IS_GFLAG_MULTILINE_ENABLED
      )?.value === 'true';

    const isConfigureYCQLEnabled =
      runtimeConfigs?.data?.configEntries?.find((c) => c.key === 'yb.configure_db_api.ycql')
        ?.value === 'true';

    if (
      getPromiseState(currentUniverse).isLoading() ||
      getPromiseState(currentUniverse).isInit() ||
      getPromiseState(supportedReleases).isLoading() ||
      getPromiseState(supportedReleases).isInit()
    ) {
      return <YBLoading />;
    } else if (isEmptyObject(currentUniverse.data)) {
      return <span />;
    }

    if (getPromiseState(currentUniverse).isError()) {
      return <YBErrorIndicator type="universe" uuid={uuid} />;
    }

    const width = this.state.dimensions.width;
    const universeInfo = currentUniverse.data;
    const nodePrefixes = [currentUniverse.data.universeDetails.nodePrefix];
    const isItKubernetesUniverse = isKubernetesUniverse(currentUniverse.data);

    const editTLSAvailability = getFeatureState(
      currentCustomer.data.features,
      'universes.details.overview.manageEncryption'
    );
    const manageKeyAvailability = getFeatureState(
      currentCustomer.data.features,
      'universes.details.overview.manageEncryption'
    );
    const defaultTab = isNotHidden(currentCustomer.data.features, 'universes.details.overview')
      ? 'overview'
      : 'overview';
    const activeTab = tab || defaultTab;
    const tabElements = [
      //common tabs for every universe
      ...[
        isNotHidden(currentCustomer.data.features, 'universes.details.overview') && (
          <Tab.Pane
            eventKey={'overview'}
            tabtitle="Overview"
            key="overview-tab"
            mountOnEnter={true}
            unmountOnExit={true}
            disabled={isDisabled(currentCustomer.data.features, 'universes.details.overview')}
          >
            <UniverseOverviewContainerNew
              width={width}
              universe={universe}
              updateAvailable={updateAvailable}
              showSoftwareUpgradesModal={showSoftwareUpgradesModal}
              tabRef={this.ybTabPanel}
            />
          </Tab.Pane>
        ),

        isNotHidden(currentCustomer.data.features, 'universes.details.tables') && (
          <Tab.Pane
            eventKey={'tables'}
            className={'universe-tables-list'}
            tabtitle="Tables"
            key="tables-tab"
            mountOnEnter={true}
            unmountOnExit={true}
            disabled={isDisabled(currentCustomer.data.features, 'universes.details.tables')}
          >
            <ListTablesContainer fetchUniverseTables={this.props.fetchUniverseTables} />
          </Tab.Pane>
        ),

        isNotHidden(currentCustomer.data.features, 'universes.details.nodes') && (
          <Tab.Pane
            eventKey={'nodes'}
            tabtitle={isItKubernetesUniverse ? 'Pods' : 'Nodes'}
            key="nodes-tab"
            mountOnEnter={true}
            unmountOnExit={true}
            disabled={isDisabled(currentCustomer.data.features, 'universes.details.nodes')}
          >
            <NodeDetailsContainer />
          </Tab.Pane>
        ),

        isNotHidden(currentCustomer.data.features, 'universes.details.metrics') && (
          <Tab.Pane
            eventKey={'metrics'}
            tabtitle="Metrics"
            key="metrics-tab"
            mountOnEnter={true}
            unmountOnExit={true}
            disabled={isDisabled(currentCustomer.data.features, 'universes.details.metrics')}
          >
            <div className="universe-detail-content-container">
              <CustomerMetricsPanel
                customer={customer}
                origin={'universe'}
                width={width}
                nodePrefixes={nodePrefixes}
                isKubernetesUniverse={isItKubernetesUniverse}
                visibleModal={visibleModal}
                featureFlags={featureFlags}
                graph={graph}
              />
            </div>
          </Tab.Pane>
        ),

        isNotHidden(currentCustomer.data.features, 'universes.details.queries') && (
          <Tab.Pane
            eventKey={'queries'}
            tabtitle="Queries"
            key="queries-tab"
            mountOnEnter={true}
            unmountOnExit={true}
            onExit={this.stripQueryParams}
            disabled={isDisabled(currentCustomer.data.features, 'universes.details.queries')}
          >
            <QueriesViewer isPerfAdvisorEnabled={isPerfAdvisorEnabled} />
          </Tab.Pane>
        ),

        isNotHidden(currentCustomer.data.features, 'universes.details.replication') && (
          <Tab.Pane
            eventKey={'replication'}
            tabtitle="Replication"
            key="replication-tab"
            mountOnEnter={true}
            unmountOnExit={true}
            disabled={isDisabled(currentCustomer.data.features, 'universes.details.replication')}
          >
            {featureFlags.released.enableXCluster || featureFlags.test.enableXCluster ? (
              <XClusterReplication currentUniverseUUID={currentUniverse.data.universeUUID} />
            ) : (
              <ReplicationContainer />
            )}
          </Tab.Pane>
        ),

        isNotHidden(currentCustomer.data.features, 'universes.details.tasks') && (
          <Tab.Pane
            eventKey={'tasks'}
            tabtitle="Tasks"
            key="tasks-tab"
            mountOnEnter={true}
            unmountOnExit={true}
            disabled={isDisabled(currentCustomer.data.features, 'universes.details.tasks')}
          >
            <UniverseTaskList
              universeUuid={currentUniverse.data.universeUUID}
              abortTask={this.props.abortTask}
              hideTaskAbortModal={this.props.hideTaskAbortModal}
              showTaskAbortModal={this.props.showTaskAbortModal}
              visibleModal={visibleModal}
            />
          </Tab.Pane>
        )
      ],
      //tabs relevant for non-imported universes only
      ...(isReadOnlyUniverse
        ? []
        : [
            isNotHidden(currentCustomer.data.features, 'universes.details.backups') && (
              <Tab.Pane
                eventKey={'backups'}
                tabtitle={<>Backups</>}
                key="backups-tab"
                mountOnEnter={true}
                unmountOnExit={true}
                disabled={isDisabled(currentCustomer.data.features, 'universes.details.backups')}
              >
                {featureFlags.test['backupv2'] || featureFlags.released['backupv2'] ? (
                  <UniverseLevelBackup />
                ) : (
                  <ListBackupsContainer currentUniverse={currentUniverse.data} />
                )}
              </Tab.Pane>
            ),

            isNotHidden(currentCustomer.data.features, 'universes.details.health') && (
              <Tab.Pane
                eventKey={'health'}
                tabtitle="Health"
                key="health-tab"
                mountOnEnter={true}
                unmountOnExit={true}
                disabled={isDisabled(currentCustomer.data.features, 'universes.details.heath')}
              >
                <UniverseHealthCheckList
                  universe={universe}
                  currentCustomer={currentCustomer}
                  currentUser={currentUser}
                  closeModal={closeModal}
                  visibleModal={visibleModal}
                />
              </Tab.Pane>
            )
          ])
    ].filter((element) => element);

    const currentBreadCrumb = (
      <div className="detail-label-small">
        <Link to="/universes">
          <YBLabelWithIcon>Universes</YBLabelWithIcon>
        </Link>
        <Link to={`/universes/${currentUniverse.data.universeUUID}`}>
          <YBLabelWithIcon icon="fa fa-angle-right fa-fw">
            {currentUniverse.data.name}
          </YBLabelWithIcon>
        </Link>
      </div>
    );

    const {
      data: {
        universeDetails: { nodeDetailsSet }
      }
    } = currentUniverse;

    const isEphemeralAwsStorage =
      nodeDetailsSet.find?.((node) => {
        return isEphemeralAwsStorageInstance(node.cloudInfo?.instance_type);
      }) !== undefined;

    /**
     * Handle the backup state toggle.
     * i.e open the confirmation model if backup is to be disabled.
     * else, Enable the backups.
     */
    const handleBackupToggle = () => {
      const takeBackups =
        currentUniverse.data.universeConfig &&
        currentUniverse.data.universeConfig?.takeBackups === 'true';
      takeBackups
        ? showToggleBackupModal()
        : updateBackupState(currentUniverse.data.universeUUID, !takeBackups);
    };

    const enableThirdpartyUpgrade =
      featureFlags.test['enableThirdpartyUpgrade'] ||
      featureFlags.released['enableThirdpartyUpgrade'];

    const isMKREnabled = featureFlags.test['enableMKR'] || featureFlags.released['enableMKR'];
    const isCACertRotationEnabled =
      !isItKubernetesUniverse &&
      (featureFlags.test['enableCACertRotation'] || featureFlags.released['enableCACertRotation']);

    return (
      <Grid id="page-wrapper" fluid={true} className={`universe-details universe-details-new`}>
        {showAlert && (
          <Alert bsStyle={alertType} onDismiss={this.closeAlert}>
            <h4>{alertType === 'success' ? 'Success' : 'Error'}</h4>
            <p>{alertMessage}</p>
          </Alert>
        )}
        {/* UNIVERSE NAME */}
        {currentBreadCrumb}
        <div className="universe-detail-status-container">
          <h2>{currentUniverse.data.name}</h2>
          <UniverseStatusContainer
            currentUniverse={currentUniverse.data}
            showLabelText={true}
            refreshUniverseData={this.getUniverseInfo}
            shouldDisplayTaskButton={true}
          />
        </div>
        {isNotHidden(currentCustomer.data.features, 'universes.details.pageActions') && (
          <div className="page-action-buttons">
            {/* UNIVERSE EDIT */}
            <div className="universe-detail-btn-group">
              <UniverseConnectModal />

              <DropdownButton
                title="Actions"
                className={this.showUpgradeMarker() ? 'btn-marked' : ''}
                id="bg-nested-dropdown"
                pullRight
                onToggle={(isOpen) => this.setState({ actionsDropdownOpen: isOpen })}
              >
                <MenuItemsContainer
                  parentDropdownOpen={this.state.actionsDropdownOpen}
                  mainMenu={(showSubmenu) => (
                    <>
                      {!universePaused && (
                        <RbacValidator
                          isControl
                          accessRequiredOn={{
                            onResource: uuid,
                            ...ApiPermissionMap.MODIFY_UNIVERSE
                          }}
                        >
                          <YBMenuItem
                            disabled={isUniverseStatusPending}
                            onClick={showSoftwareUpgradesModal}
                            availability={getFeatureState(
                              currentCustomer.data.features,
                              'universes.details.overview.upgradeSoftware'
                            )}
                          >
                            <YBLabelWithIcon icon="fa fa-arrow-up fa-fw">
                              Upgrade Software
                            </YBLabelWithIcon>
                            {this.showUpgradeMarker() && (
                              <span className="badge badge-pill badge-red pull-right">
                                {updateAvailable}
                              </span>
                            )}
                          </YBMenuItem>
                        </RbacValidator>
                      )}
                      {!universePaused &&
                        runtimeConfigs &&
                        getPromiseState(runtimeConfigs).isSuccess() &&
                        runtimeConfigs.data.configEntries.find(
                          (c) => c.key === 'yb.upgrade.vmImage'
                        ).value === 'true' && (
                          <RbacValidator
                            isControl
                            accessRequiredOn={{
                              onResource: uuid,
                              ...ApiPermissionMap.MODIFY_UNIVERSE
                            }}
                          >
                            <YBMenuItem
                              disabled={isUniverseStatusPending}
                              onClick={showVMImageUpgradeModal}
                            >
                              <YBLabelWithIcon icon="fa fa-arrow-up fa-fw">
                                Upgrade VM Image
                              </YBLabelWithIcon>
                            </YBMenuItem>
                          </RbacValidator>
                        )}
                      {!universePaused && !useSystemd && !isItKubernetesUniverse && (
                        <RbacValidator
                          isControl
                          accessRequiredOn={{
                            onResource: uuid,
                            ...ApiPermissionMap.MODIFY_UNIVERSE
                          }}
                        >
                          <YBMenuItem
                            disabled={isUniverseStatusPending || onPremSkipProvisioning}
                            onClick={showUpgradeSystemdModal}
                            availability={getFeatureState(
                              currentCustomer.data.features,
                              'universes.details.overview.systemdUpgrade'
                            )}
                          >
                            <YBLabelWithIcon icon="fa fa-wrench fa-fw">
                              Upgrade To Systemd
                            </YBLabelWithIcon>
                          </YBMenuItem>
                        </RbacValidator>
                      )}
                      {!universePaused && enableThirdpartyUpgrade && (
                        <RbacValidator
                          isControl
                          accessRequiredOn={{
                            onResource: uuid,
                            ...ApiPermissionMap.MODIFY_UNIVERSE
                          }}
                        >
                          <YBMenuItem
                            disabled={isUniverseStatusPending}
                            onClick={showThirdpartyUpgradeModal}
                            availability={getFeatureState(
                              currentCustomer.data.features,
                              'universes.details.overview.thirdpartyUpgrade'
                            )}
                          >
                            <YBLabelWithIcon icon="fa fa-wrench fa-fw">
                              Upgrade 3rd-party Software
                            </YBLabelWithIcon>
                          </YBMenuItem>
                        </RbacValidator>
                      )}
                      {!isReadOnlyUniverse &&
                        !universePaused &&
                        isNotHidden(
                          currentCustomer.data.features,
                          'universes.details.overview.editUniverse'
                        ) && (
                          <RbacValidator
                            isControl
                            accessRequiredOn={{
                              onResource: uuid,
                              ...ApiPermissionMap.GET_UNIVERSES_BY_ID
                            }}
                          >
                            <YBMenuItem
                              to={`/universes/${uuid}/edit/primary`}
                              availability={getFeatureState(
                                currentCustomer.data.features,
                                'universes.details.overview.editUniverse'
                              )}
                              disabled={isUniverseStatusPending}
                            >
                              <YBLabelWithIcon icon="fa fa-pencil">Edit Universe</YBLabelWithIcon>
                            </YBMenuItem>
                          </RbacValidator>
                        )}

                      {!universePaused && !this.isRRFlagsEnabled() && (
                        <RbacValidator
                          isControl
                          accessRequiredOn={{
                            onResource: uuid,
                            ...ApiPermissionMap.UPGRADE_UNIVERSE_GFLAGS
                          }}
                        >
                          <YBMenuItem
                            disabled={isUniverseStatusPending}
                            onClick={showGFlagsModal}
                            availability={getFeatureState(
                              currentCustomer.data.features,
                              'universes.details.overview.editGFlags'
                            )}
                          >
                            <YBLabelWithIcon icon="fa fa-flag fa-fw">Edit Flags</YBLabelWithIcon>
                          </YBMenuItem>
                        </RbacValidator>
                      )}
                      {!universePaused && this.isRRFlagsEnabled() && (
                        <RbacValidator
                          isControl
                          accessRequiredOn={{
                            onResource: uuid,
                            ...ApiPermissionMap.UPGRADE_UNIVERSE_GFLAGS
                          }}
                        >
                          <YBMenuItem
                            disabled={isUniverseStatusPending}
                            onClick={showGFlagsNewModal}
                            availability={getFeatureState(
                              currentCustomer.data.features,
                              'universes.details.overview.editGFlags'
                            )}
                          >
                            <YBLabelWithIcon icon="fa fa-flag fa-fw">Edit Flags</YBLabelWithIcon>
                          </YBMenuItem>
                        </RbacValidator>
                      )}
                      {!universePaused && isItKubernetesUniverse && (
                        <RbacValidator
                          isControl
                          accessRequiredOn={{
                            onResource: uuid,
                            ...ApiPermissionMap.MODIFY_UNIVERSE
                          }}
                        >
                          <YBMenuItem
                            disabled={isUniverseStatusPending}
                            onClick={showHelmOverridesModal}
                          >
                            <YBLabelWithIcon icon="fa fa-pencil-square">
                              Edit Kubernetes Overrides
                            </YBLabelWithIcon>
                          </YBMenuItem>
                        </RbacValidator>
                      )}
                      {!universePaused && (
                        <RbacValidator
                          isControl
                          accessRequiredOn={{
                            onResource: uuid,
                            ...ApiPermissionMap.GET_UNIVERSES_BY_ID
                          }}
                        >
                          <YBMenuItem
                            disabled={isUniverseStatusPending}
                            onClick={() => showSubmenu('security')}
                            availability={getFeatureState(
                              currentCustomer.data.features,
                              'universes.details.overview.manageEncryption'
                            )}
                          >
                            <YBLabelWithIcon icon="fa fa-key fa-fw">Edit Security</YBLabelWithIcon>
                            <span className="pull-right">
                              <i className="fa fa-chevron-right submenu-icon" />
                            </span>
                          </YBMenuItem>
                        </RbacValidator>
                      )}
                      {!universePaused && isConfigureYSQLEnabled && (
                        <RbacValidator
                          accessRequiredOn={{
                            onResource: uuid,
                            ...ApiPermissionMap.GET_UNIVERSES_BY_ID
                          }}
                          isControl
                        >
                          <YBMenuItem
                            disabled={isUniverseStatusPending}
                            onClick={showEnableYSQLModal}
                          >
                            <YBLabelWithIcon icon="fa fa-database fa-fw">
                              Edit YSQL Configuration
                            </YBLabelWithIcon>
                          </YBMenuItem>
                        </RbacValidator>
                      )}
                      {!universePaused && isConfigureYCQLEnabled && (
                        <RbacValidator
                          accessRequiredOn={{
                            onResource: uuid,
                            ...ApiPermissionMap.GET_UNIVERSES_BY_ID
                          }}
                          isControl
                        >
                          <YBMenuItem
                            disabled={isUniverseStatusPending}
                            onClick={showEnableYCQLModal}
                          >
                            <YBLabelWithIcon icon="fa fa-database fa-fw">
                              Edit YCQL Configuration
                            </YBLabelWithIcon>
                          </YBMenuItem>
                        </RbacValidator>
                      )}
                      {!universePaused && (
                        <RbacValidator
                          isControl
                          accessRequiredOn={{
                            onResource: uuid,
                            ...ApiPermissionMap.MODIFY_UNIVERSE
                          }}
                        >
                          <YBMenuItem
                            disabled={isUniverseStatusPending}
                            onClick={showRollingRestartModal}
                            availability={getFeatureState(
                              currentCustomer.data.features,
                              'universes.details.overview.restartUniverse'
                            )}
                          >
                            <YBLabelWithIcon icon="fa fa-refresh fa-fw">
                              Initiate Rolling Restart
                            </YBLabelWithIcon>
                          </YBMenuItem>
                        </RbacValidator>
                      )}

                      {!isReadOnlyUniverse && !universePaused && (
                        <RbacValidator
                          isControl
                          accessRequiredOn={{
                            onResource: uuid,
                            ...ApiPermissionMap.GET_UNIVERSES_BY_ID
                          }}
                        >
                          <YBMenuItem
                            disabled={isUniverseStatusPending}
                            to={
                              this.isNewUIEnabled()
                                ? `/universes/${uuid}/${
                                    this.hasReadReplica(universeInfo) ? 'edit' : 'create'
                                  }/async`
                                : `/universes/${uuid}/edit/async`
                            }
                            availability={getFeatureState(
                              currentCustomer.data.features,
                              'universes.details.overview.readReplica'
                            )}
                          >
                            <YBLabelWithIcon icon="fa fa-copy fa-fw">
                              {this.hasReadReplica(universeInfo) ? 'Edit' : 'Add'} Read Replica
                            </YBLabelWithIcon>
                          </YBMenuItem>
                        </RbacValidator>
                      )}

                      {!universePaused && (
                        <RbacValidator
                          isControl
                          accessRequiredOn={{
                            onResource: uuid,
                            ...ApiPermissionMap.GET_UNIVERSES_BY_ID
                          }}
                        >
                          <UniverseAppsModal
                            currentUniverse={currentUniverse.data}
                            modal={modal}
                            closeModal={closeModal}
                            button={
                              <YBMenuItem
                                disabled={isUniverseStatusPending && !backupRestoreInProgress}
                                onClick={showRunSampleAppsModal}
                              >
                                <YBLabelWithIcon icon="fa fa-terminal">
                                  Run Sample Apps
                                </YBLabelWithIcon>
                              </YBMenuItem>
                            }
                          />
                        </RbacValidator>
                      )}

                      {(featureFlags.test['supportBundle'] ||
                        featureFlags.released['supportBundle']) && (
                        <>
                          <MenuItem divider />
                          {!universePaused && (
                            <RbacValidator
                              isControl
                              accessRequiredOn={{
                                onResource: uuid,
                                ...ApiPermissionMap.GET_SUPPORT_BUNDLE
                              }}
                            >
                              <UniverseSupportBundle
                                currentUniverse={currentUniverse.data}
                                modal={modal}
                                closeModal={closeModal}
                                button={
                                  <YBMenuItem onClick={showSupportBundleModal}>
                                    <YBLabelWithIcon icon="fa fa-file-archive-o">
                                      Support Bundles
                                    </YBLabelWithIcon>
                                  </YBMenuItem>
                                }
                              />
                            </RbacValidator>
                          )}
                          <MenuItem divider />
                        </>
                      )}

                      {!universePaused && (
                        <RbacValidator
                          isControl
                          accessRequiredOn={{
                            onResource: uuid,
                            ...ApiPermissionMap.MODIFY_UNIVERSE
                          }}
                        >
                          <YBMenuItem
                            disabled={isUniverseStatusPending}
                            onClick={handleBackupToggle}
                            availability={getFeatureState(
                              currentCustomer.data.features,
                              'universes.backup'
                            )}
                          >
                            <YBLabelWithIcon
                              icon={
                                currentUniverse.data.universeConfig.takeBackups === 'true'
                                  ? 'fa fa-pause'
                                  : 'fa fa-play'
                              }
                            >
                              {currentUniverse.data.universeConfig &&
                              currentUniverse.data.universeConfig.takeBackups === 'true'
                                ? 'Disable Backup'
                                : 'Enable Backup'}
                            </YBLabelWithIcon>
                          </YBMenuItem>
                        </RbacValidator>
                      )}

                      <MenuItem divider />

                      {/* TODO:
                      1. For now, we're enabling the Pause Universe for providerType one of
                      'aws', 'gcp' or 'azu' only. This functionality needs to be enabled for
                      all the cloud providers and once that's done this condition needs
                      to be removed.
                      2. One more condition needs to be added which specifies the
                      current status of the universe. */}

                      {/*
                      Read-only users should not be given the rights to "Pause Universe"
                      */}

                      {isPausableUniverse(currentUniverse?.data) &&
                        !isEphemeralAwsStorage &&
                        (featureFlags.test['pausedUniverse'] ||
                          featureFlags.released['pausedUniverse']) && (
                          <RbacValidator
                            isControl
                            accessRequiredOn={{
                              onResource: uuid,
                              ...ApiPermissionMap.RESUME_UNIVERSE
                            }}
                          >
                            <YBMenuItem
                              onClick={showToggleUniverseStateModal}
                              availability={getFeatureState(
                                currentCustomer.data.features,
                                'universes.details.overview.pausedUniverse'
                              )}
                              disabled={universePaused && isUniverseStatusPending}
                            >
                              <YBLabelWithIcon
                                icon={
                                  universePaused ? 'fa fa-play-circle-o' : 'fa fa-pause-circle-o'
                                }
                              >
                                {universePaused ? 'Resume Universe' : 'Pause Universe'}
                              </YBLabelWithIcon>
                            </YBMenuItem>
                          </RbacValidator>
                        )}
                      <RbacValidator
                        isControl
                        accessRequiredOn={{
                          onResource: uuid,
                          ...ApiPermissionMap.DELETE_UNIVERSE
                        }}
                      >
                        <YBMenuItem
                          onClick={showDeleteUniverseModal}
                          availability={getFeatureState(
                            currentCustomer.data.features,
                            'universes.details.overview.deleteUniverse'
                          )}
                        >
                          <YBLabelWithIcon icon="fa fa-trash-o fa-fw">
                            Delete Universe
                          </YBLabelWithIcon>
                        </YBMenuItem>
                      </RbacValidator>
                    </>
                  )}
                  subMenus={{
                    security: (backToMainMenu) => (
                      <>
                        <SecurityMenu
                          backToMainMenu={backToMainMenu}
                          showTLSConfigurationModal={showTLSConfigurationModal}
                          editTLSAvailability={editTLSAvailability}
                          showManageKeyModal={showManageKeyModal}
                          manageKeyAvailability={manageKeyAvailability}
                        />
                      </>
                    )
                  }}
                />
              </DropdownButton>
            </div>
          </div>
        )}
        <RollingUpgradeFormContainer
          modalVisible={
            showModal &&
            (visibleModal === 'gFlagsModal' ||
              visibleModal === 'softwareUpgradesModal' ||
              visibleModal === 'vmImageUpgradeModal' ||
              (visibleModal === 'tlsConfigurationModal' && !isCACertRotationEnabled) ||
              visibleModal === 'rollingRestart' ||
              visibleModal === 'thirdpartyUpgradeModal' ||
              visibleModal === 'systemdUpgrade')
          }
          onHide={closeModal}
        />
        <EditGflagsModal
          open={showModal && visibleModal === 'gFlagsNewModal'}
          onClose={() => {
            closeModal();
            this.props.fetchCustomerTasks();
            this.props.getUniverseInfo(currentUniverse.data.universeUUID);
          }}
          isGFlagMultilineConfEnabled={isGFlagMultilineConfEnabled}
          universeData={currentUniverse.data}
        />

        <DeleteUniverseContainer
          visible={showModal && visibleModal === 'deleteUniverseModal'}
          onHide={closeModal}
          title="Delete Universe: "
          body="Are you sure you want to delete the universe? You will lose all your data!"
          type="primary"
        />

        <ToggleUniverseStateContainer
          visible={showModal && visibleModal === 'toggleUniverseStateForm'}
          onHide={closeModal}
          title={`${!universePaused ? 'Pause' : 'Resume'} Universe: `}
          type="primary"
          universePaused={universePaused}
        />
        <ToggleBackupStateContainer
          visible={showModal && visibleModal === 'toggleBackupModalForm'}
          onHide={closeModal}
          universe={currentUniverse.data}
          type="primary"
        />
        {isCACertRotationEnabled && showModal && visibleModal === 'tlsConfigurationModal' && (
          <EncryptionInTransit
            open={showModal && visibleModal === 'tlsConfigurationModal'}
            onClose={() => {
              closeModal();
              this.props.getUniverseInfo(currentUniverse.data.universeUUID);
              this.props.fetchCustomerTasks();
            }}
            universe={currentUniverse.data}
          />
        )}
        {isMKREnabled ? (
          <EncryptionAtRest
            open={showModal && visibleModal === 'manageKeyModal'}
            onClose={() => {
              closeModal();
              this.props.fetchCustomerTasks();
              this.props.getUniverseInfo(currentUniverse.data.universeUUID);
            }}
            universeDetails={currentUniverse.data}
          />
        ) : (
          <EncryptionKeyModalContainer
            modalVisible={showModal && visibleModal === 'manageKeyModal'}
            onHide={closeModal}
            handleSubmitKey={this.handleSubmitManageKey}
            currentUniverse={currentUniverse}
            name={currentUniverse.data.name}
            uuid={currentUniverse.data.universeUUID}
          />
        )}

        <EnableYSQLModal
          open={showModal && visibleModal === 'enableYSQLModal'}
          onClose={() => {
            closeModal();
            this.props.fetchCustomerTasks();
            this.props.getUniverseInfo(currentUniverse.data.universeUUID);
          }}
          enforceAuth={isAuthEnforced}
          universeData={currentUniverse.data}
        />
        <EnableYCQLModal
          open={showModal && visibleModal === 'enableYCQLModal'}
          onClose={() => {
            closeModal();
            this.props.fetchCustomerTasks();
            this.props.getUniverseInfo(currentUniverse.data.universeUUID);
          }}
          enforceAuth={isAuthEnforced}
          universeData={currentUniverse.data}
          isItKubernetesUniverse={isItKubernetesUniverse}
        />

        <Measure onMeasure={this.onResize.bind(this)}>
          <YBTabsWithLinksPanel
            defaultTab={defaultTab}
            activeTab={activeTab}
            routePrefix={`/universes/${currentUniverse.data.universeUUID}/`}
            id={'universe-tab-panel'}
            className={'universe-detail'}
          >
            {tabElements}
          </YBTabsWithLinksPanel>
        </Measure>
      </Grid>
    );
  }
}

export default withRouter(mouseTrap(UniverseDetail));
