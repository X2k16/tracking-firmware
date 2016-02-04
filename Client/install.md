# install


```
apt-get install python3-pip
mkdir -p /opt/cross
cd /opt/cross
git clone https://github.com/X2k16/tracking-firmware.git
cd tracking-firmware/Client/src
pip3 install -r ../requirements.txt
```

## rc.local

```
(
  export TOUCH_API_KEY=changeme;
  export CLIENT_ID=6;
  cd /opt/cross/tracking-firmware/Client/src;
  while true;
  do /usr/bin/python3 cross.py;
  done;
)
```
