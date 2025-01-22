# ThingSet SDK unit tests

## Run unit tests

With twister:

    ../zephyr/scripts/twister -T ./tests --integration -v -n

Manually (`tests/can` used as an example):

    west build -b native_sim -T tests/can/thingset_sdk.can -t run
