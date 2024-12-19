## Self-hosting Grafana

Self-hosting Grafana enables you to visualize moderation metrics and logs. The software stack used consists of Grafana and a Postgres database.

### Preparation for self-hosting Grafana

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
- 3000/tcp (Used for all application requests)

### Configure DNS for your domain

From your DNS provider's control panel, set up a domain with records pointing to your server.

| Name                  | Type | Value         | TTL |
| --------------------- | ---- | ------------- | --- |
| `grafana.example.com` | `A`  | `12.34.56.78` | 600 |

**Note:**

- Replace `example.com` with your domain name.
- Replace `12.34.56.78` with your server's IP address.
- Some providers may use the `@` symbol to represent the root of your domain.
- The TTL can be anything but 600 (10 minutes) is reasonable
nice!

### Check that DNS is working as expected

Use a service like [DNS Checker](https://dnschecker.org/) to verify that you can resolve your new DNS hostnames.

Check the following:

- `grafana.example.com` (record type `A`)

This should return your server's public IP.

### Installing on Ubuntu 24.04

> [!TIP]
> Grafana will run on other Linux distributions but will require different commands.

#### Open ports on your Linux firewall

If your server is running a Linux firewall managed with `ufw`, you will need to open these ports:

```bash
$ sudo ufw allow 80/tcp
$ sudo ufw allow 3000/tcp
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
sudo mkdir --parents /grafana/log
sudo mkdir /grafana/postgres
```

#### Create the Postgres env configuration file

Configure Postgres with superuser credentials created at startup, and initial database name.

```bash
POSTGRES_PASSWORD="$(openssl rand --hex 16)"

cat <<POSTGRES_CONFIG | sudo tee /grafana/postgres.env
POSTGRES_USER=postgres
POSTGRES_PASSWORD=${POSTGRES_PASSWORD}
POSTGRES_DB=grafana
POSTGRES_CONFIG
```

#### Create the Grafana env configuration file

```bash
POSTGRES_PASSWORD="..." # Use password from postgres env setup
cat <<GRAFANA_CONFIG | sudo tee /grafana/grafana.env
GF_DATABASE_TYPE=postgres
GF_DATABASE_HOST=pg_grafana:5432
GF_DATABASE_NAME=grafana
GF_DATABASE_USER=postgres
GF_DATABASE_PASSWORD=my_grafana_pwd
GF_DATABASE_SSL_MODE=require
GF_LOG_MODE=file
GRAFANA_CONFIG
```

#### Start the Grafana containers

##### Download the Docker compose file

Download the `compose.yaml` to run your Grafana instance, which includes the following containers:

- `grafana` Grafana server—both UI and backend—running on http://localhost:3000
- `postgres` Postgres database used by the Grafana backend
- `watchtower` Daemon responsible for auto-updating containers to keep the server secure and current

```bash
curl https://raw.githubusercontent.com/SteveTownsend/nafo-forum-moderation/main/grafana/compose.yaml | sudo tee /grafana/compose.yaml
```

##### Create the systemd service

```bash
cat <<SYSTEMD_UNIT_FILE | sudo tee /etc/systemd/system/grafana.service
[Unit]
Description=NAFO Forum Moderation Grafana Service
Documentation=https://github.com/SteveTownsend/nafo-forum-moderation
Requires=docker.service
After=docker.service

[Service]
Type=oneshot
RemainAfterExit=yes
WorkingDirectory=/grafana
ExecStart=/usr/bin/docker compose --file /grafana/compose.yaml up --detach
ExecStop=/usr/bin/docker compose --file /grafana/compose.yaml down

[Install]
WantedBy=default.target
SYSTEMD_UNIT_FILE
```

##### Set up HTTPS (recommended)

Follow instructions [here](https://www.stefanproell.at/2018/10/12/grafana-and-influxdb-with-ssl-inside-a-docker-container/), LetsEncrypt version recommended. Edit /grafana/compose.yaml accordingly.

##### Start the service

**Reload the systemd daemon to create the new service:**

```bash
sudo systemctl daemon-reload
```

**Enable the systemd service:**

```bash
sudo systemctl enable grafana
```

**Start the grafana systemd service:**

```bash
sudo systemctl start grafana
```

**Ensure that containers are running**

There should be one each of grafana, postgres, and watchtower containers running.

```bash
sudo systemctl status grafana
```

```bash
sudo docker ps
```

### Verify that Grafana is online

You can check if your server is online and healthy by requesting the healthcheck endpoint, and by visiting the UI in browser at https://grafana.example.com/.

### Updating Grafana

If you use use Docker `compose.yaml` file in this repo, Grafana will automatically update at midnight UTC when new releases are available.

### Limiting access ###

Allowing anonymous access is not recommended.
Limited access for known users to kick the tires can be enabled as shown [here](https://grafana.com/docs/grafana/latest/administration/user-management/manage-dashboard-permissions/#enable-viewers-to-edit-but-not-save-dashboards-and-use-explore).
