# YugabyteDB UI Application

## Prerequisites

Node.js **22.18.0** and NPM **10**

### Installing Node.js using nvm

The recommended way to install the correct Node.js version is using [nvm (Node Version Manager)](https://github.com/nvm-sh/nvm):

```sh
# Install nvm (if not already installed)
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.0/install.sh | bash

# Install and use the Node.js version specified in .nvmrc
cd yugabyted-ui
nvm install
nvm use
```

Alternatively, you can manually install Node.js 22.18.0 from [nodejs.org](https://nodejs.org/).

### Install dependencies

Run `npm ci` to install all dependencies

### Versions

NPM Version - 10.x
go version - go version go1.18.1 darwin/amd64

## Building Yugabyted UI Project

Run the build script from the `yugabyted-ui` directory

```sh
$ ./build.sh
✓ built in 25.32s
Yugabyted UI Binary generated successfully.
```

This will generate the binaries inside `bin` directory.

## Building components individually

### Build React.js application

`npm run build` compiles the app in production mode and outputs a fresh build into the `ui/` directory, replacing any existing contents in that directory.

### Build Go API Server application

`go build -o yugabyted-ui` - builds the Go API Server of the Yugabyted UI.

`./yugabyted-ui` - Runs the app at http://localhost:1323                        
