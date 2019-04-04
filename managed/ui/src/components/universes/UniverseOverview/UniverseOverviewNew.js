// Copyright (c) YugaByte, Inc.

import React, { Component, PureComponent } from 'react';
import { Link } from 'react-router';

import { Row, Col } from 'react-bootstrap';
import PropTypes from 'prop-types';
import { FormattedDate } from 'react-intl';
import { ClusterInfoPanelContainer, YBWidget } from '../../panels';
import { OverviewMetricsContainer, StandaloneMetricsPanelContainer, DiskUsagePanel, CpuUsagePanel } from '../../metrics';
import { YBResourceCount, YBCost, DescriptionList, YBCopyButton } from 'components/common/descriptors';
import { RegionMap, YBMapLegend} from '../../maps';
import { isNonEmptyObject, isEmptyObject, isNonEmptyArray } from 'utils/ObjectUtils';
import { isKubernetesUniverse, getPrimaryCluster, nodeComparisonFunction } from '../../../utils/UniverseUtils';
import { getUniverseEndpoint } from 'actions/common';
import { FlexContainer, FlexGrow, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { isDefinedNotNull } from '../../../utils/ObjectUtils';
import { getPromiseState } from 'utils/PromiseUtils';
import { NodeConnectModal } from '../../universes';
import moment from 'moment';
import pluralize from 'pluralize';

class DatabasePanel extends PureComponent {
  static propTypes = {
    universeInfo: PropTypes.object.isRequired
  };

  renderEndpointUrl = (endpointUrl, endpointName) => {
    return (
      <a href={endpointUrl} target="_blank" rel="noopener noreferrer">{endpointName} &nbsp;
        <i className="fa fa-external-link" aria-hidden="true"></i>
      </a>
    );
  }

  render() {
    const { universeInfo, universeInfo: {universeDetails, universeDetails: {clusters}}} = this.props;
    const primaryCluster = getPrimaryCluster(clusters);
    const userIntent = primaryCluster && primaryCluster.userIntent;
    const universeId = universeInfo.universeUUID;
    
    const formattedCreationDate = (
      <FormattedDate value={universeInfo.creationDate} year='numeric' month='long' day='2-digit'
                     hour='2-digit' minute='2-digit' second='2-digit' timeZoneName='short' />
    );

    const nodeDetails = universeDetails.nodeDetailsSet.sort((a, b) => nodeComparisonFunction(a, b, universeDetails.clusters));
    const primaryNodeDetails = nodeDetails
      .filter((node) => node.placementUuid === primaryCluster.uuid);

    const primaryNodeIPs = primaryNodeDetails
      .filter((node) => isDefinedNotNull(node.cloudInfo.private_ip) && isDefinedNotNull(node.cloudInfo.public_ip))
      .map((node) => ({ privateIP: node.cloudInfo.private_ip, publicIP: node.cloudInfo.public_ip }));
    
    const universeIdData = <FlexContainer><FlexGrow style={{overflow: 'hidden', textOverflow: 'ellipsis', flexShrink: 1, minWidth: 0 }}>{universeId}</FlexGrow><FlexGrow power={100} style={{position: "relative", flexShrink: 0, minWidth: "24px", flexBasis: "24px" }}><YBCopyButton className={"btn-copy-round"} text={universeId}><span className={"fa fa-clone"}></span></YBCopyButton></FlexGrow></FlexContainer>;
    const ycqlServiceUrl = getUniverseEndpoint(universeId) + "/yqlservers";
    const yedisServiceUrl = getUniverseEndpoint(universeId) + "/redisservers";
    const universeInfoItems = [
      {name: "Service endpoints", data: <span>{this.renderEndpointUrl(ycqlServiceUrl,"YCQL")} &nbsp;/&nbsp; {this.renderEndpointUrl(yedisServiceUrl,"YEDIS")}</span>},
      {name: "Universe ID", data: universeIdData},
      {name: "Launch Time", data: formattedCreationDate},
    ];

    if (userIntent.providerType === "aws" && universeInfo.dnsName) {
      const dnsNameData = (
        <FlexContainer>
          <FlexGrow style={{overflow: 'hidden', textOverflow: 'ellipsis'}}>
            {universeInfo.dnsName}
          </FlexGrow>
          <FlexShrink>
            <YBCopyButton className={"btn-copy-round"} text={universeInfo.dnsName} ><span className={"fa fa-clone"}></span></YBCopyButton>
          </FlexShrink>
        </FlexContainer>);
      universeInfoItems.push({name: "Hosted Zone Name", data: dnsNameData });
    }
    
    return (
      <Row className={"overview-widget-database"}>
        <Col xs={6} className="centered" >
          <YBResourceCount size={userIntent.ybSoftwareVersion} inline={true}/>
          <NodeConnectModal nodeIPs={primaryNodeIPs} providerUUID={primaryCluster.userIntent.provider} />
        </Col>
        <Col xs={6}>
          <DescriptionList type={"stack"} listItems={universeInfoItems} />
        </Col>
      </Row>
    );
  }
}

class HealthHeart extends PureComponent {
  static propTypes = {
    status: PropTypes.string
  };

  render () {
    const { status } = this.props;
    return (
      <div id="health-heart">
        <span className={`fa fa-heart${status === "loading" ? " status-loading" : ""}`}></span>
        { status === "success" &&  
          <div id="health-heartbeat">
            <svg x="0px" y="0px" viewBox="0 0 41.8 22.2" xmlns="http://www.w3.org/2000/svg" strokeLinejoin="round" strokeLinecap="round" >
              <polyline strokeLinejoin="round" strokeLinecap="round" points="38.3,11.9 29.5,11.9 27.6,9 24,18.6 21.6,3.1 18.6,11.9 2.8,11.9 "/>
            </svg>
          </div>
        }

        { status === "error" &&  
          <div id="health-droplet">
            <svg x="0px" y="0px" width="264.564px" height="264.564px" viewBox="0 0 264.564 264.564" xmlns="http://www.w3.org/2000/svg" strokeLinejoin="round" strokeLinecap="round" >
              <path strokeLinejoin="round" strokeLinecap="round" d="M132.281,264.564c51.24,0,92.931-41.681,92.931-92.918c0-50.18-87.094-164.069-90.803-168.891L132.281,0l-2.128,2.773 c-3.704,4.813-90.802,118.71-90.802,168.882C39.352,222.883,81.042,264.564,132.281,264.564z"/>
            </svg>
          </div>
        }
      </div>
    );
  }
}

class HealthInfoPanel extends PureComponent {
  static propTypes = {
    healthCheck: PropTypes.object.isRequired
  };

  render() {
    const { healthCheck } = this.props;
    if (getPromiseState(healthCheck).isSuccess()) {
      const healthCheckData = JSON.parse([...healthCheck.data].reverse()[0]);
      const lastUpdateDate = moment(healthCheckData.timestamp);
      const totalNodesCounter = healthCheckData.data.length;
      let errorNodesCounter = 0;

      healthCheckData.data.forEach(check => {
        if (check.has_error) errorNodesCounter++;
      });

      const healthCheckInfoItems = [
        {name: "", data: errorNodesCounter 
          ? <span className="text-red text-light">{errorNodesCounter + " " + pluralize('Error', errorNodesCounter)}</span>
          : (totalNodesCounter
            ? <span className="text-green text-light"><i className={"fa fa-check"}></i> All running fine</span>
            : <span className="text-light">No finished check</span>
          )
        },
        {name: "", data: lastUpdateDate
          ? <span className="text-lightgray text-light"><i className={"fa fa-clock-o"}></i> Updated <span className={"text-dark text-normal"}><FormattedDate
          value={lastUpdateDate}
          year='numeric'
          month='short'
          day='2-digit' /></span></span>
          : null
        },
      ];
      
      return (<YBWidget
        size={1}
        className={"overview-widget-cluster-primary"}
        headerLeft={
          "Health check"
        }
        headerRight={errorNodesCounter ? <span className={"fa fa-exclamation-triangle text-red"}></span> : null}
        body={
          <FlexContainer className={"centered"} direction={"column"}>
            <FlexGrow>
              <HealthHeart status={errorNodesCounter ? "error" : "success"} />
            </FlexGrow>
            <FlexShrink >
              <DescriptionList type={"inline"} className={"health-check-legend"} listItems={healthCheckInfoItems} />
            </FlexShrink>
          </FlexContainer>
        }
      />);
    }
    
    const errorContent = {};
    if (getPromiseState(healthCheck).isEmpty()) {
      errorContent.heartStatus = "empty";
      errorContent.body = "No finished checks";
    }
    if (getPromiseState(healthCheck).isError()) {
      errorContent.heartStatus = "empty";
      errorContent.body = "Cannot get checks";
    }
    if (getPromiseState(healthCheck).isLoading()) {
      errorContent.heartStatus = "loading";
      errorContent.body = "";
    }
    return (<YBWidget
      size={1}
      className={"overview-widget-cluster-primary"}
      headerLeft={
        "Health check"
      }
      body={
        <FlexContainer className={"centered"} direction={"column"}>
          <FlexGrow>
            <HealthHeart status={errorContent.heartClassName} />
          </FlexGrow>
          <FlexShrink>
            {errorContent.body}
          </FlexShrink>
        </FlexContainer>
      }
    />);
  }
}

export default class UniverseOverviewNew extends Component {
  hasReadReplica = (universeInfo) => {
    const clusters = universeInfo.universeDetails.clusters;
    return clusters.some((cluster) => cluster.clusterType === "ASYNC");
  }

  getLastUpdateDate = () => {
    const universeTasks = this.tasksForUniverse();
    if (isNonEmptyArray(universeTasks)) {
      const updateTask = universeTasks.find((taskItem) => {
        return taskItem.type === "UpgradeSoftware";
      });
      return isDefinedNotNull(updateTask) ? (updateTask.completionTime || updateTask.createTime) : null;
    }
    return null;
  }

  tasksForUniverse = () => {
    const { universe: {currentUniverse: { data: {universeUUID}}}, tasks: {customerTaskList}} = this.props;
    const resultTasks = [];
    if (isNonEmptyArray(customerTaskList)) {
      customerTaskList.forEach((taskItem) => {
        if (taskItem.targetUUID === universeUUID) resultTasks.push(taskItem);
      });
    };
    return resultTasks;
  };

  getCostWidget = (currentUniverse) => {
    if (isEmptyObject(currentUniverse.resources)) return;
    const costPerDay = <YBCost value={currentUniverse.resources.pricePerHour} multiplier={"day"} />;
    const costPerMonth = <YBCost value={currentUniverse.resources.pricePerHour} multiplier={"month"} />;
    return (<Col lg={2} md={4} sm={4} xs={6}>
      <YBWidget
        size={1}
        className={"overview-widget-cost"}
        headerLeft={
          "Cost"
        }
        body={
          <FlexContainer className={"centered"} direction={"column"} >
            <FlexGrow>
              <YBResourceCount className="hidden-costs" size={costPerDay} kind="/day" inline={true}/>
            </FlexGrow>
            <FlexShrink>
              {costPerMonth} /month
            </FlexShrink>
          </FlexContainer>
        }
      />
    </Col>);
  }

  getPrimaryClusterWidget = (currentUniverse) => {
    if (isEmptyObject(currentUniverse)) return;
    return (<Col lg={2} sm={4} xs={6}>
      <ClusterInfoPanelContainer type={"primary"} universeInfo={currentUniverse} />
    </Col>);
  }

  getTablesWidget = (universeInfo) => {
    if (isEmptyObject(this.props.tables)) return;
    const { tables } =  this.props;

    let numCassandraTables = 0;
    let numRedisTables = 0;
    let numPostgresTables = 0;
    if (isNonEmptyArray(tables.universeTablesList)) {
      tables.universeTablesList.forEach((table, idx) => {
        if (table.tableType === "REDIS_TABLE_TYPE") {
          numRedisTables++;
        } else if (table.tableType === "YQL_TABLE_TYPE") {
          numCassandraTables++;
        } else {
          numPostgresTables++;
        }
      });
    }
    return (<Col lg={4} md={6} xs={12}>
      <YBWidget
        size={1}
        className={"overview-widget-tables"}
        headerLeft={
          "Tables"
        }
        headerRight={
          isNonEmptyObject(universeInfo) ? <Link to={`/universes/${universeInfo.universeUUID}?tab=tables`}>Details</Link> : null
        }
        body={
          <FlexContainer className={"centered"}>
            <FlexGrow>
              <YBResourceCount size={numCassandraTables} kind="YCQL" />
            </FlexGrow>
            <FlexGrow>
              <YBResourceCount size={numRedisTables} kind="YEDIS" />
            </FlexGrow>
            <FlexGrow>
              <YBResourceCount size={numPostgresTables} kind="YSQL" />
            </FlexGrow>
          </FlexContainer>
        }
      />
    </Col>);
  }

  getHealthWidget = (healthCheck) => {
    return (<Col lg={2} md={4} sm={4} xs={6}>
      <HealthInfoPanel healthCheck={healthCheck} />
    </Col>);
  }

  getDiskUsageWidget = () => {
    return (<Col lg={4} md={6} xs={12}>
      <StandaloneMetricsPanelContainer metricKey="disk_usage" type="overview">
        { props => {
          return (<YBWidget
            noMargin
            headerRight={"Details"
            }
            headerLeft={props.metric.layout.title}
            body={
              <DiskUsagePanel 
                metric={props.metric}
                className={"disk-usage-container"}
              />
            }
          />);
        }}
      </StandaloneMetricsPanelContainer>
    </Col>);
  }


  getCPUWidget = () => {
    return (<Col lg={2} md={4} sm={4} xs={6}>
      <StandaloneMetricsPanelContainer metricKey="cpu_usage" type="overview">
        { props => {
          return (<YBWidget
            noMargin
            headerLeft={"CPU Usage"}
            body={
              <CpuUsagePanel 
                metric={props.metric}
                className={"disk-usage-container"}
              />
            }
          />);
        }}
      </StandaloneMetricsPanelContainer>
    </Col>);
  }

  getRegionMapWidget = (universeInfo) => {

    const isItKubernetesUniverse = isKubernetesUniverse(universeInfo);
    
    const mapWidget = (
      <YBWidget
        numNode
        noMargin
        size={2}
        headerLeft={
          isItKubernetesUniverse ? "Universe Pods" : "Universe Nodes"
        }
        body={
          <div>
            <RegionMap universe={universeInfo} type={"Universe"} />
            <YBMapLegend title="Data Placement (In AZs)" clusters={universeInfo.universeDetails.clusters} type="Universe"/>
          </div>
        }
      />
    );

    if (this.hasReadReplica(universeInfo)) {
      return (
        <Col lg={12} xs={12}>
          {mapWidget}
        </Col>
      );
    } else {
      return (
        <Col lg={4} xs={12}>
          {mapWidget}
        </Col>
      );
    }
  }

  getDatabaseWidget = (universeInfo, tasks) => {
    const lastUpdateDate = this.getLastUpdateDate();
    const { updateAvailable } = this.props;
    const infoWidget = (<YBWidget
        headerLeft={
          "Database"
        }
        headerRight={
          updateAvailable ? (
            <a onClick={this.props.showSoftwareUpgradesModal}>Upgrade Software <span className="badge badge-pill badge-orange">{updateAvailable}</span></a>
           ) : (
            lastUpdateDate
            ? <div className="text-lightgray text-light"><span className={"fa fa-clock-o"}></span> Updated <span className={"text-dark text-normal"}><FormattedDate
            value={lastUpdateDate}
            year='numeric'
            month='short'
            day='2-digit' /></span></div>
            : null)
        }
        body={
          <DatabasePanel universeInfo={universeInfo} tasks={tasks}/>
        }
    />);
    return (
      <Col lg={4} md={8} sm={8} xs={12}>
        {infoWidget}
      </Col>
    );
  }

  render() {
    const {
      universe,
      universe: { currentUniverse },
      tasks,
      width
    } = this.props;

    const universeInfo = currentUniverse.data;
    const nodePrefixes = [universeInfo.universeDetails.nodePrefix];

    return (
      <Row>
        {this.getDatabaseWidget(universeInfo, tasks)}
        {this.getPrimaryClusterWidget(universeInfo)}
        {this.getCostWidget(universeInfo)}
        {this.getHealthWidget(universe.healthCheck)}
        {this.getCPUWidget()}
        {this.getRegionMapWidget(universeInfo)}
        {this.getDiskUsageWidget()}
        {this.getTablesWidget(universeInfo)}
        <OverviewMetricsContainer universeUuid={universeInfo.universeUUID} type={"overview"} origin={"universe"}
              width={width} nodePrefixes={nodePrefixes} />
      </Row>
    );
  }
}
