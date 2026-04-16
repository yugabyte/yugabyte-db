import { Trans } from 'react-i18next';
import { makeStyles } from '@material-ui/core';
import { AlertVariant, YBAlert } from '@app/redesign/components';
import { getUnSupportedTableSpaceConfig } from '../../RestoreUtils';
import { EXTERNAL_LINKS } from '@app/components/queries/helpers/constants';
import { PreflightResponseV2Params } from '../../api/api';
import { PreflightResponseParams } from '@app/components/backupv2/components/restore/api';

interface TablespaceWarningsProps {
  preflightResponse: PreflightResponseV2Params | PreflightResponseParams;
}

const useStyles = makeStyles((theme) => ({
  root: {
    display: 'flex',
    flexDirection: 'column',
    gap: '8px',
    '& .MuiBox-root': {
      alignSelf: 'flex-start'
    }
  },
  link: {
    color: theme.palette.ybacolors.ybDarkGray,
    textDecoration: 'underline'
  }
}));

export const TablespaceWarnings = ({ preflightResponse }: TablespaceWarningsProps) => {
  const classes = useStyles();

  if (!preflightResponse) return null;

  const hasUnsupportedTablespaces = getUnSupportedTableSpaceConfig(
    preflightResponse,
    'unsupportedTablespaces'
  );
  const hasConflictingTablespaces = getUnSupportedTableSpaceConfig(
    preflightResponse,
    'conflictingTablespaces'
  );

  if (!hasUnsupportedTablespaces && !hasConflictingTablespaces) return null;

  return (
    <div className={classes.root}>
      {hasUnsupportedTablespaces && (
        <YBAlert
          open
          variant={AlertVariant.Warning}
          text={
            <Trans
              i18nKey="backup.restore.target.tablespacePlacementConflict"
              components={{
                b: <b />,
                a: (
                  <a
                    className={classes.link}
                    href={`/logs?queryRegex=${preflightResponse.loggingID}`}
                    rel="noreferrer"
                    target="_blank"
                  ></a>
                ),
                a2: (
                  <a
                    className={classes.link}
                    href={EXTERNAL_LINKS.TABLESPACE_RESTORE_PREREQUISITES}
                    rel="noreferrer"
                    target="_blank"
                  ></a>
                )
              }}
            />
          }
        />
      )}
      {hasConflictingTablespaces && (
        <YBAlert
          open
          variant={AlertVariant.Warning}
          text={
            <Trans
              i18nKey="backup.restore.target.tablespaceNameConflict"
              components={{
                a: (
                  <a
                    className={classes.link}
                    href={`/logs?queryRegex=${preflightResponse.loggingID}`}
                    rel="noreferrer"
                    target="_blank"
                  ></a>
                ),
                b: <b />
              }}
            />
          }
        />
      )}
    </div>
  );
};
