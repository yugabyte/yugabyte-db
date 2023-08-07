/*
 * Created on Mon May 16 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { BootstrapTable, BootstrapTableProps } from 'react-bootstrap-table';
import { isFunction } from 'lodash';
import './YBTable.scss';

// eslint-disable-next-line @typescript-eslint/no-empty-interface
interface YBTableProps extends BootstrapTableProps {}

const getComputedClassName = (fn: Function | string | undefined, ...params: any[]) => {
  if (typeof fn === 'string') {
    return fn;
  } else if (isFunction(fn)) {
    return fn(...params);
  }
  return '';
};

export const YBTable: FC<YBTableProps> = ({ children, trClassName, ...props }) => {
  return (
    <BootstrapTable
      containerClass="yb-table"
      tableHeaderClass="yb-table-header"
      tableBodyClass="yb-table-body"
      trClassName={(row, rowIndex) =>
        'yb-table-row ' + getComputedClassName(trClassName, row, rowIndex)
      }
      {...props}
    >
      {children}
    </BootstrapTable>
  );
};
