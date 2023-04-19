// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import { OnPremProviderAndAccessKey } from '../../../config';
import { setOnPremConfigData } from '../../../../actions/cloud';
import { isDefinedNotNull, isNonEmptyObject, isNonEmptyArray } from '../../../../utils/ObjectUtils';
import _ from 'lodash';
import { NTP_TYPES } from '../../PublicCloud/views/NTPConfig';
import { ACCEPTABLE_CHARS } from '../../constants';

const DEFAULT_NODE_EXPORTER_PORT = 9300;
const DEFAULT_NODE_EXPORTER_USER = 'prometheus';

const mapDispatchToProps = (dispatch, ownProps) => {
  return {
    setOnPremProviderAndAccessKey: (formData) => {
      Object.keys(formData).forEach((key) => {
        if (typeof formData[key] === 'string' || formData[key] instanceof String)
          formData[key] = formData[key].trim();
      });
      if (!ownProps.isEditProvider) {
        const installNodeExporter = _.get(formData, 'installNodeExporter', true);
        const formSubmitVals = {
          provider: {
            name: formData.name,
            config: {
              YB_HOME_DIR: formData.homeDir
            }
          },
          key: {
            code: formData.name.toLowerCase().replace(/ /g, '-') + '-key',
            privateKeyContent: formData.privateKeyContent,
            sshUser: formData.sshUser,
            sshPort: formData.sshPort,
            passwordlessSudoAccess: formData.passwordlessSudoAccess,
            airGapInstall: formData.airGapInstall,
            skipProvisioning: formData.skipProvisioning,
            installNodeExporter: installNodeExporter,
            nodeExporterPort: _.get(formData, 'nodeExporterPort', DEFAULT_NODE_EXPORTER_PORT),
            nodeExporterUser: _.get(formData, 'nodeExporterUser', DEFAULT_NODE_EXPORTER_USER)
          },
          ntpServers: formData.ntpServers,
          setUpChrony: formData.setUpChrony
        };

        dispatch(setOnPremConfigData(formSubmitVals));
      }
      ownProps.nextPage();
    }
  };
};

const mapStateToProps = (state, ownProps) => {
  let initialFormValues = {
    sshPort: 22,
    airGapInstall: false,
    installNodeExporter: true,
    nodeExporterUser: DEFAULT_NODE_EXPORTER_USER,
    nodeExporterPort: DEFAULT_NODE_EXPORTER_PORT,
    skipProvisioning: false,
    ntp_option: NTP_TYPES.MANUAL,
    ntpServers: [],
    setUpChrony: true
  };
  const {
    cloud: { onPremJsonFormData, accessKeys }
  } = state;
  if (ownProps.isEditProvider && isNonEmptyObject(onPremJsonFormData)) {
    const access_keys_of_provider = accessKeys?.data.find(
      (ak) => ak.idKey?.providerUUID === onPremJsonFormData.provider.uuid
    );
    initialFormValues = {
      name: onPremJsonFormData.provider.name,
      keyCode: onPremJsonFormData.key.code,
      privateKeyContent: onPremJsonFormData.key.privateKeyContent,
      sshUser: onPremJsonFormData.key.sshUser,
      sshPort: onPremJsonFormData.key.sshPort,
      airGapInstall: onPremJsonFormData.key.airGapInstall,
      skipProvisioning: onPremJsonFormData.key.skipProvisioning,
      installNodeExporter: onPremJsonFormData.key.installNodeExporter,
      nodeExporterUser: onPremJsonFormData.key.nodeExporterUser,
      nodeExporterPort: onPremJsonFormData.key.nodeExporterPort,
      homeDir: _.get(onPremJsonFormData, 'provider.config.YB_HOME_DIR', ''),
      machineTypeList: onPremJsonFormData.instanceTypes.map(function (item) {
        return {
          code: item.instanceTypeCode,
          numCores: item.numCores,
          memSizeGB: item.memSizeGB,
          volumeSizeGB: isNonEmptyArray(item.volumeDetailsList)
            ? item.volumeDetailsList[0].volumeSizeGB
            : 0,
          mountPath: isNonEmptyArray(item.volumeDetailsList)
            ? item.volumeDetailsList.map((volItem) => volItem.mountPath).join(', ')
            : '/'
        };
      }),
      regionsZonesList: onPremJsonFormData.regions.map(function (regionZoneItem) {
        return {
          uuid: regionZoneItem.uuid,
          code: regionZoneItem.code,
          location: Number(regionZoneItem.latitude) + ', ' + Number(regionZoneItem.longitude),
          zones: regionZoneItem.zones
            .map(function (zoneItem) {
              return zoneItem;
            })
            .join(', ')
        };
      }),
      ntp_option: access_keys_of_provider?.keyInfo?.setUpChrony
        ? NTP_TYPES.MANUAL
        : NTP_TYPES.NO_NTP,
      ntpServers: access_keys_of_provider?.keyInfo?.ntpServers ?? [],
      setUpChrony: access_keys_of_provider?.keyInfo?.setUpChrony
    };
  }
  return {
    onPremJsonFormData: state.cloud.onPremJsonFormData,
    cloud: state.cloud,
    initialValues: initialFormValues
  };
};

const validate = (values) => {
  const errors = {};
  if (!isDefinedNotNull(values.name)) {
    errors.name = 'Required';
  } else if (!ACCEPTABLE_CHARS.test(values.name)) {
    errors.name = 'Cannot have special characters except - and _';
  }
  if (!isDefinedNotNull(values.sshUser)) {
    errors.sshUser = 'Required';
  }
  if (!isDefinedNotNull(values.privateKeyContent)) {
    errors.privateKeyContent = 'Required';
  }
  if (values.ntp_option === NTP_TYPES.MANUAL && values.ntpServers.length === 0) {
    errors.ntpServers = 'NTP servers cannot be empty';
  }
  return errors;
};

const onPremProviderConfigForm = reduxForm({
  form: 'onPremConfigForm',
  fields: [
    'name',
    'sshUser',
    'sshPort',
    'privateKeyContent',
    'airGapInstall',
    'skipProvisioning',
    'homeDir',
    'installNodeExporter',
    'nodeExporterUser',
    'nodeExporterPort'
  ],
  validate,
  destroyOnUnmount: false,
  enableReinitialize: true,
  keepDirtyOnReinitialize: true
});

export default connect(
  mapStateToProps,
  mapDispatchToProps
)(onPremProviderConfigForm(OnPremProviderAndAccessKey));
