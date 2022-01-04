#ifndef MIXASTLEY_H__
#define	MIXASTLEY_H__

#include <stdint.h>

#define	MA_SAMPLE_BUFFER_LEN 8
#define	MA_CHANNELS_POT 1
#define	MA_CHANNELS (1 << MA_CHANNELS_POT)


struct MAState {
	uint32_t		target_rate;
	uint32_t		fraction_per_sample; // 16 bit = 1.0

	int32_t			last_sample;
	int32_t			cur_sample;
	int32_t			sample_pos; // 16 bit = 1.0

	int32_t			buffer[1 << MA_SAMPLE_BUFFER_LEN];
	int32_t			volume;
	int			next_sample;

	#ifdef TRACKER
	int			mute;
	#endif

	void			(*get_next_sample)(void *ptr, int8_t *data);
	void			*ptr;

	/* filter */
	struct {
		int32_t		d1;
		int32_t		d2;
		int32_t		f1;
	} filter;
};



struct MAMix {
	struct MAState		left[MA_CHANNELS];
	struct MAState	right[MA_CHANNELS];
};

void ma_add(struct MAState *rs, int32_t *sample, int samples);
struct MAMix ma_mix_create(int sample_rate);
void ma_mix8(struct MAMix *mix, uint8_t *buff, int samples);
void ma_set_callback(struct MAState *rs, void (*next_sample)(void *ptr, int8_t *buff), void *ptr);
void ma_set_volume(struct MAState *rs, int volume);
void ma_set_samplerate(struct MAState *rs, int samplerate);
struct MAState ma_init(int target_sample_rate);

#endif
