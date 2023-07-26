import { FC, useState } from 'react';
import { makeStyles, Box, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { EditGFlagsConf } from './EditGFlagsConf';
import { PreviewGFlagsConf } from './PreviewGFlagsConf';
import { YBButton } from '../../../redesign/components';

interface GFlagConfProps {
  formProps: any;
  mode: string;
  serverType: string;
}

export const GFlagMultilineMode = {
  EDIT: 'edit',
  PREVIEW: 'preview'
} as const;
export type GFlagMultilineMode = typeof GFlagMultilineMode[keyof typeof GFlagMultilineMode];

export const useStyles = makeStyles(() => ({
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
  }
}));

export const GFlagsConf: FC<GFlagConfProps> = ({ formProps, mode, serverType }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const [currentView, setCurentView] = useState<GFlagMultilineMode>(GFlagMultilineMode.EDIT);

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
        {
          'Input each record as a new row, then reorder them. The sequential order of the records are significant as they are searched serially for every connection request.'
        }
      </Box>
      <Box mt={2}>
        {currentView === GFlagMultilineMode.EDIT && (
          <EditGFlagsConf formProps={formProps} mode={mode} serverType={serverType} />
        )}
        {currentView === GFlagMultilineMode.PREVIEW && (
          <PreviewGFlagsConf formProps={formProps} serverType={serverType} />
        )}
      </Box>
    </>
  );
};
