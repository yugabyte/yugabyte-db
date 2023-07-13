import { CACert } from './ICerts';

export const downloadCert = (cert: CACert) => {
  const element = document.createElement('a');
  element.setAttribute(
    'href',
    'data:text/plain;charset=utf-8,' + encodeURIComponent(cert.contents)
  );
  element.setAttribute('download', cert.name + '.crt');

  element.style.display = 'none';
  document.body.appendChild(element);
  element.click();
  document.body.removeChild(element);
};
