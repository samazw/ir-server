/*

parts were taken from

pidvbip - tvheadend client for the Raspberry Pi

(C) Dave Chapman 2012-2013

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/timeb.h>
#include <libcec/cecc.h>

#include "cec.h"
#include "log.h"

#define CEC_CONFIG_VERSION CEC_CLIENT_VERSION_CURRENT

struct timeb last_volume;

/* Global variables to hold the libcec status */
static libcec_configuration cec_config;
static ICECCallbacks        cec_callbacks;

static int CecLogMessage(void *(cbParam), const cec_log_message message)
{
  //fprintf(stderr,"LOG (%d): %s\n",message.level,message.message);

  return 0;
}

static int CecKeyPress(void *(cbParam), const cec_keypress (key))
{
  mylog("Key: %d, duration: %d\n",key.keycode,key.duration);

  return 0;
}

static int CecCommand(void *(cbParam), const cec_command (command))
{
  switch (command.opcode) {
    case CEC_OPCODE_STANDBY:
      mylog("<Standby> command received\n");
      break;
    default:
      mylog("Cmd: initiator=%d, destination=%d, opcode=0x%02x\n",command.initiator,command.destination,command.opcode);
      break;
  }
  return 0;
}

static void CecSourceActivated(void *(cbParam),const cec_logical_address logical_address, const uint8_t activated)
{
  mylog("[CEC] Source %sactivated\n",(activated ? "" : "de"));
}

static int CecAlert(void *(cbParam), const libcec_alert type, const libcec_parameter (param))
{
  mylog("CecAlert - type=%d\n",type);
  switch (type)
    {
    case CEC_ALERT_CONNECTION_LOST:
      printf("Connection lost - exiting\n");
      exit(1);
      break;
    default:
      break;
    }
  return 0;
}

static void clear_cec_logical_addresses(cec_logical_addresses* addresses)
{
  int i;

  addresses->primary = CECDEVICE_UNREGISTERED;
  for (i = 0; i < 16; i++)
    addresses->addresses[i] = 0;
}

static void set_cec_logical_address(cec_logical_addresses* addresses, cec_logical_address address)
{
  if (addresses->primary == CECDEVICE_UNREGISTERED)
    addresses->primary = address;

  addresses->addresses[(int) address] = 1;
}

static void clear_config(libcec_configuration* config)
{
  int i;

  config->iPhysicalAddress =                CEC_PHYSICAL_ADDRESS_TV;
  config->baseDevice = (cec_logical_address)CEC_DEFAULT_BASE_DEVICE;
  config->iHDMIPort =                       CEC_DEFAULT_HDMI_PORT;
  config->tvVendor =              (uint64_t)CEC_VENDOR_UNKNOWN;
  config->clientVersion =         (uint32_t)CEC_CLIENT_VERSION_CURRENT;
  config->serverVersion =         (uint32_t)CEC_SERVER_VERSION_CURRENT;
  config->bAutodetectAddress =              0;
  config->bGetSettingsFromROM =             CEC_DEFAULT_SETTING_GET_SETTINGS_FROM_ROM;
  config->bUseTVMenuLanguage =              CEC_DEFAULT_SETTING_USE_TV_MENU_LANGUAGE;
  config->bActivateSource =                 CEC_DEFAULT_SETTING_ACTIVATE_SOURCE;
  config->bPowerOffScreensaver =            CEC_DEFAULT_SETTING_POWER_OFF_SCREENSAVER;
  config->bPowerOffOnStandby =              CEC_DEFAULT_SETTING_POWER_OFF_ON_STANDBY;
  config->bShutdownOnStandby =              CEC_DEFAULT_SETTING_SHUTDOWN_ON_STANDBY;
  config->bSendInactiveSource =             CEC_DEFAULT_SETTING_SEND_INACTIVE_SOURCE;
  config->iFirmwareVersion =                CEC_FW_VERSION_UNKNOWN;
  config->bPowerOffDevicesOnStandby =       CEC_DEFAULT_SETTING_POWER_OFF_DEVICES_STANDBY;
  memcpy(config->strDeviceLanguage,         CEC_DEFAULT_DEVICE_LANGUAGE, 3);
  config->iFirmwareBuildDate =              CEC_FW_BUILD_UNKNOWN;
  config->bMonitorOnly =                    0;
  config->cecVersion =         (cec_version)CEC_DEFAULT_SETTING_CEC_VERSION;
  config->adapterType =                     ADAPTERTYPE_UNKNOWN;
  config->iDoubleTapTimeoutMs =             CEC_DOUBLE_TAP_TIMEOUT_MS;
  config->comboKey =                        CEC_USER_CONTROL_CODE_STOP;
  config->iComboKeyTimeoutMs =              CEC_DEFAULT_COMBO_TIMEOUT_MS;

  memset(config->strDeviceName, 0, 13);

  /* deviceTypes.Clear(); */
  for (i = 0; i < 5; i++)
    config->deviceTypes.types[i] = CEC_DEVICE_TYPE_RESERVED;

  clear_cec_logical_addresses(&config->logicalAddresses);
  clear_cec_logical_addresses(&config->wakeDevices);
  clear_cec_logical_addresses(&config->powerOffDevices);

#if CEC_DEFAULT_SETTING_POWER_OFF_SHUTDOWN == 1
  set_cec_logical_address(&config->powerOffDevices,CECDEVICE_BROADCAST);
#endif

#if CEC_DEFAULT_SETTING_ACTIVATE_SOURCE == 1
  set_cec_logical_address(&config->wakeDevices,CECDEVICE_TV);
#endif

  config->callbackParam = NULL;
  config->callbacks     = NULL;
}

static void clear_callbacks(ICECCallbacks* cec_callbacks)
{
  cec_callbacks->CBCecLogMessage           = NULL;
  cec_callbacks->CBCecKeyPress             = NULL;
  cec_callbacks->CBCecCommand              = NULL;
  cec_callbacks->CBCecConfigurationChanged = NULL;
  cec_callbacks->CBCecAlert                = NULL;
  cec_callbacks->CBCecMenuStateChanged     = NULL;
  cec_callbacks->CBCecSourceActivated      = NULL;
}

int cec_init(int init_video, char* msgqueue)
{
  /* Set the CEC configuration */
  clear_config(&cec_config);
  snprintf(cec_config.strDeviceName, 13, "pidvbip");
  cec_config.clientVersion       = CEC_CONFIG_VERSION;
  cec_config.bActivateSource     = 0;

  /* Say we are a recording/tuner/playback device */
  cec_config.deviceTypes.types[0] = CEC_DEVICE_TYPE_RECORDING_DEVICE;

  /* Set the callbacks */
  clear_callbacks(&cec_callbacks);
  cec_callbacks.CBCecLogMessage  = &CecLogMessage;
  cec_callbacks.CBCecKeyPress    = &CecKeyPress;
  cec_callbacks.CBCecCommand     = &CecCommand;
  cec_callbacks.CBCecAlert       = &CecAlert;
  cec_callbacks.CBCecSourceActivated= &CecSourceActivated;

  cec_config.callbacks           = &cec_callbacks;

  /* Initialise the library */
  if (!cec_initialise(&cec_config)) {
    mylog("Error initialising libcec, aborting\n");
    return 1;
  }

  if (init_video) {
    /* init video on targets that need this */
    cec_init_video_standalone();
  }

  /* Locate CEC device */
  cec_adapter devices[10];
  int nadapters = cec_find_adapters(devices, 10, NULL);

  if (nadapters <= 0) {
    mylog("Error, no CEC adapters found.\n");
    cec_destroy();
    return 2;
  }

  if (nadapters > 1) {
    mylog("WARNING: %d adapters found, using first.\n",nadapters);
  }

  mylog("Using CEC adapter \"%s\", path=\"%s\"\n",devices[0].comm,devices[0].path);

  /* Open device with a 10000ms (10s) timeout */
  if (!cec_open(devices[0].comm, CEC_DEFAULT_CONNECT_TIMEOUT)) {
    mylog("Error, cannot open device %s\n",devices[0].comm);
    cec_destroy();
    return 3;
  }
  mylog("Device open");

  /* Enable callbacks, first parameter is the callback data passed to every callback */
  cec_enable_callbacks(msgqueue, &cec_callbacks);

  /* Get the menu language of the TV */
  cec_menu_language language;
  cec_get_device_menu_language(CEC_DEFAULT_BASE_DEVICE, &language);
  mylog("TV menu language: \"%c%c%c\"\n",language.language[0],language.language[1],language.language[2]);

  /* Get the power status of the TV */
  cec_power_status power_status = cec_get_device_power_status(CEC_DEFAULT_BASE_DEVICE);
  mylog("TV Power Status:  %d\n",power_status);

	ftime(&last_volume); 

  return 0;
}

int cec_done(int poweroff)
{
  if (poweroff) {
    /* Power-off the TV */
    cec_standby_devices(CEC_DEFAULT_BASE_DEVICE);
  }

  /* Cleanup */
  cec_destroy();

  return 0;
}

void tv_power_on() {
	cec_power_on_devices(CECDEVICE_TV);
}

void tv_power_off() {
	cec_standby_devices(CECDEVICE_TV);
}

void tv_set_input_xbmc() {
	cec_command command;

	// select input "tx 1f 82 13 00"
        command.initiator = 1;
        command.destination = 0xf;
        command.opcode = 0x82;
        command.parameters.data[0] = 0x13;
        command.parameters.data[1] = 0x00;
        command.parameters.size = 2;
        command.opcode_set = 1;
        cec_transmit(&command);
}

void all_power_off() {
	cec_standby_devices(0xF);
}

void philips_hts_power_on() {
	cec_send_keypress(CECDEVICE_AUDIOSYSTEM, 0x6d, true);
	cec_send_key_release(CECDEVICE_AUDIOSYSTEM, true);
	// sleep(13);
	// set_audio_input(1);
}

void philips_hts_power_off() {
	cec_standby_devices(CECDEVICE_AUDIOSYSTEM);
}

void philips_hts_set_audio_input(int input) {
	cec_command command;

	command.initiator = 1;
	command.destination = CECDEVICE_AUDIOSYSTEM;
	command.opcode = 0x44;
	command.parameters.data[0] = 0x6a;
	command.parameters.data[1] = (uint8_t) input;
	command.parameters.size = 2;
	command.opcode_set = 1;
	cec_transmit(&command);
	cec_send_key_release(CECDEVICE_AUDIOSYSTEM, true);
} 


void philips_hts_volume_up() {
	struct timeb now;
	cec_volume_up(true);
	ftime(&now); 
	if ((1000.0 * (now.time - last_volume.time) + (now.millitm - last_volume.millitm)) > 4000) {
		cec_volume_up(true);
	}
	memcpy(&last_volume, &now, sizeof(struct timeb));
}

void philips_hts_volume_down() {
	struct timeb now;
	cec_volume_down(true);
	ftime(&now); 
	if ((1000.0 * (now.time - last_volume.time) + (now.millitm - last_volume.millitm)) > 4000) {
		cec_volume_down(true);
	}
	memcpy(&last_volume, &now, sizeof(struct timeb));
}

void volume_mute() {
	cec_mute_audio(true);
}

cec_power_status last_tv_state = CEC_POWER_STATUS_STANDBY;
cec_power_status last_hts_state = CEC_POWER_STATUS_STANDBY;

void check_state(int desired_state) {
	cec_power_status tv_state, hts_state;
	cec_command command;

	mylog("Statecheck\n");

	command.initiator = 1;
	command.destination = CECDEVICE_TV;
	command.opcode = 0x8f;
	command.parameters.size = 0;
	command.opcode_set = 1;
	cec_transmit(&command);
	command.initiator = 1;
	command.destination = CECDEVICE_AUDIOSYSTEM;
	command.opcode = 0x8f;
	command.parameters.size = 0;
	command.opcode_set = 1;
	cec_transmit(&command);
	tv_state = cec_get_device_power_status(CECDEVICE_TV);
	hts_state = cec_get_device_power_status(CECDEVICE_AUDIOSYSTEM);

	if (desired_state == STATE_XBMC) {
		if (tv_state != CEC_POWER_STATUS_ON) {
			tv_power_on();
		} else if (last_tv_state != tv_state) {
			tv_set_input_xbmc();
		}
		if (hts_state != CEC_POWER_STATUS_ON) {
			philips_hts_power_on();
		} else if (last_hts_state != hts_state) {
			philips_hts_set_audio_input(1);
		}
	} else if (desired_state == STATE_OFF) {
		if (tv_state == CEC_POWER_STATUS_ON) {
			tv_power_off();
		}
		if (hts_state == CEC_POWER_STATUS_ON) {
			philips_hts_power_off();
		}
	}
	last_tv_state = tv_state;
	last_hts_state = hts_state;
}
