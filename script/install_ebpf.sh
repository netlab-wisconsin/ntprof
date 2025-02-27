cd /usr/src/linux-source-5.15.0/tools/bpf/bpftool
sudo make -j 31
sudo make install
echo '# Add bpftool to PATH (auto-added by script)' >> ~/.tcshrc
echo 'setenv PATH "${PATH} /usr/local/sbin"' >> ~/.tcshrc
source ~/.tcshrc

which bpftool
bpftool version
