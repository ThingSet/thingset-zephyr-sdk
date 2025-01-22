# ThingSet Zephyr SDK

This repository contains a software development kit (SDK) based on Zephyr RTOS to integrate communication interfaces using the ThingSet protocol into an application with minimum effort.

## Testing with native_sim board

```
west build -b native_sim samples/counter -t run
```

## Testing with LoRaWAN

```
west build -b olimex_lora_stm32wl_devkit samples/counter -- -DEXTRA_CONF_FILE=lorawan.conf
```

## Testing with CAN bus

```
west build -b native_sim samples/counter -t run -- -DEXTRA_CONF_FILE=can.conf
```

## Testing WebSocket with native_sim

Start net-setup from Zephyr net-tools to create `zeth` interface:

```
sudo ../tools/net-tools/net-setup.sh
```

Forward packets from the the `zeth` interface to the internet via wifi/ethernet interface (replace
`wifi0` with the actual interface name):

```
sudo sysctl net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -o wifi0 -j MASQUERADE
sudo iptables -A FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
sudo iptables -A FORWARD -i zeth -o wifi0 -j ACCEPT
```

Afterwards run the nativ_posix board with websocket support from another shell:

```
west build -b native_sim samples/counter -- -DEXTRA_CONF_FILE="native_websocket.conf storage_flash.conf"
./build/zephyr/zephyr.exe -flash=samples/counter/virtual-flash.bin
```

The `virtual-flash.bin` file is used to store custom data like server hostnames and credentials.

Update server details via ThingSet:

```
picocom /dev/pts/123            # replace 123 with printed number
select thingset
=Networking {\"sWebsocketHost\":\"your-server.com\",\"sWebsocketPort\":443,\"sWebsocketAuthToken\":\"your-token\"}
```

Check socket connections

```
ss -t -a -n | grep -E 'State|192.0.2.1'
```

## License

This software is released under the [Apache-2.0 License](LICENSE).
