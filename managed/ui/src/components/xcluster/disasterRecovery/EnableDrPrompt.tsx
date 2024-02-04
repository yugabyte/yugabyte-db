import clsx from 'clsx';
import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';

import { YBButton, YBTooltip } from '../../../redesign/components';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { ReactComponent as BackupIcon } from '../../../redesign/assets/fileBackup.svg';

interface EnableDrPromptProps {
  isDisabled: boolean;
  onConfigureDrButtonClick: () => void;

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
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.enableDrPrompt';
const DOCS_URL_ACTIVE_ACTIVE_SINGLE_MASTER =
  'https://docs.yugabyte.com/preview/develop/build-global-apps/active-active-single-master/';
export const EnableDrPrompt = ({
  className,
  isDisabled,
  onConfigureDrButtonClick
}: EnableDrPromptProps) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });

  return (
    <div className={clsx(classes.promptContainer, className)}>
      <BackupIcon />
      <Typography variant="body2">
        <Trans
          i18nKey={`${TRANSLATION_KEY_PREFIX}.featureDescription`}
          components={{ bold: <b /> }}
        />
      </Typography>
      <RbacValidator accessRequiredOn={ApiPermissionMap.CREATE_DR_CONFIG} isControl>
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
          components={{ docsLink: <a href={DOCS_URL_ACTIVE_ACTIVE_SINGLE_MASTER} /> }}
        />
      </Typography>
    </div>
  );
};
