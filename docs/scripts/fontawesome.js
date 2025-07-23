const { execSync } = require('child_process');
const path = require('path');
const fs = require('fs');

const npmrcPath = path.resolve(__dirname, '../.npmrc');

if (!process.env.FONTAWESOME_PRO_AUTH_TOKEN) {
  const dotenv = require('dotenv');
  if (!dotenv) {
    console.warn('\nWARNING: dotenv package is not installed.');
    process.exit(0);
  }

  dotenv.config({ path: path.resolve(__dirname, '../.env') });
  if (!process.env.FONTAWESOME_PRO_AUTH_TOKEN) {
    console.warn('\nWARNING: FONTAWESOME_PRO_AUTH_TOKEN is not set.');
    console.warn('Please set the FONTAWESOME_PRO_AUTH_TOKEN environment variable to install Font Awesome Pro.');
    process.exit(0);
  }
}

try {
  const FONTAWESOME_VERSION = '6.7.2';
  const npmrcContent = `@fortawesome:registry=https://npm.fontawesome.com/\n//npm.fontawesome.com/:_authToken=${process.env.FONTAWESOME_PRO_AUTH_TOKEN}`;

  fs.writeFileSync(npmrcPath, npmrcContent);

  console.log('.npmrc file created/updated with Font Awesome token.');
  console.log('Attempting to install @fortawesome/fontawesome-pro...');

  execSync(`npm install @fortawesome/fontawesome-pro@${FONTAWESOME_VERSION} --prefix ./ --no-save`, { stdio: 'inherit' });
  console.log('Successfully installed @fortawesome/fontawesome-pro.');
} catch (error) {
  console.error('\nERROR: Failed to install @fortawesome/fontawesome-pro.');
  console.error(error.message);
  process.exit(0);
} finally {
  fs.unlinkSync(npmrcPath);
  console.log('Remove .npmrc file.');
}
