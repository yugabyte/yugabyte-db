// Copyright (c) YugaByte, Inc.

import React from 'react';
import { TableDetailContainer } from '../components/tables';

const TableDetail = ({ params }) => {
  return (
    <div>
      <TableDetailContainer universeUUID={params.uuid}
        tableUUID={params.tableUUID}/>
    </div>
  );
};

export default TableDetail;
