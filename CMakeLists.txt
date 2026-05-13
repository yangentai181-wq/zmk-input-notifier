if(CONFIG_ZMK_INPUT_NOTIFIER)
  if(NOT CONFIG_RAW_HID)
    message(FATAL_ERROR "zmk-input-notifier requires CONFIG_RAW_HID=y. Add zmk-raw-hid to your west projects list.")
  endif()

  target_sources(app PRIVATE src/input_notifier.c)
endif()
