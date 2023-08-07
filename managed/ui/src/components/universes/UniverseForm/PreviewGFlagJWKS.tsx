import { FC } from 'react';
import { Button, Tooltip, OverlayTrigger } from 'react-bootstrap';
import { Box, makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import MoreIcon from '../../../redesign/assets/ellipsis.svg';
import Close from '../../../redesign/assets/close.svg';
import Edit from '../../../redesign/assets/edit_pen.svg';

interface PreviewGFlagJWKSProps {
  JWKSKeyset: string;
  rowIndex: number;
  openJWKSDialog: (index: number) => void;
  removeJWKSToken: (index: number) => void;
}

const useStyles = makeStyles(() => ({
  JWKSKeyBox: {},
  previewContainer: {
    backgroundColor: '#ebebeb',
    width: '460px'
  }
}));

export const PreviewGFlagJWKS: FC<PreviewGFlagJWKSProps> = ({
  JWKSKeyset,
  rowIndex,
  openJWKSDialog,
  removeJWKSToken
}) => {
  const { t } = useTranslation();
  const classes = useStyles();

  return (
    // Reusing class names from UniverseForm.scss
    <Box className={clsx('table-val-column', classes.previewContainer)}>
      <Box className="cell-font">{JWKSKeyset}</Box>
      <Box className="icons">
        <Box className="more-icon">
          <span className={'flag-icon-button mb-2'}>
            <img alt="More" src={MoreIcon} width="20" />
          </span>
        </Box>
        <Box className="flag-icons">
          <OverlayTrigger
            placement="top"
            overlay={
              <Tooltip className="high-index" id="edit-flag">
                {t('universeForm.gFlags.editFlag')}
              </Tooltip>
            }
          >
            <Button bsClass="flag-icon-button mr-10 mb-2" onClick={() => openJWKSDialog(rowIndex)}>
              <img alt="Edit" src={Edit} width="20" />
            </Button>
          </OverlayTrigger>
          &nbsp;
          <OverlayTrigger
            placement="top"
            overlay={
              <Tooltip className="high-index" id="edit-flag">
                {t('universeForm.gFlags.editFlag')}
              </Tooltip>
            }
          >
            <Button bsClass="flag-icon-button mr-10 mb-2" onClick={() => removeJWKSToken(rowIndex)}>
              <img alt="Close" src={Close} width="20" />
            </Button>
          </OverlayTrigger>
        </Box>
        &nbsp;
      </Box>
    </Box>
  );
};
