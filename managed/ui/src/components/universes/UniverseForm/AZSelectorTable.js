import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Field } from 'redux-form';
import { YBControlledSelect, YBControlledNumericInput, YBCheckBox } from '../../common/forms/fields';
import { Row, Col } from 'react-bootstrap';
import _ from 'lodash';
import { isNonEmptyArray, areUniverseConfigsEqual, isEmptyObject, isNonEmptyObject} from '../../../utils/ObjectUtils';
import { FlexContainer, FlexShrink, FlexGrow } from '../../common/flexbox/YBFlexBox';
import { getPrimaryCluster } from '../../../utils/UniverseUtils';

const nodeStates = {
  activeStates: ["ToBeAdded", "Provisioned", "SoftwareInstalled", "UpgradeSoftware", "UpdateGFlags", "Running"],
  inactiveStates: ["ToBeDecommissioned", "BeingDecommissioned", "Destroyed"]
};

export default class AZSelectorTable extends Component {
  constructor(props) {
    super(props);
    this.state = {azItemState: {}};
    this.getGroupWithCounts = this.getGroupWithCounts.bind(this);
    this.updatePlacementInfo = this.updatePlacementInfo.bind(this);
    this.resetAZSelectionConfig = this.resetAZSelectionConfig.bind(this);
  }
  static propTypes = {
    universe: PropTypes.object,
  };

  resetAZSelectionConfig() {
    const {universe: {universeConfigTemplate}} = this.props;
    const clusters = _.clone(universeConfigTemplate.data.clusters);
    if (isNonEmptyArray(clusters)) {
      clusters.forEach((cluster) => delete cluster["placementInfo"]);
    }
    this.props.submitConfigureUniverse({clusters: clusters});
  }

  handleAZChange(listKey, event) {
    const {universe: {universeConfigTemplate}} = this.props;
    const currentAZState = this.state.azItemState;
    const universeTemplate = _.clone(universeConfigTemplate.data);
    if (!currentAZState.some((azItem) => azItem.value === event.target.value)) {
      currentAZState[listKey].value = event.target.value;
      this.updatePlacementInfo(currentAZState, universeTemplate);
    }
  }

  handleAZNodeCountChange(listKey, value) {
    const {universe: {universeConfigTemplate}} = this.props;
    const universeTemplate = _.clone(universeConfigTemplate.data);
    const currentAZState = this.state.azItemState;
    currentAZState[listKey].count = value;
    this.updatePlacementInfo(currentAZState, universeTemplate);
  }

  handleAffinitizedZoneChange(idx) {
    const {universe: {universeConfigTemplate}} = this.props;
    const currentAZState = this.state.azItemState;
    const universeTemplate = _.clone(universeConfigTemplate.data);
    currentAZState[idx].isAffinitized = !currentAZState[idx].isAffinitized;
    this.updatePlacementInfo(currentAZState, universeTemplate);
  }

  updatePlacementInfo(currentAZState, universeConfigTemplate) {
    const {universe: {currentUniverse}, cloud, numNodesChangedViaAzList, currentProvider, maxNumNodes, minNumNodes} = this.props;
    this.setState({azItemState: currentAZState});
    let totalNodesInConfig = 0;
    currentAZState.forEach(function(item){
      totalNodesInConfig += item.count;
    });
    numNodesChangedViaAzList(totalNodesInConfig);

    const primaryCluster = getPrimaryCluster(universeConfigTemplate.clusters);
    if ((currentProvider.code !== "onprem" || totalNodesInConfig <= maxNumNodes) &&
        totalNodesInConfig >= minNumNodes && isNonEmptyObject(primaryCluster)) {
      const newPlacementInfo = _.clone(primaryCluster.placementInfo, true);
      const newRegionList = [];
      cloud.regions.data.forEach(function (regionItem) {
        const newAzList = [];
        let zoneFoundInRegion = false;
        regionItem.zones.forEach(function (zoneItem) {
          currentAZState.forEach(function (azItem) {
            if (zoneItem.uuid === azItem.value) {
              zoneFoundInRegion = true;
              newAzList.push({
                uuid: zoneItem.uuid,
                replicationFactor: 1,
                subnet: zoneItem.subnet,
                name: zoneItem.name,
                numNodesInAZ: azItem.count,
                isAffinitized: azItem.isAffinitized
              });
            }
          });
        });
        if (zoneFoundInRegion) {
          newRegionList.push({
            uuid: regionItem.uuid,
            code: regionItem.code,
            name: regionItem.name,
            azList: newAzList
          });
        }
      });
      newPlacementInfo.cloudList[0].regionList = newRegionList;
      const newTaskParams = _.clone(universeConfigTemplate, true);
      if (isNonEmptyArray(newTaskParams.clusters)) {
        newTaskParams.clusters.forEach((cluster) => {
          if (cluster.clusterType === 'PRIMARY') {
            cluster.placementInfo = newPlacementInfo;
            cluster.userIntent.numNodes = totalNodesInConfig;
          }
        });
      }
      if (isEmptyObject(currentUniverse.data)) {
        this.props.submitConfigureUniverse(newTaskParams);
      } else if (!areUniverseConfigsEqual(newTaskParams, currentUniverse.data.universeDetails)) {
        newTaskParams.universeUUID = currentUniverse.data.universeUUID;
        newTaskParams.expectedUniverseVersion = currentUniverse.data.version;
        this.props.submitConfigureUniverse(newTaskParams);
      } else {
        const placementStatusObject = {
          error: {
            type: "noFieldsChanged",
            numNodes: totalNodesInConfig,
            maxNumNodes: maxNumNodes
          }
        };
        this.props.setPlacementStatus(placementStatusObject);
      }
    } else if (totalNodesInConfig > maxNumNodes && currentProvider.code === "onprem") {
      const placementStatusObject = {
        error: {
          type: "notEnoughNodesConfigured",
          numNodes: totalNodesInConfig,
          maxNumNodes: maxNumNodes
        }
      };
      this.props.setPlacementStatus(placementStatusObject);
    } else {
      const placementStatusObject = {
        error: {
          type: "notEnoughNodes",
          numNodes: totalNodesInConfig,
          maxNumNodes: maxNumNodes
        }
      };
      this.props.setPlacementStatus(placementStatusObject);
    }
  }

  getGroupWithCounts(universeConfigTemplate) {
    const uniConfigArray = [];
    if (isNonEmptyObject(universeConfigTemplate) && isNonEmptyArray(universeConfigTemplate.nodeDetailsSet)) {
      universeConfigTemplate.nodeDetailsSet.forEach(function (nodeItem) {
        if (nodeStates.activeStates.indexOf(nodeItem.state) !== -1) {
          let nodeFound = false;
          for (let idx = 0; idx < uniConfigArray.length; idx++) {
            if (uniConfigArray[idx].value === nodeItem.azUuid) {
              nodeFound = true;
              uniConfigArray[idx].count++;
              break;
            }
          }
          if (!nodeFound) {
            uniConfigArray.push({value: nodeItem.azUuid, count: 1});
          }
        }
      });
    }
    const groupsArray = [];
    const uniqueRegions = [];
    const primaryCluster = isNonEmptyObject(universeConfigTemplate) ?
      getPrimaryCluster(universeConfigTemplate.clusters) :
      null;
    if (isNonEmptyObject(primaryCluster) &&
        isNonEmptyObject(primaryCluster.placementInfo) &&
        isNonEmptyArray(primaryCluster.placementInfo.cloudList) &&
        isNonEmptyArray(primaryCluster.placementInfo.cloudList[0].regionList)) {
      primaryCluster.placementInfo.cloudList[0].regionList.forEach(function(regionItem) {
        regionItem.azList.forEach(function(azItem) {
          uniConfigArray.forEach(function(configArrayItem) {
            if (configArrayItem.value === azItem.uuid) {
              groupsArray.push({value: azItem.uuid, count: configArrayItem.count,
                isAffinitized: azItem.isAffinitized === undefined ? true : azItem.isAffinitized});
              if (uniqueRegions.indexOf(regionItem.uuid) === -1) {
                uniqueRegions.push(regionItem.uuid);
              }
            }
          });
        });
      });
    }
    return ({groups: groupsArray,
      uniqueRegions: uniqueRegions.length,
      uniqueAzs: [...new Set(groupsArray.map(item => item.value))].length});
  };

  componentWillMount() {
    const {universe: {currentUniverse}, type} = this.props;
    if (type === "Edit" &&  isNonEmptyObject(currentUniverse)) {
      const azGroups = this.getGroupWithCounts(currentUniverse.data.universeDetails).groups;
      this.setState({azItemState: azGroups});
    }
  }

  componentWillReceiveProps(nextProps) {
    const {universe: {universeConfigTemplate}} = nextProps;
    const placementInfo = this.getGroupWithCounts(universeConfigTemplate.data);
    const azGroups = placementInfo.groups;
    if (!areUniverseConfigsEqual(this.props.universe.universeConfigTemplate.data, universeConfigTemplate.data)) {
      this.setState({azItemState: azGroups});
    }
    const primaryCluster = isNonEmptyObject(universeConfigTemplate.data) ?
      getPrimaryCluster(universeConfigTemplate.data.clusters) :
      null;
    if (isNonEmptyObject(primaryCluster) && isNonEmptyObject(primaryCluster.placementInfo) &&
        !_.isEqual(universeConfigTemplate, this.props.universe.universeConfigTemplate)) {
      const uniqueAZs = [ ...new Set(azGroups.map(item => item.value)) ];
      if (isNonEmptyObject(uniqueAZs)) {
        const placementStatusObject = {
          numUniqueRegions: placementInfo.uniqueRegions,
          numUniqueAzs: placementInfo.uniqueAzs,
          replicationFactor: primaryCluster.userIntent.replicationFactor
        };
        this.props.setPlacementStatus(placementStatusObject);
      }
    }
  }

  componentWillUnmount() {
    this.props.resetConfig();
  }

  render() {
    const {universe: {universeConfigTemplate}, cloud: {regions}} = this.props;
    const self = this;
    let azListForSelectedRegions = [];
    const primaryCluster = isNonEmptyObject(universeConfigTemplate.data) ?
      getPrimaryCluster(universeConfigTemplate.data.clusters) :
      null;
    if (isNonEmptyObject(primaryCluster) && isNonEmptyObject(primaryCluster.userIntent) &&
        isNonEmptyArray(primaryCluster.userIntent.regionList)) {
      azListForSelectedRegions = regions.data.filter((region) => {
        return primaryCluster.userIntent.regionList.includes(region.uuid);
      }).reduce((az, region) => {
        return az.concat(region.zones);
      }, []);
    }
    let azListOptions = <option/>;
    if (isNonEmptyArray(azListForSelectedRegions)) {
      azListOptions = azListForSelectedRegions.map((azItem, azIdx) => (
        <option key={azIdx} value={azItem.uuid}>{azItem.code}</option>
      ));
    }
    const azGroups = self.state.azItemState;
    let azList = [];
    if (isNonEmptyArray(azGroups) && isNonEmptyArray(azListForSelectedRegions)) {
      azList = azGroups.map((azGroupItem, idx) => (
        <FlexContainer key={idx}>
          <FlexGrow power={1}>
            <Row>
              <Col xs={8}>
                <Field name={`select${idx}`} component={YBControlledSelect}
                    options={azListOptions} selectVal={azGroupItem.value}
                    onInputChanged={self.handleAZChange.bind(self, idx)}/>
              </Col>
              <Col xs={4}>
                <Field name={`nodes${idx}`} component={YBControlledNumericInput}
                val={azGroupItem.count}
                onInputChanged={self.handleAZNodeCountChange.bind(self, idx)}/>
              </Col>
            </Row>
          </FlexGrow>
          <FlexShrink power={0} key={idx} className="form-right-control">
            <Field name={"test"} component={YBCheckBox} checkState={azGroupItem.isAffinitized}
                  onClick={self.handleAffinitizedZoneChange.bind(self, idx)}/>
          </FlexShrink>
        </FlexContainer>
    ));
      return (
        <div className={"az-table-container form-field-grid"}>
          <div className="az-selector-label">
            <span className="az-selector-reset" onClick={this.resetAZSelectionConfig}>Reset Config</span>
            <h4>Availability Zones</h4>
          </div>
          <FlexContainer>
            <FlexGrow power={1}>
              <Row>
                <Col xs={8}>
                  <label>Name</label>
                </Col>
                <Col xs={4}>
                  <label>Nodes</label>
                </Col>
              </Row>
            </FlexGrow>
            <FlexShrink power={0} className="form-right-control">
              <label>Preferred</label>
            </FlexShrink>
          </FlexContainer>
          {azList}
        </div>
      );
    }
    return <span/>;
  }
}
