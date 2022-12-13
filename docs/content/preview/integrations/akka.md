---
title: Akka Persistence Cassandra
linkTitle: Akka Persistence Cassandra
description: Use Akka Persistence Cassandra with YCQL API
menu:
  preview:
    identifier: ycql-akka
    parent: integrations
    weight: 571
type: docs
---

Akka is a toolkit for building highly concurrent, distributed, and resilient message-driven applications for Java and Scala.

Akka's approach to handling concurrency is based on the Actor Model.
An actor is essentially an object that receives messages and takes actions to handle them. It is a higher level construct that helps to write concurrent code without the trouble of dealing with low-level concurrency primitives when writing multi-threaded high performance applications.

Akka Persistence enables stateful akka-actors to persist their state so that it can be recovered when an actor is either restarted, such as after a JVM crash by a supervisor or a manual stop-start, or migrated in a cluster.
The Akka Persistence Cassandra plugin allows you to use Apache Cassandra as a backend for Akka Persistence and Akka Persistence Query. It uses Alpakka Cassandra for Cassandra access which is based on the Datastax Java Driver.
Alpakka Cassandra offers an Akka Streams API on top of a `CqlSession` from the Datastax Java Driver version 4.0 and later.

The following section illustrates how to run an Akka Persistence Cassandra based application with YugabyteDB.

## Prerequisites

To use the Akka Persistence Cassandra plugin, ensure that you have the following:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../../../quick-start/).
- Java Development Kit (JDK) 1.8 installed. JDK installers for Linux and macOS can be downloaded from [Oracle](http://jdk.java.net/), [Adoptium (OpenJDK)](https://adoptium.net/), or [Azul Systems (OpenJDK)](https://www.azul.com/downloads/?package=jdk). Homebrew users on macOS can install using `brew install openjdk`.
- [sbt](https://www.scala-sbt.org/1.x/docs/) is installed.

The following example is inspired from the [akka-cassandra-demo](https://github.com/rockthejvm/akka-cassandra-demo) repository.

## Create a Scala project

1. Create a project using the following command:

    ```scala
    sbt new scala/scala3.g8
    ```

    Press enter when you're prompted to name the application.

1. Add the following code in `build.sbt` file from your project's home directory.

    ```sh
    cd scala-3-project-template
    ```

    ```scala
    //Delete the existing datastax java driver from the dependencies of the application.

    lazy val akkaVersion     = "2.6.9"

    libraryDependencies ++= Seq(
     "com.typesafe.akka" %% "akka-actor-typed"           % akkaVersion,
     "com.typesafe.akka" %% "akka-stream"                % akkaVersion,
     "com.typesafe.akka" %% "akka-persistence-typed"     % akkaVersion,
     "ch.qos.logback"    % "logback-classic"             % "1.2.10",
     "com.typesafe.akka" %% "akka-persistence-cassandra" % "1.0.5"
    )

    //Add the yugabyte java driver to the dependencies of the application.
    libraryDependencies ++= Seq("com.yugabyte" % "java-driver-core" % "4.6.0-yb-11")

    //Exclude the datastax java driver from getting pulled due to nested dependency.
    excludeDependencies ++= Seq(ExclusionRule("com.datastax.oss", "java-driver-core"))
    ```

## Use the Akka Persistence Cassandra plugin

To create an application using the plugin, do the following:

### Write a sample application

To write a sample application and customize its configuration, do the following:

1. Copy the following code in `src/main/scala/Main.scala` as follows:

    ```scala
    import akka.NotUsed
    import akka.actor.typed.scaladsl.{ActorContext, Behaviors}
    import akka.actor.typed.{ActorRef, ActorSystem, Behavior, Scheduler}
    import akka.persistence.typed.PersistenceId
    import akka.persistence.typed.scaladsl.{Effect, EventSourcedBehavior}
    import akka.util.Timeout

    import java.util.UUID
    import scala.concurrent.ExecutionContext
    import scala.util.{Failure, Success, Try}

    // a single bank account
    object PersistentBankAccount {

     // commands = messages
     sealed trait Command
     object Command {
       case class CreateBankAccount(user: String, currency: String, initialBalance: Double, replyTo: ActorRef[Response]) extends     Command
     }

     // events = to persist to Cassandra
     trait Event
     case class BankAccountCreated(bankAccount: BankAccount) extends Event

     // state
     case class BankAccount(id: String, user: String, currency: String, balance: Double)

     // responses
     sealed trait Response
     object Response {
       case class BankAccountCreatedResponse(id: String) extends Response
     }

     import Command._
     import Response._

     val commandHandler: (BankAccount, Command) => Effect[Event, BankAccount] = (state, command) =>
       command match {
         case CreateBankAccount(user, currency, initialBalance, bank) =>
           val id = state.id
           Effect
             .persist(BankAccountCreated(BankAccount(id, user, currency, initialBalance))) // persisted into Cassandra
             .thenReply(bank)(_ => BankAccountCreatedResponse(id))
       }

     val eventHandler: (BankAccount, Event) => BankAccount = (state, event) =>
       event match {
         case BankAccountCreated(bankAccount) =>
           bankAccount
       }

     def apply(id: String): Behavior[Command] =
       EventSourcedBehavior[Command, Event, BankAccount](
         persistenceId = PersistenceId.ofUniqueId(id),
         emptyState = BankAccount(id, "", "", 0.0),
         commandHandler = commandHandler,
         eventHandler = eventHandler
       )
    }

    object Bank {

     import PersistentBankAccount.Command._
     import PersistentBankAccount.Response._
     import PersistentBankAccount.Command

     sealed trait Event
     case class BankAccountCreated(id: String) extends Event

     case class State(accounts: Map[String, ActorRef[Command]])

     def commandHandler(context: ActorContext[Command]): (State, Command) => Effect[Event, State] = (state, command) =>
       command match {
         case createCommand @ CreateBankAccount(_, _, _, _) =>
           val id = UUID.randomUUID().toString
           val newBankAccount = context.spawn(PersistentBankAccount(id), id)
           Effect
             .persist(BankAccountCreated(id))
             .thenReply(newBankAccount)(_ => createCommand)
       }

     def eventHandler(context: ActorContext[Command]): (State, Event) => State = (state, event) =>
       event match {
         case BankAccountCreated(id) =>
           val account = context.child(id)
             .getOrElse(context.spawn(PersistentBankAccount(id), id))
             .asInstanceOf[ActorRef[Command]]
           state.copy(state.accounts + (id -> account))
       }

     // behavior
     def apply(): Behavior[Command] = Behaviors.setup { context =>
       EventSourcedBehavior[Command, Event, State](
         persistenceId = PersistenceId.ofUniqueId("bank"),
         emptyState = State(Map()),
         commandHandler = commandHandler(context),
         eventHandler = eventHandler(context)
       )
     }
    }

    object BankPlayground {
     import PersistentBankAccount.Command._
     import PersistentBankAccount.Response._
     import PersistentBankAccount.Response

     def main(args: Array[String]): Unit = {
       val rootBehavior: Behavior[NotUsed] = Behaviors.setup { context =>
         val bank = context.spawn(Bank(), "bank")
         val logger = context.log

         val responseHandler = context.spawn(Behaviors.receiveMessage[Response]{
           case BankAccountCreatedResponse(id) =>
             logger.info(s"successfully created account $id")
             Behaviors.same
         }, "replyHandler")

         // ask pattern
         import akka.actor.typed.scaladsl.AskPattern._
         import scala.concurrent.duration._
         implicit val timeout: Timeout = Timeout(2.seconds)
         implicit val scheduler: Scheduler = context.system.scheduler
         implicit val ec: ExecutionContext = context.executionContext

         bank ! CreateBankAccount("Harsh", "INR", 10000, responseHandler)

         Behaviors.empty
       }
       val system = ActorSystem(rootBehavior, "Demo")
     }
    }
    ```

1. Create a `resources` directory in `src/main` using `mkdir resources`.

1. Create a file `src/main/resources/application.conf` and copy the following code:

    ```conf
    # Journal
    akka.persistence.journal.plugin = "akka.persistence.cassandra.journal"
    akka.persistence.cassandra.journal.keyspace-autocreate = true
    akka.persistence.cassandra.journal.tables-autocreate = true
    datastax-java-driver.advanced.reconnect-on-init = true

    # Snapshot
    akka.persistence.snapshot-store.plugin = "akka.persistence.cassandra.snapshot"
    akka.persistence.cassandra.snapshot.keyspace-autocreate = true
    akka.persistence.cassandra.snapshot.tables-autocreate = true

    akka.actor.allow-java-serialization = on
    ```

### Run the application

Run the application from the project home directory using the commands `sbt` and `run`.

You should see output similar to the following:

```output
successfully created account b2b51f94-b021-4fb8-ba33-219cd0aea3c1
```

Note that it can take some time to view the output as all the keyspaces and tables that are required by Akka Persistence Cassandra are created for the first time.

### Verify the integration using ycqlsh

Run [ycqlsh](../../admin/ycqlsh/) to connect to your database using the YCQL API as follows:

```sh
./bin/ycqlsh localhost
```

```output
Connected to local cluster at 127.0.0.1:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
ycqlsh>
```

Run the following query:

```cql
select * from akka.messages
```

The output should include an event with a `persistance_id` same as the `account_id` (b2b51f94-b021-4fb8-ba33-219cd0aea3c1) obtained after [running the application](#run-the-application).

## Limitations

Some older versions of Akka Persistence Cassandra use Cassandra Materialized Views which may not work with YugabyteDB. Use versions of Akka Persistence Cassandra that *do not* use Cassandra Materialized Views.
