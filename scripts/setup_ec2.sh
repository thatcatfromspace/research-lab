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
