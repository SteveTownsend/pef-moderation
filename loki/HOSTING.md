## Self-hosting Loki

Self-hosting Loki enables you to capture moderation logs. The software stack used consists of Grafana, Loki and Promtail.

### Preparation for self-hosting Loki

#### Launch a server

Launch a server on any cloud provider, [OVHcloud](https://us.ovhcloud.com/vps/), [Digital Ocean](https://digitalocean.com/), and [Vultr](https://vultr.com/) are popular choices.

Ensure that you can ssh to your server and have root access.

**Server Requirements**

- Public IPv4 address
- Public DNS name
- Public inbound internet access permitted on port 80/tcp and 3000/tcp

**Server Recommendations**

|                  |              |
| ---------------- | ------------ |
| Operating System | Ubuntu 24.04 |
| Memory           | 2+ GB RAM    |
| CPU              | 2+ Cores     |
| Storage          | 40+ GB SSD   |
| Architectures    | amd64, arm64 |

> [!TIP]
> It is a good security practice to restrict inbound ssh access (port 22/tcp) to your own computer's public IP address. You can check your current public IP address using [ifconfig.me](https://ifconfig.me/).

### Open your cloud firewall for HTTP and HTTPS

One of the most common sources of misconfiguration is not opening firewall ports correctly. Please be sure to double check this step.

In your cloud provider's console, the following ports should be open to inbound access from the public internet.

- 80/tcp (Used only for TLS certification verification)
- 3100/tcp (Used to push log data to Loki)

### Installing on Ubuntu 24.04

> [!TIP]
> Loki will run on other Linux distributions but will require different commands.

#### Open ports on your Linux firewall

If your server is running a Linux firewall managed with `ufw`, you will need to open these ports:

```bash
$ sudo ufw allow 80/tcp
$ sudo ufw allow 3100/tcp
```

#### Install Docker

On your server, install Docker CE (Community Edition), using the the following instructions. For other operating systems you may reference the [official Docker install guides](https://docs.docker.com/engine/install/).

**Note:** All of the following commands should be run on your server via ssh.

##### Uninstall old versions

```bash
sudo apt-get remove docker docker-engine docker.io containerd runc
```

##### Set up the repository

```bash
sudo apt-get update
sudo apt-get install \
    ca-certificates \
    curl \
    jq \
    openssl \
    xxd \
    gnupg
```

```bash
sudo install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
sudo chmod a+r /etc/apt/keyrings/docker.gpg
```

```bash
echo \
  "deb [arch="$(dpkg --print-architecture)" signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu \
  "$(. /etc/os-release && echo "$VERSION_CODENAME")" stable" | \
  sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
```

##### Install Docker Engine

```bash
sudo apt-get update
```

```bash
sudo apt-get install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
```

##### Verify Docker Engine installation

```bash
sudo docker run hello-world
```

#### Set up the Grafana directory

```bash
sudo mkdir --parents /loki
```

#### Start the Loki containers

##### Download the Docker compose file and Loki config

Download the `compose.yaml` to run your Grafana instance, which includes the following containers:

- `loki` Loki middleware,  collcts logs from FIrehose CLient and makes them available for Grafana - running on http://localhost:3100
- `watchtower` Daemon responsible for auto-updating containers to keep the server secure and current

```bash
curl https://raw.githubusercontent.com/SteveTownsend/pef-forum-moderation/main/loki/compose.yaml | sudo tee /loki/compose.yaml
curl https://raw.githubusercontent.com/SteveTownsend/pef-forum-moderation/main/firehose-client/config/loki-config.yaml | sudo tee /loki/loki-config.yaml
```

##### Create the systemd service

```bash
cat <<SYSTEMD_UNIT_FILE | sudo tee /etc/systemd/system/loki.service
[Unit]
Description=Public Education Forum Moderation Loki Service
Documentation=https://github.com/SteveTownsend/pef-forum-moderation
Requires=docker.service
After=docker.service

[Service]
Type=oneshot
RemainAfterExit=yes
WorkingDirectory=/loki
ExecStart=/usr/bin/docker compose --file /loki/compose.yaml up --detach
ExecStop=/usr/bin/docker compose --file /loki/compose.yaml down

[Install]
WantedBy=default.target
SYSTEMD_UNIT_FILE
```

##### Start the service

**Reload the systemd daemon to create the new service:**

```bash
sudo systemctl daemon-reload
```

**Enable the systemd service:**

```bash
sudo systemctl enable loki
```

**Start the grafana systemd service:**

```bash
sudo systemctl start loki
```

**Ensure that containers are running**

There should be one each of loki and watchtower containers running.

```bash
sudo systemctl status loki
```

```bash
sudo docker ps
```

### Verify that Loki is online

You can check if your server is online and healthy by requesting the healthcheck endpoint.

```bash
curl http://localhost:3100/ready
```

### Updating Loki

If you use use Docker `compose.yaml` file in this repo, Loki will automatically update at midnight UTC when new releases are available.
