import React, { FC } from "react";
import { Typography, makeStyles, Box, styled } from "@material-ui/core";
import CheckIcon from "@app/assets/check.svg";
import LoadingIcon from "@app/assets/Default-Loading-Circles.svg";
import clsx from "clsx";
import { ToggleButton, ToggleButtonGroup } from "@material-ui/lab";
// import { CLUSTER_STATE } from '@app/features/clusters/list/ClusterCard';

const useStyles = makeStyles((theme) => ({
  container: {
    position: "relative",
    padding: theme.spacing(2, 6),
    margin: theme.spacing(0, 2),
  },
  wrapper: {
    display: "flex",
    justifyContent: "space-between",
  },
  line: {
    height: "1px",
    width: "100%",
    backgroundColor: theme.palette.grey[200],
    position: "absolute",
    top: theme.spacing(9),
    left: 0,
  },
  step: {
    position: "relative",
    display: "flex",
    flexDirection: "column",
    textAlign: "center",
    padding: theme.spacing(1),
  },
  icon: {
    height: theme.spacing(4),
    width: theme.spacing(4),
    borderRadius: "50%",
    backgroundColor: theme.palette.grey[300],
    color: theme.palette.grey[900],
    margin: theme.spacing(1, "auto"),
    padding: theme.spacing(0.75, 0),
    fontSize: "15px",
    lineHeight: "18px",
    fontWeight: 700,
  },
  iconWrapper: {
    height: theme.spacing(4),
    width: theme.spacing(4),
    margin: theme.spacing(1, "auto"),
  },
  activeIcon: {
    backgroundColor: theme.palette.primary[600],
    color: theme.palette.common.white,
    viewBox: "3px 3px 18px 18px",
  },
  completedIcon: {
    borderRadius: "50%",
    backgroundColor: theme.palette.success[500],
    color: theme.palette.common.white,
  },
  description: {
    color: theme.palette.grey[600],
  },
  loadingIcon: {
    width: theme.spacing(4),
    height: theme.spacing(4),
  },
  highlight: {
    color: theme.palette.primary["700"],
    fontWeight: 600,
  },
  button: {
    height: "auto",
    border: 0,
    minWidth: "136px",
  },
}));

interface StepperProps {
  steps: string[];
  step?: number;
  runningStep?: number;
  onStepChange?: (step: number) => void;
}

const StyledToggleButtonGroup = styled(ToggleButtonGroup)(({ theme }) => ({
  "& .MuiToggleButtonGroup-grouped": {
    borderRadius: theme.shape.borderRadius,
  },
  "& .MuiToggleButtonGroup-grouped:hover": {
    backgroundColor: theme.palette.grey[100],
    "& div:has(> svg)": {
      background: "transparent",
    },
  },
  "& .MuiToggleButtonGroup-grouped.Mui-selected": {
    backgroundColor: theme.palette.grey[100],
    color: "black",
  },
}));

export const ProgressStepper: FC<StepperProps> = ({
  steps,
  step: currentStep,
  onStepChange,
  runningStep,
}) => {
  const classes = useStyles();

  const completedCheckIcon = (
    <div className={classes.iconWrapper}>
      <CheckIcon width="32" height="32" className={classes.completedIcon} />
    </div>
  );

  const getStepNumberIcon = (step: number) => {
    if (runningStep === step) {
      return (
        <Box mx="auto" my={1} bgcolor={currentStep === step ? "transparent" : "white"} px={0.5}>
          <LoadingIcon className={classes.loadingIcon} />
        </Box>
      );
    }
    return <span className={classes.icon}>{(step + 1).toString()}</span>;
  };

  return (
    <div className={classes.container}>
      <div className={classes.line}></div>
      <StyledToggleButtonGroup value={currentStep} className={classes.wrapper} exclusive>
        {steps.map((step, index) => (
          <ToggleButton
            value={index}
            onClick={() => onStepChange && onStepChange(index)}
            className={classes.button}
            key={index}
          >
            <Box className={classes.step}>
              <Typography
                variant="button"
                className={clsx(runningStep === index && classes.highlight)}
              >
                {step}
              </Typography>
              {runningStep !== undefined && runningStep > index
                ? completedCheckIcon
                : getStepNumberIcon(index)}
              {/* <Typography variant="subtitle1" className={classes.description}>
            {t('clusterDetail.voyager.exportDesc')}
          </Typography> */}
            </Box>
          </ToggleButton>
        ))}
      </StyledToggleButtonGroup>
    </div>
  );
};
