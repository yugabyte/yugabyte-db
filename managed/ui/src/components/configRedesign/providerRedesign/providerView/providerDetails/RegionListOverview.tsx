/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import pluralize from 'pluralize';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';

import { ZoneList } from './ZoneList';
import { ArchitectureType, ProviderCode } from '../../constants';

import { YBAvailabilityZone, YBProvider, YBRegion } from '../../types';

import styles from './RegionListOverview.module.scss';

interface RegionListOverviewProps {
  providerConfig: YBProvider;
}

interface RegionItem {
  name: string;
  zones: YBAvailabilityZone[];

  securityGroupId?: string;
  vnetName?: string;
  ybImage?: string;
  arch?: ArchitectureType;
}

const RegionItemField = {
  NAME: 'name',
  ZONES: 'zones',
  SECURITY_GROUP_ID: 'securityGroupId',
  VIRTUAL_NETWORK_NAME: 'vnetName',
  YB_IMAGE: 'ybImage',
  ARCH_TYPE: 'arch'
} as const;

const TABLE_MIN_PAGE_SIZE = 10;

export const RegionListOverview = ({ providerConfig }: RegionListOverviewProps) => {
  const formatZones = (zones: YBAvailabilityZone[]) => pluralize('zone', zones.length, true);
  const { fields, regionListItems } = adaptToListItems(providerConfig);
  return (
    <div className={styles.bootstrapTableContainer}>
      <BootstrapTable
        tableContainerClass={styles.bootstrapTable}
        data={regionListItems}
        expandableRow={(row: YBRegion) => row.zones.length > 0}
        expandComponent={(row: YBRegion) => (
          <ZoneList zones={row.zones} providerCode={providerConfig.code} />
        )}
        pagination={regionListItems.length > TABLE_MIN_PAGE_SIZE}
      >
        <TableHeaderColumn dataField={RegionItemField.NAME} isKey={true} dataSort={true}>
          Region
        </TableHeaderColumn>
        {fields.includes(RegionItemField.ARCH_TYPE) && (
          <TableHeaderColumn dataField={RegionItemField.ARCH_TYPE}>
            Architecture Type
          </TableHeaderColumn>
        )}
        {fields.includes(RegionItemField.YB_IMAGE) && (
          <TableHeaderColumn dataField={RegionItemField.YB_IMAGE}>
            {providerConfig.code === ProviderCode.AWS ? 'AMI' : 'Image'}
          </TableHeaderColumn>
        )}
        {fields.includes(RegionItemField.SECURITY_GROUP_ID) && (
          <TableHeaderColumn dataField={RegionItemField.SECURITY_GROUP_ID}>
            Security Group ID
          </TableHeaderColumn>
        )}
        {fields.includes(RegionItemField.VIRTUAL_NETWORK_NAME) && (
          <TableHeaderColumn dataField={RegionItemField.VIRTUAL_NETWORK_NAME}>
            {providerConfig.code === ProviderCode.AZU ? 'Virtual Network Name' : 'VPC ID'}
          </TableHeaderColumn>
        )}
        <TableHeaderColumn dataField={RegionItemField.ZONES} dataFormat={formatZones}>
          Zones
        </TableHeaderColumn>
      </BootstrapTable>
    </div>
  );
};

const adaptToListItems = (
  providerConfig: YBProvider
): {
  fields: readonly (keyof RegionItem)[];
  regionListItems: RegionItem[];
} => {
  switch (providerConfig.code) {
    case ProviderCode.AWS:
      return {
        fields: [
          RegionItemField.NAME,
          RegionItemField.ZONES,
          RegionItemField.SECURITY_GROUP_ID,
          RegionItemField.VIRTUAL_NETWORK_NAME,
          RegionItemField.ARCH_TYPE,
          RegionItemField.YB_IMAGE
        ] as const,
        regionListItems: providerConfig.regions.map((region) => ({
          [RegionItemField.NAME]: region.name,
          [RegionItemField.ZONES]: region.zones,
          [RegionItemField.SECURITY_GROUP_ID]: region.details.cloudInfo.aws.securityGroupId,
          [RegionItemField.VIRTUAL_NETWORK_NAME]: region.details.cloudInfo.aws.vnet,
          [RegionItemField.ARCH_TYPE]: region.details.cloudInfo.aws.arch,
          [RegionItemField.YB_IMAGE]: region.details.cloudInfo.aws.ybImage
        }))
      };
    case ProviderCode.AZU:
      return {
        fields: [
          RegionItemField.NAME,
          RegionItemField.ZONES,
          RegionItemField.SECURITY_GROUP_ID,
          RegionItemField.VIRTUAL_NETWORK_NAME
        ] as const,
        regionListItems: providerConfig.regions.map((region) => ({
          [RegionItemField.NAME]: region.name,
          [RegionItemField.ZONES]: region.zones,
          [RegionItemField.SECURITY_GROUP_ID]: region.details.cloudInfo.azu.securityGroupId,
          [RegionItemField.VIRTUAL_NETWORK_NAME]: region.details.cloudInfo.azu.vnet
        }))
      };
    case ProviderCode.GCP:
    case ProviderCode.KUBERNETES:
    case ProviderCode.ON_PREM:
      return {
        fields: [RegionItemField.NAME, RegionItemField.ZONES] as const,
        regionListItems: providerConfig.regions.map((region: any) => ({
          [RegionItemField.NAME]: region.name,
          [RegionItemField.ZONES]: region.zones
        }))
      };
  }
};
