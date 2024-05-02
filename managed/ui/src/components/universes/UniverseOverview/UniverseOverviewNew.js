// Copyright (c) YugaByte, Inc.

import { Component, PureComponent, Fragment } from 'react';
import { Link } from 'react-router';
import { Row, Col } from 'react-bootstrap';
import moment from 'moment';
import pluralize from 'pluralize';
import PropTypes from 'prop-types';
import { FormattedDate, FormattedRelative } from 'react-intl';
import { ClusterInfoPanelContainer, YBWidget } from '../../panels';
import {
  OverviewMetricsContainer,
  StandaloneMetricsPanelContainer,
  DiskUsagePanel,
  CpuUsagePanel,
  QueryDisplayPanel
} from '../../metrics';
import { YBResourceCount, YBCost, DescriptionList } from '../../../components/common/descriptors';
import { RegionMap, YBMapLegend } from '../../maps';
import {
  isNonEmptyObject,
  isNullOrEmpty,
  isNonEmptyArray,
  isNonEmptyString,
  isDefinedNotNull
} from '../../../utils/ObjectUtils';
import {
  isKubernetesUniverse,
  getPrimaryCluster,
  isDedicatedNodePlacement
} from '../../../utils/UniverseUtils';
import { FlexContainer, FlexGrow, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { DBVersionWidget } from '../../../redesign/features/universe/universe-overview/DBVersionWidget';
import { PreFinalizeBanner } from '../../../redesign/features/universe/universe-actions/rollback-upgrade/components/PreFinalizeBanner';
import { FailedBanner } from '../../../redesign/features/universe/universe-actions/rollback-upgrade/components/FailedBanner';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { YBButton, YBModal } from '../../common/forms/fields';
import { isEnabled, isDisabled } from '../../../utils/LayoutUtils';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { RuntimeConfigKey } from '../../../redesign/helpers/constants';
import {
  SoftwareUpgradeState,
  getcurrentUniverseFailedTask,
  SoftwareUpgradeTaskType,
  getUniverseStatus,
  UniverseState
} from '../helpers/universeHelpers';

class DatabasePanel extends PureComponent {
  static propTypes = {
    universeInfo: PropTypes.object.isRequired
  };

  render() {
    const {
      universeInfo: {
        universeDetails: { clusters }
      }
    } = this.props;
    const primaryCluster = getPrimaryCluster(clusters);
    const userIntent = primaryCluster?.userIntent;

    const optimizeVersion = (version) => {
      if (parseInt(version[version.length - 1], 10) === 0) {
        return optimizeVersion(version.slice(0, version.length - 1));
      } else {
        return version.join('.');
      }
    };
    return (
      <Row className={'overview-database-version'}>
        <Col xs={12} className="centered">
          <YBResourceCount
            className="hidden-costs"
            size={optimizeVersion(userIntent.ybSoftwareVersion.split('-')[0].split('.'))}
          />
        </Col>
      </Row>
    );
  }
}

class HealthHeart extends PureComponent {
  static propTypes = {
    status: PropTypes.string
  };

  render() {
    const { status } = this.props;
    return (
      <div id="health-heart">
        <span className={`fa fa-heart${status === 'loading' ? ' status-loading' : ''}`}></span>
        {status === 'success' && (
          <div id="health-heartbeat">
            <svg
              x="0px"
              y="0px"
              viewBox="0 0 41.8 22.2"
              xmlns="http://www.w3.org/2000/svg"
              strokeLinejoin="round"
              strokeLinecap="round"
            >
              <polyline
                strokeLinejoin="round"
                strokeLinecap="round"
                points="38.3,11.9 29.5,11.9 27.6,9 24,18.6 21.6,3.1 18.6,11.9 2.8,11.9 "
              />
            </svg>
          </div>
        )}

        {status === 'error' && (
          <div id="health-droplet">
            <svg
              x="0px"
              y="0px"
              width="264.564px"
              height="264.564px"
              viewBox="0 0 264.564 264.564"
              xmlns="http://www.w3.org/2000/svg"
              strokeLinejoin="round"
              strokeLinecap="round"
            >
              <path
                strokeLinejoin="round"
                strokeLinecap="round"
                d="M132.281,264.564c51.24,0,92.931-41.681,92.931-92.918c0-50.18-87.094-164.069-90.803-168.891L132.281,0l-2.128,2.773 c-3.704,4.813-90.802,118.71-90.802,168.882C39.352,222.883,81.042,264.564,132.281,264.564z"
              />
            </svg>
          </div>
        )}
      </div>
    );
  }
}

class AlertInfoPanel extends PureComponent {
  static propTypes = {
    alerts: PropTypes.object,
    universeInfo: PropTypes.object
  };

  render() {
    const { alerts, universeInfo } = this.props;
    const errorNodesCounter = alerts.alertsList.length;

    const errorText = errorNodesCounter + ' ' + pluralize('Alert', errorNodesCounter);
    let errorSpan = <span className="text-red text-light">{errorText}</span>;
    let errorHeader = <span className="fa fa-exclamation-triangle text-red" />;
    if (errorNodesCounter && isNonEmptyObject(universeInfo)) {
      errorSpan = (
        <Link className="text-red text-regular" to={'/alerts'}>
          {errorText}
        </Link>
      );
      errorHeader = <Link className="fa fa-exclamation-triangle text-red" to={'/alerts'} />;
    }
    if (alerts.alertsList) {
      const lastUpdateDate = alerts.updated && moment(alerts.updated);
      const healthCheckInfoItems = [
        {
          name: '',
          data: errorNodesCounter ? (
            errorSpan
          ) : (
            <span className="text-green text-light">
              <i className={'fa fa-check'}></i> All running fine
            </span>
          )
        },
        {
          name: '',
          data: lastUpdateDate ? (
            <span className="text-lightgray text-light">
              <i className={'fa fa-clock-o'}></i> Updated{' '}
              <span className={'text-dark text-normal'}>
                <FormattedRelative value={lastUpdateDate} />
              </span>
            </span>
          ) : null
        }
      ];

      return (
        <YBWidget
          size={1}
          className={'overview-widget-cluster-primary'}
          headerLeft={'Alerts'}
          headerRight={errorNodesCounter ? errorHeader : <Link to={`/alerts`}>Details</Link>}
          body={
            <FlexContainer className={'centered health-heart-cnt'} direction={'row'}>
              <FlexGrow>
                <HealthHeart status={errorNodesCounter ? 'error' : 'success'} />
              </FlexGrow>
              <FlexGrow>
                <DescriptionList
                  type={'inline'}
                  className={'health-check-legend'}
                  listItems={healthCheckInfoItems}
                />
              </FlexGrow>
            </FlexContainer>
          }
        />
      );
    }

    const errorContent = {
      heartStatus: 'empty',
      body: 'No finished checks'
    };

    return (
      <YBWidget
        size={1}
        className={'overview-widget-cluster-primary'}
        headerLeft={'Alerts'}
        headerRight={errorNodesCounter ? errorHeader : <Link to={`/alerts`}>Details</Link>}
        body={
          <FlexContainer className={'centered health-heart-cnt'} direction={'column'}>
            <FlexGrow>
              <HealthHeart status={errorNodesCounter ? 'error' : 'success'} />
            </FlexGrow>
            {isNonEmptyString(errorContent.body) && (
              <FlexShrink
                className={errorContent.heartStatus === 'empty' ? 'text-light text-lightgray' : ''}
              >
                {errorContent.body}
              </FlexShrink>
            )}
          </FlexContainer>
        }
      />
    );
  }
}

class HealthInfoPanel extends PureComponent {
  static propTypes = {
    healthCheck: PropTypes.object.isRequired,
    universeInfo: PropTypes.object.isRequired
  };

  render() {
    const { healthCheck, universeInfo } = this.props;
    let disabledUntilStr = '';
    if (getPromiseState(healthCheck).isSuccess()) {
      const healthCheckData = [...healthCheck.data].reverse()[0];
      const lastUpdateDate = moment.utc(healthCheckData.timestamp).local();
      if (universeInfo.universeConfig && 'disableAlertsUntilSecs' in universeInfo.universeConfig) {
        const disabledUntilSecs = Number(universeInfo.universeConfig.disableAlertsUntilSecs);
        const now = Date.now() / 1000;
        if (!Number.isSafeInteger(disabledUntilSecs)) {
          disabledUntilStr = ' Alerts are snoozed';
        } else if (disabledUntilSecs > now) {
          disabledUntilStr =
            ' Alerts are snoozed until ' + moment.unix(disabledUntilSecs).format('MMM DD hh:mm a');
        }
      }
      const totalNodesCounter = healthCheckData.data.length;
      let errorNodesCounter = 0;

      healthCheckData.data.forEach((check) => {
        if (check.has_error) errorNodesCounter++;
      });

      const errorText = errorNodesCounter + ' ' + pluralize('Error', errorNodesCounter);
      let errorSpan = <span className="text-red text-light">{errorText}</span>;
      let errorHeader = <span className="fa fa-exclamation-triangle text-red" />;
      if (errorNodesCounter && isNonEmptyObject(universeInfo)) {
        errorSpan = (
          <Link
            className="text-red text-regular"
            to={`/universes/${universeInfo.universeUUID}/health`}
          >
            {errorText}
          </Link>
        );
        errorHeader = (
          <Link
            className="fa fa-exclamation-triangle text-red"
            to={`/universes/${universeInfo.universeUUID}/health`}
          />
        );
      }

      const healthCheckInfoItems = [
        {
          name: '',
          data: errorNodesCounter ? (
            errorSpan
          ) : totalNodesCounter ? (
            <span className="text-green text-light">
              <i className={'fa fa-check'}></i> All running fine
            </span>
          ) : (
            <span className="text-light">No finished check</span>
          )
        },
        {
          name: '',
          data: lastUpdateDate ? (
            <span className="text-lightgray text-light">
              <i className={'fa fa-clock-o'}></i> Updated{' '}
              <span className={'text-dark text-normal'}>
                <FormattedRelative value={lastUpdateDate} unit="day" />
              </span>
            </span>
          ) : null
        },
        {
          name: '',
          data: disabledUntilStr ? (
            <span className="text-light">
              <i className={'fa fa-exclamation-triangle'}>{disabledUntilStr}</i>
            </span>
          ) : null
        }
      ];

      return (
        <YBWidget
          size={1}
          className={'overview-widget-cluster-primary'}
          headerLeft={'Health Check'}
          headerRight={
            errorNodesCounter ? (
              errorHeader
            ) : (
              <Link to={`/universes/${universeInfo.universeUUID}/health`}>Details</Link>
            )
          }
          body={
            <FlexContainer className={'centered health-heart-cnt'} direction={'row'}>
              <FlexGrow>
                <HealthHeart status={errorNodesCounter ? 'error' : 'success'} />
              </FlexGrow>
              <FlexGrow>
                <DescriptionList
                  type={'inline'}
                  className={'health-check-legend'}
                  listItems={healthCheckInfoItems}
                />
              </FlexGrow>
            </FlexContainer>
          }
        />
      );
    }

    const errorContent = {};
    if (getPromiseState(healthCheck).isEmpty()) {
      errorContent.heartStatus = 'empty';
      errorContent.body = 'No finished checks';
    }
    if (getPromiseState(healthCheck).isError()) {
      errorContent.heartStatus = 'empty';
      errorContent.body = 'Cannot get checks';
    }
    if (getPromiseState(healthCheck).isLoading()) {
      errorContent.heartStatus = 'loading';
      errorContent.body = '';
    }
    return (
      <YBWidget
        size={1}
        className={'overview-widget-cluster-primary'}
        headerLeft={'Health Check'}
        body={
          <FlexContainer className={'centered health-heart-cnt'} direction={'column'}>
            <FlexGrow>
              <HealthHeart status={errorContent.heartClassName} />
            </FlexGrow>
            {isNonEmptyString(errorContent.body) && (
              <FlexShrink
                className={errorContent.heartStatus === 'empty' ? 'text-light text-lightgray' : ''}
              >
                {errorContent.body}
              </FlexShrink>
            )}
          </FlexContainer>
        }
      />
    );
  }
}

export default class UniverseOverviewNew extends Component {
  hasReadReplica = (universeInfo) => {
    const clusters = universeInfo.universeDetails.clusters;
    return clusters.some((cluster) => cluster.clusterType === 'ASYNC');
  };

  getLastUpdateDate = () => {
    const universeTasks = this.tasksForUniverse();
    if (isNonEmptyArray(universeTasks)) {
      const updateTask = universeTasks.find((taskItem) => {
        return taskItem.type === 'UpgradeSoftware';
      });
      return isDefinedNotNull(updateTask)
        ? updateTask.completionTime || updateTask.createTime
        : null;
    }
    return null;
  };

  tasksForUniverse = () => {
    const {
      universe: {
        currentUniverse: {
          data: { universeUUID }
        }
      },
      tasks: { customerTaskList }
    } = this.props;
    const resultTasks = [];
    if (isNonEmptyArray(customerTaskList)) {
      customerTaskList.forEach((taskItem) => {
        if (taskItem.targetUUID === universeUUID) resultTasks.push(taskItem);
      });
    }
    return resultTasks;
  };

  getCostWidget = (currentUniverse) => {
    if (isNullOrEmpty(currentUniverse.resources)) return;
    const isPricingKnown = currentUniverse.resources.pricingKnown;
    const pricePerHour = currentUniverse.resources.pricePerHour;
    const costPerDay = (
      <YBCost
        value={pricePerHour}
        multiplier={'day'}
        isPricingKnown={isPricingKnown}
        runtimeConfigs={this.props.runtimeConfigs}
      />
    );
    const costPerMonth = (
      <YBCost
        value={pricePerHour}
        multiplier={'month'}
        isPricingKnown={isPricingKnown}
        runtimeConfigs={this.props.runtimeConfigs}
      />
    );
    return (
      <Col lg={4} md={6} sm={8} xs={12}>
        <YBWidget
          noHeader
          size={1}
          className={'overview-widget-cost'}
          body={
            <FlexContainer className={'cost-widget centered'} direction={'row'}>
              <FlexShrink>
                <i className="fa fa-money cost-widget__image"></i>
                <span className="cost-widget__label">{'Cost'}</span>
              </FlexShrink>
              <FlexShrink>
                <span className="cost-widget__day">{costPerDay} </span>
                <span data-testid="CostWidgetDay-Label" className="cost-widget__day-label">
                  / day
                </span>
              </FlexShrink>
              <FlexShrink>
                <span className="cost-widget__month">{costPerMonth} </span>
                <span data-testid="CostWidgetMonth-Label">/ month</span>
              </FlexShrink>
            </FlexContainer>
          }
        />
      </Col>
    );
  };

  getPrimaryClusterWidget = (currentUniverse, isRollBackFeatureEnabled) => {
    const isDedicatedNodes = isDedicatedNodePlacement(currentUniverse);
    if (isNullOrEmpty(currentUniverse)) return;

    const clusterWidgetSize = isDedicatedNodes ? 4 : this.hasReadReplica(currentUniverse) ? 3 : 4;

    if (isRollBackFeatureEnabled) {
      return (
        <Col lg={clusterWidgetSize} sm={8} md={6} xs={12}>
          <ClusterInfoPanelContainer
            type={'primary'}
            universeInfo={currentUniverse}
            isDedicatedNodes={isDedicatedNodes}
            runtimeConfigs={this.props.runtimeConfigs}
          />
        </Col>
      );
    } else {
      return isDedicatedNodes ? (
        <Col lg={clusterWidgetSize} sm={8} md={6} xs={12}>
          <ClusterInfoPanelContainer
            type={'primary'}
            universeInfo={currentUniverse}
            isDedicatedNodes={isDedicatedNodes}
            runtimeConfigs={this.props.runtimeConfigs}
          />
        </Col>
      ) : (
        <Col lg={clusterWidgetSize} sm={6} md={6} xs={12}>
          <ClusterInfoPanelContainer
            type={'primary'}
            universeInfo={currentUniverse}
            isDedicatedNodes={isDedicatedNodes}
            runtimeConfigs={this.props.runtimeConfigs}
          />
        </Col>
      );
    }
  };

  getReadReplicaClusterWidget = (currentUniverse) => {
    if (isNullOrEmpty(currentUniverse)) return;
    return (
      <Col lg={3} sm={6} md={6} xs={12}>
        <ClusterInfoPanelContainer
          type={'read-replica'}
          universeInfo={currentUniverse}
          runtimeConfigs={this.props.runtimeConfigs}
        />
      </Col>
    );
  };

  getTablesWidget = (universeInfo) => {
    if (isNullOrEmpty(this.props.tables)) return;
    const { tables } = this.props;

    let numCassandraTables = 0;
    let numRedisTables = 0;
    let numPostgresTables = 0;
    if (isNonEmptyArray(tables.universeTablesList)) {
      tables.universeTablesList.forEach((table, idx) => {
        if (table.tableType === 'REDIS_TABLE_TYPE') {
          numRedisTables++;
        } else if (table.tableType === 'YQL_TABLE_TYPE') {
          numCassandraTables++;
        } else {
          numPostgresTables++;
        }
      });
    }

    return (
      <YBWidget
        size={1}
        className={'overview-widget-tables'}
        headerLeft={'Tables'}
        headerRight={
          isNonEmptyObject(universeInfo) ? (
            <Link to={`/universes/${universeInfo.universeUUID}/tables`}>Details</Link>
          ) : null
        }
        body={
          <FlexContainer className={'centered'}>
            <FlexGrow>
              <YBResourceCount size={numPostgresTables} kind="YSQL" />
            </FlexGrow>
            <FlexGrow>
              <YBResourceCount size={numCassandraTables} kind="YCQL" />
            </FlexGrow>
            <FlexGrow>
              <YBResourceCount size={numRedisTables} kind="YEDIS" />
            </FlexGrow>
          </FlexContainer>
        }
      />
    );
  };

  getHealthWidget = (healthCheck, universeInfo) => {
    const isDedicatedNodes = isDedicatedNodePlacement(universeInfo);
    const hasReadReplicaCluster = this.hasReadReplica(universeInfo);

    return (
      <Col lg={isDedicatedNodes && hasReadReplicaCluster ? 3 : 4} md={6} sm={8} xs={12}>
        <HealthInfoPanel healthCheck={healthCheck} universeInfo={universeInfo} />
      </Col>
    );
  };

  getAlertWidget = (alerts, universeInfo) => {
    return (
      <Col lg={4} md={8} sm={8} xs={12}>
        <AlertInfoPanel alerts={alerts} universeInfo={universeInfo} />
      </Col>
    );
  };

  getDiskUsageWidget = (universeInfo) => {
    // For kubernetes the disk usage would be in container tab, rest it would be server tab.
    const isKubernetes = isKubernetesUniverse(universeInfo);
    const isDedicatedNodes = isDedicatedNodePlacement(universeInfo);
    const subTab = isKubernetes ? 'container' : 'server';
    const metricKey = isKubernetes ? 'container_volume_stats' : 'disk_usage';
    const secondaryMetric = isKubernetes
      ? [
          {
            metric: 'container_volume_max_usage',
            name: 'size'
          }
        ]
      : null;
    const useK8CustomResourcesObject = this.props.runtimeConfigs?.data?.configEntries?.find(
      (c) => c.key === RuntimeConfigKey.USE_K8_CUSTOM_RESOURCES_FEATURE_FLAG
    );
    const useK8CustomResources = useK8CustomResourcesObject?.value === 'true';

    return (
      <StandaloneMetricsPanelContainer
        metricKey={metricKey}
        additionalMetricKeys={secondaryMetric}
        isDedicatedNodes={isDedicatedNodes && !isKubernetes}
        type="overview"
      >
        {(props) => {
          return (
            <YBWidget
              noMargin
              headerRight={
                isNonEmptyObject(universeInfo) ? (
                  <Link to={`/universes/${universeInfo.universeUUID}/metrics?tab=${subTab}`}>
                    Details
                  </Link>
                ) : null
              }
              headerLeft={props.metric.layout.title}
              body={
                <DiskUsagePanel
                  metric={props.metric}
                  masterMetric={props.masterMetric}
                  isKubernetes={isKubernetes}
                  isDedicatedNodes={isDedicatedNodes && !isKubernetes}
                  useK8CustomResources={useK8CustomResources}
                  className={'disk-usage-container'}
                />
              }
            />
          );
        }}
      </StandaloneMetricsPanelContainer>
    );
  };

  getCPUWidget = (universeInfo, isRollBackFeatureEnabled) => {
    // For kubernetes the CPU usage would be in container tab, rest it would be server tab.
    const isItKubernetesUniverse = isKubernetesUniverse(universeInfo);
    const isDedicatedNodes = isDedicatedNodePlacement(universeInfo);
    const hasReadReplicaCluster = this.hasReadReplica(universeInfo);
    const subTab = isItKubernetesUniverse ? 'container' : 'server';
    const useK8CustomResourcesObject = this.props.runtimeConfigs?.data?.configEntries?.find(
      (c) => c.key === RuntimeConfigKey.USE_K8_CUSTOM_RESOURCES_FEATURE_FLAG
    );
    const useK8CustomResources = useK8CustomResourcesObject?.value === 'true';

    return (
      <Col
        lg={(isDedicatedNodes && !isRollBackFeatureEnabled) || hasReadReplicaCluster ? 2 : 4}
        md={4}
        sm={4}
        xs={6}
      >
        <StandaloneMetricsPanelContainer
          metricKey={isItKubernetesUniverse ? 'container_cpu_usage' : 'cpu_usage'}
          isDedicatedNodes={isDedicatedNodes && !isItKubernetesUniverse}
          type="overview"
        >
          {(props) => {
            return (
              <YBWidget
                noMargin
                headerLeft={'CPU Usage'}
                headerRight={
                  <Link to={`/universes/${universeInfo.universeUUID}/metrics?tab=${subTab}`}>
                    Details
                  </Link>
                }
                body={
                  <CpuUsagePanel
                    metric={props.metric}
                    masterMetric={props.masterMetric}
                    className={'disk-usage-container'}
                    isKubernetes={isItKubernetesUniverse}
                    isDedicatedNodes={isDedicatedNodes && !isItKubernetesUniverse}
                    useK8CustomResources={useK8CustomResources}
                  />
                }
              />
            );
          }}
        </StandaloneMetricsPanelContainer>
      </Col>
    );
  };

  getRegionMapWidget = (universeInfo) => {
    const isItKubernetesUniverse = isKubernetesUniverse(universeInfo);
    const {
      modal: { showModal, visibleModal },
      showUniverseOverviewMapModal,
      closeModal
    } = this.props;
    const mapWidget = (
      <YBWidget
        numNode
        noMargin
        size={2}
        headerRight={
          <Fragment>
            <YBButton
              btnClass={'btn-clear'}
              btnIcon={'fa fa-expand'}
              onClick={showUniverseOverviewMapModal}
            />
            <YBModal
              onHide={closeModal}
              className={'modal-map-overview'}
              title={'Map'}
              visible={showModal && visibleModal === 'universeOverviewMapModal'}
            >
              <YBButton
                btnIcon={'fa fa-times'}
                btnClass={'btn btn-default btn-round button-close-map'}
                onClick={closeModal}
              />
              <RegionMap universe={universeInfo} type={'Universe'} setBounds={false} />
              <YBMapLegend
                title="Data Placement (In AZs)"
                clusters={universeInfo.universeDetails.clusters}
                type="Universe"
              />
            </YBModal>
          </Fragment>
        }
        headerLeft={isItKubernetesUniverse ? 'Universe Pods' : 'Universe Nodes'}
        body={
          <div>
            <RegionMap universe={universeInfo} type={'Universe'} setBounds={false} />
            <YBMapLegend
              title="Data Placement (In AZs)"
              clusters={universeInfo.universeDetails.clusters}
              type="Universe"
            />
          </div>
        }
      />
    );

    return (
      <Col lg={4} xs={12}>
        {mapWidget}
      </Col>
    );
  };

  getDatabaseWidget = (universeInfo, tasks) => {
    const lastUpdateDate = this.getLastUpdateDate();
    const {
      universe: { currentUniverse },
      updateAvailable,
      currentCustomer
    } = this.props;
    const showUpdate =
      updateAvailable && !isDisabled(currentCustomer.data.features, 'universes.actions');
    const universePaused = currentUniverse?.data?.universeDetails?.universePaused;
    const updateInProgress = currentUniverse?.data?.universeDetails?.updateInProgress;

    const upgradeLink = () => {
      return updateInProgress ? (
        <span>
          Upgrade <span className="badge badge-pill badge-orange">{updateAvailable}</span>
        </span>
      ) : (
        <RbacValidator
          accessRequiredOn={{
            onResource: universeInfo.universeUUID,
            ...ApiPermissionMap.MODIFY_UNIVERSE
          }}
          isControl
        >
          <a
            onClick={(e) => {
              this.props.showSoftwareUpgradesModal(e);
              e.preventDefault();
            }}
            href="/"
          >
            Upgrade <span className="badge badge-pill badge-orange">{updateAvailable}</span>
          </a>
        </RbacValidator>
      );
    };
    const infoWidget = (
      <YBWidget
        className={'overview-widget-database'}
        headerRight={showUpdate && !universePaused ? upgradeLink() : null}
        noHeader
        size={1}
        body={
          <FlexContainer className={'cost-widget centered'} direction={'row'}>
            <FlexShrink>
              <span className="version__label">{'Version'}</span>
            </FlexShrink>
            <FlexShrink>
              <DatabasePanel universeInfo={universeInfo} tasks={tasks} />
            </FlexShrink>
            <FlexShrink>
              {lastUpdateDate && (
                <div className="text-lightgray text-light">
                  <span className={'fa fa-clock-o'}></span> Upgraded{' '}
                  <span className={'text-dark text-normal'}>
                    <FormattedDate
                      value={lastUpdateDate}
                      year="numeric"
                      month="short"
                      day="2-digit"
                    />
                  </span>
                </div>
              )}
            </FlexShrink>
          </FlexContainer>
        }
      />
    );
    return (
      <Col lg={4} md={4} sm={4} xs={6}>
        {infoWidget}
      </Col>
    );
  };

  render() {
    const {
      universe,
      universe: { currentUniverse },
      alerts,
      updateAvailable,
      tasks,
      currentCustomer,
      runtimeConfigs
    } = this.props;
    const universeInfo = currentUniverse.data;
    const nodePrefixes = [universeInfo.universeDetails.nodePrefix];
    const isItKubernetesUniverse = isKubernetesUniverse(universeInfo);
    const universeDetails = universeInfo.universeDetails;
    const clusters = universeDetails?.clusters;
    const primaryCluster = getPrimaryCluster(clusters);
    const userIntent = primaryCluster && primaryCluster?.userIntent;
    const dedicatedNodes = userIntent?.dedicatedNodes;
    const failedTask = getcurrentUniverseFailedTask(universeInfo, tasks.customerTaskList);
    const ybSoftwareUpgradeState = universeDetails?.softwareUpgradeState;
    const universeStatus = getUniverseStatus(universeInfo);
    const isUpgradePreCheckFailed =
      universeStatus.state === UniverseState.GOOD &&
      failedTask?.type === SoftwareUpgradeTaskType.SOFTWARE_UPGRADE;

    const isRollBackFeatureEnabled =
      runtimeConfigs?.data?.configEntries?.find(
        (c) => c.key === 'yb.upgrade.enable_rollback_support'
      )?.value === 'true';

    const isQueryMonitoringEnabled = localStorage.getItem('__yb_query_monitoring__') === 'true';
    return (
      <Fragment>
        {isRollBackFeatureEnabled &&
          ybSoftwareUpgradeState === SoftwareUpgradeState.PRE_FINALIZE && (
            <Row className="p-16">{<PreFinalizeBanner universeData={universeInfo} />}</Row>
          )}
        {isRollBackFeatureEnabled &&
          [SoftwareUpgradeState.ROLLBACK_FAILED, SoftwareUpgradeState.UPGRADE_FAILED].includes(
            ybSoftwareUpgradeState
          ) &&
          [
            SoftwareUpgradeTaskType.ROLLBACK_UPGRADE,
            SoftwareUpgradeTaskType.SOFTWARE_UPGRADE
          ].includes(failedTask?.type) &&
          !isUpgradePreCheckFailed && (
            <Row className="p-16">
              <FailedBanner universeData={universeInfo} taskDetail={failedTask} />
            </Row>
          )}
        <Row>
          {isEnabled(currentCustomer.data.features, 'universes.details.overview.costs') &&
            this.getCostWidget(universeInfo)}
          <Col lg={4} md={6} sm={8} xs={12}>
            {getPromiseState(currentUniverse).isSuccess() && (
              <DBVersionWidget
                higherVersionCount={updateAvailable}
                isRollBackFeatureEnabled={isRollBackFeatureEnabled}
                failedTaskDetails={failedTask}
              />
            )}
          </Col>
        </Row>
        <Row>
          {this.getPrimaryClusterWidget(universeInfo, isRollBackFeatureEnabled)}
          {this.hasReadReplica(universeInfo) && this.getReadReplicaClusterWidget(universeInfo)}
          {this.getCPUWidget(universeInfo, isRollBackFeatureEnabled)}
          {isDisabled(currentCustomer.data.features, 'universes.details.health')
            ? this.getAlertWidget(alerts, universeInfo)
            : this.getHealthWidget(universe.healthCheck, universeInfo)}
        </Row>
        <Row>
          {this.getRegionMapWidget(universeInfo)}

          <Col lg={4} xs={12} md={6} sm={6}>
            <OverviewMetricsContainer
              universeUuid={universeInfo.universeUUID}
              type={'overview'}
              origin={'universe'}
              nodePrefixes={nodePrefixes}
              isKubernetesUniverse={isItKubernetesUniverse}
              universeDetails={universeDetails}
              dedicatedNodes={dedicatedNodes}
            />
          </Col>
          <Col lg={4} md={6} sm={6} xs={12}>
            {this.getDiskUsageWidget(universeInfo)}
            {this.getTablesWidget(universeInfo)}
          </Col>
        </Row>
        {isQueryMonitoringEnabled && (
          <Row>
            <Col lg={12} md={12} sm={12} xs={12}>
              <QueryDisplayPanel
                universeUUID={universeInfo.universeUUID}
                enabled={isQueryMonitoringEnabled}
              />
            </Col>
          </Row>
        )}
      </Fragment>
    );
  }
}
