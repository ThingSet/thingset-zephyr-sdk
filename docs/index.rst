==========================
ThingSet SDK Documentation
==========================

`ThingSet <https://thingset.io>`_ is a transport-agnostic and self-explanatory remote API for
embedded devices and humans.

The
`ThingSet software development (SDK) for Zephyr <https://github.com/ThingSet/thingset-zephyr-sdk>`_
leverages the Zephyr RTOS APIs to provide ready-to-use implementations for multiple different
interfaces / subsystems (see navigation at the left).

Further interfaces like MQTT and WebSocket over WiFi are currently under development.

The `ThingSet node library <https://github.com/ThingSet/thingset-node-c>`_ is used internally for
data processing.

This documentation is licensed under the Creative Commons Attribution-ShareAlike 4.0 International
(CC BY-SA 4.0) License.

.. image:: static/images/cc-by-sa-centered.png

The full license text is available at `<https://creativecommons.org/licenses/by-sa/4.0/>`_.

.. _Zephyr RTOS: https://zephyrproject.org
.. _ThingSet: https://thingset.io

.. toctree::
    :caption: Interfaces
    :hidden:

    interfaces/serial
    interfaces/shell
    interfaces/ble
    interfaces/can
    interfaces/lorawan

.. toctree::
    :caption: Subsystems
    :hidden:

    subsys/sdk
    subsys/log_backend
    subsys/storage

.. toctree::
    :caption: Kconfig
    :hidden:

    kconfig
