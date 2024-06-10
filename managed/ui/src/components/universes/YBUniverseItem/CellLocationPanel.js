// Copyright (c) YugaByte, Inc.

import { Row, Col } from 'react-bootstrap';
import pluralize from 'pluralize';

import {
  getUniverseNodeCount,
  getPlacementRegions,
  getClusterProviderUUIDs,
  getProviderMetadata
} from '../../../utils/UniverseUtils';
export const CellLocationPanel = (props) => {
  const {
    universe,
    universe: { universeDetails },
    providers,
    isKubernetesUniverse
  } = props;
  const numNodes = getUniverseNodeCount(universeDetails.nodeDetailsSet);
  const clusterProviderUUIDs = getClusterProviderUUIDs(universe.universeDetails.clusters);
  const clusterProviders = providers.data.filter((p) => clusterProviderUUIDs.includes(p.uuid));
  const universeProviders = clusterProviders.map((provider) => {
    return getProviderMetadata(provider).name;
  });

  const regionList = universeDetails.clusters.reduce((regions, cluster) => {
    getPlacementRegions(cluster).forEach((region) => {
      regions.add(region.code);
    });
    return regions;
  }, new Set());

  const regionListText = Array.from(regionList).join(', ');
  const providersText = universeProviders.join(', ');
  return (
    <div>
      <Row className="cell-position-detail">
        <Col sm={3} className="cell-num-nodes">
          {pluralize(isKubernetesUniverse ? 'Pod' : 'Node', numNodes, true)}
        </Col>
        <Col sm={9}>
          <span className="cell-provider-name">{providersText}</span>
        </Col>
      </Row>
      <Row className="cell-position-detail">
        <Col sm={3} className="cell-num-nodes">
          {pluralize('Region', regionList.size, true)}
        </Col>
        <Col sm={9}>{regionListText}</Col>
      </Row>
    </div>
  );
};
