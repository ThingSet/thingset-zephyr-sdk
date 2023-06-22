SDK
===

Configuration Options
*********************

The ThingSet SDK itself has to be enabled for all subsystems and interfaces:

* :kconfig:option:`CONFIG_THINGSET_SDK`

Below options define the general publication settings:

* :kconfig:option:`CONFIG_THINGSET_PUB_EVENTS_DEFAULT`
* :kconfig:option:`CONFIG_THINGSET_PUB_LIVE_DATA_DEFAULT`
* :kconfig:option:`CONFIG_THINGSET_PUB_LIVE_DATA_PERIOD_DEFAULT`
* :kconfig:option:`CONFIG_THINGSET_PUB_SUMMARY_DEFAULT`
* :kconfig:option:`CONFIG_THINGSET_PUB_SUMMARY_PERIOD_DEFAULT`

Common options for the SDK:

* :kconfig:option:`CONFIG_THINGSET_GENERATE_NODE_ID`
* :kconfig:option:`CONFIG_THINGSET_SHARED_TX_BUF_SIZE`
* :kconfig:option:`CONFIG_THINGSET_SDK_THREAD_STACK_SIZE`
* :kconfig:option:`CONFIG_THINGSET_SDK_THREAD_PRIORITY`

API Reference
*************

.. doxygenfile:: include/thingset/sdk.h
   :project: app
