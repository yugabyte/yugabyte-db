import { FC, useState } from 'react';
import { makeStyles, Box, Typography, Theme } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { EditGFlagsConf } from './EditGFlagsConf';
import { PreviewGFlagsConf } from './PreviewGFlagsConf';
import { YBButton } from '../../../redesign/components';
import { MultilineGFlags } from '../../../utils/UniverseUtils';

interface GFlagConfProps {
  formProps: any;
  mode: string;
  serverType: string;
  flagName: string;
}

const POSTGRES_USERNAME_MAP = 'https://www.postgresql.org/docs/current/auth-username-maps.html';

export const GFlagMultilineMode = {
  EDIT: 'edit',
  PREVIEW: 'preview'
} as const;
export type GFlagMultilineMode = typeof GFlagMultilineMode[keyof typeof GFlagMultilineMode];

export const useStyles = makeStyles((theme: Theme) => ({
  buttons: {
    float: 'right'
  },
  title: {
    float: 'left'
  },
  editButton: {
    borderRadius: '8px 0px 0px 8px'
  },
  previewButton: {
    borderRadius: '0px 8px 8px 0px'
  },
  readMoreText: {
    color: '#44518B'
  },
  redirectText: {
    color: theme.palette.orange[300],
    textDecoration: 'underline'
  }
}));

const GFlagDescription = {
  ysql_hba_conf_csv: 'universeForm.gFlags.hbaConfDescription',
  ysql_ident_conf_csv: 'universeForm.gFlags.identConfDescriptionFirst'
} as const;

const GFlagAdditionalDescription = {
  ysql_ident_conf_csv: 'universeForm.gFlags.identConfDescriptionMore'
} as const;

export const GFlagsConf: FC<GFlagConfProps> = ({ formProps, mode, serverType, flagName }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const [currentView, setCurentView] = useState<GFlagMultilineMode>(GFlagMultilineMode.EDIT);
  const [readMore, setReadMore] = useState<boolean>(false);

  const handleEditClick = () => {
    setCurentView(GFlagMultilineMode.EDIT);
  };

  const handlePreviewClick = () => {
    setCurentView(GFlagMultilineMode.PREVIEW);
  };

  return (
    <>
      <Box>
        <span className={classes.title}>
          <Typography variant="h6">{'Flag Value'}</Typography>
        </span>
        <span className={classes.buttons}>
          <YBButton
            variant="secondary"
            size="small"
            data-testid={`GFlagsConfField-EditButton`}
            onClick={handleEditClick}
            className={classes.editButton}
          >
            {t('universeForm.gFlags.edit')}
          </YBButton>
          <YBButton
            variant="secondary"
            size="small"
            onClick={handlePreviewClick}
            data-testid={`GFlagsConfField-PreviewButton`}
            className={classes.previewButton}
          >
            {t('universeForm.gFlags.preview')}
          </YBButton>
        </span>
      </Box>
      <Box mt={1}>
        <Box mt={1}>
          {t(GFlagDescription[flagName])}
          {flagName === MultilineGFlags.YSQL_IDENT_CONF_CSV && (
            <>
              <a
                href={POSTGRES_USERNAME_MAP}
                target="_blank"
                rel="noopener noreferrer"
                className={classes.redirectText}
              >
                {t('universeForm.gFlags.identConfPostgreSQL')}
              </a>
              {t('universeForm.gFlags.identConfDescriptionSecond')}
            </>
          )}
          {readMore && (
            <span>
              <br />
              <br />
              {t(GFlagAdditionalDescription[flagName])}
            </span>
          )}

          {flagName === MultilineGFlags.YSQL_IDENT_CONF_CSV && (
            <span className={classes.readMoreText} onClick={() => setReadMore(!readMore)}>
              {!readMore ? t('universeForm.gFlags.readMore') : t('universeForm.gFlags.readLess')}
            </span>
          )}
        </Box>
      </Box>
      <Box mt={2}>
        {currentView === GFlagMultilineMode.EDIT && (
          <EditGFlagsConf
            formProps={formProps}
            mode={mode}
            serverType={serverType}
            flagName={flagName}
          />
        )}
        {currentView === GFlagMultilineMode.PREVIEW && (
          <PreviewGFlagsConf formProps={formProps} serverType={serverType} flagName={flagName} />
        )}
      </Box>
    </>
  );
};
