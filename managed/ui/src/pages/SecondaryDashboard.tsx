/*
 * Created on Jan 16 2024
 *
 * Copyright 2024 YugaByte, Inc. and Contributors
 */

import { TroubleshootingDashboard } from '../redesign/features/Troubleshooting/TroubleshootingDashboard';

export const SecondaryDashboard = (props: any) => {
  const universeUuid = props.params?.uuid;
  const troubleshootUuid = props.params?.troubleshootUUID;

  return (
    <TroubleshootingDashboard universeUuid={universeUuid} troubleshootUuid={troubleshootUuid} />
  );
};
