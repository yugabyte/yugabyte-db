import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Field } from 'redux-form';
import { YBControlledSelect, YBControlledNumericInput, YBCheckBox } from 'components/common/forms/fields';
import { isNonEmptyArray, isValidObject, areUniverseConfigsEqual, isEmptyObject } from 'utils/ObjectUtils';
import {Row, Col} from 'react-bootstrap';
import _ from 'lodash';
import {isNonEmptyObject} from "../../../utils/ObjectUtils";

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
    const newTaskParams = _.clone(universeConfigTemplate.data.userIntent);
    this.props.submitConfigureUniverse({userIntent: newTaskParams});
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

    if (((currentProvider.code === "onprem" && totalNodesInConfig <= maxNumNodes)
          || currentProvider.code !== "onprem") && totalNodesInConfig >= minNumNodes) {
      const newPlacementInfo = _.clone(universeConfigTemplate.placementInfo, true);
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
      newTaskParams.placementInfo = newPlacementInfo;
      newTaskParams.userIntent.numNodes = totalNodesInConfig;
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
    if (isNonEmptyObject(universeConfigTemplate) && isNonEmptyObject(universeConfigTemplate.placementInfo) &&
        isNonEmptyArray(universeConfigTemplate.placementInfo.cloudList) &&
        isNonEmptyArray(universeConfigTemplate.placementInfo.cloudList[0].regionList)) {
      universeConfigTemplate.placementInfo.cloudList[0].regionList.forEach(function(regionItem) {
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
    if (type === "Edit" &&  isValidObject(currentUniverse)) {
      const azGroups = this.getGroupWithCounts(currentUniverse.data.universeDetails).groups;
      this.setState({azItemState: azGroups});
    }
  }

  componentWillReceiveProps(nextProps) {
    const {universe: {universeConfigTemplate}} = nextProps;
    const placementInfo = this.getGroupWithCounts(universeConfigTemplate.data);
    const azGroups = placementInfo.groups;
    if (!areUniverseConfigsEqual(this.props.universe.universeConfigTemplate.data, universeConfigTemplate.data)
        && isValidObject(universeConfigTemplate.data.placementInfo)) {
      this.setState({azItemState: azGroups});
    }
    if (isValidObject(universeConfigTemplate.data) && isValidObject(universeConfigTemplate.data.placementInfo) &&
    !_.isEqual(universeConfigTemplate, this.props.universe.universeConfigTemplate)) {
      const uniqueAZs = [ ...new Set(azGroups.map(item => item.value)) ];
      if (isValidObject(uniqueAZs)) {
        const placementStatusObject = {
          numUniqueRegions: placementInfo.uniqueRegions,
          numUniqueAzs: placementInfo.uniqueAzs,
          replicationFactor: universeConfigTemplate.data.userIntent.replicationFactor
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
    if (isNonEmptyObject(universeConfigTemplate.data) && isNonEmptyObject(universeConfigTemplate.data.userIntent) &&
        isNonEmptyArray(universeConfigTemplate.data.userIntent.regionList)) {
      azListForSelectedRegions = regions.data.filter(
        region => universeConfigTemplate.data.userIntent.regionList.includes(region.uuid)
      ).reduce((az, region) => az.concat(region.zones), []);
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
        <Row key={idx} >
          <Col sm={6}>
            <Field name={`select${idx}`} component={YBControlledSelect}
                 options={azListOptions} selectVal={azGroupItem.value}
                 onInputChanged={self.handleAZChange.bind(self, idx)}/>
          </Col>
          <Col sm={4}>
            <Field name={`nodes${idx}`} component={YBControlledNumericInput}
            val={azGroupItem.count}
            onInputChanged={self.handleAZNodeCountChange.bind(self, idx)}/>
          </Col>
          <Col sm={2} key={idx}>
            <Field name={"test"} component={YBCheckBox} checkState={azGroupItem.isAffinitized} 
                   onClick={self.handleAffinitizedZoneChange.bind(self, idx)}/>
          </Col>
        </Row>
    ));
      return (
        <div className={"az-table-container form-field-grid"}>
          <div className="az-selector-label">
            <span className="az-selector-reset" onClick={this.resetAZSelectionConfig}>Reset Config</span>
            <h4>Availability Zones</h4>
          </div>
          {azList}
        </div>
      );
    }
    return <span/>;
  }
}
