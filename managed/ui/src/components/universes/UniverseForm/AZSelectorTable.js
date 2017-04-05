import React, { Component, PropTypes } from 'react';
import { Field } from 'redux-form';
import { YBControlledSelect, YBControlledNumericInput } from 'components/common/forms/fields';
import { isValidArray, isValidObject, areUniverseConfigsEqual } from 'utils/ObjectUtils';
import {Row, Col} from 'react-bootstrap';

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
    var currentAZState = this.state.azItemState;
    currentAZState[listKey].value = event.target.value;
    this.updatePlacementInfo(currentAZState);
  }

  handleAZNodeCountChange(listKey, value) {
    var currentAZState = this.state.azItemState;
    currentAZState[listKey].count = value;
    this.setState({azItemState: currentAZState});
    this.updatePlacementInfo(currentAZState);
  }

  updatePlacementInfo(currentAZState) {
    const {universe: {universeConfigTemplate, currentUniverse}, cloud, numNodesChanged} = this.props;
    this.setState({azItemState: currentAZState});
    var totalNodesInConfig = 0;
    currentAZState.forEach(function(item){
      totalNodesInConfig += item.count;
    });
    if (totalNodesInConfig !== universeConfigTemplate.userIntent.numNodes) {
      numNodesChanged(totalNodesInConfig);
    }
    var newPlacementInfo = universeConfigTemplate.placementInfo;
    newPlacementInfo.isCustom = true;
    var newRegionList = [];
    cloud.regions.forEach(function(regionItem){
      var newAzList = [];
      var zoneFoundInRegion = false;
      regionItem.zones.forEach(function(zoneItem){
        currentAZState.forEach(function(azItem){
          if (zoneItem.uuid === azItem.value) {
            zoneFoundInRegion = true;
            newAzList.push({
              uuid: zoneItem.uuid, replicationFactor: 1,
              subnet: zoneItem.subnet, name: zoneItem.name
              });
            }
          });
        });
        if (zoneFoundInRegion) {
          newRegionList.push({uuid: regionItem.uuid, code: regionItem.code,
                            name: regionItem.name, azList: newAzList});
        }
      });
      newPlacementInfo.cloudList[0].regionList = newRegionList;
      var newTaskParams = universeConfigTemplate;
      newTaskParams.placementInfo = newPlacementInfo;
      if (!currentUniverse) {
        this.props.submitConfigureUniverse(newTaskParams);
      } else {
        if (!areUniverseConfigsEqual(newTaskParams, currentUniverse.universeDetails)) {
          this.props.submitConfigureUniverse(newTaskParams);
        }
      }
  }

  getGroupWithCounts(universeConfigTemplate) {
    var uniConfigArray = [];
    if (isValidArray(universeConfigTemplate.nodeDetailsSet)) {
      universeConfigTemplate.nodeDetailsSet.forEach(function(nodeItem){
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
      universeConfigTemplate.placementInfo.cloudList[0].regionList.forEach(function(regionItem){
        regionItem.azList.forEach(function(azItem){
           uniConfigArray.forEach(function(configArrayItem){
             if(configArrayItem.value === azItem.uuid) {
               groupsArray.push({value: azItem.uuid, count: configArrayItem.count})
               if (uniqueRegions.indexOf(regionItem.uuid) === -1) {
                 uniqueRegions.push(regionItem.uuid);
               }
             }
           });
        });
      });
    }
    return ({groups: groupsArray, uniqueRegions: uniqueRegions.length, uniqueAzs: [...new Set(groupsArray.map(item => item.value))].length})
  };

  componentWillMount() {
    const {universe: {universeConfigTemplate, currentUniverse}, type} = this.props;
    if (type === "Edit" &&  isValidObject(currentUniverse)) {
      var azGroups = this.getGroupWithCounts(universeConfigTemplate).groups;
      this.setState({azItemState: azGroups});
    }
  }
  componentWillReceiveProps(nextProps) {
    const {universe: {universeConfigTemplate, currentUniverse}} = nextProps;
    var placementInfo = this.getGroupWithCounts(universeConfigTemplate);
    var azGroups = placementInfo.groups;
    if (!areUniverseConfigsEqual(this.props.universe.universeConfigTemplate, universeConfigTemplate)
      && isValidObject(universeConfigTemplate.placementInfo) && !universeConfigTemplate.placementInfo.isCustom) {
       this.setState({azItemState: azGroups});
    } else if (currentUniverse && universeConfigTemplate.userIntent && areUniverseConfigsEqual(universeConfigTemplate, currentUniverse.universeDetails)) {
      this.setState({azItemState: this.getGroupWithCounts(currentUniverse.universeDetails).groups})
    }
    if (isValidObject(universeConfigTemplate) && isValidObject(universeConfigTemplate.placementInfo)) {
        const uniqueAZs = [ ...new Set(azGroups.map(item => item.value)) ]
        if (isValidObject(uniqueAZs)) {
          var placementStatusObject = {
            numUniqueRegions: placementInfo.uniqueRegions,
            numUniqueAzs: placementInfo.uniqueAzs,
            replicationFactor: universeConfigTemplate.userIntent.replicationFactor
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
    if (isValidObject(universeConfigTemplate.userIntent) && isValidArray(universeConfigTemplate.userIntent.regionList)) {
       azListForSelectedRegions = regions.filter(
        region => universeConfigTemplate.userIntent.regionList.includes(region.uuid)
      ).reduce((az, region) => az.concat(region.zones), []);
    }
    var azListOptions = <option/>;
    if (isValidArray(azListForSelectedRegions)) {
      azListOptions = azListForSelectedRegions.map(function(azItem, azIdx){
        return <option key={azIdx} value={azItem.uuid}>{azItem.code}</option>
      });
    }
    var azGroups = self.state.azItemState;
    var azList = [];
    if (isValidArray(azGroups) && azGroups.length && isValidArray(azListForSelectedRegions)) {
      azList = azGroups.map(function(azGroupItem, idx){
        return (
          <Row key={idx} >
            <Col lg={6}>
              <Field name={`select${idx}`} component={YBControlledSelect}
                     options={azListOptions} selectVal={azGroupItem.value}
                     onInputChanged={self.handleAZChange.bind(self, idx)}/>
            </Col>
            <Col lg={6}>
              <Field name={`nodes${idx}`} component={YBControlledNumericInput}
                     val={azGroupItem.count}
                     onInputChanged={self.handleAZNodeCountChange.bind(self, idx)}/>
            </Col>
          </Row>
        )
      });
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
