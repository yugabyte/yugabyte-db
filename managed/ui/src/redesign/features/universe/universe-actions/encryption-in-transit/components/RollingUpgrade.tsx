import { FC, FocusEvent } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBInputField, YBRadioGroupField, YBModal } from '../../../../../components';
import {
  ROLLING_UPGRADE_DELAY_FIELD_NAME,
  ROLLING_UPGRADE_OPTION_FIELD_NAME,
  EncryptionInTransitFormValues,
  UpgradeOptions,
  useEITStyles
} from '../EncryptionInTransitUtils';

interface RollingUpgradeModalProps {
  open: boolean;
  onClose: () => void;
  onSubmit: () => void;
}

export const RollingUpgrade: FC<RollingUpgradeModalProps> = ({ open, onClose, onSubmit }) => {
  const { t } = useTranslation();
  const classes = useEITStyles();
  const { control, setValue, watch } = useFormContext<EncryptionInTransitFormValues>();

  const upgradeOption = watch(ROLLING_UPGRADE_OPTION_FIELD_NAME);
  const isRollingUpgrade = upgradeOption === UpgradeOptions.Rolling;

  const UPDATE_OPTIONS = [
    {
      value: UpgradeOptions.Rolling,
      label: (
        <Box display="flex" flexDirection="column" alignItems="baseline">
          <div className="upgrade-radio-label"> {t('universeForm.gFlags.rollingMsg')}</div>
          <div className="gflag-delay">
            <span className="vr-line">|</span>
            {t('universeForm.gFlags.delayBetweenServers')}&nbsp;
            <YBInputField
              name={ROLLING_UPGRADE_DELAY_FIELD_NAME}
              type="number"
              control={control}
              data-testid="UpgradeDelay-Input"
              disabled={!isRollingUpgrade}
              style={{ width: 100 }}
              onBlur={(e: FocusEvent<HTMLInputElement>) => {
                const inputVal = (e.target.value as unknown) as number;
                inputVal <= 0 && setValue(ROLLING_UPGRADE_DELAY_FIELD_NAME, 240);
              }}
            />
            &nbsp;
            {t('universeForm.gFlags.seconds')}
          </div>
        </Box>
      )
    },
    {
      value: UpgradeOptions.NonRolling,
      label: (
        <Box className="upgrade-radio-label" mb={1}>
          {t('universeForm.gFlags.nonRollingMsg')}
        </Box>
      )
    },
    {
      value: UpgradeOptions.NonRestart,
      label: (
        <Box display="flex" className="upgrade-radio-label" mb={1}>
          {t('universeForm.gFlags.nonRestart')}
        </Box>
      )
    }
  ];

  return (
    <YBModal
      open={open}
      overrideHeight={'340px'}
      overrideWidth={'650px'}
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.apply')}
      title={t('universeActions.encryptionInTransit.selectUpgradeoption')}
      size="sm"
      onClose={onClose}
      onSubmit={onSubmit}
      submitTestId="RollUpgrade-Submit"
      cancelTestId="RollUpgrade-Close"
    >
      <Box display="flex" width="100%" className="gflag-upgrade-container">
        <div className="gflag-upgrade-options">
          <YBRadioGroupField
            name={ROLLING_UPGRADE_OPTION_FIELD_NAME}
            options={UPDATE_OPTIONS}
            control={control}
            orientation="vertical"
            className={classes.updateOptions}
          />
        </div>
      </Box>
    </YBModal>
  );
};
