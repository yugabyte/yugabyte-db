import React, { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, useController } from 'react-hook-form';
import { ButtonGroup, Box, makeStyles, Theme } from '@material-ui/core';
import { YBButton, YBInputField, YBLabel } from '../../../../../../components';
import { UniverseFormData } from '../../../utils/dto';
import { REPLICATION_FACTOR_FIELD } from '../../../utils/constants';
import { themeVariables } from '../../../../../../theme/variables';

interface ReplicationFactorProps {
  disabled?: boolean;
  isPrimary: boolean;
}

const useStyles = makeStyles((theme: Theme) => ({
  rfButton: {
    height: themeVariables.inputHeight,
    borderWidth: '0.5px !important'
  }
}));

const PRIMARY_RF = [1, 3, 5, 7];

export const ReplicationFactor = ({
  disabled,
  isPrimary
}: ReplicationFactorProps): ReactElement => {
  const { control, setValue } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const classes = useStyles();

  const {
    field: { value }
  } = useController({
    name: REPLICATION_FACTOR_FIELD
  });

  const handleSelect = (val: number) => {
    setValue(REPLICATION_FACTOR_FIELD, val);
  };

  return (
    <Box width="100%" display="flex" data-testid="ReplicationFactor-Container">
      <YBLabel dataTestId="ReplicationFactor-Label">
        {t('universeForm.cloudConfig.replicationField')}
      </YBLabel>
      <Box flex={1}>
        {isPrimary ? (
          <ButtonGroup variant="contained" color="default">
            {PRIMARY_RF.map((factor) => {
              return (
                <YBButton
                  key={factor}
                  className={classes.rfButton}
                  data-testid={`ReplicationFactor-option${factor}`}
                  disabled={factor !== value && disabled}
                  variant={factor === value ? 'primary' : 'secondary'}
                  onClick={(e: any) => {
                    if (disabled) e.preventDefault();
                    else handleSelect(factor);
                  }}
                >
                  {factor}
                </YBButton>
              );
            })}
          </ButtonGroup>
        ) : (
          <YBInputField
            control={control}
            name={REPLICATION_FACTOR_FIELD}
            fullWidth
            type="number"
            disabled={disabled}
            inputProps={{
              'data-testid': 'ReplicationFactor-Input',
              min: 1,
              max: 15
            }}
          />
        )}
      </Box>
    </Box>
  );
};
