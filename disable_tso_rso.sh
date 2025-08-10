#! /bin/sh
# Replace "ethX" with your network interface
Active_netcard=$(ip addr show | awk '/inet.*brd/{print $NF}')
echo $Active_netcard
ethtool -K $Active_netcard tso off gso off gro off
