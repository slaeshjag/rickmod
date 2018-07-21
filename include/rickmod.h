#ifndef RICKMOD_H_
#define	RICKMOD_H_

#include "mixastley.h"
#include <stdint.h>

struct RickmodState;

struct RickmodChannelEffect {
	uint16_t		note;
	uint16_t		row_note;
	uint8_t			reset_note;
	uint8_t			sample;
	uint16_t		effect;
	uint8_t			volume;
	uint8_t			finetune;

	uint16_t		portamento_target;
	uint8_t			portamento_speed;
	uint8_t			portamento_vol;
	uint8_t			arpeggio_save;
	uint8_t			vibrato_pos;
	uint8_t			vibrato_speed;
	uint8_t			vibrato_wave;
	uint8_t			vibrato_vol;
	uint16_t		last_vibrato;
	uint8_t			tremolo_pos;
	uint8_t			tremolo_speed;
	uint8_t			tremolo_wave;
	int8_t			last_tremolo;
	uint8_t			delay_ticks;
	uint8_t			sample_pos;

	uint8_t			loop_count;
	uint8_t			loop_row;
	uint8_t			volume_slide;
	uint8_t			retrig;
};


struct RickmodChannel {
	uint16_t		note;
	uint16_t		effect;
	uint8_t			sample;
};


struct RickmodRow {
	struct RickmodChannel	channel[4];
};


struct RickmodPattern {
	struct RickmodRow	row[64];
};


struct RickmodSample {
	char			name[23];
	uint32_t		length;
	uint8_t			finetune;
	uint8_t			volume;
	uint32_t		repeat;
	uint32_t		repeat_length;
	int8_t			*sample_data;
};


struct RickmodChannelState {
	struct RickmodState	*rm;
	int			channel;
	uint8_t			sample;
	uint8_t			play_sample;
	uint8_t			trigger;
	uint32_t		sample_pos;

	struct RickmodChannelEffect rce;
};


struct RickmodState {
	char			name[21];
	uint8_t			*data;

	uint8_t			samples;
	uint8_t			*pattern_lookup;
	uint8_t			patterns;
	uint8_t			song_length;
	struct RickmodSample	sample[31];
	struct RickmodPattern	pattern[128];

	struct MAState		mix[4];
	struct RickmodChannelState channel[4];
	uint16_t		samplerate;
	uint8_t			repeat;
	uint8_t			end;

	void			(*row_callback)(void *data);
	void			*user_data;

	struct {
		uint8_t		pattern;
		uint8_t		translated_pattern;
		uint8_t		row;

		uint8_t		bpm;
		uint8_t		speed; // Number of ticks per row, effectively
		uint16_t	samples_per_tick; // bpm is tied to ticks per minute
		uint16_t	samples_this_tick;
		uint16_t	tick;

		uint8_t		set_on_tick;

		uint8_t		next_pattern;
		uint8_t		next_row;
	} cur;

};

struct RickmodState *rm_init(int sample_rate, uint8_t *mod, int mod_len);
void rm_mix_s16(struct RickmodState *rm, int16_t *buff, int samples);
void rm_mix_u8(struct RickmodState *rm, uint8_t *buff, int samples);
void rm_repeat_set(struct RickmodState *rm, uint8_t repeat);
uint8_t rm_end_reached(struct RickmodState *rm);
void rm_free(struct RickmodState *rm);
void rm_row_callback_set(struct RickmodState *rm, void (*row_callback)(void *data), void *user_data);

#endif
