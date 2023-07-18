import { cloneDeep, isEqual, map, sortBy } from 'lodash';
import { Component } from 'react';
import { Col, Row } from 'react-bootstrap';
import { browserHistory, Link, withRouter } from 'react-router';
import { Field, reduxForm } from 'redux-form';
import {
  isDefinedNotNull,
  isNonEmptyArray,
  isNonEmptyObject,
  pickArray
} from '../../../utils/ObjectUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { DescriptionList } from '../../common/descriptors';
import { YBButton, YBControlledSelectWithLabel } from '../../common/forms/fields';
import { YBLoading } from '../../common/indicators';
import {
  ProviderCode,
  PROVIDER_ROUTE_PREFIX
} from '../../configRedesign/providerRedesign/constants';
import { RegionMap, YBMapLegend } from '../../maps';
import { YBConfirmModal } from '../../modals';
import OnPremNodesListContainer from './OnPremNodesListContainer';

const PROVIDER_TYPE = 'onprem';

class NewOnPremSuccess extends Component {
  constructor(props) {
    super(props);
    this.state = {
      currentProvider: undefined,
      isLoading: true
    };
  }

  showProviderView = () => {
    this.setState({ manageInstances: false });
  };
  deleteProvider = async () => {
    const { currentProvider } = this.state;
    await this.props.deleteProviderConfig(currentProvider.uuid);
    window.location.reload();
  };
  handleManageNodesClick = () => {
    this.setState({ manageInstances: true });
  };

  async fetchInstanceAndNodeList(currentProviderFromProps) {
    const { currentProvider } = this.state;
    if (!currentProvider && !currentProviderFromProps) return;
    await this.props.fetchConfiguredNodeList(
      currentProviderFromProps ? currentProviderFromProps.uuid : currentProvider.uuid
    );
    await this.props.fetchInstanceTypeList(
      currentProviderFromProps ? currentProviderFromProps.uuid : currentProvider.uuid
    );
    this.updateJSONStore();
  }

  updateJSONStore() {
    const { currentProvider } = this.state;

    const {
      accessKeys,
      cloud: { nodeInstanceList, instanceTypes },
      configuredRegions,
      selectedProviderUUID
    } = this.props;

    if (!currentProvider) return;

    const onPremRegions = configuredRegions.data.filter(
      (configuredRegion) => configuredRegion.provider.uuid === selectedProviderUUID
    );
    let onPremAccessKey = {};
    if (isNonEmptyArray(accessKeys.data)) {
      onPremAccessKey = accessKeys.data.find(
        (accessKey) => accessKey.idKey.providerUUID === currentProvider.uuid
      );
    }
    let keyJson = {};
    if (isNonEmptyObject(onPremAccessKey) && onPremAccessKey.idKey && onPremAccessKey.keyInfo) {
      const { idKey, keyInfo } = onPremAccessKey;
      keyJson = {
        code: idKey.keyCode,
        privateKeyContent: keyInfo.privateKey,
        sshUser: keyInfo.sshUser,
        sshPort: keyInfo.sshPort,
        airGapInstall: keyInfo.airGapInstall,
        installNodeExporter: keyInfo.installNodeExporter,
        nodeExporterUser: keyInfo.nodeExporterUser,
        nodeExporterPort: keyInfo.nodeExporterPort,
        preProvisionScript: keyInfo.provisionInstanceScript,
        skipProvisioning: keyInfo.skipProvisioning
      };
    }

    const jsonData = {
      provider: {
        name: currentProvider.name,
        config: currentProvider.config,
        uuid: currentProvider.uuid
      },
      key: keyJson,
      regions: onPremRegions
        .filter((region) => region.active === true)
        .map((regionItem) => {
          return {
            code: regionItem.code,
            longitude: regionItem.longitude,
            latitude: regionItem.latitude,
            uuid: regionItem.uuid,
            zones: regionItem.zones
              .filter((zoneItem) => zoneItem.active === true)
              .map((zoneItem) => zoneItem.code)
          };
        }),
      instanceTypes: instanceTypes.data.map((instanceTypeItem) => ({
        instanceTypeCode: instanceTypeItem.instanceTypeCode,
        numCores: instanceTypeItem.numCores,
        memSizeGB: instanceTypeItem.memSizeGB,
        volumeDetailsList: pickArray(instanceTypeItem.instanceTypeDetails.volumeDetailsList, [
          'volumeSizeGB',
          'volumeType',
          'mountPath'
        ])
      })),
      nodes: nodeInstanceList.data.map((nodeItem) => ({
        ip: nodeItem.details.ip,
        region: nodeItem.details.region,
        zone: nodeItem.details.zone,
        instanceType: nodeItem.details.instanceType
      }))
    };
    this.props.setOnPremJsonData(jsonData);
  }

  componentDidMount() {
    const { configuredProviders, selectedProviderUUID, universeList } = this.props;
    const currentProvider = configuredProviders.data.find(
      (provider) => provider.uuid === selectedProviderUUID
    );
    this.props.setSelectedProvider(currentProvider.uuid);
    this.setState({ currentProvider }, () => {
      this.fetchInstanceAndNodeList();
    });
    if (!this.getReadyState(universeList)) {
      this.props.fetchUniverseList();
    }
  }

  componentDidUpdate(prevProps) {
    const {
      configuredProviders,
      configuredRegions,
      cloudBootstrap: {
        data: { type, response }
      },
      cloud: { nodeInstanceList, instanceTypes }
    } = this.props;
    const { isLoading } = this.state;

    if (
      (this.props.selectedProviderUUID !== prevProps.selectedProviderUUID &&
        this.props.selectedProviderUUID !== undefined) ||
      !isEqual(configuredRegions.data, prevProps.configuredRegions.data)
    ) {
      const currentProvider = configuredProviders.data.find(
        (provider) => provider.uuid === this.props.selectedProviderUUID
      );
      this.setState({ currentProvider, isLoading: true });
      this.fetchInstanceAndNodeList(currentProvider);
      // this.updateJSONStore()
    }

    if (
      prevProps.cloudBootstrap !== this.props.cloudBootstrap &&
      type === 'cleanup' &&
      isDefinedNotNull(response)
    ) {
      this.props.resetConfigForm();
      this.props.fetchCloudMetadata();
    }

    if (isLoading) {
      if (this.getReadyState(instanceTypes) && this.getReadyState(nodeInstanceList)) {
        this.setState({ isLoading: false }, () => {
          this.updateJSONStore();
        });
      }
    }
  }
  getReadyState = (dataObject) => {
    return getPromiseState(dataObject).isSuccess() || getPromiseState(dataObject).isEmpty();
  };
  setSelectedProvider = (e) => {
    this.props.setSelectedProvider(e.target.value);
  };
  render() {
    const {
      configuredRegions,
      configuredProviders,
      accessKeys,
      universeList,
      setCreateProviderView,
      selectedProviderUUID,
      cloud: { nodeInstanceList }
    } = this.props;
    const { isLoading } = this.state;

    if (isLoading) {
      return <YBLoading />;
    }

    if (this.state.manageInstances) {
      return (
        <OnPremNodesListContainer
          {...this.props}
          showProviderView={this.showProviderView.bind(this)}
        />
      );
    }

    const currentProvider = configuredProviders.data.find(
      (provider) => provider.code === PROVIDER_TYPE && provider.uuid === selectedProviderUUID
    );
    if (!currentProvider) {
      return <span />;
    }

    const nodesByRegionAndZone = {};
    nodeInstanceList.data.forEach((node) => {
      const region = node.details.region;
      const zone = node.details.zone;
      if (!nodesByRegionAndZone[region]) nodesByRegionAndZone[region] = {};
      if (!nodesByRegionAndZone[region][zone]) nodesByRegionAndZone[region][zone] = [];
      nodesByRegionAndZone[region][zone].push(node);
    });

    const onPremRegions = cloneDeep(
      configuredRegions.data.filter((region) => region.provider.uuid === selectedProviderUUID)
    );
    onPremRegions.forEach((region) => {
      region.zones.forEach((zone) => {
        zone.nodes = nodesByRegionAndZone?.[region.name]?.[zone.name] || [];
      });
    });

    let keyPairName = 'Not Configured';
    if (isNonEmptyArray(accessKeys.data)) {
      const onPremAccessKey = accessKeys.data.find(
        (accessKey) => accessKey.idKey.providerUUID === currentProvider.uuid
      );
      if (isDefinedNotNull(onPremAccessKey)) {
        keyPairName = onPremAccessKey.idKey.keyCode;
      }
    }

    const universeExistsForProvider = (universeList.data || []).some(
      (universe) =>
        universe?.universeDetails?.clusters[0]?.userIntent.provider === currentProvider.uuid
    );

    const buttons = (
      <span className="buttons pull-right">
        <YBButton
          btnText="Delete Provider"
          disabled={universeExistsForProvider}
          btnIcon="fa fa-trash"
          btnClass={'btn btn-default yb-button delete-btn'}
          onClick={this.props.showDeleteProviderModal}
        />
        <YBButton
          btnText="Edit Provider"
          disabled={false}
          btnIcon="fa fa-pencil"
          btnClass={'btn btn-default yb-button'}
          onClick={this.props.showEditProviderForm}
        />
        <YBButton
          btnText="Manage Instances"
          disabled={false}
          btnIcon="fa fa-server"
          btnClass={'btn btn-default yb-button'}
          onClick={this.handleManageNodesClick}
        />
        {this.props.isRedesign && (
          <YBButton
            btnText="View Details"
            btnIcon="fa fa-info"
            btnClass="btn btn-default yb-button"
            onClick={() =>
              browserHistory.push(
                `/${PROVIDER_ROUTE_PREFIX}/${ProviderCode.ON_PREM}/${selectedProviderUUID}`
              )
            }
          />
        )}
        <YBConfirmModal
          name="delete-aws-provider"
          title={'Confirm Delete'}
          onConfirm={this.deleteProvider}
          currentModal="deleteOnPremProvider"
          visibleModal={this.props.visibleModal}
          hideConfirmModal={this.props.hideDeleteProviderModal}
        >
          Are you sure you want to delete this on-premises datacenter configuration?
        </YBConfirmModal>
      </span>
    );

    const editNodesLinkText = 'Setup Instances';
    const nodeItemObject = (
      <div>
        {nodeInstanceList.data.length}
        {(!nodeInstanceList.data.length && (
          <Link
            onClick={this.handleManageNodesClick}
            className="node-link-container"
            title={editNodesLinkText}
          >
            {editNodesLinkText}
          </Link>
        )) ||
          null}
      </div>
    );

    let regionLabels = <Col md={12}>No Regions Configured</Col>;
    if (isNonEmptyArray(onPremRegions)) {
      regionLabels = sortBy(onPremRegions, 'longitude').map((region) => {
        const zoneList = sortBy(region.zones, 'name').map((zone) => {
          const nodeIps = map(map(zone.nodes, 'details'), 'ip');
          return (
            <div key={`zone-${zone.uuid}`} className="zone">
              <div className="zone-name">{zone.name}:</div>
              <span title={nodeIps.join(', ')}>{zone.nodes.length} instances</span>
            </div>
          );
        });
        return (
          <Col key={`region-${region.uuid}`} md={3} lg={2} className="region">
            <div className="region-name">{region.name}</div>
            {zoneList}
          </Col>
        );
      });
    }

    const providerInfo = [
      { name: 'Name', data: currentProvider.name },
      { name: 'SSH Key', data: keyPairName },
      { name: 'Instances', data: nodeItemObject }
    ];
    const currentCloudProviders =
      configuredProviders?.data?.filter?.((provider) => provider.code === PROVIDER_TYPE) || [];
    return (
      <div className="provider-config-container">
        <Row className="provider-row-flex" data-testid="change-or-add-provider">
          <Col md={2}>
            <Field
              name="change.provider"
              type="select"
              component={YBControlledSelectWithLabel}
              defaultValue={selectedProviderUUID}
              label="Change provider"
              onInputChanged={this.setSelectedProvider}
              options={[
                currentCloudProviders.map((cloudProvider) => (
                  <option key={cloudProvider.uuid} value={cloudProvider.uuid}>
                    {cloudProvider.name}
                  </option>
                ))
              ]}
            />
          </Col>
          <Col md={2} className="add-config-margin">
            <div className="yb-field-group add-provider-col">
              <YBButton
                btnClass="btn btn-orange add-provider-config"
                btnText="Add Configuration"
                onClick={setCreateProviderView}
              />
            </div>
          </Col>
        </Row>
        <Row className="config-section-header">
          <Col md={12}>
            {buttons}
            <DescriptionList listItems={providerInfo} />
          </Col>
        </Row>
        <Row className="yb-map-labels">{regionLabels}</Row>
        <Row>
          <Col lg={12} className="provider-map-container">
            <RegionMap
              title="All Supported Regions"
              regions={onPremRegions}
              type="Region"
              showRegionLabels={false}
              showLabels={true}
            />
            <YBMapLegend title="Region Map" />
          </Col>
        </Row>
      </div>
    );
  }
}

export default reduxForm({
  form: 'onPremSuccess'
})(withRouter(NewOnPremSuccess));
