# ThingSet Zephyr SDK

This repository contains a software development kit (SDK) based on Zephyr RTOS to integrate communication interfaces using the ThingSet protocol into an application with minimum effort.

## Testing with native_posix board

```
west build -b native_posix samples/serial -t run
```

## Testing with LoRaWAN

```
west build -b olimex_lora_stm32wl_devkit samples/serial -- -DOVERLAY_CONFIG=lorawan.conf
```
