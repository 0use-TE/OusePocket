// SPDX-License-Identifier: MIT
#include "ouse_usb_private.h"
#include "esp_mac.h"
#include <stdio.h>

// Audio control + two streaming interfaces, followed by CDC NCM control/data.
// Each streaming direction has one altsetting (PCM16 mono 16 kHz).
#define AUDIO_DESC(interval) \
    TUD_AUDIO_DESC_IAD(0,3,2), \
    TUD_AUDIO_DESC_STD_AC(0,0,2), \
    TUD_AUDIO_DESC_CS_AC(0x0200,AUDIO_FUNC_HEADSET,66,0), \
    TUD_AUDIO_DESC_CLK_SRC(4,AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK,5,0,0), \
    TUD_AUDIO_DESC_INPUT_TERM(1,AUDIO_TERM_TYPE_USB_STREAMING,0,4,1,0,0,0,0), \
    TUD_AUDIO_DESC_OUTPUT_TERM(3,AUDIO_TERM_TYPE_OUT_GENERIC_SPEAKER,0,1,4,0,4), \
    TUD_AUDIO_DESC_INPUT_TERM(0x11,AUDIO_TERM_TYPE_IN_GENERIC_MIC,0,4,1,0,0,0,5), \
    TUD_AUDIO_DESC_OUTPUT_TERM(0x13,AUDIO_TERM_TYPE_USB_STREAMING,0,0x11,4,0,0), \
    TUD_AUDIO_DESC_STD_AS_INT(1,0,0,4), \
    TUD_AUDIO_DESC_STD_AS_INT(1,1,2,4), \
    TUD_AUDIO_DESC_CS_AS_INT(1,0,AUDIO_FORMAT_TYPE_I,AUDIO_DATA_FORMAT_TYPE_I_PCM,1,0,0), \
    TUD_AUDIO_DESC_TYPE_I_FORMAT(2,16), \
    TUD_AUDIO_DESC_STD_AS_ISO_EP(0x01,TUSB_XFER_ISOCHRONOUS|TUSB_ISO_EP_ATT_ASYNCHRONOUS|TUSB_ISO_EP_ATT_DATA,34,interval), \
    TUD_AUDIO_DESC_CS_AS_ISO_EP(0,0,0,0), \
    TUD_AUDIO_DESC_STD_AS_ISO_FB_EP(0x82,4,interval), \
    TUD_AUDIO_DESC_STD_AS_INT(2,0,0,5), \
    TUD_AUDIO_DESC_STD_AS_INT(2,1,1,5), \
    TUD_AUDIO_DESC_CS_AS_INT(0x13,0,AUDIO_FORMAT_TYPE_I,AUDIO_DATA_FORMAT_TYPE_I_PCM,1,0,0), \
    TUD_AUDIO_DESC_TYPE_I_FORMAT(2,16), \
    TUD_AUDIO_DESC_STD_AS_ISO_EP(0x81,TUSB_XFER_ISOCHRONOUS|TUSB_ISO_EP_ATT_ASYNCHRONOUS|TUSB_ISO_EP_ATT_DATA,34,interval), \
    TUD_AUDIO_DESC_CS_AS_ISO_EP(0,0,0,0)
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN+CFG_TUD_AUDIO_FUNC_1_DESC_LEN+TUD_CDC_NCM_DESC_LEN)
static const uint8_t fs_config[]={
    TUD_CONFIG_DESCRIPTOR(1,5,0,CONFIG_LEN,0,500),
    AUDIO_DESC(1),
    TUD_CDC_NCM_DESCRIPTOR(3,6,7,0x83,64,0x04,0x84,64,1514)
};
// DWC2 arms ISO OUT for the next microframe parity. Poll every microframe
// at HS so a host phase at bInterval=4 cannot leave OUT waiting indefinitely.
static const uint8_t hs_config[]={
    TUD_CONFIG_DESCRIPTOR(1,5,0,CONFIG_LEN,0,500),
    AUDIO_DESC(1),
    TUD_CDC_NCM_DESCRIPTOR(3,6,7,0x83,64,0x04,0x84,512,1514)
};
_Static_assert(sizeof(fs_config)==CONFIG_LEN,"USB audio descriptor length");
_Static_assert(sizeof(hs_config)==CONFIG_LEN,"USB HS descriptor length");
static const tusb_desc_device_t device={
    .bLength=sizeof(tusb_desc_device_t),.bDescriptorType=TUSB_DESC_DEVICE,.bcdUSB=0x0200,
    .bDeviceClass=TUSB_CLASS_MISC,.bDeviceSubClass=MISC_SUBCLASS_COMMON,.bDeviceProtocol=MISC_PROTOCOL_IAD,
    .bMaxPacketSize0=64,.idVendor=0x303a,.idProduct=0x4019,.bcdDevice=0x0142,
    .iManufacturer=1,.iProduct=2,.iSerialNumber=3,.bNumConfigurations=1
};
static const tusb_desc_device_qualifier_t qualifier={
    .bLength=sizeof(tusb_desc_device_qualifier_t),.bDescriptorType=TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB=0x0200,.bDeviceClass=TUSB_CLASS_MISC,.bDeviceSubClass=MISC_SUBCLASS_COMMON,
    .bDeviceProtocol=MISC_PROTOCOL_IAD,.bMaxPacketSize0=64,.bNumConfigurations=1
};
static char serial[13],mac_text[13];
static const char *strings[]={"\x09\x04","Ouse","Ouse Pocket USB",serial,
    "Ouse Speaker","Ouse Microphone","Ouse Wi-Fi Sharing",mac_text};
// esp_tinyusb 2.0.1~1 accepts at most eight strings including LANGID.
_Static_assert(sizeof(strings)/sizeof(strings[0])<=8,"esp_tinyusb string capacity");
void ouse_usb_descriptors(tinyusb_config_t *config)
{
    uint8_t mac[6];esp_read_mac(mac,ESP_MAC_BASE);
    snprintf(serial,sizeof(serial),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    mac[0]=(mac[0]|2)&0xfe;mac[5]^=0x80;
    snprintf(mac_text,sizeof(mac_text),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    config->descriptor=(tinyusb_desc_config_t){.device=&device,.qualifier=&qualifier,
        .full_speed_config=fs_config,.high_speed_config=hs_config,
        .string=strings,.string_count=sizeof(strings)/sizeof(strings[0])};
}
