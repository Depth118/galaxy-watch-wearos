#!/bin/sh
case "$1" in
	deconfig) ifconfig "$interface" 0.0.0.0 ;;
	bound|renew)
		ifconfig "$interface" "$ip" netmask "${subnet:-255.255.255.0}"
		if [ -n "$router" ]; then
			while route del default gw 0.0.0.0 dev "$interface" 2>/dev/null; do :; done
			for r in $router; do route add default gw "$r" dev "$interface"; break; done
		fi
		: > /etc/resolv.conf
		for d in $dns; do echo "nameserver $d" >> /etc/resolv.conf; done
		echo "<0>r800-sh: dhcp $1 ip=$ip router=$router" > /dev/kmsg
		;;
esac
exit 0
