# YugabyteDB UI Application

## Prerequisites

Node.js **16** (LTS from Oct 2021) and NPM **6**

Run `npm ci` to install all dependencies

### Versions

NPM Version - 6.14.4
go version - go version go1.18.1 darwin/amd64

## Building Yugabyted UI Project

Run the build script from the `yugabyted-ui` directory

```sh
$ ./build.sh
[18:23:22] [snowpack] ▶ Build Complete!
Yugabyted UI Binary generated successfully in the bin/ directory.
```

This will generate the binaries inside `bin` directory.

## Building components individually

### Build React.js application

`npm start` - runs the app in dev mode at http://localhost:3000

`npm run build` - builds the app in prod mode into `build/` dir, wiping the previous build if any

### Build Go API Server application

`go build -o yugabyted-ui` - builds the Go API Server of the Yugabyted UI.

`./yugabyted-ui` - Runs the app at http://localhost:1323                             |
