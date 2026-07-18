import { useState, useEffect, ChangeEvent, ReactElement, useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, useController } from 'react-hook-form';
import { Box, makeStyles } from '@material-ui/core';
import { toast } from 'react-toastify';
import { YBButtonGroup, YBLabel, YBInputField } from '../../../../../../components';
import { UniverseFormContext } from '../../../UniverseFormContainer';
import { UniverseFormData } from '../../../utils/dto';
import { getPrimaryCluster } from '../../../../../../../utils/universeUtilsTyped';
import { REPLICATION_FACTOR_FIELD, TOAST_AUTO_DISMISS_INTERVAL } from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';

interface ReplicationFactorProps {
  disabled: boolean;
  isPrimary: boolean;
  isEditMode: boolean;
}

const useStyles = makeStyles((theme) => ({
  overrideMuiInput: {
    '& .MuiInput-root': {
      minWidth: '80px'
    }
  }
}));

const PRIMARY_RF = [1, 3, 5, 7];
const ASYNC_RF_MIN = 1;
const ASYNC_RF_MAX = 15;

export const ReplicationFactor = ({
  disabled,
  isPrimary,
  isEditMode
}: ReplicationFactorProps): ReactElement => {
  const { universeConfigureTemplate } = useContext(
    UniverseFormContext
  )[0];
  const clusters = universeConfigureTemplate?.clusters;
  const primaryCluster = getPrimaryCluster(clusters);
  const replicationFactor = primaryCluster?.userIntent?.replicationFactor;

  const { control, setValue } = useFormContext<UniverseFormData>();
  const [initialRF, setInitialRF] = useState<number>(ASYNC_RF_MIN);
  const { t } = useTranslation();
  const classes = useStyles();
  const fieldClasses = useFormFieldStyles();

  const {
    field: { value }
  } = useController({
    name: REPLICATION_FACTOR_FIELD
  });

  useEffect(() => {
    setInitialRF(replicationFactor!);
  }, []);

  const handleSelect = (val: number) => {
    setValue(REPLICATION_FACTOR_FIELD, val);
  };

  const handleChange = (e: ChangeEvent<HTMLInputElement>) => {
    //reset field value
    const fieldValue = parseInt(e.target.value);

    if (!fieldValue || fieldValue < ASYNC_RF_MIN) {
      setValue(REPLICATION_FACTOR_FIELD, ASYNC_RF_MIN, { shouldValidate: true });
      toast.error(t('universeForm.cloudConfig.minRFValue', { rfValue: ASYNC_RF_MIN }), {
        autoClose: TOAST_AUTO_DISMISS_INTERVAL
      });
    } else if (fieldValue > ASYNC_RF_MAX) {
      setValue(REPLICATION_FACTOR_FIELD, ASYNC_RF_MAX, { shouldValidate: true });
      toast.error(t('universeForm.cloudConfig.maxRFvalue', { rfValue: ASYNC_RF_MAX }), {
        autoClose: TOAST_AUTO_DISMISS_INTERVAL
      });
    } else {
      setValue(REPLICATION_FACTOR_FIELD, fieldValue, { shouldValidate: true });
    }
  };

  return (
    <Box width="100%" display="flex" data-testid="ReplicationFactor-Container">
      <YBLabel dataTestId="ReplicationFactor-Label">
        {isPrimary
          ? t('universeForm.cloudConfig.replicationField')
          : t('universeForm.cloudConfig.numReadReplicas')}
      </YBLabel>
      <Box flex={1} className={fieldClasses.defaultTextBox}>
        {isPrimary ? (
          <YBButtonGroup
            dataTestId={'ReplicationFactor'}
            variant={'contained'}
            color={'default'}
            values={PRIMARY_RF}
            selectedNum={value}
            disabled={disabled}
            handleSelect={handleSelect}
            shouldDisableButtonFn={(buttonValue: number) => isEditMode && buttonValue < initialRF}
          />
        ) : (
          <Box>
            <YBInputField
              control={control}
              name={REPLICATION_FACTOR_FIELD}
              type="number"
              inputProps={{
                'data-testid': 'ReplicationFactor-Input',
                min: ASYNC_RF_MIN,
                max: ASYNC_RF_MAX
              }}
              disabled={disabled}
              className={classes.overrideMuiInput}
              onChange={handleChange}
            />
          </Box>
        )}
      </Box>
    </Box>
  );
};
