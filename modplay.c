#include "mixastley.h"
#include "modplay.h"
#include "lut.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


static void _flush_channel_samples(struct RickmodState *rm, int channel) {
	rm->mix[channel].next_sample = (1 << MA_SAMPLE_BUFFER_LEN);
}


static void _set_bpm(struct RickmodState *rm) {
	int tps = (rm->cur.bpm * 2 / 5);
	if (!tps)
		tps = 1;
	rm->cur.samples_per_tick = rm->samplerate / tps;
}


static void _do_row(struct RickmodState *rm, int channel) {
	struct RickmodChannelEffect rce = rm->channel[channel].rce;


	if (!rce.effect) {
	} else if ((rce.effect & 0xF00) == 0x100) {
	} else if ((rce.effect & 0xF00) == 0x200) {
	} else if ((rce.effect & 0xF00) == 0x300) {
		rce.reset_note = 0;
		if (rce.row_note)
			rce.portamento_target = rce.row_note;
		if (rce.effect & 0xFF)
			rce.portamento_speed = rce.effect;
	} else if ((rce.effect & 0xF00) == 0x900) {
		rm->channel[channel].sample_pos = (rce.effect & 0xFF) << 8;
	} else if ((rce.effect & 0xF00) == 0xA00) {
	} else if ((rce.effect & 0xF00) == 0xF00) {
		if ((rce.effect & 0xFF) < 0x20) {
			rm->cur.speed = rce.effect & 0xFF;
		} else {
			rm->cur.bpm = rce.effect & 0xFF, _set_bpm(rm);
		}
	} else if ((rce.effect & 0xF00) == 0xC00) {
		rce.volume = rce.effect & 0xFF;
	} else if ((rce.effect & 0xF00) == 0xD00) {
		rm->cur.next_pattern = rm->cur.pattern + 1, rm->cur.next_row = 0;
		if (rm->cur.next_pattern >= rm->song_length)
			rm->cur.next_pattern = 0;
	} else {
		fprintf(stderr, "Unhandled effect 0x%.3X\n", rce.effect);
	}
	ma_set_volume(&rm->mix[channel], rce.volume);
	
	if (rce.reset_note) {
		rm->channel[channel].trigger = 1;
		ma_set_samplerate(&rm->mix[channel], rickmod_lut_samplerate[rce.note - 113]);
		rm->channel[channel].sample = rce.sample;
		rm->channel[channel].play_sample = rce.sample;
		rm->channel[channel].sample_pos = 0;

		_flush_channel_samples(rm, channel);
	}

	rm->channel[channel].rce = rce;
}


static void _handle_tick_effect(struct RickmodState *rm, int channel) {
	struct RickmodChannelEffect rce;
	rce = rm->channel[channel].rce;
	if (!rce.effect) {
		return;
	} else if ((rce.effect & 0xF00) == 0x100) {
		if (rce.note > (rce.effect & 0xFF) + 113)
			rce.note -= rce.effect & 0xFF;
		else
			rce.note = 113;
	} else if ((rce.effect & 0xF00) == 0x200) {
		if (rce.note + (rce.effect & 0xFF) > 865)
			rce.note = 865;
		else
			rce.note += (rce.effect & 0xFF);
	} else if ((rce.effect & 0xF00) == 0x300) {
		if (!rce.portamento_target)
			return;
		if (rce.note < rce.portamento_target) {
			if (rce.portamento_target - rce.note >= rce.portamento_speed)
				rce.note += rce.portamento_speed;
			else
				rce.note = rce.portamento_target;
			fprintf(stderr, "portamento down\n");
		} else if (rce.note > rce.portamento_target) {
			if (rce.note - rce.portamento_target >= rce.portamento_speed)
				rce.note -= rce.portamento_speed;
			else
				rce.note = rce.portamento_target;
			fprintf(stderr, "portamento up\n");
		} else
			return;
	} else if ((rce.effect & 0xF00) == 0xA00) {
		if (rce.effect & 0xF) {
			rce.volume += rce.effect & 0xF;
			if (rce.volume > 64)
				rce.volume = 64;
		} else if (rce.effect & 0xF0) {
			if (((rce.effect & 0xF0) >> 4) > rce.volume)
				rce.volume = 0;
			else
				rce.volume -= ((rce.effect & 0xF0) >> 4);
		}
	}

	ma_set_samplerate(&rm->mix[channel], rickmod_lut_samplerate[rce.note - 113]);
	rm->channel[channel].rce = rce;
	
}


static void _handle_tick_effects(struct RickmodState *rm) {
	int i;
	for (i = 0; i < 4; i++)
		_handle_tick_effect(rm, i);
}


static void _handle_delayed_row(struct RickmodState *rm) {
	int i;
	for (i = 0; i < 4; i++)
		if (rm->cur.tick == rm->channel[i].set_on_tick)
			_do_row(rm, i);
}


static void _set_row_channel(struct RickmodState *rm, int channel) {
	//int i;
	uint8_t sample;
	uint32_t note, finetune;
	int effect;
	struct RickmodChannelEffect rce = rm->channel[channel].rce;
	/* TODO: Look at effect value */

	note = rm->pattern[rm->pattern_lookup[rm->cur.pattern]].row[rm->cur.row].channel[channel].note;
	effect = rm->pattern[rm->pattern_lookup[rm->cur.pattern]].row[rm->cur.row].channel[channel].effect;
	rce.effect = effect;
	
	if (!note) {
		rce.reset_note = 0;
		note = 0;
		rce.row_note = 0;
		/*ma_set_samplerate(&rm->mix[channel], 0);
		rm->channel[channel].play_sample = 0;
		_flush_channel_samples(rm, channel);
		return;*/
	} else {
		if (note < 113)
			note = 113;
		if (note > 856)
			note = 856;
		//fprintf(stderr, "setting note %i on channel %i\n", note, channel);
		rce.reset_note = 1;
		rce.note = note;
		rce.row_note = note;
	}

	sample = rm->pattern[rm->pattern_lookup[rm->cur.pattern]].row[rm->cur.row].channel[channel].sample;
	if (!sample)
		sample = rm->channel[channel].sample;
	else {
		rce.volume = rm->sample[sample - 1].volume;
		rce.reset_note = 1;
		rce.volume = rm->sample[sample - 1].volume;
		finetune = rm->sample[sample - 1].finetune;
	} 
	
	if (!sample) {
		rce.sample = 0, rce.volume = 0;
		finetune = 0;
	} else {
		rce.sample = sample;
	}
	if (finetune) {
		note *= rickmod_lut_finetune[finetune - 1];
		note >>= 16;
	}

	rm->channel[channel].set_on_tick = 0;
	rm->channel[channel].rce = rce;
	rce.note = note;

	return;
}


static void _handle_tick(struct RickmodState *rm) {
	if (rm->cur.tick == rm->cur.speed) {
		rm->cur.row = rm->cur.next_row, rm->cur.pattern = rm->cur.next_pattern;
		rm->cur.tick = 0;
		rm->cur.next_row++;
		if (rm->cur.next_row == 64)
			rm->cur.next_row = 0, rm->cur.next_pattern = rm->cur.pattern + 1;
		if (rm->cur.next_pattern >= rm->song_length)
			rm->cur.next_pattern = 0;
		_set_row_channel(rm, 0);
		_set_row_channel(rm, 1);
		_set_row_channel(rm, 2);
		_set_row_channel(rm, 3);
		_handle_delayed_row(rm);
	} else {
		_handle_delayed_row(rm);
		_handle_tick_effects(rm);
	}
}


/* Stereo, non-interleaved */
static void _mix(struct RickmodState *rm, int32_t *buffer, int samples) {
	int i, len;

	memset(buffer, 0, 4*2*samples);
	/* TODO: This is where all timing will be handled regarding row/pattern/effect playback */
	for (i = 0; i < samples;) {
		len = rm->cur.samples_per_tick - rm->cur.samples_this_tick;
		if (i + len > samples)
			len = samples - i;
		ma_add(&rm->mix[0], buffer + i, len);
		ma_add(&rm->mix[3], buffer + i, len);
		ma_add(&rm->mix[1], buffer + samples + i, len);
		ma_add(&rm->mix[2], buffer + samples + i, len);
		rm->cur.samples_this_tick += len;
		i += len;
		if (rm->cur.samples_this_tick < rm->cur.samples_per_tick)
			return; // Our work here is done
		/* More work to do */
		rm->cur.tick++;
		rm->cur.samples_this_tick = 0;
		_handle_tick(rm);
	}
}


static void _pull_samples(void *ptr, int8_t *buff) {
	struct RickmodChannelState *rcs = ptr;
	struct RickmodSample *s;
	int i = 0;
	int8_t sample, *data;
	uint32_t pos, repeat, wrap, len;


	sample = rcs->sample;
	if (rcs->play_sample == 0)
		return memset(buff, 0, (1 << MA_SAMPLE_BUFFER_LEN)), (void) 0;
	s = &rcs->rm->sample[sample - 1];
	
	data = s->sample_data;
	repeat = s->repeat;
	pos = rcs->sample_pos;
loop:
	wrap = rcs->trigger?s->length:s->repeat_length+repeat;
	for (; i < (1 << MA_SAMPLE_BUFFER_LEN);) {
		len = wrap - pos;
		if (len > (1 << MA_SAMPLE_BUFFER_LEN) - i)
			len = (1 << MA_SAMPLE_BUFFER_LEN) - i;
		memcpy(buff + i, data + pos, len);
		//fprintf(stderr, "Copied %i bytes to buffer, from pos=%i from sample %i, src=%i, wrap=%i\n", len, i, rcs->sample, pos, wrap);
		i += len;
		pos += len;
		
		if (pos < wrap)
			continue;
		if (!s->repeat_length) {
			ma_set_samplerate(rcs->rm->mix + rcs->channel, 0); // no more samples please
			rcs->play_sample = 0;
			pos = 0;
			memset(buff + i, 0, (1 << MA_SAMPLE_BUFFER_LEN) - i);
			break;
		}

		pos = repeat;
		rcs->trigger = 0;
		goto loop;
	}

	//if (sample == 18)
	//	fprintf(stderr, "18 is at %i (wrap=%i)\n", pos, wrap);
	rcs->sample_pos = pos;
}


static void _parse_sample_info(struct RickmodState *rm, uint8_t *mod, uint16_t wavepos, int samples) {
	int i;
	uint8_t *sample_data;
	uint32_t next_wave = wavepos;

	for (i = 0; i < samples; i++) {
		sample_data = mod + 20 + i*30;
		memcpy(rm->sample[i].name, sample_data, 22);
		rm->sample[i].name[22] = 0;
		rm->sample[i].length = (sample_data[22] << 9) | (sample_data[23] << 1);
		rm->sample[i].finetune = sample_data[24] & 0xF;
		rm->sample[i].volume = sample_data[25];
		rm->sample[i].repeat = (sample_data[26] << 9) | (sample_data[27] << 1);
		rm->sample[i].repeat_length = (sample_data[28] << 9) | (sample_data[29] << 1);
		rm->sample[i].sample_data = (int8_t *) mod + next_wave;
		fprintf(stderr, "sample %i at 0x%X, length=%i, repeat=%i, repeat_length=%i\n", i + 1, next_wave, rm->sample[i].length, rm->sample[i].repeat, rm->sample[i].repeat_length);
		next_wave += rm->sample[i].length;
	}
}


static void _find_number_of_patterns(struct RickmodState *rm, int max_patterns) {
	int i, max, mask;
	mask = max_patterns - 1;

	max = 0;

	for (i = 0; i < rm->song_length; i++)
		if ((rm->pattern_lookup[i] & mask) > max)
			max = (rm->pattern_lookup[i] & mask);
	max++;
	rm->patterns = max;
}


static void _parse_pattern_data(struct RickmodState *rm, uint8_t *data) {
	int i, j, k;

	for (i = 0; i < rm->patterns; i++)
		for (j = 0; j < 64; j++)
			for (k = 0; k < 4; k++, data += 4) {
				rm->pattern[i].row[j].channel[k].sample = (data[0] & 0xF0) | (data[2] >> 4);
				rm->pattern[i].row[j].channel[k].note = ((data[0] & 0xF) << 8) | data[1];
				rm->pattern[i].row[j].channel[k].effect = ((data[2] & 0xF) << 8) | data[3];
				/*if (rm->pattern[i].row[j].channel[k].note >= 113 && rm->pattern[i].row[j].channel[k].note <= 856)
					rm->pattern[i].row[j].channel[k].note = rickmod_lut_samplerate[rm->pattern[i].row[j].channel[k].note - 113];*/
			}
}


struct RickmodState *rm_init(int sample_rate, uint8_t *mod, int mod_len) {
	struct RickmodState *rm;
	uint8_t patterns;
	int max_patterns, i;

	rm = malloc(sizeof(*rm));
	rm->data = mod;
	rm->samplerate = sample_rate;

	if (mod[1080] == 'M' && mod[1082] == 'K') {
		fprintf(stderr, "Found 31 sample mod\n");
		max_patterns = (mod[1081] == '!' && mod[1083] == '!') ? 128 : 64;
		if (max_patterns == 128)
			fprintf(stderr, "This mod has 128 patterns\n");
		rm->song_length = mod[950];
		rm->pattern_lookup = mod + 952;
		_find_number_of_patterns(rm, max_patterns);
		_parse_sample_info(rm, mod, 1084 + 1024*rm->patterns, 31);
		rm->samples = 31;
		_parse_pattern_data(rm, mod + 1084);
	} else if (!memcmp(mod + 1080, "4CHN", 4)) {
		fprintf(stderr, "Mystery 4 channel format\n");
		rm->song_length = mod[950];
		rm->pattern_lookup = mod + 952;
		_find_number_of_patterns(rm, 128);
		_parse_sample_info(rm, mod, 1084 + 1024*rm->patterns, 31);
		rm->samples = 31;
		_parse_pattern_data(rm, mod + 1084);
	} else {
		free(rm);
		fprintf(stderr, "Unsupported module format %c%c%c%c\n", mod[1080], mod[1081], mod[1082], mod[1083]);
		return NULL;

	}

	rm->cur.bpm = 125;
	rm->cur.speed = 6;
	rm->cur.samples_this_tick = 0;
	rm->cur.tick = 0;
	rm->cur.next_row = 0;
	rm->cur.next_pattern = 0;;
	_set_bpm(rm);
	// TODO: Set pattern
	
	for (i = 0; i < 4; i++) {
		rm->channel[i].rm = rm, rm->channel[i].channel = i, rm->channel[i].sample = rm->channel[i].trigger = rm->channel[i].play_sample = 0;
		rm->channel[i].sample_pos = 0;
		rm->mix[i] = ma_init(sample_rate);
		ma_set_callback(&rm->mix[i], _pull_samples, &rm->channel[i]);
		ma_set_volume(&rm->mix[i], 0);
		ma_set_samplerate(&rm->mix[i], 0);
		_set_row_channel(rm, i);
	}

	#if 0
	else {
		rm->song_length = mod[470];
		rm->pattern_lookup = mod + 472;
		_find_number_of_patterns(rm, 64);
		_parse_sample_info(rm, mod, 600 + 1024 * rm->patterns, 15);
		rm->samples = 15;
		_parse_pattern_data(rm, mod + 600);
	}
	#endif
	
	
	return rm;
}


void tmp_mix(struct RickmodState *rm, int16_t *buff, int samples) {
	int32_t sample[samples * 2];
	int i;

	_mix(rm, sample, samples);
	for (i = 0; i < samples; i++) {
		buff[i<<1] = sample[i] >> 1;
		buff[(i<<1) + 1] = sample[i+samples] >> 1;
	}
}


int main(int argc, char **argv) {
	FILE *fp;
	uint8_t *data;
	int len, i;
	struct RickmodState *rm;
	int16_t buff[44100*2] = { 0 };

	fp = fopen(argv[1], "r");
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	data = malloc(len);
	fseek(fp, 0, SEEK_SET);
	fread(data, 1, len, fp);
	fclose(fp);
	
	rm = rm_init(44100, data, len);
	//rm->channel[0].sample = rm->channel[0].play_sample = 12, rm->channel[0].trigger = 1;
	//ma_set_samplerate(&rm->mix[0], 4148);
	//ma_set_volume(&rm->mix[0], 64);
	/*_pull_samples(&rm->channel[0], (void *) buff);
	_pull_samples(&rm->channel[0], (void *) buff + 256);
	_pull_samples(&rm->channel[0], (void *) buff + 512);
	_pull_samples(&rm->channel[0], (void *) buff + 768);
	_pull_samples(&rm->channel[0], (void *) buff + 1024);
	_pull_samples(&rm->channel[0], (void *) buff + 1280);
	_pull_samples(&rm->channel[0], (void *) buff + 1536);
	_pull_samples(&rm->channel[0], (void *) buff + 1792);
	_pull_samples(&rm->channel[0], (void *) buff + 2048);*/
	fp = fopen("/tmp/out.raw", "w");
	for (i = 0; i < 50; i++) {
		tmp_mix(rm, buff, 44100);
		fwrite(buff, 44100, 4, fp);
	}
	fclose(fp);

	return 0;
}
