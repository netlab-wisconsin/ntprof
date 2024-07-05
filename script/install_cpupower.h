sudo apt-get update
sudo apt-get install libpci-dev gettext
cd /usr/src/linux-source-5.15.0/tools/power/cpupower
sudo make
sudo ln -s libcpupower.so.0 /usr/lib/libcpupower.so.0
echo "/usr/src/linux-source-5.15.0/tools/power/cpupower" | sudo tee /etc/ld.so.conf.d/cpupower.conf
sudo ldconfig
sudo make install

