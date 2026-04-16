import { makeStyles, Typography } from '@material-ui/core';
import { YBTooltip } from '@yugabyte-ui-library/core';

import { isNonEmptyString } from '../../../../../utils/ObjectUtils';
import { ReleaseOption } from './types';

import InfoIcon from '../../../../assets/info-message.svg?img';

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
}

export const DbReleaseAutocompleteOption = ({
  releaseOption
}: DbReleaseAutocompleteOptionProps) => {
  const classes = useStyles();

  const releaseTag = releaseOption?.releaseInfo?.release_tag ?? '';
  const releaseTagExceedsMaxLength = releaseTag.length > MAX_RELEASE_TAG_CHAR;

  return (
    <div className={classes.releaseTagContainer}>
      {releaseOption?.version}
      {isNonEmptyString(releaseOption?.releaseInfo?.release_tag) && (
        <>
          <Typography variant="body2" className={classes.releaseTagBadge}>
            {releaseTagExceedsMaxLength
              ? `${releaseTag.substring(0, MAX_RELEASE_TAG_CHAR)}...`
              : releaseTag}
          </Typography>
          {releaseTagExceedsMaxLength && (
            <YBTooltip title={releaseTag}>
              <img src={InfoIcon} alt="info" />
            </YBTooltip>
          )}
        </>
      )}
    </div>
  );
};
