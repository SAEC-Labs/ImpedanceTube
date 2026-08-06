/*
 * The MIT License (MIT)
 */

#include "tusb.h"
#include <string.h>

#define USB_PID 0x4003

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+

static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,

    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = 0xCafe,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01};

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

enum { ITF_NUM_AUDIO_CONTROL = 0, ITF_NUM_AUDIO_STREAMING, ITF_NUM_TOTAL };

#define CONFIG_TOTAL_LEN                                                       \
  (TUD_CONFIG_DESC_LEN + CFG_TUD_AUDIO * TUD_AUDIO20_MIC_ONE_CH_DESC_LEN)

#define EPNUM_AUDIO 0x01

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    TUD_AUDIO20_MIC_ONE_CH_DESCRIPTOR(
        ITF_NUM_AUDIO_CONTROL, 0, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX,
        CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX * 8, 0x80 | EPNUM_AUDIO,
        CFG_TUD_AUDIO_EP_SZ_IN)};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
};

char const *string_desc_arr[] = {(const char[]){0x09, 0x04}, "PaniRCorp",
                                 "MicNode", NULL};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;

  size_t chr_count;

  switch (index) {
  case STRID_LANGID:
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
    break;

  case STRID_SERIAL: {
    const char *serial = "STM32F401_001";

    chr_count = strlen(serial);

    for (size_t i = 0; i < chr_count; i++) {
      _desc_str[1 + i] = serial[i];
    }

    break;
  }

  default: {
    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
      return NULL;
    }

    const char *str = string_desc_arr[index];

    chr_count = strlen(str);

    size_t max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;

    if (chr_count > max_count) {
      chr_count = max_count;
    }

    for (size_t i = 0; i < chr_count; i++) {
      _desc_str[1 + i] = str[i];
    }

    break;
  }
  }

  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

  return _desc_str;
}

//--------------------------------------------------------------------+
// Audio Class Control Callbacks
//--------------------------------------------------------------------+

static uint32_t sampFreq = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
static uint8_t clkValid = 1;

static audio20_control_range_4_n_t(1) sampleFreqRng = {
    .wNumSubRanges = 1,
    .subrange[0] = {.bMin = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE,
                    .bMax = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE,
                    .bRes = 0}};

static uint8_t mute[2] = {0, 0};
static int16_t volume[2] = {0, 0};
static audio20_control_range_2_n_t(1) volumeRng = {
    .wNumSubRanges = 1, .subrange[0] = {.bMin = -90, .bMax = 90, .bRes = 1}};

bool tud_audio_set_req_ep_cb(uint8_t rhport,
                             tusb_control_request_t const *p_request,
                             uint8_t *pBuff) {
  (void)rhport;
  (void)p_request;
  (void)pBuff;
  return false;
}

bool tud_audio_set_req_itf_cb(uint8_t rhport,
                              tusb_control_request_t const *p_request,
                              uint8_t *pBuff) {
  (void)rhport;
  (void)p_request;
  (void)pBuff;
  return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *p_request,
                                 uint8_t *pBuff) {
  (void)rhport;

  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  if (p_request->bRequest == AUDIO20_CS_REQ_CUR) {
    if (entityID == 2) {
      if (ctrlSel == AUDIO20_FU_CTRL_MUTE && channelNum < 2) {
        mute[channelNum] = ((audio20_control_cur_1_t *)pBuff)->bCur;
        return true;
      }
      if (ctrlSel == AUDIO20_FU_CTRL_VOLUME && channelNum < 2) {
        volume[channelNum] = ((audio20_control_cur_2_t *)pBuff)->bCur;
        return true;
      }
    }
    if (entityID == 4) {
      if (ctrlSel == AUDIO20_CS_CTRL_SAM_FREQ) {
        sampFreq = ((audio20_control_cur_4_t *)pBuff)->bCur;
        return true;
      }
    }
  }
  return false;
}

bool tud_audio_get_req_ep_cb(uint8_t rhport,
                             tusb_control_request_t const *p_request) {
  (void)rhport;
  (void)p_request;
  return false;
}

bool tud_audio_get_req_itf_cb(uint8_t rhport,
                              tusb_control_request_t const *p_request) {
  (void)rhport;
  (void)p_request;
  return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *p_request) {
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  if (entityID == 1) {
    if (ctrlSel == AUDIO20_TE_CTRL_CONNECTOR) {
      audio20_desc_channel_cluster_t ret = {
          .bNrChannels = 1, .bmChannelConfig = 0, .iChannelNames = 0};
      return tud_audio_buffer_and_schedule_control_xfer(
          rhport, p_request, (void *)&ret, sizeof(ret));
    }
  }

  if (entityID == 2) {
    if (ctrlSel == AUDIO20_FU_CTRL_MUTE && channelNum < 2) {
      return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request,
                                                        &mute[channelNum], 1);
    }
    if (ctrlSel == AUDIO20_FU_CTRL_VOLUME && channelNum < 2) {
      if (p_request->bRequest == AUDIO20_CS_REQ_CUR) {
        return tud_audio_buffer_and_schedule_control_xfer(
            rhport, p_request, &volume[channelNum], sizeof(volume[channelNum]));
      }
      if (p_request->bRequest == AUDIO20_CS_REQ_RANGE) {
        return tud_audio_buffer_and_schedule_control_xfer(
            rhport, p_request, (void *)&volumeRng, sizeof(volumeRng));
      }
    }
  }

  if (entityID == 4) {
    if (ctrlSel == AUDIO20_CS_CTRL_SAM_FREQ) {
      if (p_request->bRequest == AUDIO20_CS_REQ_CUR) {
        return tud_audio_buffer_and_schedule_control_xfer(
            rhport, p_request, &sampFreq, sizeof(sampFreq));
      }
      if (p_request->bRequest == AUDIO20_CS_REQ_RANGE) {
        return tud_audio_buffer_and_schedule_control_xfer(
            rhport, p_request, (void *)&sampleFreqRng, sizeof(sampleFreqRng));
      }
    }
    if (ctrlSel == AUDIO20_CS_CTRL_CLK_VALID) {
      return tud_audio_buffer_and_schedule_control_xfer(
          rhport, p_request, &clkValid, sizeof(clkValid));
    }
  }

  return false;
}

bool tud_audio_set_itf_cb(uint8_t rhport,
                          tusb_control_request_t const *p_request) {
  (void)rhport;
  (void)p_request;
  return true;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport,
                                   tusb_control_request_t const *p_request) {
  (void)rhport;
  (void)p_request;
  return true;
}