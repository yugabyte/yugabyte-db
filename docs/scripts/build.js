const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const projectRoot = process.cwd();

/**
 * Check if Font Awesome Pro is installed.
 */
function fontAwesomeProInstalled() {
  const faProPath = path.join(projectRoot, 'node_modules', '@fortawesome', 'fontawesome-pro');

  return fs.existsSync(faProPath);
}

/**
 * Copy the content of the predefined SCSS versions based on whether the FA Pro
 * is installed or not.
 */
function setFontAwesome(isFontAwesomeProInstalled) {
  const defaultFilePath = path.join(projectRoot, 'assets', 'scss', 'fontawesome', '_default.scss');
  const freeFilePath = path.join(projectRoot, 'assets', 'scss', 'fontawesome', '_free.scss');
  const proFilePath = path.join(projectRoot, 'assets', 'scss', 'fontawesome', '_pro.scss');

  let sourceFilePath;
  if (isFontAwesomeProInstalled) {
    sourceFilePath = proFilePath;
    console.log(`Condition met: Copying content from ${proFilePath}`);
  } else {
    sourceFilePath = freeFilePath;
    console.log(`Condition met: Copying content from ${freeFilePath}`);
  }

  fs.readFile(sourceFilePath, 'utf8', (readErr, data) => {
    if (readErr) {
      console.error(`Error reading source file ${sourceFilePath}:`, readErr);
      return;
    }

    // Create the default file if it doesn't exist, or overwrite it if it does.
    fs.writeFile(defaultFilePath, data, (writeErr) => {
      if (writeErr) {
        console.error(`Error writing to ${defaultFilePath}:`, writeErr);
        return;
      }

      console.log(`Content copied successfully to ${defaultFilePath}.`);
    });
  });
}

/*
 * Determine the Hugo command and environment from the script arguments.
 *
 * Examples:
 *   `node scripts/build.js server fast`
 *   `node scripts/build.js build production`
 */
const hugoCommand = process.argv[2];
const hugoEnvironment = process.argv[3];

if (!hugoCommand) {
  console.error('Error: Hugo command (server or build) not specified.');
  process.exit(1);
}

/*
 * Construct the list of TOML config files to merge. Hugo merges configs in the
 * order they are provided to --config. Later files override earlier files for
 * conflicting settings.
 */
const configFilesToLoad = [];

/*
 * Always load _default configs first (base settings).
 */
const defaultConfigsDir = path.join(projectRoot, 'config', '_default');
const defaultTomlFiles = ['hugo.toml', 'params.toml', 'menus.toml', 'markup.toml'];

defaultTomlFiles.forEach(file => {
  const filePath = path.join(defaultConfigsDir, file);
  if (fs.existsSync(filePath)) {
    configFilesToLoad.push(filePath);
  }
});


/*
 * Conditionally add `hugo.fa.toml` if Font Awesome Pro is installed.
 */
const isFontAwesomeProInstalled = fontAwesomeProInstalled();
if (isFontAwesomeProInstalled) {
  // Define which FA version to use.
  setFontAwesome(isFontAwesomeProInstalled);

  const faConfigPath = path.join(defaultConfigsDir, 'hugo.fa.toml');
  if (fs.existsSync(faConfigPath)) {
    configFilesToLoad.push(faConfigPath);
  } else {
    console.warn(`Warning: Font Awesome Pro is installed but hugo.fa.toml not found at ${faConfigPath}`);
  }
}

/*
 * Add environment-specific configs files in the last (to override defaults).
 */
if (hugoEnvironment) {
  const envConfigsDir = path.join(projectRoot, 'config', hugoEnvironment);
  const envTomlFiles = ['hugo.toml', 'params.toml'];

  envTomlFiles.forEach(file => {
    const filePath = path.join(envConfigsDir, file);
    if (fs.existsSync(filePath)) {
      configFilesToLoad.push(filePath);
    }
  });
}

/*
 * Prepare Hugo command arguments either 'server' or 'build'.
 */
const hugoArgs = [hugoCommand];

/*
 * Add the --config flag with the comma-separated list of files.
 */
hugoArgs.push('--config', configFilesToLoad.join(','));

/*
 * Add environment flag if specified.
 */
if (hugoEnvironment) {
  hugoArgs.push('--environment', hugoEnvironment);
}

/*
 * Add common Hugo flags based on command.
 */
if (hugoCommand === 'server') {
  // Resolve the baseURL directly in Node.js.
  const resolvedBaseURL = process.env.YB_HUGO_BASE || 'http://localhost:1313';

  hugoArgs.push(
    '--bind', '0.0.0.0',
    '--baseURL', resolvedBaseURL,
  );
}

const hugoProcess = spawn('hugo', hugoArgs);
hugoProcess.on('close', (code) => {
  if (code !== 0) {
    console.error(`Hugo ${hugoCommand} failed with code ${code}`);
    process.exit(code);
  }
});
