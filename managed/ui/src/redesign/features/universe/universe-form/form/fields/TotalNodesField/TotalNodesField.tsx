import { FocusEvent, ReactElement } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { YBInputField, YBLabel, YBTooltip } from '../../../../../../components';
import { UniverseFormData, CloudType, MasterPlacementMode } from '../../../utils/dto';
import {
  TOTAL_NODES_FIELD,
  MASTER_TOTAL_NODES_FIELD,
  REPLICATION_FACTOR_FIELD,
  PLACEMENTS_FIELD,
  PROVIDER_FIELD,
  MASTER_PLACEMENT_FIELD
} from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';

interface TotalNodesFieldProps {
  disabled?: boolean;
}

const useStyles = makeStyles((theme) => ({
  numNodesInputField: {
    maxWidth: theme.spacing(10)
  }
}));

export const TotalNodesField = ({ disabled }: TotalNodesFieldProps): ReactElement => {
  const { control, setValue, getValues } = useFormContext<UniverseFormData>();
  const classes = useFormFieldStyles();
  const helperClasses = useStyles();
  const { t } = useTranslation();

  // Tooltip message
  const masterNodesTooltipText = t('universeForm.cloudConfig.masterNumNodesHelper');

  //watchers
  const provider = useWatch({ name: PROVIDER_FIELD });
  const replicationFactor = useWatch({ name: REPLICATION_FACTOR_FIELD });
  const masterPlacement = getValues(MASTER_PLACEMENT_FIELD);
  const placements = useWatch({ name: PLACEMENTS_FIELD });
  const currentTotalNodes = getValues(TOTAL_NODES_FIELD);

  const fieldLabel =
    provider?.code === CloudType.kubernetes
      ? t('universeForm.cloudConfig.totalPodsField')
      : t('universeForm.cloudConfig.totalNodesField');

  const handleChange = (e: FocusEvent<HTMLInputElement>) => {
    //reset field value to replication factor if field is empty or less than RF
    const fieldValue = (e.target.value as unknown) as number;
    if (!fieldValue || fieldValue < replicationFactor)
      setValue(TOTAL_NODES_FIELD, replicationFactor, { shouldValidate: true });
    else setValue(TOTAL_NODES_FIELD, fieldValue, { shouldValidate: true });
  };

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
  const isDedicatedNodes = masterPlacement === MasterPlacementMode.DEDICATED;

  const numNodesElement = (
    <Box display="flex" flexDirection="row">
      {isDedicatedNodes && (
        <Box mt={2}>
          <Typography className={classes.labelFont}>{t('universeForm.tserver')}</Typography>
        </Box>
      )}
      <Box className={helperClasses.numNodesInputField} ml={isDedicatedNodes ? 2 : 0}>
        <YBInputField
          control={control}
          name={TOTAL_NODES_FIELD}
          type="number"
          disabled={disabled}
          rules={{
            required: !disabled
              ? (t('universeForm.validation.required', { field: fieldLabel }) as string)
              : ''
          }}
          inputProps={{
            'data-testid': 'TotalNodesField-TServer-Input',
            min: replicationFactor
          }}
          onChange={handleChange}
        />
      </Box>

      {isDedicatedNodes && (
        <>
          <Box mt={2} ml={2}>
            <Typography className={classes.labelFont}>{t('universeForm.master')}</Typography>
          </Box>
          <Box className={helperClasses.numNodesInputField} ml={2}>
            <YBTooltip title={masterNodesTooltipText}>
              <span>
                <YBInputField
                  control={control}
                  name={MASTER_TOTAL_NODES_FIELD}
                  type="number"
                  disabled={true}
                  value={replicationFactor}
                  inputProps={{
                    'data-testid': 'TotalNodesField-Master-Input'
                  }}
                ></YBInputField>
              </span>
            </YBTooltip>
          </Box>
        </>
      )}
    </Box>
  );

  return (
    <Box display="flex" width="100%" data-testid="TotalNodesField-Container">
      <YBLabel dataTestId="TotalNodesField-Label">{fieldLabel}</YBLabel>
      {numNodesElement}
    </Box>
  );
};
