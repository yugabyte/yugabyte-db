// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import PropTypes from 'prop-types';
import pluralize from 'pluralize';
import { YBWidget } from '../../panels';
import { FlexContainer, FlexGrow } from '../../common/flexbox/YBFlexBox';
import {
  getPrimaryCluster,
  isKubernetesUniverse,
  getUniverseNodeCount,
  getUniverseDedicatedNodeCount
} from '../../../utils/UniverseUtils';
import { RuntimeConfigKey } from '../../../redesign/helpers/constants';
import '../UniverseDisplayPanel/UniverseDisplayPanel.scss';

export default class ClusterInfoPanel extends Component {
  static propTypes = {
    type: PropTypes.oneOf(['primary', 'read-replica']).isRequired
  };

  render() {
    const {
      isDedicatedNodes,
      universeInfo,
      runtimeConfigs,
      universeInfo: {
        universeDetails,
        universeDetails: { clusters }
      }
    } = this.props;
    const cluster = getPrimaryCluster(clusters);
    const isItKubernetesUniverse = isKubernetesUniverse(universeInfo);

    const useK8CustomResourcesObject = runtimeConfigs?.data?.configEntries?.find(
      (c) => c.key === RuntimeConfigKey.USE_K8_CUSTOM_RESOURCES_FEATURE_FLAG
    );
    const useK8CustomResources = !!(useK8CustomResourcesObject?.value === 'true');

    const colocatedNodesCount = getUniverseNodeCount(universeDetails.nodeDetailsSet, cluster);
    const dedicatedNodesCount = isDedicatedNodes
      ? getUniverseDedicatedNodeCount(universeDetails.nodeDetailsSet, cluster)
      : null;

    const nodeCount = {
      numTserverNodes: isDedicatedNodes ? dedicatedNodesCount.numTserverNodes : colocatedNodesCount,
      numMasterNodes: isDedicatedNodes ? dedicatedNodesCount.numMasterNodes : 0
    };
    const userIntent = cluster?.userIntent;

    return (
      <YBWidget
        className={'overview-widget-cluster-primary'}
        headerLeft={'Primary Cluster'}
        body={
          <FlexContainer className={'cluster-metadata-container'} direction={'row'}>
            <FlexGrow className={'cluster-metadata-tserver'}>
              {isDedicatedNodes && (
                <Row className={'cluster-metadata-tserver__header'}>
                  <Col lg={10} md={6} sm={6} xs={6}>
                    <span>{'TServer'}</span>
                  </Col>
                </Row>
              )}
              <Row className={'cluster-metadata'}>
                <Col lg={6} md={6} sm={6} xs={6}>
                  <span className={'cluster-metadata__label'}>
                    {pluralize(isItKubernetesUniverse ? 'Pod' : 'Node', nodeCount.numTserverNodes)}
                  </span>
                </Col>
                <Col lg={6} md={6} sm={6} xs={6}>
                  <span className={'cluster-metadata__count cluster-metadata__align'}>
                    {nodeCount.numTserverNodes}
                  </span>
                </Col>
              </Row>
              {useK8CustomResources &&
              isItKubernetesUniverse &&
              userIntent?.tserverK8SNodeResourceSpec?.cpuCoreCount ? (
                <Row className={'cluster-metadata'}>
                  <Col lg={8} md={6} sm={6} xs={6}>
                    <span className={'cluster-metadata__label'}>{'Number of Cores:'}</span>
                  </Col>
                  <Col lg={4} md={6} sm={6} xs={6}>
                    <span className={'cluster-metadata__align'}>
                      {userIntent?.tserverK8SNodeResourceSpec?.cpuCoreCount}
                    </span>
                  </Col>
                </Row>
              ) : (
                <Row className={'cluster-metadata'}>
                  <Col lg={6} md={6} sm={6} xs={6}>
                    <span className={'cluster-metadata__label'}>{'Instance Type:'}</span>
                  </Col>
                  <Col lg={6} md={6} sm={6} xs={6}>
                    <span className={'cluster-metadata__align'}>
                      {userIntent && userIntent.instanceType}
                    </span>
                  </Col>
                </Row>
              )}
              <Row className={'cluster-metadata'}>
                <Col lg={8} md={6} sm={6} xs={6}>
                  <span className={'cluster-metadata__label'}>{'Replication Factor:'}</span>
                </Col>
                <Col lg={4} md={6} sm={6} xs={6}>
                  <span className={'cluster-metadata__align'}>
                    &nbsp;{userIntent.replicationFactor}
                  </span>
                </Col>
              </Row>
            </FlexGrow>
            {isDedicatedNodes && (
              <>
                <span className={'cluster-metadata-container__divider'}></span>
                <FlexGrow className={'cluster-metadata-master'}>
                  <Row className={'cluster-metadata-master__header'}>
                    <Col lg={10} md={6} sm={6} xs={6}>
                      <span>{'Master'}</span>
                    </Col>
                  </Row>
                  <Row className={'cluster-metadata'}>
                    <Col lg={6} md={6} sm={6} xs={6}>
                      <span className={'cluster-metadata__label'}>
                        {pluralize(
                          isItKubernetesUniverse ? 'Pod' : 'Node',
                          nodeCount.numMasterNodes
                        )}
                      </span>
                    </Col>
                    <Col lg={6} md={6} sm={6} xs={6}>
                      <span className={'cluster-metadata__count cluster-metadata__align'}>
                        {nodeCount.numMasterNodes}
                      </span>
                    </Col>
                  </Row>
                  {useK8CustomResources &&
                  isItKubernetesUniverse &&
                  userIntent?.masterK8SNodeResourceSpec?.cpuCoreCount ? (
                    <Row className={'cluster-metadata'}>
                      <Col lg={8} md={6} sm={6} xs={6}>
                        <span className={'cluster-metadata__label'}>{'Number of Cores:'}</span>
                      </Col>
                      <Col lg={4} md={6} sm={6} xs={6}>
                        <span className={'cluster-metadata__align'}>
                          {userIntent?.masterK8SNodeResourceSpec?.cpuCoreCount}
                        </span>
                      </Col>
                    </Row>
                  ) : (
                    <Row className={'cluster-metadata'}>
                      <Col lg={6} md={6} sm={6} xs={6}>
                        <span className={'cluster-metadata__label'}>{'Instance Type:'}</span>
                      </Col>
                      <Col lg={6} md={6} sm={6} xs={6}>
                        <span className={'cluster-metadata__align'}>
                          {userIntent && userIntent.masterInstanceType}
                        </span>
                      </Col>
                    </Row>
                  )}
                </FlexGrow>
              </>
            )}
          </FlexContainer>
        }
      />
    );
  }
}
