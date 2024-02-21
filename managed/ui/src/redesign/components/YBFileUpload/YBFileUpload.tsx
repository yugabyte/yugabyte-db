import React, { FC, useRef, useState } from 'react';
import { Box, makeStyles, Typography } from '@material-ui/core';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { YBInputProps, YBInput } from '../YBInput/YBInput';
import { YBButton } from '../YBButton/YBButton';
import { getMemorySizeUnits } from '../../helpers/utils';
import { YBProgress } from '../YBProgress/YBProgress';

import { ReactComponent as UploadIcon } from '../../assets/upload.svg';
import { ReactComponent as CloseIcon } from '../../assets/close-large.svg';
import Checked from '../../assets/check-new.svg';

const useStyles = makeStyles((theme) => ({
  fileItem: {
    width: '100%',
    height: 40,
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    padding: theme.spacing(1, 1.5),
    borderRadius: theme.shape.borderRadius,
    border: `1px solid ${theme.palette.grey[300]}`
  },
  actions: {
    display: 'flex',
    alignItems: 'center'
  },
  statusBox: {
    border: '1px',
    borderRadius: '6px',
    padding: '4px 6px 4px 6px',
    backgroundColor: theme.palette.success[100],
    marginRight: theme.spacing(1)
  },
  statusText: {
    fontFamily: 'Inter',
    fontWeight: 400,
    fontSize: '11.5px'
  },
  closeIcon: {
    cursor: 'pointer'
  },
  error: {
    color: theme.palette.error[500],
    border: `1px solid ${theme.palette.error[500]}`,
    backgroundColor: theme.palette.error[100]
  },
  overirdeMuiButton: {
    border: '1px solid #2B59C3'
  },
  overirdeMuiButtonUploaded: {
    border: '1px solid #D7DEE4',
    backgroundColor: '#D7DEE4'
  },
  overirdeMuiButtonLabelUploaded: {
    color: '#97A5B0',
    fontWeight: 600,
    fontSize: '13px'
  },
  overirdeMuiButtonLabel: {
    color: '#2B59C3',
    fontWeight: 600,
    fontSize: '13px'
  },
  overrides: {
    MuiButton: {
      raisedPrimary: {
        color: '#2B59C3'
      }
    }
  }
}));

export type YBFileUploadProps = {
  label: string;
  progressValue?: number;
  isUploaded?: boolean;
  uploadedFileName?: string;
  dataTestId?: string;
  fileList?: File[];
  fileSizeLimit?: number;
} & YBInputProps;

export const YBFileUpload: FC<YBFileUploadProps> = ({
  label,
  isUploaded = false,
  uploadedFileName,
  dataTestId,
  fileList,
  fileSizeLimit,
  progressValue = 0,
  ...props
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const [files, setFiles] = useState<File[]>(fileList ?? []);
  const inputRef = useRef<HTMLInputElement>(null);
  const [displayUploadedFileName, setdisplayUploadFileName] = useState<string | undefined>(
    uploadedFileName
  );
  const [isFileUploaded, setIsFileUploaded] = useState<boolean>(isUploaded);

  const handleChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    if (props.onChange) {
      props.onChange(event);
    }
    const selectedFiles: any = event.target.files ?? [];
    setFiles([...selectedFiles]);
  };

  const removeFile = (file: File) => {
    const event = new Event('change', { bubbles: true });
    const dataTransfer = new DataTransfer();

    const filesList = files.filter((fileItem) => fileItem !== file);

    if (inputRef.current) {
      filesList?.map((fileItem) => dataTransfer.items.add(fileItem));
      inputRef.current.files = dataTransfer.files;
      setFiles([...filesList]);
      inputRef.current.dispatchEvent(event);
    }
  };

  const isValid = (file: File) => {
    if (fileSizeLimit && file.size > fileSizeLimit) {
      return true;
    }
    return false;
  };

  return (
    <>
      <Box>
        <YBButton
          variant="secondary"
          component={('label' as unknown) as React.ComponentType}
          startIcon={<UploadIcon />}
          className={clsx({
            [classes.overirdeMuiButton]: files.length === 0 && !isFileUploaded,
            [classes.overirdeMuiButtonUploaded]: files.length > 0 || isFileUploaded,
            [classes.error]: props.error
          })}
          // className={clsx(props.error && classes.error, classes.overirdeMuiButton)}
          data-testid={dataTestId}
        >
          <span
            className={clsx({
              [classes.overirdeMuiButtonLabel]: files.length === 0 && !isFileUploaded,
              [classes.overirdeMuiButtonLabelUploaded]: files.length > 0 || isFileUploaded
            })}
            // className={classes.overirdeMuiButtonLabel}
          >
            {label}
          </span>
          <Box hidden>
            <YBInput
              {...props}
              value={props.value}
              type="file"
              inputRef={inputRef}
              onChange={handleChange}
            />
          </Box>
        </YBButton>
      </Box>
      {props.error && (
        <Box mt={0.5}>
          <Typography variant="body2" color="error">
            {props.helperText}
          </Typography>
        </Box>
      )}

      {files.length === 0 && isFileUploaded && (
        <Box mt={2} display="flex" flexDirection="column" width="100%">
          <span>
            <Box className={clsx(classes.fileItem)} mb={1}>
              <Box>{displayUploadedFileName}</Box>
              <Box className={clsx(classes.actions)}>
                <Box className={classes.statusBox}>
                  <span className={classes.statusText}>{'Completed'}</span>
                  <img src={Checked} alt="status" />
                </Box>
                <CloseIcon
                  className={classes.closeIcon}
                  onClick={() => {
                    setdisplayUploadFileName(undefined);
                    setIsFileUploaded(false);
                    removeFile(files[0]);
                  }}
                />
              </Box>
            </Box>
          </span>
        </Box>
      )}
      {files.length > 0 && (
        <Box mt={2} display="flex" flexDirection="column" width="100%">
          {Array.from(files)?.map((file: File) => {
            return (
              <span key={file?.name}>
                <Box className={clsx(classes.fileItem, { [classes.error]: isValid(file) })} mb={1}>
                  <Box>{file?.name}</Box>
                  <Box className={clsx(classes.actions)}>
                    <Box>
                      {!isUploaded && (
                        <YBProgress color="primary" value={progressValue}></YBProgress>
                      )}
                    </Box>
                    {isUploaded && (
                      <Box className={classes.statusBox}>
                        <>
                          <span className={classes.statusText}>{'Completed'}</span>
                          <img src={Checked} alt="status" />
                        </>
                      </Box>
                    )}

                    <CloseIcon
                      className={classes.closeIcon}
                      onClick={() => {
                        removeFile(file);
                      }}
                    />
                  </Box>
                </Box>
                {isValid(file) && fileSizeLimit && (
                  <Box mt={-0.25} mb={1} ml={1}>
                    <Typography variant="subtitle1" color="error">
                      {t('common.fileSizeLimitError', {
                        limit: getMemorySizeUnits(fileSizeLimit ?? 0)
                      })}
                    </Typography>
                  </Box>
                )}
              </span>
            );
          })}
        </Box>
      )}
    </>
  );
};
