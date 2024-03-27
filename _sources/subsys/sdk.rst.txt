SDK
===

Configuration Options
*********************

The ThingSet SDK itself has to be enabled for all subsystems and interfaces:

* :kconfig:option:`CONFIG_THINGSET_SDK`

Below options define the general publication settings:

* :kconfig:option:`CONFIG_THINGSET_SUBSET_LIVE_METRICS`
* :kconfig:option:`CONFIG_THINGSET_REPORTING_LIVE_ENABLE_PRESET`
* :kconfig:option:`CONFIG_THINGSET_REPORTING_LIVE_PERIOD_PRESET`
* :kconfig:option:`CONFIG_THINGSET_SUBSET_SUMMARY_METRICS`
* :kconfig:option:`CONFIG_THINGSET_REPORTING_SUMMARY_ENABLE_PRESET`
* :kconfig:option:`CONFIG_THINGSET_REPORTING_SUMMARY_PERIOD_PRESET`

Common options for the SDK:

* :kconfig:option:`CONFIG_THINGSET_GENERATE_NODE_ID`
* :kconfig:option:`CONFIG_THINGSET_SHARED_TX_BUF_SIZE`
* :kconfig:option:`CONFIG_THINGSET_SDK_THREAD_STACK_SIZE`
* :kconfig:option:`CONFIG_THINGSET_SDK_THREAD_PRIORITY`

API Reference
*************

.. doxygenfile:: include/thingset/sdk.h
   :project: app
