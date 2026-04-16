/**
 * Install pro packages like FA Pro.
 */
const { execSync } = require('child_process');
const path = require('path');
const fs = require('fs');

const npmrcPath = path.resolve(__dirname, '../.npmrc');

/**
 * Check whether the package is installed or not.
 *
 * @param {String} packageName The full package name, including scope (e.g., '@fortawesome/fontawesome-pro').
 *
 * @returns boolean
 */
function isPackageInstalled(packageName) {
  const projectRoot = process.cwd();
  const packagePath = path.join(projectRoot, 'node_modules', packageName);

  return fs.existsSync(packagePath);
}

/**
 * Install FA Pro package.
 *
 * @param {String} packageName The full package name, including scope (e.g., '@fortawesome/fontawesome-pro').
 */
function installFontAwesomePro(packageName) {
  if (!process.env.FONTAWESOME_PRO_AUTH_TOKEN) {
    // Dynamically require dotenv to avoid error if not installed.
    let dotenv;
    try {
      dotenv = require('dotenv');
    } catch (e) {
      console.warn('\nWARNING: dotenv package is not installed. Cannot load .env file.');
    }

    if (dotenv) {
      dotenv.config({ path: path.resolve(__dirname, '../.env') });
    }

    if (!process.env.FONTAWESOME_PRO_AUTH_TOKEN) {
      throw new Error('FONTAWESOME_PRO_AUTH_TOKEN not found.');
    }
  }

  try {
    const FONTAWESOME_VERSION = '6.7.2';

    fs.writeFileSync(npmrcPath, `@fortawesome:registry=https://npm.fontawesome.com/\n//npm.fontawesome.com/:_authToken=${process.env.FONTAWESOME_PRO_AUTH_TOKEN}`);

    // console.log('.npmrc file created/updated with Font Awesome token.');
    console.log(`Attempting to install ${packageName}@${FONTAWESOME_VERSION}...`);
    // execSync(`npm install ${packageName}@${FONTAWESOME_VERSION} --prefix ./ --no-save`, { stdio: 'inherit' });
    execSync(`npm install ${packageName}@${FONTAWESOME_VERSION} --prefix ./ --no-save`);
    console.log(`Successfully installed ${packageName}.`);
  } catch (error) {
    console.error(`\nERROR: Failed to install ${packageName}.`);
    console.error(error.message);

    throw error;
  }
}

try {
  const FONTAWESOME_PRO_PACKAGE = '@fortawesome/fontawesome-pro';
  const isFontAwesomeProInstalled = isPackageInstalled(FONTAWESOME_PRO_PACKAGE);

  if (!isFontAwesomeProInstalled) {
    // console.log('Font Awesome Pro is not installed. Attempting to install...');
    installFontAwesomePro(FONTAWESOME_PRO_PACKAGE);
  }
} catch (error) {
  // console.error('\nScript terminated due to an error during setup:');
  console.error(error.message);
} finally {
  if (fs.existsSync(npmrcPath)) {
    try {
      fs.unlinkSync(npmrcPath);
      console.log('.npmrc file removed successfully.');
    } catch (unlinkError) {
      console.error(`Error removing .npmrc file: ${unlinkError.message}`);
      process.exit(1);
    }
  } else {
    console.log('.npmrc file did not exist, no cleanup needed.');
  }
}
