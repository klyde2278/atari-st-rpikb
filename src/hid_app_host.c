/* 
 * HID Application Host - Adapter for official TinyUSB
 * This provides backward compatibility with the custom TinyUSB fork API
 */

#include "tusb.h"
#include "hid_app_host.h"
#include "ps4_controller.h"
#include "ssd1306.h"
#include <string.h>

typedef struct {
  uint8_t               dev_addr;
  uint8_t               instance;
  HID_TYPE              hid_type;
  bool                  mounted;
  bool                  has_report_info;
  HID_ReportInfo_t      report_info;
  uint16_t              report_size;
  uint8_t               report_buffer[64];
  void*                 report_dest;
  bool                  report_pending;
} hidh_device_t;

static hidh_device_t hid_devices[CFG_TUH_HID];
static HID_TYPE filter_type = HID_UNDEFINED;

static uint32_t debug_mount_calls    = 0;
static uint32_t debug_report_calls   = 0;
static uint32_t debug_report_copied  = 0;
static uint32_t debug_unmount_calls  = 0;
static uint8_t  debug_last_dev_addr  = 0;
static uint8_t  debug_last_instance  = 0;
static uint8_t  debug_active_devices = 0;
// Descriptor length and detected type of the last mounted device.
// Used by the USB debug page to diagnose receivers with oversized descriptors.
static uint16_t debug_last_desc_len  = 0;
static HID_TYPE debug_last_hid_type  = HID_UNDEFINED;

uint32_t hid_debug_get_mount_calls(void)    { return debug_mount_calls;    }
uint32_t hid_debug_get_report_calls(void)   { return debug_report_calls;   }
uint32_t hid_debug_get_report_copied(void)  { return debug_report_copied;  }
uint32_t hid_debug_get_unmount_calls(void)  { return debug_unmount_calls;  }
uint32_t hid_debug_get_active_devices(void) { return debug_active_devices; }
uint32_t hid_debug_get_last_addr_inst(void) { return (debug_last_dev_addr << 8) | debug_last_instance; }
uint16_t hid_debug_get_last_desc_len(void)  { return debug_last_desc_len;  }
HID_TYPE hid_debug_get_last_hid_type(void)  { return debug_last_hid_type;  }

// Forward declarations - these helpers are defined later in this file.
static hidh_device_t* find_device(uint8_t dev_addr);
static hidh_device_t* find_device_by_inst(uint8_t dev_addr, uint8_t instance);

uint8_t tuh_hid_get_instance(uint8_t dev_addr) { hidh_device_t* dev = find_device(dev_addr); return dev ? dev->instance : 0; }

// ---------------------------------------------------------------------------
// find_device
//
// Key encoding (mirrors tuh_hid_mount_cb):
//   key < 128  : plain USB address
//   key >= 128 : mouse-interface marker — actual_addr = key - 128
// ---------------------------------------------------------------------------
static hidh_device_t* find_device(uint8_t dev_addr) {
  uint8_t actual_addr = (dev_addr >= 128) ? (dev_addr - 128) : dev_addr;

  if (dev_addr >= 128) {
    for (int i = 0; i < CFG_TUH_HID; i++) {
      if (hid_devices[i].dev_addr == actual_addr &&
          hid_devices[i].mounted  &&
          hid_devices[i].hid_type == HID_MOUSE) {
        return &hid_devices[i];
      }
    }
    return NULL;
  }

  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (hid_devices[i].dev_addr == actual_addr && hid_devices[i].mounted) {
      return &hid_devices[i];
    }
  }
  return NULL;
}

static hidh_device_t* find_device_by_inst(uint8_t dev_addr, uint8_t instance) {
  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (hid_devices[i].dev_addr == dev_addr &&
        hid_devices[i].instance == instance &&
        hid_devices[i].mounted) {
      return &hid_devices[i];
    }
  }
  return NULL;
}

static hidh_device_t* alloc_device(uint8_t dev_addr, uint8_t instance) {
  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (!hid_devices[i].mounted) {
      memset(&hid_devices[i], 0, sizeof(hidh_device_t));
      hid_devices[i].dev_addr = dev_addr;
      hid_devices[i].instance = instance;
      hid_devices[i].mounted  = true;
      return &hid_devices[i];
    }
  }
  return NULL;
}

static void free_device(uint8_t dev_addr) {
  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (hid_devices[i].dev_addr == dev_addr) {
      memset(&hid_devices[i], 0, sizeof(hidh_device_t));
      return;
    }
  }
}

bool CALLBACK_HIDParser_FilterHIDReportItem(HID_ReportItem_t* const item)
{
  if (filter_type == HID_UNDEFINED) {
    for (HID_CollectionPath_t* path = item->CollectionPath; path != NULL; path = path->Parent)
    {
      if ((path->Usage.Page  == USAGE_PAGE_GENERIC_DCTRL) && (path->Usage.Usage == USAGE_JOYSTICK)) {
        filter_type = HID_JOYSTICK;
        break;
      }
      else if ((path->Usage.Page  == USAGE_PAGE_GENERIC_DCTRL) && (path->Usage.Usage == USAGE_MOUSE)) {
        filter_type = HID_MOUSE;
        break;
      }
    }

    if (filter_type == HID_UNDEFINED &&
        item->Attributes.Usage.Page == USAGE_PAGE_GENERIC_DCTRL) {
      if (item->Attributes.Usage.Usage == USAGE_X ||
          item->Attributes.Usage.Usage == USAGE_Y) {
        filter_type = HID_MOUSE;
      }
    }
  }

  if (filter_type == HID_JOYSTICK || filter_type == HID_MOUSE) {
    return ((item->Attributes.Usage.Page == USAGE_PAGE_BUTTON) ||
            (item->Attributes.Usage.Page == USAGE_PAGE_GENERIC_DCTRL));
  }
  return false;
}

//--------------------------------------------------------------------+
// Public API Implementation
//--------------------------------------------------------------------+

bool tuh_hid_is_mounted(uint8_t dev_addr) {
  hidh_device_t* dev = find_device(dev_addr);
  return dev && dev->mounted && tuh_hid_mounted(dev->dev_addr, dev->instance);
}

HID_TYPE tuh_hid_get_type(uint8_t dev_addr) {
  hidh_device_t* dev = find_device(dev_addr);
  return dev ? dev->hid_type : HID_UNDEFINED;
}

bool tuh_hid_is_busy(uint8_t dev_addr) {
  hidh_device_t* dev = find_device(dev_addr);
  if (!dev) return false;
  return dev->report_pending;
}

bool hid_app_request_report(uint8_t dev_addr, void * p_report) {
  hidh_device_t* dev = find_device(dev_addr);
  if (!dev) return false;
  dev->report_dest    = p_report;
  dev->report_pending = true;
  return true;
}

uint16_t tuh_hid_get_report_size(uint8_t dev_addr) {
  hidh_device_t* dev = find_device(dev_addr);
  return dev ? dev->report_size : 0;
}

HID_ReportInfo_t* tuh_hid_get_report_info(uint8_t dev_addr) {
  hidh_device_t* dev = find_device(dev_addr);
  // Return NULL if the device does not exist or descriptor parsing failed,
  // so callers can distinguish "no info" from "zeroed info".
  if (!dev || !dev->has_report_info) return NULL;
  return &dev->report_info;
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

static bool try_parse_mouse_descriptor(const uint8_t* report_desc,
                                       uint16_t        desc_len,
                                       HID_ReportInfo_t* out_info)
{
  if (!report_desc || desc_len == 0 || desc_len >= 1024) return false;
  filter_type = HID_UNDEFINED;
  if (USB_ProcessHIDReport(report_desc, desc_len, out_info) != HID_PARSE_Successful)
    return false;
  return (filter_type == HID_MOUSE);
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report_desc, uint16_t desc_len) {
  debug_mount_calls++;
  debug_last_dev_addr = dev_addr;
  debug_last_instance = instance;

  // Record descriptor length immediately — useful even if parsing fails.
  debug_last_desc_len = desc_len;
  debug_last_hid_type = HID_UNDEFINED;

  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  uint8_t protocol = tuh_hid_interface_protocol(dev_addr, instance);

  bool is_ps4 = ps4_is_dualshock4(vid, pid);

  if (is_ps4) {
    hidh_device_t* dev = alloc_device(dev_addr, instance);
    if (!dev) return;
    dev->hid_type    = HID_JOYSTICK;
    dev->report_size = 64;
    dev->has_report_info = false;
    ps4_mount_cb(dev_addr);
    tuh_hid_receive_report(dev_addr, instance);
    debug_last_hid_type = dev->hid_type;
    tuh_hid_mounted_cb(dev_addr);
    return;
  }

  hidh_device_t* dev = alloc_device(dev_addr, instance);
  if (!dev) return;

  debug_active_devices = 0;
  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (hid_devices[i].mounted) debug_active_devices++;
  }

  if (protocol == HID_ITF_PROTOCOL_KEYBOARD) {
    bool is_composite_mouse = false;

    if (report_desc && desc_len > 0 && desc_len < 1024) {
      if (try_parse_mouse_descriptor(report_desc, desc_len, &dev->report_info)) {
        is_composite_mouse   = true;
        dev->hid_type        = HID_MOUSE;
        dev->has_report_info = true;
        dev->report_size     = 64;
      }
    }

    if (!is_composite_mouse) {
      dev->hid_type    = HID_KEYBOARD;
      dev->report_size = sizeof(hid_keyboard_report_t);
    }

    if (is_composite_mouse) {
      tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);
    }

    tuh_hid_receive_report(dev_addr, instance);

    bool first_interface = true;
    for (int i = 0; i < CFG_TUH_HID; i++) {
      if (hid_devices[i].dev_addr == dev_addr &&
          hid_devices[i].mounted  &&
          &hid_devices[i] != dev) {
        first_interface = false;
        break;
      }
    }

    if (first_interface) {
      debug_last_hid_type = dev->hid_type;
      tuh_hid_mounted_cb(dev_addr);
    }
  }
  else if (protocol == HID_ITF_PROTOCOL_MOUSE) {
    if (report_desc && desc_len > 0 && desc_len < 1024) {
      filter_type = HID_UNDEFINED;
      if (USB_ProcessHIDReport(report_desc, desc_len, &dev->report_info) == HID_PARSE_Successful) {
        dev->has_report_info = true;
        dev->hid_type    = HID_MOUSE;
        dev->report_size = 64;
		// Fallback to Report Protocol
        tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT); 
      }
    }

    if (dev->hid_type == HID_UNDEFINED) {
      dev->hid_type    = HID_MOUSE;
      dev->report_size = sizeof(hid_mouse_report_t);
    }

    tuh_hid_receive_report(dev_addr, instance);
    debug_last_hid_type = dev->hid_type;
    tuh_hid_mounted_cb(dev_addr | 0x80);
  }
  else if (report_desc && desc_len > 0 && desc_len < 1024) {
    filter_type = HID_UNDEFINED;
    if (USB_ProcessHIDReport(report_desc, desc_len, &dev->report_info) == HID_PARSE_Successful) {
      dev->has_report_info = true;
      dev->hid_type        = filter_type;
      dev->report_size     = 64;

      tuh_hid_receive_report(dev_addr, instance);

      if (filter_type == HID_KEYBOARD) {
        debug_last_hid_type = dev->hid_type;
        tuh_hid_mounted_cb(dev_addr);
      }
      else if (filter_type == HID_MOUSE) {
        debug_last_hid_type = dev->hid_type;
        tuh_hid_mounted_cb(dev_addr | 0x80);
      }
      else {
        bool first_interface = true;
        for (int i = 0; i < CFG_TUSB_HOST_DEVICE_MAX; i++) {
          if (hid_devices[i].dev_addr == dev_addr &&
              hid_devices[i].mounted  &&
              &hid_devices[i] != dev) {
            first_interface = false;
            break;
          }
        }
        if (first_interface) {
          debug_last_hid_type = dev->hid_type;
          tuh_hid_mounted_cb(dev_addr);
        }
      }
    }
  }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  debug_unmount_calls++;
  // Keep active device count in sync with unmounts.
  if (debug_active_devices > 0) --debug_active_devices;

  hidh_device_t* dev = find_device_by_inst(dev_addr, instance);
  if (!dev) return;

  dev->report_dest    = NULL;
  dev->report_pending = false;

  bool should_notify = true;
  for (int i = 0; i < CFG_TUH_HID; i++) {
    if (hid_devices[i].dev_addr == dev_addr &&
        hid_devices[i].mounted  &&
        hid_devices[i].instance < instance) {
      should_notify = false;
      break;
    }
  }

  if (should_notify) {
    tuh_hid_unmounted_cb(dev_addr);
  }

  memset(dev, 0, sizeof(hidh_device_t));
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  debug_report_calls++;

  if (!report || len == 0 || len > 64) return;

  hidh_device_t* dev = find_device_by_inst(dev_addr, instance);
  if (!dev || !dev->mounted) return;

  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  if (ps4_is_dualshock4(vid, pid)) {
    ps4_process_report(dev_addr, report, len);
    tuh_hid_receive_report(dev_addr, instance);
    return;
  }

  uint16_t copy_len = (len < 64) ? len : 64;
  memcpy(dev->report_buffer, report, copy_len);

  if (dev->report_dest && dev->report_pending) {
    debug_report_copied++;
    memcpy(dev->report_dest, report, copy_len);
    dev->report_pending = false;
    extern void tuh_hid_isr(uint8_t dev_addr, xfer_result_t event);
    tuh_hid_isr(dev_addr, XFER_RESULT_SUCCESS);
    dev->report_dest = NULL;
  }

  tuh_hid_receive_report(dev_addr, instance);
}