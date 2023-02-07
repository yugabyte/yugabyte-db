import React, { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, useFieldArray, useWatch, Controller } from 'react-hook-form';
import { Box, Typography, MenuItem } from '@material-ui/core';
import { YBButton, YBSelect, YBLabel, YBCheckbox, YBInput } from '../../../../../../components';
import { YBLoadingCircleIcon } from '../../../../../../../components/common/indicators';
import { PlacementStatus } from './PlacementStatus';
import { useGetAllZones, useGetUnusedZones, useNodePlacements } from './PlacementsFieldHelper';
import { Placement, UniverseFormData, CloudType } from '../../../utils/dto';
import {
  REPLICATION_FACTOR_FIELD,
  PLACEMENTS_FIELD,
  PROVIDER_FIELD,
  RESET_AZ_FIELD
} from '../../../utils/constants';

interface PlacementsFieldProps {
  disabled: boolean;
  isPrimary: boolean;
}

//Extended for useFieldArray
export type PlacementWithId = Placement & { id: any };

const DEFAULT_MIN_NUM_NODE = 1;
export const PlacementsField = ({ disabled, isPrimary }: PlacementsFieldProps): ReactElement => {
  const { control, setValue, getValues } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  //watchers
  const replicationFactor = useWatch({ name: REPLICATION_FACTOR_FIELD });
  const provider = useWatch({ name: PROVIDER_FIELD });

  //custom hooks
  const allZones = useGetAllZones(); //returns all AZ
  const unUsedZones = useGetUnusedZones(allZones); //return unused AZ
  const { isLoading } = useNodePlacements(); // Places Nodes

  const { fields, update, append } = useFieldArray({
    control,
    name: PLACEMENTS_FIELD
  });

  const renderHeader = (
    <Box flex={1} mt={1} display="flex" flexDirection="row" data-testid="PlacementsField-Container">
      <Box flex={2}>
        <YBLabel dataTestId="PlacementsField-AZNameLabel">
          {t('universeForm.cloudConfig.azNameLabel')}
        </YBLabel>
      </Box>
      <Box flexShrink={1} width="150px">
        <YBLabel dataTestId="PlacementsField-IndividualUnitLabel">
          {provider?.code === CloudType.kubernetes
            ? t('universeForm.cloudConfig.azPodsLabel')
            : t('universeForm.cloudConfig.azNodesLabel')}
        </YBLabel>
      </Box>
      {isPrimary && (
        <Box flexShrink={1} width="100px">
          <YBLabel dataTestId="PlacementsField-PreferredLabel">
            {t('universeForm.cloudConfig.preferredAZLabel')}
          </YBLabel>
        </Box>
      )}
    </Box>
  );

  const handleAZChange = (oldAz: Placement, azName: string, index: any) => {
    const selectedZone = allZones.find((az: any) => az.name === azName);
    const updateAz = { ...oldAz, ...selectedZone };
    update(index, updateAz);
  };

  //get Minimum AZ count to update before making universe_configure call
  const getMinCountAZ = (index: number) => {
    const initialCount = 0;
    const totalNodesinAz = fields
      .map((e) => e.numNodesInAZ)
      .reduce((prev, cur) => prev + cur, initialCount);
    const min = fields[index].numNodesInAZ - (totalNodesinAz - getValues(REPLICATION_FACTOR_FIELD));
    return min > 0 ? min : DEFAULT_MIN_NUM_NODE;
  };

  const renderPlacements = () => {
    return fields.map((field: PlacementWithId, index: number) => {
      const prefferedAZField = `${PLACEMENTS_FIELD}.${index}.isAffinitized` as any;

      return (
        <Box flex={1} display="flex" mb={1} flexDirection="row" key={field.id}>
          <Box flex={2} mr={0.5}>
            <YBSelect
              fullWidth
              disabled={isLoading}
              value={field.name}
              inputProps={{
                'data-testid': `PlacementsField-AZName${index}`
              }}
              onChange={(e) => {
                handleAZChange(field, e.target.value, index);
              }}
            >
              {[field, ...unUsedZones].map((az) => (
                <MenuItem key={az.name} value={az.name}>
                  {az.name}
                </MenuItem>
              ))}
            </YBSelect>
          </Box>
          <Box flexShrink={1} width="150px" mr={0.5}>
            <Controller
              control={control}
              name={`${PLACEMENTS_FIELD}.${index}.numNodesInAZ` as const}
              render={({ field: { onChange, ref, ...rest } }) => (
                <YBInput
                  type="number"
                  fullWidth
                  inputRef={ref}
                  disabled={isLoading}
                  onChange={(e) => {
                    if (!e.target.value || Number(e.target.value) < getMinCountAZ(index))
                      onChange(getMinCountAZ(index));
                    else onChange(Number(e.target.value));
                  }}
                  {...rest}
                  inputProps={{
                    'data-testid': `PlacementsField-IndividualCount${index}`
                  }}
                />
              )}
            />
          </Box>
          {isPrimary && (
            <Box flexShrink={1} display="flex" alignItems="center" width="100px">
              <YBCheckbox
                size="medium"
                name={prefferedAZField}
                onChange={(e) => {
                  setValue(prefferedAZField, e.target.checked);
                }}
                defaultChecked={field.isAffinitized}
                value={field.isAffinitized}
                disabled={isLoading}
                label=""
                inputProps={{
                  'data-testid': `PlacementsField-PrefferedCheckbox${index}`
                }}
              />
            </Box>
          )}
        </Box>
      );
    });
  };

  if (fields.length) {
    return (
      <Box display="flex" width="100%" flexDirection="column">
        <Box width="100%" display="flex" flexDirection="row" alignItems={'center'}>
          <Box flexShrink={1} mr={3}>
            <Typography variant="h5">{t('universeForm.cloudConfig.azHeader')}</Typography>
          </Box>
          <YBButton variant="secondary" onClick={() => setValue(RESET_AZ_FIELD, true)}>
            {t('universeForm.cloudConfig.resetAZLabel')}
          </YBButton>
        </Box>
        {renderHeader}
        {renderPlacements()}
        {unUsedZones.length > 0 && fields.length < replicationFactor && (
          <Box display="flex" justifyContent={'flex-start'} mr={0.5} mt={1}>
            <YBButton
              style={{ width: '150px' }}
              variant="primary"
              disabled={isLoading}
              data-testid="PlacementsField-AddAZButton"
              onClick={() =>
                append({
                  ...unUsedZones[0],
                  numNodesInAZ: 1,
                  replicationFactor: 1,
                  isAffinitized: true
                })
              }
            >
              {t('universeForm.cloudConfig.addZoneButton')}
            </YBButton>
          </Box>
        )}
        <PlacementStatus />
      </Box>
    );
  }

  if (isLoading)
    return (
      <Box
        display="flex"
        justifyContent="center"
        alignItems={'center'}
        height="100%"
        flexDirection={'column'}
      >
        <YBLoadingCircleIcon size="small" />
        Loading placements
      </Box>
    );
  return <></>;
};
