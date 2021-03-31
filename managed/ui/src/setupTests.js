import '@testing-library/jest-dom';

// monkey patch console.warn() to hide nasty lifecycle deprecation warnings in console
const originalWarn = console.warn;
const pattern = 'https://fb.me/react-unsafe-component-lifecycles';
console.warn = (...args) => {
  const isLifecycleDeprecationWarning = args.some(item => item.includes(pattern));
  if (!isLifecycleDeprecationWarning) {
    originalWarn(...args);
  }
};
