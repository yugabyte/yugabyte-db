// Copyright (c) YugaByte, Inc.

import React from 'react';
import { UniverseDetailContainer } from '../components/universes';

const UniverseDetail = ({ params }) => {
  return (
    <div>
      <UniverseDetailContainer uuid={params.uuid}/>
    </div>
  );
};

export default UniverseDetail;
