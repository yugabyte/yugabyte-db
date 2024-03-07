/*
 * Created on Mon Dec 04 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { noop } from 'lodash';
import { useTranslation } from 'react-i18next';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { LinuxVersionsCard } from './LinuxVersionsList';
import { IsOsPatchingEnabled } from './LinuxVersionUtils';
import { ImageBundle } from '../../../../../redesign/features/universe/universe-form/utils/dto';
import { ArchitectureType } from '../../constants';

import styles from '../../providerView/providerDetails/RegionListOverview.module.scss';
import regionListStyles from '../../providerView/providerDetails/ProviderOverview.module.scss';

interface LinuxVersionOverviewProps {
  imageBundles: ImageBundle[];
}

const COLLAPSED_ICON = <i className="fa fa-caret-right expand-keyspace-icon" />;
const EXPANDED_ICON = <i className="fa fa-caret-down expand-keyspace-icon" />;

export const LinuxVersionOverview: FC<LinuxVersionOverviewProps> = ({ imageBundles }) => {
  const X86Bundles = imageBundles.filter((img) => img.details.arch === ArchitectureType.X86_64);
  const ArmBundles = imageBundles.filter((img) => img.details.arch === ArchitectureType.ARM64);
  const { t } = useTranslation();

  const bundles = [];

  if (X86Bundles.length > 0) {
    bundles.push({
      name: t('x86_64', { keyPrefix: 'universeForm.instanceConfig' }),
      archType: ArchitectureType.X86_64,
      bundles: X86Bundles
    });
  }

  if (ArmBundles.length > 0) {
    bundles.push({
      name: t('aarch64', { keyPrefix: 'universeForm.instanceConfig' }),
      archType: ArchitectureType.ARM64,
      bundles: ArmBundles
    });
  }

  const osPatchingEnabled = IsOsPatchingEnabled();

  if (!osPatchingEnabled) {
    return null;
  }

  return (
    <div className={regionListStyles.regionListContainer}>
      <div className={styles.bootstrapTableContainer}>
        <BootstrapTable
          tableContainerClass={styles.bootstrapTable}
          data={bundles}
          expandableRow={() => true}
          expandColumnOptions={{
            expandColumnVisible: true,
            expandColumnComponent: ({ isExpanded }) =>
              isExpanded ? EXPANDED_ICON : COLLAPSED_ICON,
            expandColumnBeforeSelectColumn: true
          }}
          expandComponent={(row: any) => {
            return (
              <LinuxVersionsCard
                images={row.bundles}
                archType={row.archType}
                onDelete={noop}
                setEditDetails={noop}
                setImageAsDefault={noop}
                showMoreActions={false}
                showTitle={false}
                viewMode="EDIT"
              />
            );
          }}
        >
          <TableHeaderColumn dataField={'name'} isKey={true} dataSort={true}>
            Architecture
          </TableHeaderColumn>
          <TableHeaderColumn dataField={'bundles'} dataFormat={(bundles) => <>{bundles.length}</>}>
            Versions
          </TableHeaderColumn>
        </BootstrapTable>
      </div>
    </div>
  );
};
