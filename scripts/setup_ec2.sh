#!/bin/bash
set -e

echo "================================================="
echo "Setting up EC2 Environment for Benchmarking"
echo "================================================="

# Update package lists
sudo apt-get update
sudo apt-get upgrade -y

echo "Installing system dependencies and telemetry tools..."
sudo apt-get install -y build-essential cmake git dstat python3 python3-pip apt-transport-https wget gpg curl

echo "Installing Database Servers..."
sudo apt-get install -y mysql-server postgresql

echo "Installing Cassandra..."
wget -qO - https://downloads.apache.org/cassandra/KEYS | sudo gpg --dearmor -o /usr/share/keyrings/cassandra-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/cassandra-archive-keyring.gpg] https://debian.cassandra.apache.org 41x main" | sudo tee /etc/apt/sources.list.d/cassandra.sources.list
sudo apt-get update
sudo apt-get install -y cassandra

echo "Installing C++ dependencies (RocksDB, LevelDB, MySQL, PostgreSQL, nlohmann-json)..."
sudo apt-get install -y librocksdb-dev libleveldb-dev libmariadb-dev libpq-dev nlohmann-json3-dev

echo "Building and installing DataStax Cassandra C++ Driver..."
sudo apt-get install -y libuv1-dev libssl-dev zlib1g-dev
if [ ! -d "cpp-driver" ]; then
    git clone https://github.com/datastax/cpp-driver.git
    cd cpp-driver
    mkdir build && cd build
    cmake ..
    make -j $(nproc)
    sudo make install
    sudo ldconfig
    cd ../..
else
    echo "cpp-driver already exists. Skipping build."
fi

echo "================================================="
echo "Configuring NVMe Storage & Performance Settings..."
echo "================================================="

# Detect ephemeral NVMe device (typically nvme1n1 or nvme0n2 on EC2)
NVME_DEV=""
for dev in /dev/nvme1n1 /dev/nvme0n2 /dev/nvme2n1; do
    if [ -b "$dev" ]; then
        NVME_DEV="$dev"
        break
    fi
done

if [ -n "$NVME_DEV" ]; then
    echo "Found local NVMe storage device: $NVME_DEV"
    if ! blkid "$NVME_DEV" | grep -q "ext4"; then
        echo "Formatting $NVME_DEV as ext4..."
        sudo mkfs.ext4 -F "$NVME_DEV"
    fi

    sudo mkdir -p /mnt/nvme
    if ! mountpoint -q /mnt/nvme; then
        echo "Mounting $NVME_DEV at /mnt/nvme..."
        sudo mount -o noatime,nodiratime "$NVME_DEV" /mnt/nvme
    fi

    # Redirect DB data dirs to NVMe
    sudo mkdir -p /mnt/nvme/mysql /mnt/nvme/postgresql /mnt/nvme/cassandra
    
    # Stop services before relocating data dirs if active
    sudo systemctl stop mysql postgresql cassandra || true
    
    if [ ! -L /var/lib/mysql ] && [ -d /var/lib/mysql ]; then
        sudo rsync -av /var/lib/mysql/ /mnt/nvme/mysql/
        sudo rm -rf /var/lib/mysql
        sudo ln -s /mnt/nvme/mysql /var/lib/mysql
        sudo chown -R mysql:mysql /mnt/nvme/mysql
    fi

    if [ ! -L /var/lib/postgresql ] && [ -d /var/lib/postgresql ]; then
        sudo rsync -av /var/lib/postgresql/ /mnt/nvme/postgresql/
        sudo rm -rf /var/lib/postgresql
        sudo ln -s /mnt/nvme/postgresql /var/lib/postgresql
        sudo chown -R postgres:postgres /mnt/nvme/postgresql
    fi

    if [ ! -L /var/lib/cassandra ] && [ -d /var/lib/cassandra ]; then
        sudo rsync -av /var/lib/cassandra/ /mnt/nvme/cassandra/
        sudo rm -rf /var/lib/cassandra
        sudo ln -s /mnt/nvme/cassandra /var/lib/cassandra
        sudo chown -R cassandra:cassandra /mnt/nvme/cassandra
    fi
else
    echo "No unattached ephemeral NVMe storage found. Using root storage paths."
fi

echo "Configuring OS limits and kernel tuning..."
sudo sysctl -w vm.max_map_count=1048576 || true
sudo sysctl -w net.core.somaxconn=2048 || true

echo "vm.max_map_count=1048576" | sudo tee -a /etc/sysctl.d/99-db-bench.conf
echo "net.core.somaxconn=2048" | sudo tee -a /etc/sysctl.d/99-db-bench.conf

cat <<'EOF' | sudo tee /etc/security/limits.d/99-db-bench.conf
* soft nofile 65536
* hard nofile 65536
* soft memlock unlimited
* hard memlock unlimited
EOF
ulimit -n 65536 || true

echo "Configuring Cassandra JVM Memory Limits..."
CASSANDRA_ENV="/etc/cassandra/cassandra-env.sh"
if [ -f "$CASSANDRA_ENV" ]; then
    sudo sed -i 's/#MAX_HEAP_SIZE="4G"/MAX_HEAP_SIZE="8G"/' "$CASSANDRA_ENV"
    sudo sed -i 's/#HEAP_NEWSIZE="800M"/HEAP_NEWSIZE="2G"/' "$CASSANDRA_ENV"
    if ! grep -q "MAX_HEAP_SIZE=\"8G\"" "$CASSANDRA_ENV"; then
        echo 'MAX_HEAP_SIZE="8G"' | sudo tee -a "$CASSANDRA_ENV"
        echo 'HEAP_NEWSIZE="2G"' | sudo tee -a "$CASSANDRA_ENV"
    fi
fi

echo "Starting and enabling services..."
sudo systemctl enable --now mysql
sudo systemctl enable --now postgresql
sudo systemctl enable --now cassandra

echo "Setting up database schemas..."
# PostgreSQL
sudo -u postgres psql -c "CREATE USER bench WITH PASSWORD 'benchpass';" || true
sudo -u postgres psql -c "CREATE DATABASE bench OWNER bench;" || true
sudo -u postgres psql -d bench -c "CREATE TABLE IF NOT EXISTS bench_kv (key_id INT PRIMARY KEY, value_data TEXT);" || true

# MySQL
sudo mysql -e "CREATE USER IF NOT EXISTS 'bench'@'localhost' IDENTIFIED BY 'benchpass';"
sudo mysql -e "CREATE DATABASE IF NOT EXISTS bench;"
sudo mysql -e "GRANT ALL PRIVILEGES ON bench.* TO 'bench'@'localhost';"
sudo mysql -e "FLUSH PRIVILEGES;"
sudo mysql -D bench -e "CREATE TABLE IF NOT EXISTS bench_kv (key_id INT PRIMARY KEY, value_data TEXT);"

echo "Installing Python plotting dependencies..."
pip3 install -r scripts/requirements.txt --break-system-packages

echo "================================================="
echo "Setup Complete! You can now compile the project:"
echo "mkdir build && cd build && cmake .. && make -j"
echo "================================================="

