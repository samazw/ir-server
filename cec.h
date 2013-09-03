#ifndef __CEC_H__
#define __CEC_H__

#define STATE_OFF 0
#define STATE_XBMC 1

int cec_init(int, char*);
int cec_done();

void all_power_off();
void tv_power_on();
void tv_power_off();
void tv_set_input_xbmc();
void philips_hts_power_on();
void philips_hts_set_audio_input(int);
void philips_hts_volume_up();
void philips_hts_volume_down();
void volume_mute();

void check_state(int);

#endif /* __CEC_H__ */
