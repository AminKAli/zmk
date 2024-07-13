if (CONFIG_ZMK_ESB AND CONFIG_ZMK_ESB_RECEIVER)
  target_sources(app PRIVATE receiver.c)
endif()

if (CONFIG_ZMK_ESB AND NOT CONFIG_ZMK_ESB_RECEIVER)
  target_sources(app PRIVATE transmitter.c)
endif()