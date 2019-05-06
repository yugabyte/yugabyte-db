// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { OverlayTrigger } from 'react-bootstrap';
import { DescriptionList, YBResourceCount, YBPopover } from '../../common/descriptors';
import { getPrimaryCluster, getReadOnlyCluster, nodeComparisonFunction, isKubernetesUniverse } from '../../../utils/UniverseUtils';
import { isNonEmptyArray, isNonEmptyObject } from '../../../utils/ObjectUtils';
import { FlexContainer, FlexGrow, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { YBWidget } from '../../panels';
const pluralize = require('pluralize');

export default class ClusterInfoPanel extends Component {
  static propTypes = {
    type: PropTypes.oneOf(['primary', 'read-replica']).isRequired
  }

  render() {
    const { type, universeInfo, universeInfo: {universeDetails, universeDetails: {clusters}}, providers } = this.props;
    let cluster = null;
    if (type === 'primary') {
      cluster = getPrimaryCluster(clusters);
    } else if (type === 'read-replica') {
      cluster = getReadOnlyCluster(clusters);
    }
    const userIntent = cluster && cluster.userIntent;
    let provider = null;
    if (isNonEmptyObject(userIntent) && isNonEmptyArray(providers.data)) {
      if (userIntent.provider) {
        provider = providers.data.find(item => item.uuid === userIntent.provider);
      } else {
        provider = providers.data.find(item => item.code === userIntent.providerType);
      }
    }
    const regionList = cluster.regions && cluster.regions.map((region) => region.name).join(", ");
    const connectStringPanelItems = [
      {name: "Provider", data: provider && provider.name},
      {name: "Regions", data: regionList},
      {name: "Instance Type", data: userIntent && userIntent.instanceType},
      {name: "Replication Factor", data: userIntent.replicationFactor}
    ];
    const connectStringPanelItemsShrink = [
      {name: "Instance Type", data: userIntent && userIntent.instanceType},
      {name: "Replication Factor", data: userIntent.replicationFactor}
    ];

    const nodeDetails = universeDetails.nodeDetailsSet.sort((a, b) => nodeComparisonFunction(a, b, clusters));
    const primaryNodes = nodeDetails
      .filter((node) => node.placementUuid === cluster.uuid);

    const isItKubernetesUniverse = isKubernetesUniverse(universeInfo);

    return (
      <YBWidget
        size={1}
        className={"overview-widget-cluster-primary"}
        headerLeft={
          "Primary Cluster"
        }
        headerRight={
          <OverlayTrigger
            trigger="click"
            placement="right"
            overlay={
              <YBPopover>
                <DescriptionList listItems={connectStringPanelItems} />
              </YBPopover>
            }
          >
            <span className="fa fa-info"></span>
          </OverlayTrigger>    
        }
        body={
          <FlexContainer className={"centered"} direction={"column"}>
            <FlexGrow>
              <YBResourceCount className="hidden-costs" size={primaryNodes.length} kind={pluralize(isItKubernetesUniverse ? 'Pod' : 'Node', primaryNodes.length)} />
            </FlexGrow>
            <FlexShrink>
              <DescriptionList type={"inline"} listItems={connectStringPanelItemsShrink} />
            </FlexShrink>
          </FlexContainer>
        }
      />
    );
  }
}
