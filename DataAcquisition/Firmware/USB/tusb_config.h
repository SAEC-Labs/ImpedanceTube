/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software.
 */

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C"
{
#endif

    //--------------------------------------------------------------------+
    // Board Specific Configuration
    //--------------------------------------------------------------------+

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT 0
#endif

#ifndef CFG_TUSB_RHPORT0_MODE
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#endif

    //--------------------------------------------------------------------
    // COMMON CONFIGURATION
    //--------------------------------------------------------------------+

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU OPT_MCU_STM32F4
#endif

#ifndef CFG_TUSB_RHPORT0_MODE
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

    // Enable device stack

#define CFG_TUD_ENABLED 1

#define CFG_TUD_MAX_SPEED BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

    //--------------------------------------------------------------------
    // DEVICE CONFIGURATION
    //--------------------------------------------------------------------+

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

    //------------- CLASS -------------//

#define CFG_TUD_AUDIO 1

#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

    //--------------------------------------------------------------------
    // AUDIO CLASS CONFIGURATION
    //--------------------------------------------------------------------+

#define CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE 48000

#define CFG_TUD_AUDIO_ENABLE_EP_IN 1

    // Audio format

#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX 2

#define CFG_TUD_AUDIO_FUNC_1_N_BITS_PER_SAMPLE_TX 16

#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX 1

    /*
     * STM32F401 USB is Full Speed only.
     *
     * TUD_AUDIO_EP_SIZE() first argument:
     *
     * 0 = Full Speed
     * 1 = High Speed
     *
     */

#define CFG_TUD_AUDIO_EP_SZ_IN                      \
    TUD_AUDIO_EP_SIZE(                              \
        0,                                          \
        CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE,           \
        CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX, \
        CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX)

#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX \
    CFG_TUD_AUDIO_EP_SZ_IN

#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ \
    (4 * CFG_TUD_AUDIO_EP_SZ_IN)

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */