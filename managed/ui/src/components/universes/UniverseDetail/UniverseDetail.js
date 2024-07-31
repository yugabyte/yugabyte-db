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
import { ListTablesContainer, ReplicationContainer } from '../../tables';
import { QueriesViewer } from '../../queries';
import { isEmptyObject } from '../../../utils/ObjectUtils';
import {
  isKubernetesUniverse,
  isPausableUniverse,
  getPrimaryCluster,
  hasLiveNodes,
  isAsymmetricCluster
} from '../../../utils/UniverseUtils';
import { getReadOnlyClusters } from '../../../utils/universeUtilsTyped';
import { getPromiseState } from '../../../utils/PromiseUtils';

import { YBTooltip } from '../../../redesign/components';
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
import { EditPGCompatibilityModal } from '../../../redesign/features/universe/universe-actions/edit-pg-compatibility/EditPGCompatibilityModal';
import { EnableYSQLModal } from '../../../redesign/features/universe/universe-actions/edit-ysql-ycql/EnableYSQLModal';
import { EnableYCQLModal } from '../../../redesign/features/universe/universe-actions/edit-ysql-ycql/EnableYCQLModal';
import { EditGflagsModal } from '../../../redesign/features/universe/universe-actions/edit-gflags/EditGflags';
import { UpgradeLinuxVersionModal } from '../../configRedesign/providerRedesign/components/linuxVersionCatalog/UpgradeLinuxVersionModal';
import { DBUpgradeModal } from '../../../redesign/features/universe/universe-actions/rollback-upgrade/DBUpgradeModal';
import { DBRollbackModal } from '../../../redesign/features/universe/universe-actions/rollback-upgrade/DBRollbackModal';
import { ReplicationSlotTable } from '../../../redesign/features/universe/universe-tabs/replication-slots/ReplicationSlotTable';
import { AuditLog } from '../../../redesign/features/universe/universe-tabs/db-audit-logs/AuditLog';
import { UniverseState, getUniverseStatus, SoftwareUpgradeState } from '../helpers/universeHelpers';
import { TaskDetailBanner } from '../../../redesign/features/tasks/components/TaskDetailBanner';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { DrPanel } from '../../xcluster/disasterRecovery/DrPanel';
import { TroubleshootRegistrationDetails } from '../TroubleshootUniverse/TroubleshootRegistrationDetails';
import {
  VM_PATCHING_RUNTIME_CONFIG,
  isImgBundleSupportedByProvider
} from '../../configRedesign/providerRedesign/components/linuxVersionCatalog/LinuxVersionUtils';

import { AppName } from '../../../redesign/features/Troubleshooting/TroubleshootingDashboard';
import { RuntimeConfigKey, UNIVERSE_TASKS } from '../../../redesign/helpers/constants';
import { isActionFrozen } from '../../../redesign/helpers/utils';
import {
  getCurrentVersion,
  isVersionPGSupported
} from '../../../redesign/features/universe/universe-form/utils/helpers';

//icons
import ClockRewind from '../../../redesign/assets/clock-rewind.svg';
import ClockRewindDisabled from '../../../redesign/assets/clock-rewind-disabled.svg';

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
        this.props.fetchProviderRunTimeConfigs(providerUUID);
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
    // Always refresh universe info on Overview tab
    if (prevProps.params.tab !== this.props.params.tab && this.props.params.tab === 'overview') {
      this.props.getUniverseInfo(currentUniverse.data.universeUUID);
    }
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

  isUniverseDeleting = () => {
    const updateInProgress = this.props.universe?.currentUniverse?.data?.universeDetails
      ?.updateInProgress;
    const universeUUID = this.props.universe.currentUniverse.data.universeUUID;
    const currentUniverseTasks = this.isCurrentUniverseDeleteTask(universeUUID);
    if (currentUniverseTasks?.length > 0 && updateInProgress) {
      return true;
    }
    return false;
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
      showLinuxSoftwareUpgradeModal,
      showSoftwareUpgradesNewModal,
      showRollbackModal,
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
      showPGCompatibilityModal,
      updateBackupState,
      closeModal,
      customer,
      customer: { currentCustomer, currentUser, runtimeConfigs, providerRuntimeConfigs },
      params: { tab },
      featureFlags,
      providers,
      accessKeys,
      graph
    } = this.props;
    const { showAlert, alertType, alertMessage } = this.state;
    const universePaused = universe?.currentUniverse?.data?.universeDetails?.universePaused;
    const updateInProgress = universe?.currentUniverse?.data?.universeDetails?.updateInProgress;
    //universe state to show Rollback optin in uniiverse actions
    const isRollBackAllowed =
      universe?.currentUniverse?.data?.universeDetails?.isSoftwareRollbackAllowed;
    const backupRestoreInProgress =
      updateInProgress &&
      ['BackupTable', 'MultiTableBackup', 'CreateBackup', 'RestoreBackup'].includes(
        universe?.currentUniverse?.data?.universeDetails?.updatingTask
      );
    //universe state to determine upgrade state of universe
    const upgradeState = universe?.currentUniverse?.data?.universeDetails?.softwareUpgradeState;

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
    const isProviderNodeAgentEnabled = provider?.details?.enableNodeAgent;
    let onPremSkipProvisioning = false;
    if (provider && provider.code === 'onprem') {
      const onPremKey = accessKeys.data.find(
        (accessKey) => accessKey.idKey.providerUUID === provider.uuid
      );
      onPremSkipProvisioning = onPremKey?.keyInfo.skipProvisioning;
    }

    const isNodeAgentClientEnabled =
      providerRuntimeConfigs?.data?.configEntries?.find(
        (config) => config.key === RuntimeConfigKey.ENABLE_NODE_AGENT
      )?.value === 'true';
    runtimeConfigs?.data?.configEntries?.find(
      (config) => config.key === RuntimeConfigKey.PERFOMANCE_ADVISOR_UI_FEATURE_FLAG
    )?.value === 'true';
    const isPerfAdvisorEnabled =
      runtimeConfigs?.data?.configEntries?.find(
        (config) => config.key === RuntimeConfigKey.PERFOMANCE_ADVISOR_UI_FEATURE_FLAG
      )?.value === 'true';
    const isDrEnabled =
      runtimeConfigs?.data?.configEntries?.find(
        (config) => config.key === RuntimeConfigKey.DISASTER_RECOVERY_FEATURE_FLAG
      )?.value === 'true';
    const isAuthEnforced =
      runtimeConfigs?.data?.configEntries?.find(
        (config) => config.key === RuntimeConfigKey.IS_UNIVERSE_AUTH_ENFORCED
      )?.value === 'true';
    const isGFlagMultilineConfEnabled =
      runtimeConfigs?.data?.configEntries?.find(
        (config) => config.key === RuntimeConfigKey.IS_GFLAG_MULTILINE_ENABLED
      )?.value === 'true';
    const isReleasesEnabled =
      runtimeConfigs?.data?.configEntries?.find(
        (config) => config.key === RuntimeConfigKey.RELEASES_REDESIGN_UI_FEATURE_FLAG
      )?.value === 'true';
    const isGFlagAllowDuringPrefinalize =
      runtimeConfigs?.data?.configEntries?.find(
        (config) => config.key === RuntimeConfigKey.GFLAGS_ALLOW_DURING_PREFINALIZE
      )?.value === 'true';

    const isConfigureYSQLEnabled =
      runtimeConfigs?.data?.configEntries?.find((c) => c.key === 'yb.configure_db_api.ysql')
        ?.value === 'true';

    const isConfigureYCQLEnabled =
      runtimeConfigs?.data?.configEntries?.find((c) => c.key === 'yb.configure_db_api.ycql')
        ?.value === 'true';

    const isRollBackFeatureEnabled =
      runtimeConfigs?.data?.configEntries?.find(
        (c) => c.key === 'yb.upgrade.enable_rollback_support'
      )?.value === 'true';

    const isOsPatchingEnabled =
      runtimeConfigs?.data?.configEntries?.find((c) => c.key === VM_PATCHING_RUNTIME_CONFIG)
        ?.value === 'true';

    const isTroubleshootingEnabled =
      runtimeConfigs?.data?.configEntries?.find(
        (c) => c.key === RuntimeConfigKey.ENABLE_TROUBLESHOOTING
      )?.value === 'true';

    const isK8OperatorBlocked =
      runtimeConfigs?.data?.configEntries?.find(
        (config) => config.key === RuntimeConfigKey.BLOCK_K8_OPERATOR
      )?.value === 'true';

    const isAuditLogEnabled =
      runtimeConfigs?.data?.configEntries?.find(
        (config) => config.key === RuntimeConfigKey.ENABLE_AUDIT_LOG
      )?.value === 'true';

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
    const hasAsymmetricPrimaryCluster = !!isAsymmetricCluster(
      getPrimaryCluster(currentUniverse.data?.universeDetails.clusters)
    );
    const hasAsymmetricAsyncCluster = !!getReadOnlyClusters(
      currentUniverse.data?.universeDetails.clusters
    )?.some((cluster) => isAsymmetricCluster(cluster));
    const allowedTasks = currentUniverse.data.allowedTasks;
    const isItKubernetesUniverse = isKubernetesUniverse(currentUniverse.data);
    const isKubernetesOperatorControlled =
      currentUniverse.data?.universeDetails?.isKubernetesOperatorControlled;
    // isKubernetesOperatorControlled can be undefined in older universes and
    // hence using double exclamation to have a boolean value
    const isK8ActionsDisabled =
      isItKubernetesUniverse && isK8OperatorBlocked && !!isKubernetesOperatorControlled;
    const isUpgradeSoftwareDisabled =
      isUniverseStatusPending ||
      [SoftwareUpgradeState.PRE_FINALIZE].includes(upgradeState) ||
      isActionFrozen(allowedTasks, UNIVERSE_TASKS.UPGRADE_SOFTWARE) ||
      isK8ActionsDisabled;
    const isUpgradeDBDisabled =
      isUniverseStatusPending ||
      [SoftwareUpgradeState.PRE_FINALIZE].includes(upgradeState) ||
      isActionFrozen(allowedTasks, UNIVERSE_TASKS.UPGRADE_DB_VERSION) ||
      isK8ActionsDisabled;
    const isRollBackUpgradeDisabled =
      isUniverseStatusPending || isActionFrozen(allowedTasks, UNIVERSE_TASKS.ROLLBACK_UPGRADE);

    const isUpgradeLinuxDisabled =
      isUniverseStatusPending || isActionFrozen(allowedTasks, UNIVERSE_TASKS.UPGRADE_LINUX_VERSION);
    const isUpgradeVMImageDisabled =
      isUniverseStatusPending || isActionFrozen(allowedTasks, UNIVERSE_TASKS.UPGRADE_VM_IMAGE);
    const isUpgradeToSystemdDisabled =
      isUniverseStatusPending ||
      onPremSkipProvisioning ||
      isActionFrozen(allowedTasks, UNIVERSE_TASKS.UPGRADE_TO_SYSTEMD);
    const isThirdPartySoftwareDisabled =
      isUniverseStatusPending ||
      isActionFrozen(allowedTasks, UNIVERSE_TASKS.UPGRADE_THIRD_PARTY_SOFTWARE);
    const isEditUniverseDisabled =
      isUniverseStatusPending ||
      hasAsymmetricPrimaryCluster ||
      isActionFrozen(allowedTasks, UNIVERSE_TASKS.EDIT_UNIVERSE) ||
      isK8ActionsDisabled;
    const isEditGFlagsDisabled =
      isUniverseStatusPending ||
      ([SoftwareUpgradeState.PRE_FINALIZE].includes(upgradeState) &&
        !isGFlagAllowDuringPrefinalize) ||
      hasAsymmetricPrimaryCluster ||
      hasAsymmetricAsyncCluster ||
      isActionFrozen(allowedTasks, UNIVERSE_TASKS.EDIT_FLAGS) ||
      isK8ActionsDisabled;
    const isEditK8Overrides = isUniverseStatusPending || isK8ActionsDisabled;
    const isEditSecurityDisabled = isUniverseStatusPending || isK8ActionsDisabled;
    const isYSQLConfigDisabled =
      isUniverseStatusPending ||
      isActionFrozen(allowedTasks, UNIVERSE_TASKS.EDIT_YSQL_CONFIG) ||
      isK8ActionsDisabled;
    const isYCQLConfigDisabled =
      isUniverseStatusPending ||
      isActionFrozen(allowedTasks, UNIVERSE_TASKS.EDIT_YCQL_CONFIG) ||
      isK8ActionsDisabled;
    const isRollingRestartDisabled =
      isUniverseStatusPending ||
      isActionFrozen(allowedTasks, UNIVERSE_TASKS.INITIATE_ROLLING_RESTART);
    const isReadReplicaDisabled =
      isUniverseStatusPending ||
      hasAsymmetricAsyncCluster ||
      isActionFrozen(
        allowedTasks,
        this.hasReadReplica(universeInfo) ? UNIVERSE_TASKS.EDIT_RR : UNIVERSE_TASKS.ADD_RR
      ) ||
      isK8ActionsDisabled;
    const isSampleAppsDisabled = isUniverseStatusPending && !backupRestoreInProgress;
    const isSupportBundleDisabled =
      this.isUniverseDeleting() || isActionFrozen(allowedTasks, UNIVERSE_TASKS.SUPPORT_BUNDLES);
    const isBackupsDisabled = isUniverseStatusPending || isK8ActionsDisabled;
    const isPauseUniverseDisabled =
      (universePaused && isUniverseStatusPending) ||
      this.isUniverseDeleting() ||
      isActionFrozen(allowedTasks, UNIVERSE_TASKS.PAUSE_UNIVERSE);
    const isDeleteUniverseDisabled =
      isActionFrozen(allowedTasks, UNIVERSE_TASKS.DELETE_UNIVERSE) || isK8ActionsDisabled;

    const isPGCompatibilitySupported = isVersionPGSupported(
      getCurrentVersion(currentUniverse.data.universeDetails)
    );

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
              isReleasesEnabled={isReleasesEnabled}
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
        isNotHidden(currentCustomer.data.features, 'universes.details.recovery') && isDrEnabled && (
          <Tab.Pane
            eventKey={'recovery'}
            tabtitle="xCluster Disaster Recovery"
            key="recovery-tab"
            mountOnEnter={true}
            unmountOnExit={true}
          >
            <DrPanel currentUniverseUuid={currentUniverse.data.universeUUID} />
          </Tab.Pane>
        ),
        isNotHidden(currentCustomer.data.features, 'universes.details.replication') && (
          <Tab.Pane
            eventKey={'replication'}
            tabtitle="xCluster Replication"
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
              refreshUniverseData={this.getUniverseInfo}
              visibleModal={visibleModal}
            />
          </Tab.Pane>
        ),
        isNotHidden(currentCustomer.data.features, 'universes.details.troubleshooting') &&
          isTroubleshootingEnabled && (
            <Tab.Pane
              eventKey={'troubleshoot'}
              tabtitle="Troubleshoot"
              key="troubleshoot-tab"
              mountOnEnter={true}
              unmountOnExit={true}
              disabled={isDisabled(
                currentCustomer.data.features,
                'universes.details.troubleshooting'
              )}
            >
              <TroubleshootRegistrationDetails
                universeUuid={currentUniverse.data.universeUUID}
                appName={AppName.YBA}
                timezone={currentUser.data.timezone}
              />
            </Tab.Pane>
          )
      ],
      //tabs relevant for non-imported universes only
      ...(isReadOnlyUniverse
        ? []
        : [
            !isItKubernetesUniverse && isAuditLogEnabled && (
              <Tab.Pane
                eventKey={'db-audit-log'}
                tabtitle="Logs"
                key="db-audit-log"
                mountOnEnter={true}
                unmountOnExit={true}
              >
                <AuditLog universeData={currentUniverse.data} universePaused={universePaused} />
              </Tab.Pane>
            ),
            isNotHidden(currentCustomer.data.features, 'universes.details.backups') && (
              <Tab.Pane
                eventKey={'backups'}
                tabtitle={<>Backups</>}
                key="backups-tab"
                mountOnEnter={true}
                unmountOnExit={true}
                disabled={isDisabled(currentCustomer.data.features, 'universes.details.backups')}
              >
                <UniverseLevelBackup />
              </Tab.Pane>
            ),
            (featureFlags.released.showReplicationSlots ||
              featureFlags.test.showReplicationSlots) && (
              <Tab.Pane
                eventKey={'replication-slots'}
                tabtitle="CDC Replication Slots"
                key="ReplicationSlots-tab"
                mountOnEnter={true}
                unmountOnExit={true}
              >
                <ReplicationSlotTable
                  universeUUID={currentUniverse.data.universeUUID}
                  nodePrefix={currentUniverse.data.universeDetails.nodePrefix}
                />
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
                  isNodeAgentEnabled={isProviderNodeAgentEnabled && isNodeAgentClientEnabled}
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
    const actionMenuButtons = isNotHidden(
      currentCustomer.data.features,
      'universes.details.pageActions'
    ) && (
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
                  {!universePaused && !isRollBackFeatureEnabled && (
                    <RbacValidator
                      isControl
                      accessRequiredOn={{
                        onResource: uuid,
                        ...ApiPermissionMap.MODIFY_UNIVERSE
                      }}
                    >
                      <YBMenuItem
                        disabled={isUpgradeSoftwareDisabled}
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
                  {!universePaused && isRollBackFeatureEnabled && (
                    <RbacValidator
                      isControl
                      accessRequiredOn={{
                        onResource: uuid,
                        ...ApiPermissionMap.MODIFY_UNIVERSE
                      }}
                    >
                      <YBMenuItem
                        disabled={isUpgradeDBDisabled}
                        onClick={showSoftwareUpgradesNewModal}
                        availability={getFeatureState(
                          currentCustomer.data.features,
                          'universes.details.overview.upgradeSoftware'
                        )}
                      >
                        <YBLabelWithIcon icon="fa fa-arrow-up fa-fw">
                          Upgrade Database Version
                        </YBLabelWithIcon>
                        {/* {this.showUpgradeMarker() && (
                          <span className="badge badge-pill badge-red pull-right">
                            {updateAvailable}
                          </span>
                        )} */}
                      </YBMenuItem>
                    </RbacValidator>
                  )}
                  {!universePaused && isRollBackAllowed && isRollBackFeatureEnabled && (
                    <RbacValidator
                      isControl
                      accessRequiredOn={{
                        onResource: uuid,
                        ...ApiPermissionMap.MODIFY_UNIVERSE
                      }}
                    >
                      <YBMenuItem
                        disabled={isRollBackUpgradeDisabled}
                        onClick={showRollbackModal}
                        availability={getFeatureState(
                          currentCustomer.data.features,
                          'universes.details.overview.upgradeSoftware'
                        )}
                      >
                        <YBLabelWithIcon>
                          <img
                            src={isUniverseStatusPending ? ClockRewindDisabled : ClockRewind}
                            height="16px"
                            width="16px"
                          />
                          &nbsp; Roll Back Upgrade
                        </YBLabelWithIcon>
                      </YBMenuItem>
                    </RbacValidator>
                  )}
                  {!universePaused &&
                    isOsPatchingEnabled &&
                    isImgBundleSupportedByProvider(provider) && (
                      <RbacValidator
                        isControl
                        accessRequiredOn={{
                          onResource: uuid,
                          ...ApiPermissionMap.MODIFY_UNIVERSE
                        }}
                      >
                        <YBMenuItem
                          disabled={isUpgradeLinuxDisabled}
                          onClick={showLinuxSoftwareUpgradeModal}
                          availability={getFeatureState(
                            currentCustomer.data.features,
                            'universes.details.overview.upgradeSoftware'
                          )}
                        >
                          <YBLabelWithIcon icon="fa fa-arrow-up fa-fw">
                            Upgrade Linux Version
                          </YBLabelWithIcon>
                        </YBMenuItem>
                      </RbacValidator>
                    )}
                  {!universePaused &&
                    runtimeConfigs &&
                    !isOsPatchingEnabled &&
                    getPromiseState(runtimeConfigs).isSuccess() &&
                    runtimeConfigs.data.configEntries.find((c) => c.key === 'yb.upgrade.vmImage')
                      .value === 'true' && (
                      <RbacValidator
                        isControl
                        accessRequiredOn={{
                          onResource: uuid,
                          ...ApiPermissionMap.MODIFY_UNIVERSE
                        }}
                      >
                        <YBMenuItem
                          disabled={isUpgradeVMImageDisabled}
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
                        disabled={isUpgradeToSystemdDisabled}
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
                        disabled={isThirdPartySoftwareDisabled}
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
                        <YBTooltip
                          title={
                            hasAsymmetricPrimaryCluster
                              ? 'Editing asymmetric clusters is not supported from the UI. Please use the YBA API to edit instead.'
                              : ''
                          }
                          placement="left"
                        >
                          <span>
                            <YBMenuItem
                              to={`/universes/${uuid}/edit/primary`}
                              availability={getFeatureState(
                                currentCustomer.data.features,
                                'universes.details.overview.editUniverse'
                              )}
                              disabled={isEditUniverseDisabled}
                            >
                              <YBLabelWithIcon icon="fa fa-pencil">Edit Universe</YBLabelWithIcon>
                            </YBMenuItem>
                          </span>
                        </YBTooltip>
                      </RbacValidator>
                    )}
                  {!universePaused && !this.isRRFlagsEnabled() && (
                    <RbacValidator
                      isControl
                      accessRequiredOn={{
                        onResource: uuid,
                        ...ApiPermissionMap.UPGRADE_UNIVERSE_GFLAGS
                      }}
                      overrideStyle={{ display: 'block' }}
                    >
                      <YBTooltip
                        title={
                          hasAsymmetricPrimaryCluster
                            ? 'Editing gflags for asymmetric clusters is not supported from the UI. Please use the YBA API to edit instead.'
                            : ''
                        }
                        placement="left"
                      >
                        <span>
                          <YBMenuItem
                            disabled={isEditGFlagsDisabled}
                            onClick={showGFlagsModal}
                            availability={getFeatureState(
                              currentCustomer.data.features,
                              'universes.details.overview.editGFlags'
                            )}
                          >
                            <YBLabelWithIcon icon="fa fa-flag fa-fw">Edit Flags</YBLabelWithIcon>
                          </YBMenuItem>
                        </span>
                      </YBTooltip>
                    </RbacValidator>
                  )}
                  {!universePaused && this.isRRFlagsEnabled() && (
                    <RbacValidator
                      isControl
                      accessRequiredOn={{
                        onResource: uuid,
                        ...ApiPermissionMap.UPGRADE_UNIVERSE_GFLAGS
                      }}
                      overrideStyle={{ display: 'block' }}
                    >
                      <YBTooltip
                        title={
                          hasAsymmetricPrimaryCluster
                            ? 'Editing gflags for asymmetric clusters is not supported from the UI. Please use the YBA API to edit instead.'
                            : ''
                        }
                        placement="left"
                      >
                        <span>
                          <YBMenuItem
                            disabled={isEditGFlagsDisabled}
                            onClick={showGFlagsNewModal}
                            availability={getFeatureState(
                              currentCustomer.data.features,
                              'universes.details.overview.editGFlags'
                            )}
                          >
                            <YBLabelWithIcon icon="fa fa-flag fa-fw">Edit Flags</YBLabelWithIcon>
                          </YBMenuItem>
                        </span>
                      </YBTooltip>
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
                        disabled={isEditK8Overrides}
                        onClick={
                          showHelmOverridesModal ||
                          isActionFrozen(allowedTasks, UNIVERSE_TASKS.EDIT_KUBERNETES_OVERRIDES)
                        }
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
                        disabled={isEditSecurityDisabled}
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
                  {!universePaused && isPGCompatibilitySupported && (
                    <RbacValidator
                      accessRequiredOn={{
                        onResource: uuid,
                        ...ApiPermissionMap.UPGRADE_UNIVERSE_GFLAGS
                      }}
                      isControl
                    >
                      <YBMenuItem
                        disabled={isEditGFlagsDisabled}
                        onClick={showPGCompatibilityModal}
                        availability={getFeatureState(
                          currentCustomer.data.features,
                          'universes.details.overview.editGFlags'
                        )}
                      >
                        <YBLabelWithIcon icon="fa fa-retweet fa-fw">
                          Edit Postgres Compatibility
                        </YBLabelWithIcon>
                      </YBMenuItem>
                    </RbacValidator>
                  )}
                  {!universePaused && isConfigureYSQLEnabled && (
                    <RbacValidator
                      accessRequiredOn={{
                        onResource: uuid,
                        ...ApiPermissionMap.UNIVERSE_CONFIGURE_YSQL
                      }}
                      isControl
                    >
                      <YBMenuItem
                        disabled={isYSQLConfigDisabled}
                        onClick={showEnableYSQLModal}
                        availability={getFeatureState(
                          currentCustomer.data.features,
                          'universes.details.overview.editUniverse'
                        )}
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
                        ...ApiPermissionMap.UNIVERSE_CONFIGURE_YCQL
                      }}
                      isControl
                    >
                      <YBMenuItem
                        disabled={isYCQLConfigDisabled}
                        onClick={showEnableYCQLModal}
                        availability={getFeatureState(
                          currentCustomer.data.features,
                          'universes.details.overview.editUniverse'
                        )}
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
                        disabled={isRollingRestartDisabled}
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
                        ...(this.hasReadReplica(universeInfo)
                          ? ApiPermissionMap.GET_UNIVERSES_BY_ID
                          : ApiPermissionMap.CREATE_READ_REPLICA)
                      }}
                    >
                      <YBTooltip
                        title={
                          hasAsymmetricAsyncCluster
                            ? 'Editing asymmetric clusters is not supported from the UI. Please use the YBA API to edit instead.'
                            : ''
                        }
                        placement="left"
                      >
                        <span>
                          <YBMenuItem
                            disabled={isReadReplicaDisabled}
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
                        </span>
                      </YBTooltip>
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
                            disabled={isSampleAppsDisabled}
                            onClick={showRunSampleAppsModal}
                          >
                            <YBLabelWithIcon icon="fa fa-terminal">Run Sample Apps</YBLabelWithIcon>
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
                              <YBMenuItem
                                onClick={showSupportBundleModal}
                                disabled={isSupportBundleDisabled}
                              >
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
                        disabled={isBackupsDisabled}
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
                          disabled={isPauseUniverseDisabled}
                        >
                          <YBLabelWithIcon
                            icon={universePaused ? 'fa fa-play-circle-o' : 'fa fa-pause-circle-o'}
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
                      disabled={isDeleteUniverseDisabled}
                    >
                      <YBLabelWithIcon icon="fa fa-trash-o fa-fw">Delete Universe</YBLabelWithIcon>
                    </YBMenuItem>
                  </RbacValidator>
                </>
              )}
              subMenus={{
                security: (backToMainMenu) => (
                  <>
                    <SecurityMenu
                      backToMainMenu={backToMainMenu}
                      allowedTasks={allowedTasks}
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
    );
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
          <h2>
            <a href={`/universes/${currentUniverse.data.universeUUID}`}>
              {currentUniverse.data.name}
            </a>
          </h2>
          <UniverseStatusContainer
            currentUniverse={currentUniverse.data}
            showLabelText={true}
            refreshUniverseData={this.getUniverseInfo}
            shouldDisplayTaskButton={true}
          />
        </div>
        <TaskDetailBanner taskUUID={currentUniverse.data.universeDetails.updatingTaskUUID} universeUUID={currentUniverse.data.universeUUID}/>
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
        <UpgradeLinuxVersionModal
          visible={showModal && visibleModal === 'linuxVersionUpgradeModal'}
          onHide={() => {
            closeModal();
          }}
          universeData={currentUniverse.data}
        />

        <DBUpgradeModal
          open={showModal && visibleModal === 'softwareUpgradesNewModal'}
          onClose={() => {
            closeModal();
            this.props.fetchCustomerTasks();
            this.props.getUniverseInfo(currentUniverse.data.universeUUID);
          }}
          universeData={currentUniverse.data}
          isReleasesEnabled={isReleasesEnabled}
        />

        <DBRollbackModal
          open={showModal && visibleModal === 'rollbackModal'}
          onClose={() => {
            closeModal();
            this.props.fetchCustomerTasks();
            this.props.getUniverseInfo(currentUniverse.data.universeUUID);
          }}
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

        <EditPGCompatibilityModal
          open={showModal && visibleModal === 'enablePGCompatibility'}
          onClose={() => {
            closeModal();
            this.props.fetchCustomerTasks();
            this.props.getUniverseInfo(currentUniverse.data.universeUUID);
          }}
          universeData={currentUniverse.data}
        />

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
            {
              [
                ...tabElements,
                <div title={actionMenuButtons} />
              ]
            }
          </YBTabsWithLinksPanel>
        </Measure>
      </Grid>
    );
  }
}

export default withRouter(mouseTrap(UniverseDetail));
