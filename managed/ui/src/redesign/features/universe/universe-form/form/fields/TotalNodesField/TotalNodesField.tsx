import React, { ReactElement } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBInputField, YBLabel } from '../../../../../../components';
import { UniverseFormData, CloudType } from '../../../utils/dto';
import {
  TOTAL_NODES_FIELD,
  REPLICATION_FACTOR_FIELD,
  PLACEMENTS_FIELD,
  PROVIDER_FIELD
} from '../../../utils/constants';

interface TotalNodesFieldProps {
  disabled?: boolean;
}

export const TotalNodesField = ({ disabled }: TotalNodesFieldProps): ReactElement => {
  const { control, setValue, getValues } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  //watchers
  const provider = useWatch({ name: PROVIDER_FIELD });
  const replicationFactor = useWatch({ name: REPLICATION_FACTOR_FIELD });
  const placements = useWatch({ name: PLACEMENTS_FIELD });
  const currentTotalNodes = getValues(TOTAL_NODES_FIELD);

  //set TotalNodes to RF Value when totalNodes < RF
  useUpdateEffect(() => {
    if (replicationFactor > currentTotalNodes) setValue(TOTAL_NODES_FIELD, replicationFactor);
  }, [replicationFactor]);

  //set Total Nodes to TotalNodesInAZ Value in placements
  useUpdateEffect(() => {
    if (placements && placements.length) {
      const initalCount = 0;
      const totalNodesinAz = placements
        .map((e: any) => e.numNodesInAZ)
        .reduce((prev: any, curr: any) => Number(prev) + Number(curr), initalCount);
      if (totalNodesinAz >= replicationFactor) setValue(TOTAL_NODES_FIELD, totalNodesinAz);
    }
  }, [placements]);

  return (
    <Box display="flex" width="100%" data-testid="TotalNodesField-Container">
      <YBLabel dataTestId="TotalNodesField-Label">
        {provider?.code === CloudType.kubernetes
          ? t('universeForm.cloudConfig.totalPodsField')
          : t('universeForm.cloudConfig.totalNodesField')}
      </YBLabel>
      <Box flex={1}>
        <YBInputField
          control={control}
          name={TOTAL_NODES_FIELD}
          // fullWidth
          type="number"
          disabled={disabled}
          inputProps={{
            'data-testid': 'TotalNodesField-Input',
            min: replicationFactor
          }}
        />
      </Box>
    </Box>
  );
};
