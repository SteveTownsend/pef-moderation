## Self-hosting Firehose Client

Self-hosting Firehose Client enables you to participatedetect your preferred strings in posted content realtime. The Firehose Client consumes the public Jetstream firehose and uses a configured set of strings to filter posts and account or profile updates. Statistics on filter matches are published to Prometheus.

### Preparation for self-hosting Firehose Client

#### Launch a server

Launch a server on any cloud provider, [OVHcloud](https://us.ovhcloud.com/vps/), [Digital Ocean](https://digitalocean.com/), and [Vultr](https://vultr.com/) are popular choices.

Ensure that you can ssh to your server and have root access.

**Server Requirements**

- Public IPv4 address
- Public DNS name
- Public inbound internet access permitted on port 80/tcp and 443/tcp

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

### Open your cloud firewall for TCP

One of the most common sources of misconfiguration is not opening firewall ports correctly. Please be sure to double check this step.

In your cloud provider's console, the following ports should be open to inbound access from the public internet.

- 59090/tcp (Used for Prometheus metric scraping)

### Installing on Ubuntu 24.04

> [!TIP]
> Ozone will run on other Linux distributions but will require different commands.

#### Open ports on your Linux firewall

If your server is running a Linux firewall managed with `ufw`, you will need to open these ports:

```bash
$ sudo ufw allow 59090/tcp
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

#### Set up the Ozone directory

```bash
sudo mkdir --parents /firehose-client/logs
```

#### Start the Firehose Client containers

##### Download the Docker compose file

Download the `firehose-client/compose.yaml` to run your Firehose Client instance. The file includes the following containers:

- `firehose-client` is the BlueSky Websocket firehose client
- `watchtower` Daemon responsible for auto-updating containers to keep the server secure and current

```bash
curl https://raw.githubusercontent.com/SteveTownsend0/nafo-forum-moderation/main/firehose-client/compose.yaml | sudo tee /firehose-client/compose.yaml
```

##### Create the systemd service

```bash
cat <<SYSTEMD_UNIT_FILE | sudo tee /etc/systemd/system/firehose-client.service
[Unit]
Description=NAFO Forum Firehose Client
Documentation=https://github.com/SteveTownsend0/nafo-forum-moderation
Requires=docker.service
After=docker.service

[Service]
Type=oneshot
RemainAfterExit=yes
WorkingDirectory=/firehose-client
ExecStart=/usr/bin/docker compose --file /firehose-client/compose.yaml up --detach
ExecStop=/usr/bin/docker compose --file /firehose-client/compose.yaml down

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
sudo systemctl enable firehose-client
```

**Start the firehose-client systemd service:**

```bash
sudo systemctl start firehose-client
```

**Ensure that containers are running**

There should be a firehose-client and watchtower container running.

```bash
sudo systemctl status firehose-client
```

```bash
sudo docker ps
```

### Verify that Ozone is online

You can check if your server is online and healthy by requesting the Prometheus metrics in browser at your server's IPv4 address w.x.y.z. You would instead use a domain name here if DNS is set up for your server's IP address.

```bash
curl https://w.x.y.z:59090/metrics
```

### Manually updating Ozone

If you use use Docker `compose.yaml` file in this repo, the Firehose Client will automatically update at midnight UTC when new releases are available. To manually update to the latest version use the following commands.

**Pull the latest Firehose Client container image:**

```bash
sudo docker pull ghcr.io/SteveTownsend0/firehose-client:latest
```

**Restart Firehose Client with the new container image:**

```bash
sudo systemctl restart firehose-client
```
