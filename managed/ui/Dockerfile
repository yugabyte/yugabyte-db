FROM node:alpine
MAINTAINER YugaByte
ENV container=yugaware-ui

# Create app directory
WORKDIR /opt/yugaware-ui

RUN npm install -g pushstate-server 

# Bundle app source
COPY build /opt/yugaware-ui

EXPOSE 4000

# Start production version via pushstate server
CMD [ "pushstate-server", "/opt/yugaware-ui", "4000" ]
