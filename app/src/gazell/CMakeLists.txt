if (CONFIG_ZMK_GAZELL AND CONFIG_ZMK_GAZELL_RECEIVER)
  target_sources(app PRIVATE receiver.c)
endif()

if (CONFIG_ZMK_GAZELL AND NOT CONFIG_ZMK_GAZELL_RECEIVER)
  target_sources(app PRIVATE transmitter.c)
endif()