# ThingSet Zephyr SDK

This repository contains a software development kit (SDK) based on Zephyr RTOS to integrate communication interfaces using the ThingSet protocol into an application with minimum effort.

## Testing with native_posix board

```
west build -b native_posix samples/counter -t run
```

## Testing with LoRaWAN

```
west build -b olimex_lora_stm32wl_devkit samples/counter -- -DOVERLAY_CONFIG=lorawan.conf
```

## Testing with CAN bus

```
west build -b native_posix samples/counter -t run -- -DOVERLAY_CONFIG=can.conf
```

## Testing with WebSocket

Start net-setup from Zephyr net-tools:

```
sudo ../tools/net-tools/net-setup.sh
```

Afterwards run the nativ_posix board with websocket support from another shell:

```
west build -b native_posix samples/counter -t run -- -DOVERLAY_CONFIG=native_websocket.conf
```

Check socket connections

```
ss -t -a -n | grep -E 'State|192.0.2.1'
```

## License

This software is released under the [Apache-2.0 License](LICENSE).
