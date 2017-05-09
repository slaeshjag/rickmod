#ifndef MIXASTLEY_H__
#define	MIXASTLEY_H__

#define	MA_SAMPLE_BUFFER_LEN 8
#define	MA_CHANNELS_POT 1
#define	MA_CHANNELS (1 << MA_CHANNELS_POT)


struct MAState {
	uint32_t		target_rate;
	uint32_t		fraction_per_sample; // 16 bit = 1.0

	int32_t			last_sample;
	int32_t			cur_sample;
	int32_t			sample_pos; // 16 bit = 1.0

	int32_t			buffer[1 << MA_CHANNELS_POT];
	int32_t			volume;
	int			next_sample;

	void			(*get_next_sample)(void *ptr, uint8_t *data);
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


#endif
