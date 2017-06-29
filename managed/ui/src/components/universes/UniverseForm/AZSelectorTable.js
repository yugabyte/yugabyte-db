import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Field } from 'redux-form';
import { YBControlledSelect, YBControlledNumericInput } from 'components/common/forms/fields';
import { isNonEmptyArray, isValidObject, areUniverseConfigsEqual, isEmptyObject } from 'utils/ObjectUtils';
import {Row, Col} from 'react-bootstrap';
import _ from 'lodash';

const nodeStates = {
  activeStates: ["ToBeAdded", "Provisioned", "SoftwareInstalled", "UpgradeSoftware", "UpdateGFlags", "Running"],
  inactiveStates: ["ToBeDecommissioned", "BeingDecommissioned", "Destroyed"]
}

export default class AZSelectorTable extends Component {
  constructor(props) {
    super(props);
    this.state = {azItemState: {}};
    this.getGroupWithCounts = this.getGroupWithCounts.bind(this);
    this.updatePlacementInfo = this.updatePlacementInfo.bind(this);
  }
  static propTypes = {
    universe: PropTypes.object,
  }

  handleAZChange(listKey, event) {
    const {universe: {universeConfigTemplate}} = this.props;
    var currentAZState = this.state.azItemState;
    var universeTemplate = _.clone(universeConfigTemplate.data);
    currentAZState[listKey].value = event.target.value;
    this.updatePlacementInfo(currentAZState, universeTemplate);
  }

  handleAZNodeCountChange(listKey, value) {
    const {universe: {universeConfigTemplate}} = this.props;
    var universeTemplate = _.clone(universeConfigTemplate.data);
    var currentAZState = this.state.azItemState;
    currentAZState[listKey].count = value;
    this.updatePlacementInfo(currentAZState, universeTemplate);
  }

  updatePlacementInfo(currentAZState, universeConfigTemplate) {
    const {universe: {currentUniverse}, cloud, numNodesChangedViaAzList, currentProvider, maxNumNodes, minNumNodes} = this.props;
    this.setState({azItemState: currentAZState});
    var totalNodesInConfig = 0;
    currentAZState.forEach(function(item){
      totalNodesInConfig += item.count;
    });
    numNodesChangedViaAzList(totalNodesInConfig);

    if (((currentProvider.code === "onprem" && totalNodesInConfig <= maxNumNodes)
          || currentProvider.code !== "onprem") && totalNodesInConfig >= minNumNodes) {
      var newPlacementInfo = _.clone(universeConfigTemplate.placementInfo, true);
      var newRegionList = [];
      cloud.regions.data.forEach(function (regionItem) {
        var newAzList = [];
        var zoneFoundInRegion = false;
        regionItem.zones.forEach(function (zoneItem) {
          currentAZState.forEach(function (azItem) {
            if (zoneItem.uuid === azItem.value) {
              zoneFoundInRegion = true;
              newAzList.push({
                uuid: zoneItem.uuid,
                replicationFactor: 1,
                subnet: zoneItem.subnet,
                name: zoneItem.name,
                numNodesInAZ: azItem.count
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
      var newTaskParams = _.clone(universeConfigTemplate, true);
      newTaskParams.placementInfo = newPlacementInfo;
      newTaskParams.userIntent.numNodes = totalNodesInConfig;
      if (isEmptyObject(currentUniverse.data)) {
        this.props.submitConfigureUniverse(newTaskParams);
      } else if (!areUniverseConfigsEqual(newTaskParams, currentUniverse.data.universeDetails)) {
          newTaskParams.universeUUID = currentUniverse.data.universeUUID;
          newTaskParams.expectedUniverseVersion = currentUniverse.data.version;
          this.props.submitConfigureUniverse(newTaskParams);
      } else {
        let placementStatusObject = {
            error: {
              type: "noFieldsChanged",
              numNodes: totalNodesInConfig,
              maxNumNodes: maxNumNodes
            }
          }
          this.props.setPlacementStatus(placementStatusObject);
      }
    } else if (totalNodesInConfig > maxNumNodes && currentProvider.code === "onprem") {
        let placementStatusObject = {
          error: {
            type: "notEnoughNodesConfigured",
            numNodes: totalNodesInConfig,
            maxNumNodes: maxNumNodes
          }
        }
        this.props.setPlacementStatus(placementStatusObject);
    } else {
        let placementStatusObject = {
          error: {
            type: "notEnoughNodes",
            numNodes: totalNodesInConfig,
            maxNumNodes: maxNumNodes
          }
        }
        this.props.setPlacementStatus(placementStatusObject);
    }
  }

  getGroupWithCounts(universeConfigTemplate) {
    var uniConfigArray = [];
    if (isNonEmptyArray(universeConfigTemplate.nodeDetailsSet)) {
      universeConfigTemplate.nodeDetailsSet.forEach(function (nodeItem) {
        if (nodeStates.activeStates.indexOf(nodeItem.state) !== -1) {
          var nodeFound = false;
          for (var idx = 0; idx < uniConfigArray.length; idx++) {
            if (uniConfigArray[idx].value === nodeItem.azUuid) {
              nodeFound = true;
              uniConfigArray[idx].count++;
              break;
            }
          }
          if (!nodeFound) {
            uniConfigArray.push({value: nodeItem.azUuid, count: 1})
          }
        }
      });
    }
    var groupsArray = [];
    var uniqueRegions = [];
    if (isValidObject(universeConfigTemplate.placementInfo)) {
      universeConfigTemplate.placementInfo.cloudList[0].regionList.forEach(function(regionItem) {
        regionItem.azList.forEach(function(azItem) {
          uniConfigArray.forEach(function(configArrayItem) {
            if (configArrayItem.value === azItem.uuid) {
              groupsArray.push({value: azItem.uuid, count: configArrayItem.count})
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
             uniqueAzs: [...new Set(groupsArray.map(item => item.value))].length})
  };

  componentWillMount() {
    const {universe: {currentUniverse}, type} = this.props;
    if (type === "Edit" &&  isValidObject(currentUniverse)) {
      var azGroups = this.getGroupWithCounts(currentUniverse.data.universeDetails).groups;
      this.setState({azItemState: azGroups});
    }
  }
  componentWillReceiveProps(nextProps) {
    const {universe: {universeConfigTemplate}} = nextProps;
    var placementInfo = this.getGroupWithCounts(universeConfigTemplate.data);
    var azGroups = placementInfo.groups;
    if (!areUniverseConfigsEqual(this.props.universe.universeConfigTemplate.data, universeConfigTemplate.data)
        && isValidObject(universeConfigTemplate.data.placementInfo)) {
       this.setState({azItemState: azGroups});
    }
    if (isValidObject(universeConfigTemplate.data) && isValidObject(universeConfigTemplate.data.placementInfo) &&
    !_.isEqual(universeConfigTemplate, this.props.universe.universeConfigTemplate)) {
      const uniqueAZs = [ ...new Set(azGroups.map(item => item.value)) ]
      if (isValidObject(uniqueAZs)) {
        var placementStatusObject = {
          numUniqueRegions: placementInfo.uniqueRegions,
          numUniqueAzs: placementInfo.uniqueAzs,
          replicationFactor: universeConfigTemplate.data.userIntent.replicationFactor
        }
        this.props.setPlacementStatus(placementStatusObject);
      }
    }
  }

  componentWillUnmount() {
    this.props.resetConfig();
  }

  render() {
    const {universe: {universeConfigTemplate}, cloud: {regions}} = this.props;
    var self = this;
    var azListForSelectedRegions = [];
    if (isValidObject(universeConfigTemplate.data.userIntent) && 
      isNonEmptyArray(universeConfigTemplate.data.userIntent.regionList)
    ) {
       azListForSelectedRegions = regions.data.filter(
        region => universeConfigTemplate.data.userIntent.regionList.includes(region.uuid)
      ).reduce((az, region) => az.concat(region.zones), []);
    }
    var azListOptions = <option/>;
    if (isNonEmptyArray(azListForSelectedRegions)) {
      azListOptions = azListForSelectedRegions.map((azItem, azIdx) => (
        <option key={azIdx} value={azItem.uuid}>{azItem.code}</option>
      ));
    }
    var azGroups = self.state.azItemState;
    var azList = [];
    if (isNonEmptyArray(azGroups) && isNonEmptyArray(azListForSelectedRegions)) {
      azList = azGroups.map((azGroupItem, idx) => (
        <Row key={idx} >
        <Col sm={6}>
          <Field name={`select${idx}`} component={YBControlledSelect}
                 options={azListOptions} selectVal={azGroupItem.value}
                 onInputChanged={self.handleAZChange.bind(self, idx)}/>
        </Col>
        <Col sm={6}>
          <Field name={`nodes${idx}`} component={YBControlledNumericInput}
            val={azGroupItem.count}
            onInputChanged={self.handleAZNodeCountChange.bind(self, idx)}/>
        </Col>
      </Row>
    ));
    return (
      <div className={"az-table-container form-field-grid"}>
        <h4>Availability Zones</h4>
        {azList}
        </div>
      );
    }
    return <span/>;
  }
}
