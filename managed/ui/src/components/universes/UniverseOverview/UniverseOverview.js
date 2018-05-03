// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { Row, Col } from 'react-bootstrap';
import { UniverseInfoPanelContainer, ResourceStringPanelContainer } from '../../panels';
import { OverviewMetricsContainer } from '../../metrics';
import { UniverseResources } from '../UniverseResources';
import { YBPanelItem, YBWidget } from '../../panels';
import { RegionMap, YBMapLegend} from '../../maps';
import { isNonEmptyObject } from 'utils/ObjectUtils';

import { FlexContainer, FlexGrow, FlexShrink } from '../../common/flexbox/YBFlexBox';

export default class UniverseOverview extends Component {
  hasReadReplica = (universeInfo) => {
    const clusters = universeInfo.universeDetails.clusters;
    return clusters.some((cluster) => cluster.clusterType === "ASYNC");
  }

  getRegionMapWidget = (universeInfo) => {
    const universeResources = universeInfo.resources;
    let numNodes = 0;
    if (isNonEmptyObject(universeResources)) {
      numNodes = universeResources.numNodes;
    }
    const mapWidget = (
      <YBWidget
        noMargin
        size={2}
        headerLeft={
          "universe nodes"
        }
        headerRight={
          numNodes +" nodes"
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
        <Col lg={8} xs={12}>
          {mapWidget}
        </Col>
      );
    }
  }

  getInfoWidgets = (universeInfo) => {
    const hasReadReplica = this.hasReadReplica(universeInfo);
    const infoWidgets = [
      <YBWidget
        headerLeft={
          "resource info"
        }
        body={
          <UniverseInfoPanelContainer universeInfo={universeInfo} />
        }
      />,
      <YBWidget
        headerLeft={
          "primary cluster"
        }
        body={
          <ResourceStringPanelContainer universeInfo={universeInfo} type='primary'/>
        }
      />
    ];
    if (hasReadReplica) {
      infoWidgets.push(
        <YBWidget
          headerLeft={
            "read replica cluster"
          }
          body={
            <ResourceStringPanelContainer universeInfo={universeInfo} type='read-replica' />
          }
        />
      );
    }
    if (hasReadReplica) {
      return (
        <div key={"universe_info_widgets"}>
          {
            infoWidgets.map((widget, idx) => {
              return (<Col key={`info_widget_${idx}`} lg={4} md={6} sm={6} xs={12}>{widget}</Col>);
            })
          }
        </div>
      );
    } else {
      return (
        <Col lg={4} md={12} sm={12} xs={12} key={"universe_info_widgets"}>
          <Row>
            {
              infoWidgets.map((widget, idx) => {
                return (<Col lg={12} md={6} sm={6} xs={12} key={`info_widget_${idx}`}>{widget}</Col>);
              })
            }
          </Row>
        </Col>
      );
    }
  }

  render() {
    const {
      currentUniverse,
      width
    } = this.props;

    const universeInfo = currentUniverse.data;
    const universeResources = universeInfo.resources;
    const nodePrefixes = [universeInfo.universeDetails.nodePrefix];

    return (
      <YBPanelItem noBackground
        header={
          <FlexContainer>
            <FlexGrow>
              <UniverseResources split='left'
                  resources={universeResources} renderType={"Display"}/>
            </FlexGrow>
            <FlexShrink>
              <div className="operating-costs">
                <UniverseResources split='right' resources={universeResources} renderType={"Display"}/>
              </div>
            </FlexShrink>
          </FlexContainer>
        }
        body={
          <div>
            <Row>
              {this.getInfoWidgets(universeInfo)}
              {this.getRegionMapWidget(universeInfo)}
              <OverviewMetricsContainer type={"overview"} origin={"universe"}
                    width={width} nodePrefixes={nodePrefixes} />
            </Row>
          </div>
        }
      />
    );
  }
}
