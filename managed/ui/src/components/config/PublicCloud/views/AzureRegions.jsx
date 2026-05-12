import _ from 'lodash';
import * as Yup from 'yup';
import { Field, Formik } from 'formik';
import { Col, Row } from 'react-bootstrap';
import { useEffect, useState } from 'react';
import { YBFormInput, YBFormSelect, YBModal } from '../../../common/forms/fields';

// The following region array is used in the old provider form.
// Please also add any new regions and zones to:
// src/components/configRedesign/providerRedesign/providerRegionsData.ts
// so that the new kubernetes provider UI stays in sync.
// The old provider UI and its related components/constants/types will be removed once
// it is no longer used as a fallback option.
const AZURE_REGIONS = [
  {
    region: 'westus',
    name: 'West US',
    zones: ['westus']
  },
  {
    region: 'westus2',
    name: 'West US 2',
    zones: ['westus2-1', 'westus2-2', 'westus2-3']
  },
  {
    region: 'westus3',
    name: 'West US 3',
    zones: ['westus3-1', 'westus3-2', 'westus3-3']
  },
  {
    region: 'westcentralus',
    name: 'West Central US',
    zones: ['westcentralus']
  },
  {
    region: 'centralus',
    name: 'Central US',
    zones: ['centralus-1', 'centralus-2', 'centralus-3']
  },
  {
    region: 'northcentralus',
    name: 'North Central US',
    zones: ['northcentralus']
  },
  {
    region: 'southcentralus',
    name: 'South Central US',
    zones: ['southcentralus-1', 'southcentralus-2', 'southcentralus-3']
  },
  {
    region: 'eastus',
    name: 'East US',
    zones: ['eastus-1', 'eastus-2', 'eastus-3']
  },
  {
    region: 'eastus2',
    name: 'East US 2',
    zones: ['eastus2-1', 'eastus2-2', 'eastus2-3']
  },
  {
    region: 'canadacentral',
    name: 'Canada Central',
    zones: ['canadacentral-1', 'canadacentral-2', 'canadacentral-3']
  },
  {
    region: 'canadaeast',
    name: 'Canada East',
    zones: ['canadaeast']
  },
  {
    region: 'westeurope',
    name: 'West Europe',
    zones: ['westeurope-1', 'westeurope-2', 'westeurope-3']
  },
  {
    region: 'northeurope',
    name: 'North Europe',
    zones: ['northeurope-1', 'northeurope-2', 'northeurope-3']
  },
  {
    region: 'ukwest',
    name: 'UK West',
    zones: ['ukwest']
  },
  {
    region: 'uksouth',
    name: 'UK South',
    zones: ['uksouth-1', 'uksouth-2', 'uksouth-3']
  },
  {
    region: 'francecentral',
    name: 'France Central',
    zones: ['francecentral-1', 'francecentral-2', 'francecentral-3']
  },
  {
    region: 'germanywestcentral',
    name: 'Germany West Central',
    zones: ['germanywestcentral-1', 'germanywestcentral-2', 'germanywestcentral-3']
  },
  {
    region: 'norwayeast',
    name: 'Norway East',
    zones: ['norwayeast-1', 'norwayeast-2', 'norwayeast-3']
  },
  {
    region: 'switzerlandnorth',
    name: 'Switzerland North',
    zones: ['switzerlandnorth-1', 'switzerlandnorth-2', 'switzerlandnorth-3']
  },
  {
    region: 'eastasia',
    name: 'East Asia',
    zones: ['eastasia-1', 'eastasia-2', 'eastasia-3']
  },
  {
    region: 'southeastasia',
    name: 'Southeast Asia',
    zones: ['southeastasia-1', 'southeastasia-2', 'southeastasia-3']
  },
  {
    region: 'centralindia',
    name: 'Central India',
    zones: ['centralindia-1', 'centralindia-2', 'centralindia-3']
  },
  {
    region: 'southindia',
    name: 'South India',
    zones: ['southindia']
  },
  {
    region: 'westindia',
    name: 'West India',
    zones: ['westindia']
  },
  {
    region: 'japaneast',
    name: 'Japan East',
    zones: ['japaneast-1', 'japaneast-2', 'japaneast-3']
  },
  {
    region: 'japanwest',
    name: 'Japan West',
    zones: ['japanwest']
  },
  {
    region: 'koreacentral',
    name: 'Korea Central',
    zones: ['koreacentral-1', 'koreacentral-2', 'koreacentral-3']
  },
  {
    region: 'koreasouth',
    name: 'Korea South',
    zones: ['koreasouth']
  },
  {
    region: 'uaenorth',
    name: 'UAE North',
    zones: ['uaenorth-1', 'uaenorth-2', 'uaenorth-3']
  },
  {
    region: 'australiacentral',
    name: 'Australia Central',
    zones: ['australiacentral']
  },
  {
    region: 'australiaeast',
    name: 'Australia East',
    zones: ['australiaeast-1', 'australiaeast-2', 'australiaeast-3']
  },
  {
    region: 'australiasoutheast',
    name: 'Australia Southeast',
    zones: ['australiasoutheast']
  },
  {
    region: 'southafricanorth',
    name: 'South Africa North',
    zones: ['southafricanorth-1', 'southafricanorth-2', 'southafricanorth-3']
  },
  {
    region: 'brazilsouth',
    name: 'Brazil South',
    zones: ['brazilsouth-1', 'brazilsouth-2', 'brazilsouth-3']
  }
];

const zonesMap = {};
AZURE_REGIONS.forEach((item) => {
  zonesMap[item.region] = item.zones.map((zone) => ({ value: zone, label: zone }));
});

const initialRegionForm = {
  region: '',
  vpcId: '',
  customSecurityGroupId: '',
  customImageId: '',
  azToSubnetIds: [
    { zone: '', subnet: '' },
    { zone: '', subnet: '' },
    { zone: '', subnet: '' }
  ]
};

const validationSchema = Yup.object().shape({
  region: Yup.object().required('Region is a required field'),
  vpcId: Yup.string().required('Virtual Network Name is a required field'),
  azToSubnetIds: Yup.lazy((value) => {
    const isAnyZoneSelected = value.some((item) => !_.isEmpty(item.zone?.value));
    if (isAnyZoneSelected) {
      return Yup.array().of(
        Yup.object().shape({
          subnet: Yup.string().when('zone', {
            is: (value) => !_.isEmpty(value),
            then: Yup.string().required('Subnet is a required field')
          })
        })
      );
    } else {
      return Yup.array().test(
        'non-empty-test',
        'At least one zone has to be selected',
        () => false
      );
    }
  })
});

export const AzureRegions = ({ regions, onChange }) => {
  const [isModalVisible, setModalVisibility] = useState(false);
  const [availableRegions, setAvailableRegions] = useState([]);
  const [currentRegion, setCurrentRegion] = useState(); // region that is currently edited in modal

  // remove already selected regions from the list and convert them to rendering format
  useEffect(() => {
    const selectedRegions = _.map(regions, 'region.value');
    const result = AZURE_REGIONS.filter(
      (item) => !selectedRegions.includes(item.region)
    ).map((region) => ({ value: region.region, label: region.name }));

    // when editing existing region - make sure to keep it in dropdown options
    if (currentRegion) {
      result.push({ value: currentRegion.region.value, label: currentRegion.region.label });
    }

    setAvailableRegions(result);
  }, [regions, currentRegion]);

  const removeRegion = (item) => onChange(_.without(regions, item));

  const editRegion = (item) => {
    setCurrentRegion(item);
    setModalVisibility(true);
  };

  return (
    <div>
      <h4 className="regions-form-title">Regions</h4>

      {!_.isEmpty(regions) && (
        <ul className="config-region-list">
          <li className="header-row">
            <div>Name</div>
            <div>Virtual Network</div>
            <div>Security Group</div>
            <div>Zones</div>
            <div />
          </li>
          {regions.map((item) => (
            <li key={item.region.value} onClick={() => editRegion(item)}>
              <div>
                <strong>{item.region.label}</strong>
              </div>
              <div>{item.vpcId}</div>
              <div>{item.customSecurityGroupId}</div>
              <div>{item.azToSubnetIds.filter((item) => !_.isEmpty(item.zone.value)).length}</div>
              <div>
                <button
                  type="button"
                  className="delete-provider"
                  onClick={(event) => {
                    event.stopPropagation();
                    removeRegion(item);
                  }}
                >
                  <i className="fa fa-times fa-fw delete-row-btn"></i>
                </button>
              </div>
            </li>
          ))}
        </ul>
      )}
      <button
        type="button"
        className="btn btn-default btn-add-region"
        onClick={() => editRegion(null)}
      >
        <div className="btn-icon">
          <i className="fa fa-plus"></i>
        </div>{' '}
        Add Region
      </button>

      {isModalVisible && (
        <Formik
          initialValues={currentRegion ?? initialRegionForm}
          validationSchema={validationSchema}
        >
          {({ values, errors, isValid, setFieldValue }) => (
            <YBModal
              formName="azureRegionsConfig"
              visible={true}
              submitLabel={currentRegion ? 'Edit Region' : 'Add Region'}
              showCancelButton={true}
              disableSubmit={!isValid}
              title="Specify Region Info"
              onHide={() => setModalVisibility(false)}
              onFormSubmit={() => {
                let updatedRegions;
                if (currentRegion) {
                  const i = regions.findIndex((item) => item === currentRegion);
                  regions[i] = values;
                  updatedRegions = regions;
                } else {
                  updatedRegions = [...regions, values];
                }

                onChange(updatedRegions);
                setModalVisibility(false);
              }}
            >
              <div>
                <div className="form-item-custom-label">Region</div>
                <Field
                  name="region"
                  component={YBFormSelect}
                  options={availableRegions}
                  onChange={({ field }, value) => {
                    // reset zones on region change and then update region itself
                    setFieldValue('azToSubnetIds', initialRegionForm.azToSubnetIds);
                    setFieldValue('region', value);
                  }}
                />
              </div>
              <div>
                <div className="form-item-custom-label">Virtual Network Name</div>
                <Field name="vpcId" component={YBFormInput} />
              </div>
              <div>
                <div className="form-item-custom-label">Security Group Name (optional)</div>
                <Field name="customSecurityGroupId" component={YBFormInput} />
              </div>
              <div>
                <div className="form-item-custom-label">
                  Marketplace Image URN/Shared Gallery Image ID (optional)
                </div>
                <Field name="customImageId" component={YBFormInput} />
              </div>
              <div className="divider" />
              <h5>AZ Mapping</h5>
              {values.azToSubnetIds.map((item, index) => (
                <Row
                  // eslint-disable-next-line react/no-array-index-key
                  key={index}
                  className={
                    index >= (zonesMap[values.region.value] || []).length && values.region
                      ? 'invisible'
                      : 'visible'
                  }
                >
                  <Col lg={6}>
                    <Field
                      name={`azToSubnetIds[${index}].zone`}
                      component={YBFormSelect}
                      isClearable={true}
                      options={
                        // skip already selected zones from dropdown options
                        (zonesMap[values.region.value] || []).filter((item) => {
                          const selectedZones = _.compact(
                            _.map(values.azToSubnetIds, 'zone.value')
                          );
                          return !selectedZones.includes(item.value);
                        })
                      }
                    />
                  </Col>
                  <Col lg={6}>
                    <Field
                      name={`azToSubnetIds[${index}].subnet`}
                      component={YBFormInput}
                      placeholder="Subnet"
                    />
                  </Col>
                </Row>
              ))}
              {_.isString(errors.azToSubnetIds) && (
                <div className="has-error">
                  <span className="help-block">{errors.azToSubnetIds}</span>
                </div>
              )}
            </YBModal>
          )}
        </Formik>
      )}
    </div>
  );
};
