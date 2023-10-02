/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';

import { VolumeDetails } from '../../../../../redesign/helpers/dtos';

import styles from './VolumeList.module.scss';

interface VolumeListProps {
  volumes: VolumeDetails[];
}

export const VolumeList = ({ volumes }: VolumeListProps) => (
  <div className={styles.expandComponent}>
    <BootstrapTable tableContainerClass={styles.bootstrapTable} data={volumes}>
      <TableHeaderColumn dataField="volumeType" isKey={true} dataSort={true}>
        Volume Type
      </TableHeaderColumn>
      <TableHeaderColumn dataField="volumeSizeGB" dataSort={true}>
        Volume Size (GB)
      </TableHeaderColumn>
      <TableHeaderColumn dataField="mountPath" dataSort={true}>
        Mount Path
      </TableHeaderColumn>
    </BootstrapTable>
  </div>
);
