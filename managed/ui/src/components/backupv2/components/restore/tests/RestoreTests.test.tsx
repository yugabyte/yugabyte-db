/*
 * Created on Mon Jul 03 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

/////////// WIP: IN PROGRESS

// import React from 'react';
// import { render } from '../../../../../test-utils';
// import { MuiThemeProvider } from '@material-ui/core';
// import { testBackupDetail } from './TestValue';
// import { mainTheme } from '../../../../../redesign/theme/mainTheme';
// import { useTranslation } from 'react-i18next';
// import {
//   RestoreContext,
//   RestoreContextMethods,
//   RestoreFormContext,
//   initialRestoreContextState,
//   restoreMethods
// } from '../RestoreContext';
// import { useMethods } from 'react-use';
// import { act, renderHook } from '@testing-library/react-hooks';
// import SwitchRestoreContextPages from '../SwitchRestoreContextPages';

// jest.mock('react-i18next', () => {
//   return {
//     withTranslation: (x: any) => (y: any) => y,
//     Trans: ({ children, i18nKey }: { children: any; i18nKey: any }) =>
//       children ? children : i18nKey,
//     useTranslation: () => {
//       return {
//         t: (str: any) => str,
//         i18n: {
//           changeLanguage: () => new Promise(() => {})
//         }
//       };
//     }
//   };
// });

// const mockData: RestoreContext = {
//   ...initialRestoreContextState,
//   backupDetails: testBackupDetail as any
// };

// const Setup = (restoreContext: RestoreContextMethods) => {
//   const { t } = useTranslation();

//   const component = render(
//     <MuiThemeProvider theme={mainTheme}>
//       <RestoreFormContext.Provider value={restoreContext as any}>
//         <SwitchRestoreContextPages />
//       </RestoreFormContext.Provider>
//     </MuiThemeProvider>
//   );

//   return { component, t };
// };

// describe('New Restore Modal tests', () => {
//   it('should render', async () => {
//     const { result } = renderHook(() => useMethods(restoreMethods, mockData));
//     const { result: render } = renderHook(() => Setup(result.current as any));
//     const { component, t } = render.current;
//     expect(
//       component.getByText(t('newRestoreModal.generalSettings.universeSelection.title') as any)
//     ).toBeInTheDocument();
//   });

//   it('should display general settings as first page', () => {
//     const { result } = renderHook(() => useMethods(restoreMethods, mockData));
//     const [
//       {
//         formProps: { currentPage }
//       }
//     ] = result.current;
//     expect(currentPage).toEqual('GENERAL_SETTINGS');
//   });
//   it('should navigate to correct page', async () => {
//     const { result } = renderHook(() => useMethods(restoreMethods, mockData));

//     act(() => {
//       result.current[1].moveToPage('RENAME_KEYSPACES');
//     });
//     expect(result.current[0].formProps.currentPage).toEqual('RENAME_KEYSPACES');

//     act(() => {
//       result.current[1].moveToPage('SELECT_TABLES');
//     });

//     const {
//       result: {
//         current: { component }
//       }
//     } = renderHook(() => Setup(result.current as any));
//     expect(component.getByText('Head')).toBeInTheDocument();
//   });
// });

describe('WIP', () => {
  it('wip', () => {
    expect(true).toBeTruthy();
  });
});
