The [local cluster] (/quick-start/install/) approach is great for testing operational scenarios including fault-tolerance and auto rebalancing. The same local cluster approach is also possible in YugaWare.

Go to the Docker tab in the Configuration section and click Setup to initialize Docker as a cloud provider. Note that Docker Platform is already installed on the YugaWare host when you installed Replicated.

![Configure Docker](/images/ee/configure-docker-1.png)

![Docker Configured Successfully](/images/ee/configure-docker-2.png)

As you can see above, the above initialization setup creates 2 dummy regions (US West and US East) with 3 dummy availability zones each. Now we are ready to create a containerized YugaByte universe running on the YugaWare host.
