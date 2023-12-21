import { FC } from 'react';
import _ from 'lodash';
import { toast } from 'react-toastify';
import { useMutation } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useForm, FormProvider } from 'react-hook-form';
import { Box, Typography } from '@material-ui/core';
import { YBModal, YBCheckboxField, YBInputField, YBLabel } from '../../../../components';
import { api } from '../../../../utils/api';
import { createErrorMessage, transitToUniverse } from '../../universe-form/utils/helpers';
import { Universe } from '../../universe-form/utils/dto';
import { TOAST_AUTO_DISMISS_INTERVAL } from '../../universe-form/utils/constants';
import { DBRollbackFormFields, UPGRADE_TYPE, DBRollbackPayload } from './utils/types';
import { dbUpgradeFormStyles } from './utils/RollbackUpgradeStyles';
//icons
import { ReactComponent as ClockRewindIcon } from '../../../../assets/clock-rewind.svg';

interface DBRollbackModalProps {
  open: boolean;
  onClose: () => void;
  universeData: Universe;
}
const TOAST_OPTIONS = { autoClose: TOAST_AUTO_DISMISS_INTERVAL };

export const DBRollbackModal: FC<DBRollbackModalProps> = ({ open, onClose, universeData }) => {
  const { t } = useTranslation();
  const classes = dbUpgradeFormStyles();
  const { universeDetails, universeUUID } = universeData;
  const prevVersion = _.get(universeDetails, 'prevYBSoftwareConfig.softwareVersion', '');
  const formMethods = useForm<DBRollbackFormFields>({
    defaultValues: {
      rollingUpgrade: true,
      timeDelay: 180
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const { control, handleSubmit, watch } = formMethods;
  const isRollingUpgrade = watch('rollingUpgrade');

  //rollback upgrade
  const rollingUpgrade = useMutation(
    (values: DBRollbackPayload) => {
      return api.rollbackUpgrade(universeUUID, values);
    },
    {
      onSuccess: () => {
        toast.success('Rollback form submitted successfully', TOAST_OPTIONS);
        transitToUniverse(universeUUID);
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error), TOAST_OPTIONS);
      }
    }
  );

  const handleFormSubmit = handleSubmit(async (values) => {
    const payload: DBRollbackPayload = {
      sleepAfterMasterRestartMillis: Number(values.timeDelay) * 1000,
      sleepAfterTServerRestartMillis: Number(values.timeDelay) * 1000,
      upgradeOption: values.rollingUpgrade ? UPGRADE_TYPE.ROLLING : UPGRADE_TYPE.NON_ROLLING
    };
    try {
      await rollingUpgrade.mutateAsync(payload);
    } catch (e) {
      console.log(e);
    }
  });

  return (
    <YBModal
      open={open}
      overrideHeight={'400px'}
      overrideWidth={'610px'}
      cancelLabel={t('common.cancel')}
      submitLabel={t('universeActions.dbRollbackUpgrade.proceedRollback')}
      title={t('universeActions.dbRollbackUpgrade.rollbackUpgradeTite')}
      size="sm"
      onClose={onClose}
      titleSeparator
      onSubmit={handleFormSubmit}
      submitTestId="RollBackUpgrade-Submit"
      cancelTestId="RollBackUpgrade-Back"
      titleIcon={<ClockRewindIcon />}
    >
      <FormProvider {...formMethods}>
        <Box
          display="flex"
          width="100%"
          height="100%"
          flexDirection={'column'}
          style={{ padding: '16px 20px 16px 8px' }}
        >
          <Typography variant="body2">
            {t('universeActions.dbRollbackUpgrade.rollbackModalMsg')}
            <b>
              {t('universeActions.dbRollbackUpgrade.version')} {prevVersion}?
            </b>
          </Typography>
          <Box className={classes.upgradeDetailsContainer}>
            <YBCheckboxField
              name={'rollingUpgrade'}
              label={t('universeActions.dbRollbackUpgrade.rollbackOnenode')}
              control={control}
              style={{ width: 'fit-content' }}
              inputProps={{
                'data-testid': 'RotateUniverseKey-Checkbox'
              }}
            />
            <Box
              display={'flex'}
              flexDirection={'row'}
              mt={1}
              ml={1}
              width="100%"
              alignItems={'center'}
            >
              <YBLabel width="170px">
                {t('universeActions.dbRollbackUpgrade.delayBwRollback')}
              </YBLabel>
              <Box width="160px" mr={1}>
                <YBInputField
                  control={control}
                  type="number"
                  name="timeDelay"
                  fullWidth
                  disabled={!isRollingUpgrade}
                  inputProps={{
                    autoFocus: true,
                    'data-testid': 'time-delay'
                  }}
                />
              </Box>
              <Typography variant="body2">{t('common.seconds')}</Typography>
            </Box>
          </Box>
        </Box>
      </FormProvider>
    </YBModal>
  );
};
