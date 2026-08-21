import { makeStyles, Typography } from '@material-ui/core';
import { YBTooltip } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';

import { isNonEmptyString } from '@app/utils/ObjectUtils';
import { ReleaseOption } from './types';

import InfoIcon from '@app/redesign/assets/info-message.svg';

const MAX_RELEASE_TAG_CHAR = 20;

const useStyles = makeStyles((theme) => ({
  releaseTagContainer: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1)
  },
  releaseTagBadge: {
    padding: `${theme.spacing(0.5)}px ${theme.spacing(0.75)}px`,

    borderRadius: 6,
    backgroundColor: theme.palette.grey[200]
  }
}));

export interface DbReleaseAutocompleteOptionProps {
  releaseOption: ReleaseOption;
  currentDbVersion: string;
}

export const DbReleaseAutocompleteOption = ({
  releaseOption,
  currentDbVersion
}: DbReleaseAutocompleteOptionProps) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.upgradeModal.dbVersionStep'
  });

  const releaseTag = releaseOption?.releaseInfo?.release_tag ?? '';
  const releaseTagExceedsMaxLength = releaseTag.length > MAX_RELEASE_TAG_CHAR;

  return (
    <div className={classes.releaseTagContainer}>
      {releaseOption?.version === currentDbVersion
        ? `${releaseOption?.label} (${t('currentVersion').toLocaleLowerCase()})`
        : releaseOption?.label}
      {isNonEmptyString(releaseOption?.releaseInfo?.release_tag) && (
        <>
          <Typography variant="body2" className={classes.releaseTagBadge}>
            {releaseTagExceedsMaxLength
              ? `${releaseTag.substring(0, MAX_RELEASE_TAG_CHAR)}...`
              : releaseTag}
          </Typography>
          {releaseTagExceedsMaxLength && (
            <YBTooltip title={releaseTag}>
              <span>
                <InfoIcon aria-hidden />
              </span>
            </YBTooltip>
          )}
        </>
      )}
    </div>
  );
};
