import { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';
import { useFormContext, useFieldArray, useWatch, Controller } from 'react-hook-form';
import { Box, Typography, MenuItem, makeStyles, IconButton } from '@material-ui/core';
import { YBButton, YBSelect, YBLabel, YBCheckbox, YBInput } from '../../../../../../components';
import { YBLoadingCircleIcon } from '../../../../../../../components/common/indicators';
import { PlacementStatus } from './PlacementStatus';
import { useGetAllZones, useGetUnusedZones, useNodePlacements } from './PlacementsFieldHelper';
import { Placement, UniverseFormData, CloudType, MasterPlacementMode } from '../../../utils/dto';
import {
  REPLICATION_FACTOR_FIELD,
  PLACEMENTS_FIELD,
  PROVIDER_FIELD,
  RESET_AZ_FIELD,
  USER_AZSELECTED_FIELD,
  MASTER_PLACEMENT_FIELD,
  TOTAL_NODES_FIELD
} from '../../../utils/constants';
//Icons
import { CloseSharp } from '@material-ui/icons';
import { useFormFieldStyles } from '../../../universeMainStyle';

interface PlacementsFieldProps {
  disabled: boolean;
  isPrimary: boolean;
  isEditMode: boolean;
  isGeoPartitionEnabled: boolean;
}

// Override MuiFormControl style to ensure flexDirection is inherited
// and this ensures all the columns are aligned at the same level
const useStyles = makeStyles((theme) => ({
  overrideMuiFormControl: {
    '& .MuiFormControl-root': {
      flexDirection: 'inherit'
    }
  },
  nameColumn: {
    width: 400,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'flex-start'
  },
  nodesColumn: {
    width: 100,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'flex-start',
    margin: theme.spacing(0, 1)
  },
  preferredColumn: {
    width: 80,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'flex-start'
  }
}));

//Extended for useFieldArray
export type PlacementWithId = Placement & { id: any };

const DEFAULT_MIN_NUM_NODE = 1;
export const PlacementsField = ({
  isPrimary,
  isEditMode,
  isGeoPartitionEnabled
}: PlacementsFieldProps): ReactElement => {
  const { control, setValue, getValues } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const helperClasses = useStyles();
  const featureFlags = useSelector((state: any) => state.featureFlags);
  //watchers
  const replicationFactor = useWatch({ name: REPLICATION_FACTOR_FIELD });
  const provider = useWatch({ name: PROVIDER_FIELD });
  const masterPlacement = useWatch({ name: MASTER_PLACEMENT_FIELD });
  const totalNodes = useWatch({ name: TOTAL_NODES_FIELD });

  //custom hooks
  const allZones = useGetAllZones(); //returns all AZ
  const unUsedZones = useGetUnusedZones(allZones); //return unused AZ
  const { isLoading } = useNodePlacements(featureFlags); // Places Nodes

  const { fields, update, append, remove } = useFieldArray({
    control,
    name: PLACEMENTS_FIELD
  });
  const isDedicatedNodes = masterPlacement === MasterPlacementMode.DEDICATED;

  const renderHeader = (
    <Box flex={1} mb={1} display="flex" flexDirection="row" data-testid="PlacementsField-Container">
      <Box className={helperClasses.nameColumn}>
        <YBLabel dataTestId="PlacementsField-AZNameLabel">
          {t('universeForm.cloudConfig.azNameLabel')}
        </YBLabel>
      </Box>
      <Box className={helperClasses.nodesColumn}>
        <YBLabel dataTestId="PlacementsField-IndividualUnitLabel">
          {provider?.code === CloudType.kubernetes
            ? t('universeForm.cloudConfig.azPodsLabel')
            : isDedicatedNodes
            ? t('universeForm.cloudConfig.azTServerNodesLabel')
            : t('universeForm.cloudConfig.azNodesLabel')}
        </YBLabel>
      </Box>
      {isPrimary && (
        <Box className={helperClasses.preferredColumn}>
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

  const getTotalNodesinAZ = () => {
    const initialCount = 0;
    return fields.reduce((prev, cur) => prev + cur.numNodesInAZ, initialCount);
  };

  //get Minimum AZ count to update before making universe_configure call
  const getMinCountAZ = (index: number) => {
    const totalNodesinAz = getTotalNodesinAZ();
    const min = fields[index].numNodesInAZ - (totalNodesinAz - getValues(REPLICATION_FACTOR_FIELD));
    return min > 0 ? min : DEFAULT_MIN_NUM_NODE;
  };

  const renderPlacements = () => {
    return fields.map((field: PlacementWithId, index: number) => {
      const prefferedAZField = `${PLACEMENTS_FIELD}.${index}.isAffinitized` as any;

      return (
        <Box flex={1} display="flex" mb={2} flexDirection="row" alignItems="center" key={field.id}>
          <Box className={helperClasses.nameColumn}>
            <YBSelect
              fullWidth
              disabled={isLoading}
              value={field.name}
              inputProps={{
                'data-testid': `PlacementsField-AZName${index}`
              }}
              onChange={(e) => {
                handleAZChange(field, e.target.value, index);
                setValue(USER_AZSELECTED_FIELD, true);
              }}
            >
              {[field, ...unUsedZones].map((az) => (
                <MenuItem key={az.name} value={az.name}>
                  {az.name}
                </MenuItem>
              ))}
            </YBSelect>
          </Box>
          <Box className={helperClasses.nodesColumn}>
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
                    setValue(USER_AZSELECTED_FIELD, true);
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
            <Box className={helperClasses.preferredColumn}>
              <YBCheckbox
                name={prefferedAZField}
                onChange={(e) => {
                  setValue(prefferedAZField, e.target.checked);
                  setValue(USER_AZSELECTED_FIELD, true);
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
          <Box flexShrink={1}>
            <IconButton
              color="default"
              size="medium"
              disabled={isLoading || getTotalNodesinAZ() - field.numNodesInAZ < replicationFactor}
              data-testid={`PlacementsField-RemoveButton${index}`}
              onClick={() => {
                remove(index);
                setValue(USER_AZSELECTED_FIELD, true);
              }}
            >
              <CloseSharp />
            </IconButton>
          </Box>
        </Box>
      );
    });
  };

  if (fields.length) {
    return (
      <Box
        display="flex"
        width="100%"
        flexDirection="column"
        data-testid="PlacementsField-Container"
      >
        <Box width="100%" display="flex" flexDirection="row" mb={1.5} alignItems={'center'}>
          <Box flexShrink={1} mr={3}>
            <Typography variant="h4">{t('universeForm.cloudConfig.azHeader')}</Typography>
          </Box>
          <YBButton variant="primary" size="medium" onClick={() => setValue(RESET_AZ_FIELD, true)}>
            {t('universeForm.cloudConfig.resetAZLabel')}
          </YBButton>
        </Box>
        {renderHeader}
        {renderPlacements()}
        {unUsedZones.length > 0 &&
          fields.length < (isGeoPartitionEnabled ? totalNodes : replicationFactor) &&
          fields.length < allZones.length && (
            <Box display="flex" justifyContent={'flex-start'} mr={0.5} mt={1}>
              <YBButton
                style={{ width: '150px' }}
                variant="primary"
                disabled={isLoading}
                data-testid="PlacementsField-AddAZButton"
                onClick={() => {
                  const remainingAZ = getValues(REPLICATION_FACTOR_FIELD) - getTotalNodesinAZ();
                  append({
                    ...unUsedZones[0],
                    numNodesInAZ: remainingAZ > 0 || isEditMode ? 1 : 0,
                    replicationFactor: 1,
                    isAffinitized: true
                  });
                  setValue(USER_AZSELECTED_FIELD, true);
                }}
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
        marginTop={15}
        width="600px"
        alignItems={'center'}
        flexDirection={'column'}
        data-testid="PlacementsField-Loader"
      >
        <YBLoadingCircleIcon size="small" />
        Loading placements
      </Box>
    );
  return <></>;
};
