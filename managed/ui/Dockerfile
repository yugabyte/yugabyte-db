FROM node:7.6

# Create app directory
RUN mkdir -p /src/app
WORKDIR /src/app

# Install app dependencies
COPY package.json /src/app/
COPY webpack.custom-config.js /src/app
RUN npm install && \
    npm install -g pushstate-server

# Bundle app source
COPY . /src/app

# Build production version
RUN npm run build

EXPOSE 4000

# Start production version via pushstate server
CMD [ "npm", "run", "start:prod", "--port", "4000" ]
