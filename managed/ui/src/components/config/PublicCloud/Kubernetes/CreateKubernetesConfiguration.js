// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { withRouter } from 'react-router';
import * as Yup from 'yup';
import { Formik, Field } from 'formik';
import JsYaml from 'js-yaml';
import _ from 'lodash';
import clsx from 'clsx';
import { YBButton, YBFormSelect, YBFormInput, YBFormDropZone } from '../../../common/forms/fields';
import { toast } from 'react-toastify';

import YBInfoTip from '../../../common/descriptors/YBInfoTip';
import { isNonEmptyObject, isDefinedNotNull } from '../../../../utils/ObjectUtils';
import { readUploadedFile } from '../../../../utils/UniverseUtils';
import { KUBERNETES_PROVIDERS, REGION_DICT } from '../../../../config';
import AddRegionList from './AddRegionList';
import { ACCEPTABLE_CHARS } from '../../constants';
import {
  QUAY_IMAGE_REGISTRY,
  REDHAT_IMAGE_REGISTRY
} from '../../../configRedesign/providerRedesign/forms/k8s/constants';

const convertStrToCode = (s) => s.trim().toLowerCase().replace(/\s/g, '-');
const quayImageRegistry = QUAY_IMAGE_REGISTRY;
const redhatImageRegistry = REDHAT_IMAGE_REGISTRY;

class CreateKubernetesConfiguration extends Component {
  createProviderConfig = (vals, setSubmitting) => {
    const { type } = this.props;
    const self = this;
    const pullSecretFile = vals.pullSecret;
    const providerName = vals.accountName;
    const readerSecret = readUploadedFile(pullSecretFile, false);
    const fileConfigArray = [readerSecret]; // Pull secret file is required
    // Record which regions and zones have configs with tuple of (region, zone)
    const configIndexRecord = [];
    const providerTypeMetadata = KUBERNETES_PROVIDERS.find(
      (providerType) => providerType.code === type
    );
    const providerKubeConfig = vals.kubeConfig ? readUploadedFile(vals.kubeConfig, false) : '{}';
    fileConfigArray.push(providerKubeConfig);
    // Loop thru regions and check for config files
    vals.regionList.forEach((region, rIndex) => {
      region.zoneList.forEach((zone, zIndex) => {
        const content = zone.zoneKubeConfig;
        if (content) {
          if (!_.isEqual(content, vals.kubeConfig)) {
            const zoneConfig = readUploadedFile(content, true);
            fileConfigArray.push(zoneConfig);
            configIndexRecord.push([rIndex, zIndex]);
          } else {
            fileConfigArray.push(providerKubeConfig);
            configIndexRecord.push([rIndex, zIndex]);
          }
        }
      });
    });

    // Loop thru regions to add location information
    const regionsLocInfo = vals.regionList.map((region) => {
      const regInDict = REGION_DICT[region.regionCode.value];
      return {
        name: region.regionCode.label ?? regInDict?.name,
        code: regInDict?.code ?? region.regionCode.value,
        latitude: regInDict?.latitude,
        longitude: regInDict?.longitude,
        zoneList: region.zoneList.map((zone) => {
          const config = {
            STORAGE_CLASS: zone.storageClasses || undefined,
            KUBENAMESPACE: zone.namespace || undefined,
            KUBE_DOMAIN: zone.kubeDomain || undefined,
            OVERRIDES: zone.zoneOverrides,
            // eslint-disable-next-line @typescript-eslint/prefer-nullish-coalescing
            KUBECONFIG_NAME: (zone.zoneKubeConfig && zone.zoneKubeConfig.name) || undefined,
            KUBE_POD_ADDRESS_TEMPLATE: zone.zonePodAddressTemplate || undefined
          };

          //Add cert manager issuer
          if (zone.issuerType === 'ISSUER') config['CERT-MANAGER-ISSUER'] = zone.issuerName;
          if (zone.issuerType === 'CLUSTER') config['CERT-MANAGER-CLUSTERISSUER'] = zone.issuerName;

          return {
            code: convertStrToCode(zone.zoneLabel),
            name: zone.zoneLabel,
            config
          };
        })
      };
    });

    // Catch all onload events for configs
    Promise.all(fileConfigArray).then(
      (configs) => {
        const providerConfig = {
          KUBECONFIG_PROVIDER: vals.providerType
            ? vals.providerType.value
            : providerTypeMetadata
            ? providerTypeMetadata.code
            : 'gke',
          KUBECONFIG_IMAGE_REGISTRY: vals.imageRegistry || quayImageRegistry
        };

        if (!vals.imageRegistry && providerConfig['KUBECONFIG_PROVIDER'] === 'openshift') {
          providerConfig['KUBECONFIG_IMAGE_REGISTRY'] = redhatImageRegistry;
        }

        configIndexRecord.forEach(([regionIdx, zoneIdx], i) => {
          const currentZone = regionsLocInfo[regionIdx].zoneList[zoneIdx];
          currentZone.config.KUBECONFIG_CONTENT = configs[2 + i];
        });
        // TODO: fetch the service account name from the kubeconfig.
        if (isDefinedNotNull(vals.kubeConfig)) {
          Object.assign(providerConfig, {
            KUBECONFIG_NAME: vals.kubeConfig.name,
            KUBECONFIG_CONTENT: configs[1]
          });
        }
        if (isDefinedNotNull(pullSecretFile)) {
          const pullSecretYaml = JsYaml.load(configs[0]);
          Object.assign(providerConfig, {
            KUBECONFIG_IMAGE_PULL_SECRET_NAME:
              pullSecretYaml.metadata && pullSecretYaml.metadata.name,
            KUBECONFIG_PULL_SECRET_NAME: pullSecretFile.name,
            KUBECONFIG_PULL_SECRET_CONTENT: configs[0]
          });
        }

        self.props.createKubernetesProvider(providerName.trim(), providerConfig, regionsLocInfo);
      },
      (reason) => {
        console.warn('File Upload gone wrong. ' + reason);
      }
    );
    this.props.toggleListView(true);
    setSubmitting(false);
  };

  fillFormWithKubeConfig = (formikProps) => {
    const { setFieldValue } = formikProps;
    this.props
      .fetchKubenetesConfig()
      .then((resp) => {
        const {
          KUBECONFIG_PULL_SECRET_NAME,
          KUBECONFIG_PULL_SECRET_CONTENT,
          KUBECONFIG_IMAGE_REGISTRY,
          KUBECONFIG_PROVIDER
        } = resp.data.config;
        const { regionList, name } = resp.data;
        const fileObj = new File([KUBECONFIG_PULL_SECRET_CONTENT], KUBECONFIG_PULL_SECRET_NAME, {
          type: 'text/plain',
          lastModified: new Date().getTime()
        });
        setFieldValue('pullSecret', fileObj);
        setFieldValue('accountName', name);
        setFieldValue('imageRegistry', KUBECONFIG_IMAGE_REGISTRY);
        const provider = _.find(KUBERNETES_PROVIDERS, { code: KUBECONFIG_PROVIDER.toLowerCase() });
        setFieldValue('providerType', { label: provider.name, value: provider.code });

        const parsedRegionList = regionList.map((region) => {
          return {
            regionCode: {
              label: region.name || REGION_DICT[region.code].name,
              value: region.code
            },
            zoneList: region.zoneList.map((z) => {
              return {
                storageClasses: z.config.STORAGE_CLASS,
                zoneLabel: z.name
              };
            }),
            isValid: true
          };
        });
        setFieldValue('regionList', parsedRegionList);
      })
      .catch(() => {
        toast.error('Unable to fetch Kube Config');
      });
  };

  render() {
    const { type, modal, showModal, closeModal } = this.props;

    const providerTypeMetadata = KUBERNETES_PROVIDERS.find(
      (providerType) => providerType.code === type
    );
    let title = 'Create Managed Kubernetes Configuration';
    let providerTypeOptions = null;
    if (providerTypeMetadata) {
      providerTypeOptions = [
        { value: providerTypeMetadata.code, label: providerTypeMetadata.name }
      ];
      title = `Create ${providerTypeMetadata.name} Configuration`;
    } else {
      providerTypeOptions = KUBERNETES_PROVIDERS
        // skip providers with dedicated tab
        .filter(
          (provider) =>
            provider.code !== 'tanzu' && provider.code !== 'pks' && provider.code !== 'openshift'
        )
        .map((provider) => ({ value: provider.code, label: provider.name }));
    }

    const initialValues = {
      // preselect the only available provider type, if any
      providerType: providerTypeOptions.length === 1 ? providerTypeOptions[0] : null,
      accountName: '',
      pullSecret: null,
      regionCode: '',
      zoneLabel: '',
      kubeConfig: null,
      imageRegistry: '',
      storageClasses: '',
      kubeDomain: '',
      regionList: [],
      zoneOverrides: ''
    };

    Yup.addMethod(Yup.array, 'unique', function (message, mapper = (a) => a) {
      return this.test('unique', message, function (list) {
        return list.length === new Set(list.map(mapper)).size;
      });
    });

    const validationSchema = Yup.object().shape({
      accountName: Yup.string()
        .required('Config name is Required')
        .matches(ACCEPTABLE_CHARS, 'Config Name cannot contain special characters except - and _'),

      kubeConfig: Yup.mixed().nullable(),

      pullSecret: Yup.mixed(),

      regionCode: Yup.string(),

      regionList: Yup.array().of(
        Yup.object().shape({
          regionCode: Yup.object().nullable().required('Region is required'),

          zoneList: Yup.array()
            .of(
              Yup.object()
                .shape({
                  zoneLabel: Yup.string().required('Zone label is required'),
                  namespace: Yup.string().when('issuerType', {
                    is: 'ISSUER',
                    then: Yup.string().required('Namespace is Required')
                  }),
                  issuerName: Yup.string().when('issuerType', (issuerType) => {
                    if (issuerType === 'CLUSTER')
                      return Yup.string().required('Cluster Issuer Name is Required');
                    if (issuerType === 'ISSUER')
                      return Yup.string().required('Issuer Name is Required');
                  })
                })
                .required()
            )
            .unique('Duplicate zone label', (a) => a.zoneLabel)
        })
      )
    });

    const containsValidRegion = (p) => {
      return p.values.regionList.filter((region) => region.isValid).length > 0;
    };

    const showPrefillKubeConfigLink =
      type === 'k8s' &&
      (this.props.featureFlags.test.enablePrefillKubeConfig ||
        this.props.featureFlags.released.enablePrefillKubeConfig);

    return (
      <div>
        <h2 className="table-container-title">{title}</h2>
        <div className="provider-config-container">
          <Formik
            validationSchema={validationSchema}
            initialValues={initialValues}
            onSubmit={(values, { setSubmitting }) => {
              const payload = {
                ...values
              };
              this.createProviderConfig(payload, setSubmitting);
            }}
          >
            {(props) => (
              <form name="kubernetesConfigForm" onSubmit={props.handleSubmit}>
                <div className="editor-container">
                  <Row>
                    {showPrefillKubeConfigLink && (
                      <YBButton
                        btnText="Fetch suggested config"
                        btnClass="btn btn-orange fetch-kube-config-but"
                        onClick={() => {
                          this.fillFormWithKubeConfig(props);
                        }}
                      />
                    )}
                    <Col lg={8}>
                      <Row
                        className={clsx('config-provider-row', {
                          hidden: providerTypeOptions.length === 1
                        })}
                      >
                        <Col lg={3}>
                          <div className="form-item-custom-label">Type</div>
                        </Col>
                        <Col lg={7}>
                          <Field
                            name="providerType"
                            component={YBFormSelect}
                            options={providerTypeOptions}
                          />
                        </Col>
                      </Row>
                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Name</div>
                        </Col>
                        <Col lg={7}>
                          <Field
                            name="accountName"
                            placeholder="Provider name"
                            component={YBFormInput}
                            className={'kube-provider-input-field'}
                          />
                        </Col>
                      </Row>
                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Kube Config</div>
                          <div className={`help-block`}></div>
                        </Col>
                        <Col lg={7}>
                          <Field
                            name="kubeConfig"
                            component={YBFormDropZone}
                            title={'Upload Kube Config file'}
                          />
                        </Col>
                        <Col lg={1} className="config-provider-tooltip">
                          <YBInfoTip
                            title="Kube Config"
                            content={
                              'Use this setting to set a kube config for all regions and zones.'
                            }
                          />
                        </Col>
                      </Row>
                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Image Registry</div>
                        </Col>
                        <Col lg={7}>
                          <Field
                            name="imageRegistry"
                            placeholder={
                              providerTypeOptions.length === 1 &&
                              providerTypeOptions[0].value === 'openshift'
                                ? redhatImageRegistry
                                : quayImageRegistry
                            }
                            component={YBFormInput}
                            className={'kube-provider-input-field'}
                          />
                        </Col>
                      </Row>
                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Pull Secret File</div>
                        </Col>
                        <Col lg={7}>
                          <Field
                            name="pullSecret"
                            component={YBFormDropZone}
                            title={'Upload Pull Secret file'}
                          />
                        </Col>
                      </Row>
                    </Col>
                  </Row>
                  <AddRegionList modal={modal} showModal={showModal} closeModal={closeModal} />
                </div>
                <div className="form-action-button-container">
                  <YBButton
                    btnText={'Save'}
                    disabled={
                      props.isSubmitting ||
                      isNonEmptyObject(props.errors) ||
                      !containsValidRegion(props)
                    }
                    btnClass={'btn btn-default save-btn'}
                    btnType="submit"
                  />
                  {this.props.hasConfigs && (
                    <YBButton
                      btnText={'Cancel'}
                      disabled={props.isSubmitting}
                      btnClass={'btn btn-default cancel-btn'}
                      btnType="button"
                      onClick={this.props.toggleListView}
                    />
                  )}
                </div>
              </form>
            )}
          </Formik>
        </div>
      </div>
    );
  }
}

export default withRouter(CreateKubernetesConfiguration);
