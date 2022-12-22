set -ex

add-apt-repository ppa:ubuntu-toolchain-r/test -y
add-apt-repository ppa:mhier/libboost-latest -y
apt update -y

# g++-9
apt install gcc-9 g++-9 -y
update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 60 --slave /usr/bin/g++ g++ /usr/bin/g++-9

# cmake
apt remove --purge --auto-remove cmake -y
wget -qO- https://github.com/Kitware/CMake/releases/download/v3.19.2/cmake-3.19.2-Linux-x86_64.tar.gz | sudo tar -xvz -C /opt
sudo ln -s /opt/cmake-3.19.2-Linux-x86_64/bin/cmake /usr/bin/cmake

# boost 1.74.0
apt install libboost1.74-dev -y

# versions
g++ --version
cmake --version
dpkg -S /usr/include/boost/version.hpp

# /home/box/final.txt
echo 'https://github.com/MrNickev/final' >> /home/box/final.txt