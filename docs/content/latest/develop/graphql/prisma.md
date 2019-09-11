---
title: Prisma
linkTitle: Prisma
description: Prisma
menu:
  latest:
    identifier: prisma
    parent: ecosystem-integrations
    weight: 573
isTocNested: true
showAsideToc: true
---




## Before you begin

1. Install and start YugaByte
2. Install and start Prisma



Initialize a project
Set up a sample schema
Try Prisma GraphQL
Create Sample Data
Create a user Jane with three posts
Create a user John with two posts
Query the data
Get all users
Get all posts
Get all users ordered alphabetically
Get all posts ordered by popularity
Try Prisma ORM (JS)
Initialize an npm project.
Read and Write Data
Create a file
Add the following code to index.js
Run the file
Clean Up
1. Stop the YugaByte cluster
2. Stop The Prisma container



Prerequisites
1. Install and start YugaByte
Follow the Instructions from https://docs.yugabyte.com/latest/quick-start/install/ 
and then run
./bin/yb-ctl create --tserver_flags='ysql_pg_conf="default_transaction_isolation=serializable"'

Note: Needing to explicitly set the isolation level is a temporary limitation.

<!--
Test of HTML commenting.
-->

2. Install and start Prisma
Initialize a project
First install npm and docker.

Then, to install prisma run:
npm i -g prisma

Set up a default prisma project
prisma init prisma-yb

When prompted, input or select the following values:
? Set up a new Prisma server or deploy to an existing server? Use existing database
? What kind of database do you want to deploy to? PostgreSQL
? Does your database contain existing data? No
? Enter database host localhost
? Enter database port 5433
? Enter database user postgres
? Enter database password [No password, just press enter]
? Enter database name (the database includes the schema) postgres
? Use SSL? N
? Select the programming language for the generated Prisma client Prisma JavaScript Client

Start prisma docker container
cd prisma-yb
docker-compose up -d

Note: Should now have a prismagraphql/prisma container running. You can check that by running docker ps.

Set up a sample schema
Open datamodel.prisma and replace the contents with:

type Post {
  id: ID! @id
  createdAt: DateTime! @createdAt
  text: String!
  views: Int!
  author: User! @relation(link: INLINE)
}

type User {
  id: ID! @id
  createdAt: DateTime! @createdAt
  updatedAt: DateTime! @updatedAt
  handle: String! @unique
  name: String
  posts: [Post!]!
}

Deploy prisma (locally)
prisma deploy
Try Prisma GraphQL
Open the Prisma UI at http://localhost:4466/
Create Sample Data
Create a user Jane with three posts
mutation {
  createUser(data: {
    name: "Jane Doe"
    handle: "jane"
    posts: {
      create: [
        {
           text: "Jane's First Post"
           views: 10
        },
        {
           text:"Jane's Second Post"
	         views: 80
        },
        {
           text:"Jane's Third Post"
           views: 25 
        }
      ]
    }
  }) {
    id
  }
}




Create a user John with two posts

mutation {
  createUser(data: {
    name: "John Doe"
    handle: "john"
    posts: {
      create: [
        {
           text: "John's First Post"
           views: 15
        },
        {
           text:"John's Second Post"
	         views: 20
        }
      ]
    }
  }) {
    id
  }
}


Query the data
Get all users
{
  users {
    id
    name
    handle
    createdAt
  }
}

Get all posts
{
  posts {
    id
    text
    views
    createdAt
  }
}





Get all users ordered alphabetically 
{
  users(orderBy: name_ASC) {
    name
    posts(orderBy: text_ASC) {
      text
      views
    }
  }
}


Get all posts ordered by popularity
{
  posts(orderBy: views_DESC) {
    text
    views
    author {
      name
      handle
    }
  }
}


Try Prisma ORM (JS)
Initialize an npm project.
npm init -y
npm install --save prisma-client-lib
Read and Write Data
Create a file
touch index.js

Add the following code to index.js
const { prisma } = require('./generated/prisma-client')

// A `main` function so that we can use async/await
async function main() {

  // Create a new user called `Alice`
  const alice = await prisma.createUser({ name: 'Alice Doe', handle: 'alice' })
  console.log(`Created new user: ${alice.name} (ID: ${alice.id})`)

  // Create a new post for 'Alice'
  const alicesPost = await prisma.createPost({ text: 'Alice\'s First Post', views: 0, author: {connect: {id : alice.id} }})
  console.log(`Created new post: ${alicesPost.text} (ID: ${alicesPost.id})`)

  // Get all users ordered by name and print them to the console
  console.log("All Users:")
  console.log(await prisma.users({ orderBy: 'name_DESC' }))

  // Get all of Alice's posts (just one)
  console.log("Alice's Posts:")
  console.log(await prisma.user( {handle : 'alice'}).posts());
}

main().catch(e => console.error(e))


Note: The code above will create a new user `Alice` and a new Post for that user. 
Finally it will list all users, and then all posts by the new user Alice.
Run the file
node index.js


Clean Up
1. Stop the YugaByte cluster
./bin/yb-ctl stop

To completely remove all YugaByte data/cluster-state you can instead run:
./bin/yb-ctl destroy
2. Stop The Prisma container
docker stop <container-id>

To completely remove all Prisma data/tate you can additionally run:
docker rm <container-id>

Note: you can list running containers with docker ps

