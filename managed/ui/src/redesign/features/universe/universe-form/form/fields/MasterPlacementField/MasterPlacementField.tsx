import { ReactElement, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box, Typography, makeStyles } from '@material-ui/core';
import {
  YBRadioGroupField,
  YBLabel,
  YBTooltip,
  RadioGroupOrientation
} from '../../../../../../components';
import { UniverseFormData, MasterPlacementMode, CloudType } from '../../../utils/dto';
import { MASTER_PLACEMENT_FIELD, PROVIDER_FIELD } from '../../../utils/constants';

interface MasterPlacementFieldProps {
  isPrimary: boolean;
  useK8CustomResources: boolean;
}

const useStyles = makeStyles((theme) => ({
  tooltipLabel: {
    textDecoration: 'underline',
    marginLeft: theme.spacing(2),
    fontSize: '11.5px',
    fontWeight: 400,
    fontFamily: 'Inter',
    color: '#67666C',
    marginTop: '2px',
    cursor: 'default'
  }
}));

export const MasterPlacementField = ({
  isPrimary,
  useK8CustomResources
}: MasterPlacementFieldProps): ReactElement => {
  const { control, setValue } = useFormContext<UniverseFormData>();
  const classes = useStyles();
  const { t } = useTranslation();

  // Tooltip message
  const masterPlacementTooltipText = t('universeForm.cloudConfig.masterPlacementHelper');

  // watchers
  const masterPlacement = useWatch({ name: MASTER_PLACEMENT_FIELD });
  const provider = useWatch({ name: PROVIDER_FIELD });

  useEffect(() => {
    if (!isPrimary) {
      setValue(MASTER_PLACEMENT_FIELD, MasterPlacementMode.COLOCATED);
    }
    if (isPrimary && provider?.code === CloudType.kubernetes) {
      setValue(
        MASTER_PLACEMENT_FIELD,
        useK8CustomResources ? MasterPlacementMode.DEDICATED : MasterPlacementMode.COLOCATED
      );
    }
  }, [isPrimary, provider]);

  return (
    <>
      {isPrimary && provider?.code !== CloudType.kubernetes ? (
        <Box display="flex" width="100%" data-testid="MasterPlacement-Container">
          <Box>
            <YBLabel dataTestId="MasterPlacement-Label">
              {t('universeForm.cloudConfig.masterPlacement')}
            </YBLabel>
          </Box>
          <Box flex={1}>
            <YBRadioGroupField
              name={MASTER_PLACEMENT_FIELD}
              control={control}
              value={masterPlacement}
              orientation={RadioGroupOrientation.VERTICAL}
              onChange={(e) => {
                setValue(MASTER_PLACEMENT_FIELD, e.target.value as MasterPlacementMode);
              }}
              options={[
                {
                  value: MasterPlacementMode.COLOCATED,
                  label: (
                    <Box display="flex">{t('universeForm.cloudConfig.colocatedModeHelper')}</Box>
                  )
                },
                {
                  value: MasterPlacementMode.DEDICATED,
                  label: (
                    <Box display="flex">
                      {t('universeForm.cloudConfig.dedicatedModeHelper')}
                      <YBTooltip
                        title={masterPlacementTooltipText}
                        className={classes.tooltipLabel}
                      >
                        <Typography display="inline">
                          {t('universeForm.cloudConfig.whenToUseDedicatedHelper')}
                        </Typography>
                      </YBTooltip>
                    </Box>
                  )
                }
              ]}
            />
          </Box>
        </Box>
      ) : (
        <></>
      )}
    </>
  );
};
