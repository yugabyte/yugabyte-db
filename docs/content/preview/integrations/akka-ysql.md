---
title: Akka Persistence R2DBC
linkTitle: Akka Persistence
description: Use Akka Persistence R2DBC with YSQL API
menu:
  preview_integrations:
    identifier: ysql-akka
    parent: data-integration
    weight: 571
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../akka-ysql/" class="nav-link">
      YSQL
    </a>
  </li>

  <li>
    <a href="../akka-ycql/" class="nav-link">
      YCQL
    </a>
  </li>
</ul>

Akka is a toolkit for building highly concurrent, distributed, and resilient message-driven applications for Java and Scala.

Akka's approach to handling concurrency is based on the Actor Model.
An actor is essentially an object that receives messages and takes actions to handle them. It is a higher level construct that helps to write concurrent code without the trouble of dealing with low-level concurrency primitives when writing multi-threaded high performance applications.

Akka Persistence enables stateful akka-actors to persist their state so that it can be recovered when an actor is either restarted, such as after a JVM crash by a supervisor or a manual stop-start, or migrated in a cluster.
The Akka Persistence R2DBC plugin allows you to use Postgres or Yugabyte as a backend for Akka Persistence and Akka Persistence Query.
The following section illustrates how to run an Akka Persistence R2DBC based application with YugabyteDB.

## Prerequisites

To use the [Akka Persistence R2DBC plugin](https://doc.akka.io/docs/akka-persistence-r2dbc/current/overview.html), ensure that you have the following:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../quick-start/).
- Java Development Kit (JDK) 8, 11 or 17 installed. JDK installers for Linux and macOS can be downloaded from [Oracle](http://jdk.java.net/), [Adoptium (OpenJDK)](https://adoptium.net/), or [Azul Systems (OpenJDK)](https://www.azul.com/downloads/?package=jdk). Homebrew users on macOS can install using `brew install openjdk`.
- [sbt](https://www.scala-sbt.org/1.x/docs/) is installed.

The following example is inspired from the [akka-cassandra-demo](https://github.com/rockthejvm/akka-cassandra-demo) repository.

## Create a Scala project

1. Create a project using the following command:

    ```scala
    sbt new scala/hello-world.g8
    ```

    Press enter when you're prompted to name the application.

1. Replace the existing code in the `build.sbt` file from your project's home directory with the following:

    ```sh
    cd hello-world-template
    ```

    ```scala
    lazy val akkaVersion = "2.7.0"

    libraryDependencies ++= Seq(
      "com.typesafe.akka" %% "akka-actor-typed" % akkaVersion,
      "com.typesafe.akka" %% "akka-stream" % akkaVersion,
      "com.typesafe.akka" %% "akka-persistence-typed" % akkaVersion,
      "com.typesafe.akka" %% "akka-serialization-jackson" % akkaVersion,
      "ch.qos.logback" % "logback-classic" % "1.2.11",
      "com.lightbend.akka" %% "akka-persistence-r2dbc" % "1.0.1")

    //Add the yugabyte java driver to the dependencies of the application.
    libraryDependencies ++= Seq("com.yugabyte" % "java-driver-core" % "4.6.0-yb-11")

    //Exclude the datastax java driver from getting pulled due to nested dependency.
    excludeDependencies ++= Seq(
      ExclusionRule("com.datastax.oss", "java-driver-core"))

    run / fork := true
    Global / cancelable := false // ctrl-c
    ```

## Use the Akka Persistence R2DBC plugin

To create an application using the plugin, do the following:

### Write a sample application

To write a sample application and customize its configuration, do the following:

1. Replace the existing code in `src/main/scala/Main.scala` with the following:

    ```scala
    import akka.NotUsed
    import akka.actor.typed.scaladsl.{ ActorContext, Behaviors }
    import akka.actor.typed.{ ActorRef, ActorSystem, Behavior }
    import akka.persistence.typed.PersistenceId
    import akka.persistence.typed.RecoveryCompleted
    import akka.persistence.typed.scaladsl.{ Effect, EventSourcedBehavior }
    import java.util.UUID

    // a single bank account
    object PersistentBankAccount {

      // commands = messages
      sealed trait Command extends CborSerializable
      final case class CreateBankAccount(
          id: String,
          user: String,
          currency: String,
          initialBalance: Double,
          replyTo: ActorRef[Response])
          extends Command

      // events = to persist to Yugabyte
      trait Event extends CborSerializable
      final case class BankAccountCreated(bankAccount: BankAccount) extends Event

      // state
      final case class BankAccount(
          id: String,
          user: String,
          currency: String,
          balance: Double)
          extends CborSerializable

      // responses
      sealed trait Response
      final case class BankAccountCreatedResponse(id: String)
          extends Response
          with CborSerializable

      val commandHandler: (BankAccount, Command) => Effect[Event, BankAccount] =
        (state, command) =>
          command match {
            case CreateBankAccount(id, user, currency, initialBalance, replyTo) =>
              require(id == state.id, s"Wrong id $id, expected ${state.id}")
              if (state.user == "") {
                // new account
                Effect
                  .persist(
                    BankAccountCreated(
                      BankAccount(id, user, currency, initialBalance)
                    )
                  ) // persisted into Yugabyte
                  .thenReply(replyTo)(_ => BankAccountCreatedResponse(state.id))
              } else if (state.user == user) {
                // already created, idempotent retry
                Effect.reply(replyTo)(BankAccountCreatedResponse(id))
              } else {
                throw new IllegalArgumentException(
                  s"Wrong user $user for existing account $id, expected ${state.user}")
              }
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
          eventHandler = eventHandler)
    }

    object Bank {

      import PersistentBankAccount.{ Command, CreateBankAccount }

      sealed trait Event extends CborSerializable
      case class BankAccountCreated(id: String) extends Event

      case class State(accounts: Set[String]) extends CborSerializable

      def commandHandler(context: ActorContext[Command])
          : (State, Command) => Effect[Event, State] = (state, command) =>
        command match {
          case createCommand @ CreateBankAccount(id, _, _, _, _) =>
            if (state.accounts.contains(id)) {
              val account = context
                .child(id)
                .getOrElse(context.spawn(PersistentBankAccount(id), id))
                .unsafeUpcast[Command]
              account ! createCommand
              Effect.none
            } else {
              Effect.persist(BankAccountCreated(id)).thenRun { _ =>
                val newBankAccount = context.spawn(PersistentBankAccount(id), id)
                newBankAccount ! createCommand
              }
            }
        }

      def eventHandler(context: ActorContext[Command]): (State, Event) => State =
        (state, event) =>
          event match {
            case BankAccountCreated(id) =>
              state.copy(accounts = state.accounts + id)
          }

      // behavior
      def apply(): Behavior[Command] = Behaviors.setup { context =>
        EventSourcedBehavior[Command, Event, State](
          persistenceId = PersistenceId.ofUniqueId("bank"),
          emptyState = State(Set.empty),
          commandHandler = commandHandler(context),
          eventHandler = eventHandler(context)).receiveSignal {
          case (state, RecoveryCompleted) =>
            context.log.info(s"Accounts [{}]", state.accounts.mkString(", "))
            state.accounts.foreach(id =>
              context.spawn(PersistentBankAccount(id), id))
        }
      }
    }

    object BankPlayground {
      import PersistentBankAccount.{
        BankAccountCreatedResponse,
        CreateBankAccount,
        Response
      }

      def main(args: Array[String]): Unit = {
        val rootBehavior: Behavior[NotUsed] = Behaviors.setup { context =>
          val bank = context.spawn(Bank(), "bank")

          val responseHandler = context.spawn(ResponseHandler(), "replyHandler")

          val id = UUID.randomUUID().toString
          bank ! CreateBankAccount(id, "Harsh", "INR", 10000, responseHandler)

          Behaviors.empty
        }
        val system = ActorSystem(rootBehavior, "Demo")
      }

      object ResponseHandler {
        def apply(): Behavior[Response] = {
          Behaviors.receive[Response] {
            case (context, BankAccountCreatedResponse(id)) =>
              context.log.info(s"successfully created account $id")
              Behaviors.same
          }
        }
      }
    }

    /**
     * Marker trait for serialization with Jackson CBOR
     */
    trait CborSerializable
    ```

1. Create a `resources` directory in `src/main` using `mkdir src/main/resources`.

1. Create a file `src/main/resources/application.conf` and copy the following code:

    ```conf
    # Journal
    akka.persistence.journal.plugin = "akka.persistence.r2dbc.journal"

    # Snapshot
    akka.persistence.snapshot-store.plugin = "akka.persistence.r2dbc.snapshot"

    akka.persistence.r2dbc {
      dialect = yugabyte

      connection-factory {
        driver = "postgres"
        host = "localhost"
        port = 5433
        user = "yugabyte"
        password = "yugabyte"
        database = "yugabyte"
      }
    }

    akka.actor.serialization-bindings {
      "CborSerializable" = jackson-cbor
    }
    ```

### Create the tables

Create the tables documented in [Creating the schema](https://doc.akka.io/docs/akka-persistence-r2dbc/current/getting-started.html#creating-the-schema) with `ysqlsh`. Note the Yugabyte tab.

### Run the application

Run the application from the project home directory using the command `sbt run`.

You should see output similar to the following:

```output
successfully created account b2b51f94-b021-4fb8-ba33-219cd0aea3c1
```

### Verify the integration using ysqlsh

Run [ysqlsh](../../admin/ysqlsh/) to connect to your database using the YSQL API as follows:

```sh
./bin/ysqlsh -h localhost
```

Run the following query:

```sql
select * from event_journal;
```

The output should include an event with a `persistance_id` same as the `account_id` (b2b51f94-b021-4fb8-ba33-219cd0aea3c1) obtained after [running the application](#run-the-application).

