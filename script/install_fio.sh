wget https://brick.kernel.dk/snaps/fio-3.36.tar.gz
sudo apt-get update
sudo apt-get install build-essential libaio-dev zlib1g-dev
tar -xzvf fio-3.36.tar.gz
cd fio-3.36
./configure
make -j32
sudo make install
sudo fio -v
