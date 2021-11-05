# [Merlin container poller](https://github.com/ITRS-Group/merlin-container-poller)

A containerized version of [Naemon](https://www.naemon.org/) with
[Merlin](https://github.com/ITRS-Group/monitor-merlin), running in containers.
The images are targeted to run mainly in Kubernetes, but also support running
with docker-compose. When running in Kubernetes the Merlin poller can be scaled
either manually by setting the number of replicas, or by using Kubernetes
horizontal pod autoscaler.

## Usage

### Adding SSH keys

In order for the poller to register with masters, SSH keys need to be added to
the image. Generate a pair of SSH keys using `ssh-keygen` and build a custom
Naemon image:

```
FROM op5com/merlin-naemon:latest

COPY --chown=naemon:root id_rsa /var/lib/naemon/.ssh/id_rsa
COPY --chown=naemon:root id_rsa.pub /var/lib/naemon/.ssh/authorized_keys

RUN chmod 600 /var/lib/naemon/.ssh/id_rsa
RUN chmod 644 /var/lib/naemon/.ssh/authorized_keys
```

Ensure that the public key is added to all masters (including master peers)
`authorized_keys` file for the naemon user at:
`/var/lib/naemon/.ssh/authorized_keys`.

### Adding plugins

The Naemon image doesn't contain any plugins by default, so users are required
to ensure that plugins are added to the image. The recommended method is
building a custom image, with all required plugins. The plugins should ideally
be installed at the same paths that corrosponding plugins are installed on
masters.

An example dockerfile, that adds the plugin suite `nagios-plugins-all` can be
seen below. In this example, we also include the SSH keys as mentioned in the
previous section.

Note that it's required to change the user to root, and back to `$NAEMON_UID`.

```
FROM op5com/merlin-naemon:latest

USER root
RUN yum install -y nagios-plugins-all
USER $NAEMON_UID

COPY --chown=naemon:root id_rsa /var/lib/naemon/.ssh/id_rsa
COPY --chown=naemon:root id_rsa.pub /var/lib/naemon/.ssh/authorized_keys

RUN chmod 600 /var/lib/naemon/.ssh/id_rsa
RUN chmod 644 /var/lib/naemon/.ssh/authorized_keys
```

### Deployment

After building your custom image, with SSH keys and plugins you can start
deployment of your container poller. Use the example [deployment files](https://github.com/ITRS-Group/merlin-container-poller/tree/master/deployment_examples)
, ensure that the image for the Naemon container is
updated to your custom image, and fill in the enviorment variables as needed
(see below).

### Configuration

The Naemon image contains a number of enviorment variables that should be setup
in order for the poller to be correctly registered with the master.

| Setting | Description | Required
| ----------- | ----------- | ----------- |
| MASTER_ADDRESS | IP address of the designated master. Only provide one IP, any peers will be automatically added during startup of the poller. | yes |
| MASTER_NAME | The name of the master which will be used on the poller. |  yes |
| MASTER_PORT | Merlin TCP port, default 15551. |  no |
| POLLER_ADDRESS | Address of the poller. This IP needs to be accessible from any poller-peers. Use status.podIP in kubernetes |  yes |
| POLLER_NAME | Name of the poller as registered on the master. On kubernetes use metadata.name |  yes |
| POLLER_HOSTGROUPS | Comma seperated list of hostgroups which the poller should monitor. These hostgroups should exists on the master prior to starting the deployment. |  yes |
| LOG_LEVEL | Set the loglevel to either: debug, info (default), error, critical. |  no |
| FILES_TO_SYNC | Comma-separated list of paths to sync from the master server. |  no |

### Volumes

Two volumes are required, in order to share data between the two running containers.

| Volume | Description | Mount point |
| ----------- | ----------- | ----------- |
| ipc | This volume contains are unix socket, which is used for communicating between the Merlin NEB module and the merlin Daemon. | `/var/lib/merlin/` |
| merlin_config | This contains the merlin configuration, and is needed by both the Merlin NEB module and the merlin Daemon.  | `/etc/merlin/` |

### Kubernetes quick-start

Start by getting the [example deployment file](https://raw.githubusercontent.com/ITRS-Group/merlin-container-poller/master/deployment_examples/k8s.yaml)
and adjust the environment variable to appropriate values. Ensure that you've
built an image included your SSH keys, installed some plugins, and that your
kubenetes cluster has access to your own naemon image. Replace the `image` of
the Naemon container to match your custom image.

You can now start a single pod with:

```
kubectl apply -f ./k8s.yaml
```

You should now see the poller registering with the master and any master-peers.
It might take a little while for the cluster to stabilize.

Now you can scale your deployment to include multiple poller-peers:
```
kubectl scale deployment.v1.apps/merlin-poller --replicas=2
```

### Overwriting default configuration

During startup both the Naemon and Merlin image copies default configuration
from `/usr/local/etc/naemon` and `/usr/local/etc/naemon` respectively.

If you wish to change configuration, from example Naemon/Merlin log level, the
recommended way is to create your own images that overwrites the configuration
at the above paths.

## Image structure

### Base image

The base image contains things which are common between the Naemon & Daemon
images. This include things such as tini, and a bunch of init/entry scripts
for both Naemon & the Merlin daemon.

This image is not used in deployment, however it is created in order to be able
to built custom images with less effort. For example if you wish to compile
your own versions of Naemon/Merlin. This could be necessary if compiling
Nameon/Merlin on masters servers, due to the installation and configuration
paths between masters and pollers need to match.

### Naemon image

The Naemon image contains, naemon-core, and the Merlin NEB module. This image
is responsible for executing all checks. The image also contains logic that
automatically registers the container poller with masters.

Note that the image doesn't contain any check plugins.

### Daemon image

Contains the Merlin daemon.

## Building images

Building images from [source](https://github.com/ITRS-Group/merlin-container-poller)
is done with docker-compose:

```
docker-compose build
```
