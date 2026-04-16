import clsx from 'clsx';
import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';

import { YBButton, YBTooltip } from '../../../redesign/components';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { ReactComponent as BackupIcon } from '../../../redesign/assets/fileBackup.svg';
import { DOCS_URL_ACTIVE_ACTIVE_SINGLE_MASTER } from './constants';

interface EnableDrPromptProps {
  isDisabled: boolean;
  onConfigureDrButtonClick: () => void;
  universeUUID: string;
  className?: string;
}

const useStyles = makeStyles((theme) => ({
  promptContainer: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    flexDirection: 'column',
    gap: theme.spacing(3),

    padding: `${theme.spacing(10)}px ${theme.spacing(2)}px`,

    border: `1px dashed ${theme.palette.ybacolors.ybBorderGrayDark}`,
    borderRadius: '8px',
    background: theme.palette.ybacolors.backgroundGrayRegular
  },
  promptPrimaryText: {
    maxWidth: '550px',
    textAlign: 'center'
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.enableDrPrompt';
export const EnableDrPrompt = ({
  className,
  isDisabled,
  onConfigureDrButtonClick,
  universeUUID
}: EnableDrPromptProps) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });

  return (
    <div className={clsx(classes.promptContainer, className)}>
      <BackupIcon />
      <Typography className={classes.promptPrimaryText} variant="body2">
        <Trans
          i18nKey={`${TRANSLATION_KEY_PREFIX}.featureDescription`}
          components={{ bold: <b /> }}
        />
      </Typography>
      <RbacValidator
        accessRequiredOn={{
          onResource: universeUUID,
          ...ApiPermissionMap.CREATE_DR_CONFIG
        }}
        isControl
      >
        <YBTooltip
          title={isDisabled ? t('tooltip.universeLinkedToTxnXCluster') : ''}
          placement="top"
        >
          <span>
            <YBButton
              style={{ minWidth: '200px' }}
              variant="primary"
              onClick={onConfigureDrButtonClick}
              disabled={isDisabled}
              data-testid={`EnableDrPrompt-ConfigureDrButton`}
            >
              {t('actionButton')}
            </YBButton>
          </span>
        </YBTooltip>
      </RbacValidator>
      <Typography variant="body2">
        <Trans
          i18nKey={`${TRANSLATION_KEY_PREFIX}.learnMore`}
          components={{
            docsLink: (
              <a
                href={DOCS_URL_ACTIVE_ACTIVE_SINGLE_MASTER}
                target="_blank"
                rel="noopener noreferrer"
              />
            )
          }}
        />
      </Typography>
    </div>
  );
};
