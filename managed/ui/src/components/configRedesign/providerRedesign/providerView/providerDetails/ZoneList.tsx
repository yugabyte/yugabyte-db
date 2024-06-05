/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { ProviderCode } from '../../constants';

import { YBAvailabilityZone } from '../../types';

import styles from './ZoneList.module.scss';

interface ZoneListProps {
  providerCode: ProviderCode;
  zones: YBAvailabilityZone[];
}
export const ZoneList = ({ providerCode, zones }: ZoneListProps) => {
  return (
    <div className={styles.expandComponent}>
      <BootstrapTable tableContainerClass={styles.bootstrapTable} data={zones}>
        <TableHeaderColumn dataField="code" isKey={true} dataSort={true}>
          Zone
        </TableHeaderColumn>
        {([ProviderCode.AWS, ProviderCode.AZU, ProviderCode.GCP] as ProviderCode[]).includes(
          providerCode
        ) && (
          <TableHeaderColumn dataField="subnet" dataSort={true}>
            Subnet
          </TableHeaderColumn>
        )}
      </BootstrapTable>
    </div>
  );
};
