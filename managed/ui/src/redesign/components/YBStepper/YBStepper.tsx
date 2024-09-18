/*
 * Created on Wed Jul 17 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */


import { FC } from 'react';
import clsx from 'clsx';
import { keys, size } from 'lodash';
import { makeStyles } from '@material-ui/core';
import LeftArrow from '../../assets/chevron-right.svg';
import Check from '../../assets/check-white.svg';

type YBStepperProps = {
    steps: Record<string, string>;
    currentStep: string;
    className?: any;
};


const useStyles = makeStyles((theme) => ({
    root: {
        display: 'flex',
        gap: theme.spacing(2),
        userSelect: 'none'
    },
    step: {
        display: 'flex',
        alignItems: 'center',
        gap: theme.spacing(2),
        fontSize: '11.5px',
        fontWeight: 500,
        textTransform: 'uppercase',
        '& svg,img': {
            width: theme.spacing(3),
            height: theme.spacing(3)
        },
        color: theme.palette.grey[600]
    },
    stepCount: {
        background: theme.palette.grey[300],
        color: theme.palette.grey[600],
        width: theme.spacing(4),
        height: theme.spacing(4),
        borderRadius: theme.spacing(4),
        fontSize: '15px',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center'
    },
    stepActive: {
        background: theme.palette.primary[600],
        color: theme.palette.common.white
    },
    stepActiveTitle: {
        color: theme.palette.grey[900]
    },
    stepCompleted: {
        background: theme.palette.success[500],
    }
}));

export const YBStepper: FC<YBStepperProps> = ({ className, steps, currentStep }) => {

    const classes = useStyles();

    const currentPageIndex = keys(steps).indexOf(currentStep);

    const getStepIndicationComponent = (pageIndex: number) => {
        if (pageIndex < currentPageIndex) {
            // if page is completed, show check mark
            return (
                <div className={clsx(classes.stepCount, classes.stepCompleted)}>
                    <img alt="completed" src={Check} />
                </div>
            );
        }
        return (
            // show count
            <div
                className={clsx(classes.stepCount, pageIndex === currentPageIndex && classes.stepActive)}
            >
                {pageIndex + 1}
            </div>
        );
    };

    return (
        <div className={clsx(classes.root, className)}>
            {keys(steps).map((s, index) => (
                <div className={clsx(classes.step, index === currentPageIndex && classes.stepActiveTitle)} key={s}>
                    {getStepIndicationComponent(index)}
                    {steps[s]}
                    {index !== size(steps) - 1 && <img alt="next" src={LeftArrow} />}
                </div>
            ))}
        </div>
    );
};
