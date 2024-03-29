#include "mixastley.h"
#include "rickmod.h"
#include "lut.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef TRACKER
#include <stdlib.h>
#include <stdio.h>
#endif

#ifdef STANDALONE
#include <stdlib.h>
#include <stdio.h>
#else
#define fprintf(...)
int rand(void);
#endif

static uint16_t valid_notes[36] = {
	856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
	428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
	214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
};


static uint8_t sinetable[32] = {
	0, 24, 49, 74, 97, 120, 141, 161,
	180, 197, 212, 224, 235, 244, 250, 253,
	255, 253, 250, 244, 235, 224, 212, 197,
	180, 161, 141, 120, 97, 74, 49, 24,
};


static int _lookup_arpeggio(int base, int steps) {
	int i;

	for (i = 0; i < 36; i++)
		if (valid_notes[i] == base) {
			if (steps < 0) {
				if (i + steps < 0)
					return valid_notes[0];
				return valid_notes[i + steps];
			} else {
				if (i + steps >= 36)
					return valid_notes[35];
				return valid_notes[i + steps];
			}
		}
	return base;
}


static void _set_samplerate_finetune(struct MAState *rs, int samplerate, int finetune) {
	uint32_t samplerate_n;
	if (finetune > 15) {
		fprintf(stderr, "invalid finetune %i\n", finetune);
	}
	if (!finetune)
		ma_set_samplerate(rs, samplerate);
	else {
		samplerate_n = samplerate * rickmod_lut_finetune[finetune - 1];
		ma_set_samplerate(rs, samplerate_n >> 15);
	}
}


static void _flush_channel_samples(struct RickmodState *rm, int channel) {
	rm->mix[channel].next_sample = (1 << MA_SAMPLE_BUFFER_LEN);
}


static void _set_bpm(struct RickmodState *rm) {
	int tps = (rm->cur.bpm * 2 / 5);
	if (!tps)
		tps = 1;
	rm->cur.samples_per_tick = rm->samplerate / tps;
}


static void _calculate_portamento(struct RickmodChannelEffect *rce) {
	if (!rce->portamento_target) {
		fprintf(stderr, "No portamento target!\n");
		return;
	}
	if (rce->note < rce->portamento_target) {
		if (rce->portamento_target - rce->note >= rce->portamento_speed)
			rce->note += rce->portamento_speed;
		else
			rce->note = rce->portamento_target;
	} else if (rce->note > rce->portamento_target) {
		if (rce->note - rce->portamento_target >= rce->portamento_speed)
			rce->note -= rce->portamento_speed;
		else
			rce->note = rce->portamento_target;
	} else
		return;

}


static void _calculate_tremolo(struct RickmodChannelEffect *rce) {
	int16_t tremolo_level;
	uint16_t tremolo;
	rce->tremolo_pos += rce->tremolo_speed;
	if ((rce->tremolo_wave & 0x3) == 0) {
		tremolo_level = ((int16_t) sinetable[rce->tremolo_pos & 0x1F])*((rce->tremolo_pos&0x20)?-1:1);
	} else if ((rce->tremolo_wave & 0x3) == 1) {
		tremolo_level = (rce->tremolo_pos & 0x1F) << 3;
	} else if ((rce->tremolo_wave & 0x3) == 2) {
		tremolo_level = (rce->tremolo_pos&0x1F)>15?255:0;
	} else {
		tremolo_level = rand() & 0x7F;
	}
	tremolo = (tremolo_level * (rce->tremolo_speed & 0xF) >> 6) + rce->volume;
	if (tremolo < 0) tremolo = 0;
	if (tremolo > 64) tremolo = 64;
	rce->last_tremolo = tremolo;
}


static void _calculate_vibrato(struct RickmodChannelEffect *rce) {
	int16_t vibrato_level;
	rce->vibrato_pos += (rce->vibrato_speed >> 4);
	if ((rce->vibrato_wave & 0x3) == 0) {
		vibrato_level = ((int16_t) sinetable[rce->vibrato_pos & 0x1F])*((rce->vibrato_pos&0x20)?-1:1);
	} else if ((rce->vibrato_wave & 0x3) == 1) {
		vibrato_level = (rce->vibrato_pos & 0x1F) << 3;
	} else if ((rce->vibrato_wave & 0x3) == 2) {
		vibrato_level = (rce->vibrato_pos&0x1F)>15?255:0;
	} else {
		vibrato_level = rand() & 0x7F;
	}

	rce->last_vibrato = (vibrato_level*(rce->vibrato_speed & 0xF) >> 7) + rce->note;
	if (rce->last_vibrato < 113)
		rce->last_vibrato = 113;
	if (rce->last_vibrato > 856)
		rce->last_vibrato = 856;
}


static void _calculate_volume_slide(struct RickmodChannelEffect *rce, uint8_t fallback, uint8_t parm) {
	uint8_t effect;

	effect = rce->effect & 0xFF;
	if (!effect && !fallback)
		effect = parm;
	else if (!effect)
		effect = rce->volume_slide;
	if (effect & 0xF0) {
		rce->volume += (effect & 0xF0) >> 4;
		if (rce->volume > 64)
			rce->volume = 64;
	} else if (effect & 0xF) {
		if (((effect & 0xF)) > rce->volume)
			rce->volume = 0;
		else
			rce->volume -= ((effect & 0xF));
	}
}


static void _do_row(struct RickmodState *rm, int channel) {
	struct RickmodChannelEffect rce = rm->channel[channel].rce;
	int8_t reset = 0xFF;
	uint32_t pos = 2;
	
	rce.retrig = 0;
	rce.delay_ticks = 0;

	if (!rce.effect) {
	} else if ((rce.effect & 0xF00) == 0x000) {
	} else if ((rce.effect & 0xF00) == 0x100) {
	} else if ((rce.effect & 0xF00) == 0x200) {
	} else if ((rce.effect & 0xF00) == 0x300) {
		rce.reset_note = 0;
		if (rce.row_note)
			rce.portamento_target = rce.row_note;
		if (rce.effect & 0xFF)
			rce.portamento_speed = rce.effect;
	} else if ((rce.effect & 0xF00) == 0x400) {
		if (rce.vibrato_wave & 4) {
			if (rce.last_vibrato)
				_set_samplerate_finetune(&rm->mix[channel], rickmod_lut_samplerate[rce.last_vibrato - 113], rce.finetune);
		} else {
			rce.vibrato_pos = 0;
		}
		if (rce.effect & 0xFF)
			rce.vibrato_speed = rce.effect & 0xFF;
		reset &= ~1;
	} else if ((rce.effect & 0xF00) == 0x500) {
		rce.reset_note = 0;
		if (rce.row_note)
			rce.portamento_target = rce.row_note;
		if (rce.effect & 0xFF)
			rce.portamento_vol = rce.effect & 0xFF;
	} else if ((rce.effect & 0xF00) == 0x600) {
		if (rce.vibrato_wave & 4) {
			if (rce.last_vibrato)
				_set_samplerate_finetune(&rm->mix[channel], rickmod_lut_samplerate[rce.last_vibrato - 113], rce.finetune);
		} else
			rce.vibrato_pos = 0;
		if (rce.effect & 0xFF)
			rce.vibrato_vol = rce.effect & 0xFF;
		reset &= ~1;
	} else if ((rce.effect & 0xF00) == 0x700) {
		if (rce.tremolo_wave & 4) {
			if (rce.last_tremolo)
				ma_set_volume(&rm->mix[channel], rce.last_tremolo);
			else
				ma_set_volume(&rm->mix[channel], rce.volume);
		} else
			rce.tremolo_pos = 0;
		if (rce.effect & 0xFF)
			rce.tremolo_speed = rce.effect & 0xFF;
		reset &= ~2;
		goto no_volume;
	} else if ((rce.effect & 0xF00) == 0x900) {
		pos = (rce.effect & 0xFF) << 8;
		if (pos)
			rce.sample_pos = pos;
		if (!pos)
			pos = rce.sample_pos;
		rm->channel[channel].sample_pos = pos;
		if (!rce.reset_note)
			_flush_channel_samples(rm, channel);
	} else if ((rce.effect & 0xF00) == 0xA00) {
		if (rce.effect & 0xFF)
			rce.volume_slide = rce.effect & 0xFF;
	} else if ((rce.effect & 0xF00) == 0xB00) {
		rm->cur.next_pattern = rce.effect & 0xFF;
		if (rm->cur.next_pattern >= rm->song_length) {
			rm->cur.next_pattern = 0;
		}
		rm->cur.next_row = 0;
	} else if ((rce.effect & 0xF00) == 0xC00) {
		rce.volume = rce.effect & 0xFF;
	} else if ((rce.effect & 0xF00) == 0xD00) {
		int hex;
		#ifndef TRACKER
		rm->cur.next_pattern = rm->cur.pattern + 1;
		#else
		if (!rm->repeat_pattern)
			rm->cur.next_pattern = rm->cur.pattern + 1;
		#endif
		hex = rce.effect & 0x7F;
		hex = (hex & 0xF) + ((hex & 0xF0) >> 4) * 10;
		rm->cur.next_row = hex;
		if (rm->cur.next_pattern >= rm->song_length) {
			rm->cur.next_pattern = 0;
		}
	} else if ((rce.effect & 0xFF0) == 0xE10) {
		rce.note -= rce.effect & 0xF;
		if (rce.note < 113)
			rce.note = 113;
		_set_samplerate_finetune(&rm->mix[channel], rickmod_lut_samplerate[rce.note - 113], rce.finetune);
	} else if ((rce.effect & 0xFF0) == 0xE20) {
		rce.note += rce.effect & 0xF;
		if (rce.note > 856)
			rce.note = 856;
		_set_samplerate_finetune(&rm->mix[channel], rickmod_lut_samplerate[rce.note - 113], rce.finetune);
	} else if ((rce.effect & 0xFF0) == 0xE60) {
		if (rce.effect & 0xF) {
			if (!rce.loop_count)
				rce.loop_count = (rce.effect & 0xF) + 1;
			rce.loop_count--;
			if (rce.loop_count) {
				rm->cur.next_row = rce.loop_row;
				rm->cur.next_pattern = rm->cur.pattern;
			}
		} else {
			rce.loop_row = rm->cur.row;
		}
	} else if ((rce.effect & 0xFF0) == 0xE90) {
		rce.retrig = rce.effect & 0xF;
		rce.reset_note = 1;
	} else if ((rce.effect & 0xFF0) == 0xEA0) {
		rce.volume += rce.effect & 0xF;
		if (rce.volume > 64)
			rce.volume = 64;
	} else if ((rce.effect & 0xFF0) == 0xEB0) {
		if (rce.volume < (rce.effect & 0xF))
			rce.volume = 0;
		else
			rce.volume -= (rce.effect & 0xF);
	} else if ((rce.effect & 0xFF0) == 0xEC0) {
	} else if ((rce.effect & 0xFF0) == 0xED0) {
		rce.delay_ticks = rce.effect & 0xF;
		if (rce.delay_ticks != rm->cur.tick) {
			rm->channel[channel].rce = rce;
			return;
		}
	} else if ((rce.effect & 0xFF0) == 0xEE0) {
		rm->cur.set_on_tick = (rce.effect & 0xF) * rm->cur.speed;
	} else if ((rce.effect & 0xF00) == 0xF00) {
		if ((rce.effect & 0xFF) < 0x20) {
			rm->cur.speed = rce.effect & 0xFF;
		} else {
			rm->cur.bpm = rce.effect & 0xFF, _set_bpm(rm);
		}
	} else {
		fprintf(stderr, "Unhandled effect 0x%.3X\n", rce.effect);
	}

	ma_set_volume(&rm->mix[channel], rce.volume);
no_volume:
	
	if (reset & 1)
		rce.last_vibrato = 0;
	if (reset & 2)
		rce.last_tremolo = 0;
	
	if (rce.reset_note) {
		rm->channel[channel].trigger = 1;
		_set_samplerate_finetune(&rm->mix[channel], rickmod_lut_samplerate[rce.note - 113], rce.finetune);
		rm->channel[channel].sample = rce.sample;
		rm->channel[channel].play_sample = rce.sample;
		rm->channel[channel].sample_pos = pos;

		_flush_channel_samples(rm, channel);
	}

	rm->channel[channel].rce = rce;
}


static void _handle_tick_effect(struct RickmodState *rm, int channel) {
	struct RickmodChannelEffect rce;
	uint16_t note;
	rce = rm->channel[channel].rce;

	if (!rce.effect) {
		return;
	} else if ((rce.effect & 0xF00) == 0x000) {
		int mode = rm->cur.tick % 3;
		int arpeggio = rce.effect & 0xFF;
		int step;
		if (!mode)
			return _set_samplerate_finetune(&rm->mix[channel], rickmod_lut_samplerate[rce.note - 113], rce.finetune);
		if (arpeggio) {
			rm->channel[channel].rce.arpeggio_save = arpeggio;
		} else
			arpeggio = rm->channel[channel].rce.arpeggio_save;
		if (mode == 1)
			step = _lookup_arpeggio(rce.note, arpeggio & 0xF);
		if (mode == 2)
			step = _lookup_arpeggio(rce.note, (arpeggio & 0xF0) >> 4);
		_set_samplerate_finetune(&rm->mix[channel], rickmod_lut_samplerate[step - 113], rce.finetune);
		return;
	} else if ((rce.effect & 0xF00) == 0x100) {
		if (rce.note > (rce.effect & 0xFF) + 113)
			rce.note -= rce.effect & 0xFF;
		else
			rce.note = 113;
	} else if ((rce.effect & 0xF00) == 0x200) {
		if (rce.note + (rce.effect & 0xFF) > 856)
			rce.note = 856;
		else
			rce.note += (rce.effect & 0xFF);
	} else if ((rce.effect & 0xF00) == 0x300) {
		_calculate_portamento(&rce);
	} else if ((rce.effect & 0xF00) == 0x400) {
		_calculate_vibrato(&rce);
		note = rce.last_vibrato;
		goto special_note;
	} else if ((rce.effect & 0xF00) == 0x500) {
		_calculate_portamento(&rce);
		_calculate_volume_slide(&rce, 0, rce.portamento_vol);
	} else if ((rce.effect & 0xF00) == 0x600) {
		_calculate_vibrato(&rce);
		note = rce.last_vibrato;
		_calculate_volume_slide(&rce, 0, rce.vibrato_vol);
		goto special_note;
	} else if ((rce.effect & 0xF00) == 0x700) {
		_calculate_tremolo(&rce);
		ma_set_volume(&rm->mix[channel], rce.last_tremolo);
	} else if ((rce.effect & 0xF00) == 0xA00) {
		_calculate_volume_slide(&rce, 1, 0);
	} else if ((rce.effect & 0xFF0) == 0xEC0) {
		if ((rce.effect & 0xF) == rm->cur.tick) {
			rce.volume = 0;
		}
	}
	note = rce.note;
special_note:
	if (note)
		_set_samplerate_finetune(&rm->mix[channel], rickmod_lut_samplerate[note - 113], rce.finetune);
	ma_set_volume(&rm->mix[channel], rce.volume);
//tremolo:
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
		_do_row(rm, i);
}


static void _handle_retrig(struct RickmodState *rm) {
	int i;

	for (i = 0; i < 4; i++) {
		if (rm->channel[i].rce.retrig && !(rm->cur.tick % rm->channel[i].rce.retrig))
			_do_row(rm, i);
		else if (rm->channel[i].rce.delay_ticks == rm->cur.tick)
			_do_row(rm, i);
	}
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
	
	if (!note || note == 0xFFF) {
		rce.reset_note = 0;
		note = rce.note;
		rce.row_note = 0;
	} else {
		if (note < 113)
			note = 113;
		if (note > 856)
			note = 856;
		rce.row_note = note;
		if ((effect & 0xF00) != 0x300 && (effect & 0xF00) != 0x500 && (effect & 0xFF0) != 0xED0) {
			rce.reset_note = 1;
			rce.note = note;
		} else {
			rce.reset_note = 0;
			note = rce.note;
		}
	}

	sample = rm->pattern[rm->pattern_lookup[rm->cur.pattern]].row[rm->cur.row].channel[channel].sample;
	if (!sample) {
		sample = rm->channel[channel].sample;
		/*if (rce.reset_note) {
			rce.volume = rm->sample[sample - 1].volume;
		}*/
		finetune = rce.finetune;
	} else {
		// NOTE: Note is not supposed to be reset on sample number... For some reason
		if (sample != rce.sample && rce.sample)
			rce.reset_note = 1;
		//rce.reset_note = 1;
		rce.volume = rm->sample[sample - 1].volume;
		finetune = rm->sample[sample - 1].finetune;
	} 
	
	if (!sample && rce.reset_note) {
		rce.sample = 0, rce.volume = 0;
		rce.finetune = 0;
	} else {
		rce.sample = sample;
		rce.finetune = finetune;
	}

	rm->channel[channel].rce = rce;
	rce.note = note;

	return;
}


static void _handle_tick(struct RickmodState *rm) {
	if (rm->cur.tick >= rm->cur.speed + rm->cur.set_on_tick) {
		if (rm->cur.next_pattern < rm->cur.pattern) {
			if (!rm->repeat)
				return (void) (rm->end = 1);
			#ifdef TRACKER
			else if (rm->repeat_callback)
				rm->repeat_callback(rm->repeat_user_data);
			#endif
		}

		rm->cur.row = rm->cur.next_row, rm->cur.pattern = rm->cur.next_pattern;
		rm->cur.tick = 0;
		rm->cur.set_on_tick = 0;
		rm->cur.next_row++;
		if (rm->cur.next_row == 64) {
		#ifdef TRACKER
			if (rm->repeat_pattern)
				rm->cur.next_row = 0, rm->cur.next_pattern = rm->cur.pattern;
			else
				rm->cur.next_row = 0, rm->cur.next_pattern = rm->cur.pattern + 1;
		#else
			rm->cur.next_row = 0, rm->cur.next_pattern = rm->cur.pattern + 1;
		#endif
		}
		if (rm->cur.next_pattern >= rm->song_length) {
			if (rm->repeat) {
				rm->cur.next_pattern = 0;
			} else {
				rm->end = 1;
				return;
			}
		}
		_set_row_channel(rm, 0);
		_set_row_channel(rm, 1);
		_set_row_channel(rm, 2);
		_set_row_channel(rm, 3);
		_handle_delayed_row(rm);
		if (rm->row_callback)
			rm->row_callback(rm->user_data);
	} else {
		_handle_tick_effects(rm);
		_handle_retrig(rm);
	}
}


/* Stereo, non-interleaved */
static void _mix(struct RickmodState *rm, int32_t *buffer, int samples) {
	int i, len;

	memset(buffer, 0, 4*2*samples);
	if (rm->end)
		return;
	
	/* TODO: This is where all timing will be handled regarding row/pattern/effect playback */
	for (i = 0; i < samples;) {
		len = rm->cur.samples_per_tick - rm->cur.samples_this_tick;
		#ifdef TRACKER
		if (len <= 0)
			goto tick_done;
		#endif
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
		#ifdef TRACKER
	tick_done:
		#endif
		/* More work to do */
		rm->cur.tick++;
		rm->cur.samples_this_tick = 0;
		_handle_tick(rm);
		if (rm->end)
			break;
	}
}


/* Stereo, non-interleaved */
static void _mix_fast(struct RickmodState *rm, int32_t *buffer, int samples) {
	int i, len;

	memset(buffer, 0, 4*2*samples);
	if (rm->end)
		return;
	
	/* TODO: This is where all timing will be handled regarding row/pattern/effect playback */
	for (i = 0; i < samples;) {
		len = rm->cur.samples_per_tick - rm->cur.samples_this_tick;
		#ifdef TRACKER
		if (len <= 0)
			goto tick_done;
		#endif
		if (i + len > samples)
			len = samples - i;
		ma_add_fast(&rm->mix[0], buffer + i, len);
		ma_add_fast(&rm->mix[3], buffer + i, len);
		ma_add_fast(&rm->mix[1], buffer + samples + i, len);
		ma_add_fast(&rm->mix[2], buffer + samples + i, len);
		rm->cur.samples_this_tick += len;
		i += len;
		if (rm->cur.samples_this_tick < rm->cur.samples_per_tick)
			return; // Our work here is done
		#ifdef TRACKER
	tick_done:
		#endif
		/* More work to do */
		rm->cur.tick++;
		rm->cur.samples_this_tick = 0;
		_handle_tick(rm);
		if (rm->end)
			break;
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
		i += len;
		pos += len;
		
		if (pos < wrap)
			continue;
		if (!s->repeat_length || (!s->repeat && s->repeat_length <= 2)) {
			ma_set_samplerate(rcs->rm->mix + rcs->channel, 0); // no more samples please
			rcs->play_sample = 0;
			pos = 2;
			memset(buff + i, 0, (1 << MA_SAMPLE_BUFFER_LEN) - i);
			break;
		}

		pos = repeat;
		rcs->trigger = 0;
		goto loop;
	}

	rcs->sample_pos = pos;
}


static void _parse_sample_info(struct RickmodState *rm, uint8_t *mod, uint16_t wavepos, int samples) {
	int i, j;
	uint8_t *sample_data;
	uint32_t next_wave = wavepos;

	#ifdef TRACKER
	rm->data = malloc(1024*128*31);
	memset(rm->data, 0, 1024*128*31);
	#endif

	for (i = 0; i < samples; i++) {
		sample_data = mod + 20 + i*30;
		memcpy(rm->sample[i].name, sample_data, 22);
		#ifndef TRACKER
		for (j = 0; j < 22; j++)
			if (rm->sample[i].name[j] == 0)
				rm->sample[i].name[j] = ' ';
		#endif
		rm->sample[i].name[22] = 0;
		rm->sample[i].length = (sample_data[22] << 9) | (sample_data[23] << 1);
		rm->sample[i].finetune = sample_data[24] & 0xF;
		rm->sample[i].volume = sample_data[25];
		rm->sample[i].repeat = (sample_data[26] << 9) | (sample_data[27] << 1);
		rm->sample[i].repeat_length = (sample_data[28] << 9) | (sample_data[29] << 1);
		rm->sample[i].sample_data = (int8_t *) mod + next_wave;
		
		#ifdef TRACKER
		memcpy(rm->data + 1024*128*i, rm->sample[i].sample_data, rm->sample[i].length);
		rm->sample[i].sample_data = (int8_t *) rm->data + 1024*128*i;
		#endif

		#ifndef TRACKER
		fprintf(stderr, "%.22s sample %i at 0x%X, length=%i, repeat=%i, repeat_length=%i\n", rm->sample[i].name, i + 1, next_wave, rm->sample[i].length, rm->sample[i].repeat, rm->sample[i].repeat_length);
		#endif
		next_wave += rm->sample[i].length;
	}
}


static void _find_number_of_patterns(struct RickmodState *rm, int max_patterns) {
	int i, max, mask;
	mask = max_patterns - 1;

	max = 0;

	for (i = 0; i < 128; i++)
		if ((rm->pattern_lookup[i] & mask) > max)
			max = (rm->pattern_lookup[i] & mask), rm->pattern_lookup[i] &= mask;
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
			}
}


void rm_reset(struct RickmodState *rm) {
	struct RickmodChannelState rcs = { 0 };
	rm->cur.bpm = 125;
	rm->cur.speed = 6;
	rm->cur.samples_this_tick = 0;
	rm->cur.tick = 0;
	rm->cur.set_on_tick = 0;
	rm->cur.next_row = 0;
	rm->cur.next_pattern = 0;
	rm->cur.row = rm->cur.pattern = 0;
	rm->end = 0;
	rm->channel[0] = rm->channel[1] = rm->channel[2] = rm->channel[3] = rcs;
	_set_bpm(rm);
	// TODO: Set pattern

	rm_clear(rm);
}


void rm_clear(struct RickmodState *rm) {
	int i;

	rm->cur.speed = 6;
	rm->cur.tick = 0;
	rm->cur.samples_this_tick = 0;

	for (i = 0; i < 4; i++) {
		#ifdef TRACKER
		int mute;
		#endif

		rm->channel[i].rm = rm, rm->channel[i].channel = i, rm->channel[i].sample = rm->channel[i].trigger = rm->channel[i].play_sample = 0;
		memset(&rm->channel[i].rce, 0, sizeof(rm->channel[i].rce));
		rm->channel[i].sample_pos = 0;
		#ifdef TRACKER
		mute = rm->mix[i].mute;
		#endif
		rm->mix[i] = ma_init(rm->samplerate);
		#ifdef TRACKER
		rm->mix[i].mute = mute;
		#endif
		ma_set_callback(&rm->mix[i], _pull_samples, &rm->channel[i]);
		ma_set_volume(&rm->mix[i], 0);
		ma_set_samplerate(&rm->mix[i], 0);
		_set_row_channel(rm, i);
	}

	#ifdef TRACKER
	rm->repeat_pattern = 0;
	#endif

}


struct RickmodState *rm_init(int sample_rate, uint8_t *mod, int mod_len) {
	struct RickmodState *rm;
	int max_patterns;

	rm = malloc(sizeof(*rm));
	rm->data = mod;
	rm->samplerate = sample_rate;
	rm->repeat = rm->end = 0;
	rm->row_callback = NULL;
	#ifdef TRACKER
	rm->repeat_callback = NULL;
	#endif

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

	
	rm_reset(rm);
	rm_clear(rm);

	memcpy(rm->name, mod, 20);
	rm->name[20] = 0;

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

#if 0
static void _print_pattern(struct RickmodState *rm, int pattern) {
	pattern = rm->pattern_lookup[pattern];

	int i, j;
	for (i = 0; i < 64; i++) {
		for (j = 0; j < 4; j++)
			fprintf(stderr, "%.2i: [%.4X] %.2X %.3X   ", i, rm->pattern[pattern].row[i].channel[j].note, rm->pattern[pattern].row[i].channel[j].sample, rm->pattern[pattern].row[i].channel[j].effect);
		fprintf(stderr, "\n");
	}
}
#endif


void rm_mix_s16_fast(struct RickmodState *rm, int16_t *buff, int samples) {
	int32_t sample[samples * 2];
	int i;

	_mix_fast(rm, sample, samples);
	for (i = 0; i < samples; i++) {
		buff[i<<1] = ((sample[i]));
		buff[(i<<1) + 1] = ((sample[i+samples]));
	}

}


void rm_mix_s16(struct RickmodState *rm, int16_t *buff, int samples) {
	int32_t sample[samples * 2];
	int i;

	_mix(rm, sample, samples);
	for (i = 0; i < samples; i++) {
		buff[i<<1] = (((sample[i] * 70) + (sample[i + samples] * 30))/100) >> 1;
		buff[(i<<1) + 1] = (((sample[i+samples] * 70) + (sample[i] * 30))/100) >> 1;
	}
}

void rm_mix_u8(struct RickmodState *rm, uint8_t *buff, int samples) {
	int32_t sample[samples * 2];
	int i;

	_mix_fast(rm, sample, samples);
	for (i = 0; i < samples; i++) {
		buff[i<<1] = ((sample[i] >> 9) & 0xFF) + 128;
		buff[(i<<1) + 1] = ((sample[i+samples] >> 9) & 0xFF) + 128;
	}
}


void rm_repeat_set(struct RickmodState *rm, uint8_t repeat) {
	rm->repeat = repeat;
}


uint8_t rm_end_reached(struct RickmodState *rm) {
	return rm->end;
}


void rm_free(struct RickmodState *rm) {
	#ifdef TRACKER
	free(rm->data);
	#endif

	free(rm);
}


void rm_row_callback_set(struct RickmodState *rm, void (*row_callback)(void *data), void *user_data) {
	rm->user_data = user_data;
	rm->row_callback = row_callback;
}



#ifdef TRACKER

struct RickmodState *rm_new(int sample_rate) {
	struct RickmodState *rm;
	int i;

	rm = malloc(sizeof(*rm));
	memset(rm->name, 0, 21);
	rm->data = malloc(128*1024*31);
	rm->samples = 31;
	rm->pattern_lookup = malloc(128);
	memset(rm->pattern_lookup, 0, 128);
	rm->patterns = 128;
	rm->song_length = 1;

	for (i = 0; i < 31; i++) {
		memset(rm->sample[i].name, 0, 23);
		rm->sample[i].repeat = 0, rm->sample[i].repeat_length = 2;
		rm->sample[i].length = 0;
		rm->sample[i].finetune = 0;
		rm->sample[i].volume = 0x40;
		rm->sample[i].sample_data = (int8_t *) rm->data + 128*1024*i;
	}

	memset(rm->pattern, 0, sizeof(rm->pattern));

	rm->samplerate = sample_rate;
	rm->repeat = 0;
	rm->end = 0;
	rm->row_callback = NULL;
	#ifdef TRACKER
	rm->repeat_callback = NULL;
	#endif

	rm->cur.bpm = 125;
	rm->cur.speed = 6;
	rm->cur.samples_this_tick = 0;
	rm->cur.tick = 0;
	rm->cur.set_on_tick = 0;
	rm->cur.next_row = 0;
	rm->cur.next_pattern = 0;
	rm->cur.row = rm->cur.pattern = 0;
	_set_bpm(rm);
	
	for (i = 0; i < 4; i++) {
		rm->channel[i].rm = rm, rm->channel[i].channel = i, rm->channel[i].sample = rm->channel[i].trigger = rm->channel[i].play_sample = 0;
		memset(&rm->channel[i].rce, 0, sizeof(rm->channel[i].rce));
		rm->channel[i].sample_pos = 0;
		rm->mix[i] = ma_init(sample_rate);
		ma_set_callback(&rm->mix[i], _pull_samples, &rm->channel[i]);
		ma_set_volume(&rm->mix[i], 0);
		ma_set_samplerate(&rm->mix[i], 0);
		_set_row_channel(rm, i);
	}


	return rm;
}


int rm_save(struct RickmodState *rm, const char *path) {
	FILE *fp;
	int i, j, k;
	uint8_t ch;

	// When the mod file was loaded, extra bits were already masked out, so this is fine
	_find_number_of_patterns(rm, 128);

	if (!(fp = fopen(path, "wb")))
		return 0;
	fwrite(rm->name, 20, 1, fp);
	for (i = 0; i < 31; i++) {
		fwrite(rm->sample[i].name, 22, 1, fp);
		ch = rm->sample[i].length >> 9;
		fwrite(&ch, 1, 1, fp);
		ch = rm->sample[i].length >> 1;
		fwrite(&ch, 1, 1, fp);
		
		ch = rm->sample[i].finetune & 0xF;
		fwrite(&ch, 1, 1, fp);
		fwrite(&rm->sample[i].volume, 1, 1, fp);
	
		ch = rm->sample[i].repeat >> 9;
		fwrite(&ch, 1, 1, fp);
		ch = rm->sample[i].repeat >> 1;
		fwrite(&ch, 1, 1, fp);

		ch = rm->sample[i].repeat_length >> 9;
		fwrite(&ch, 1, 1, fp);
		ch = rm->sample[i].repeat_length >> 1;
		fwrite(&ch, 1, 1, fp);
	}

	fwrite(&rm->song_length, 1, 1, fp);
	ch = 127;
	fwrite(&ch, 1, 1, fp);
	fwrite(rm->pattern_lookup, 128, 1, fp);

	if (rm->patterns > 63)
		fwrite("M!K!", 4, 1, fp);
	else
		fwrite("M.K.", 4, 1, fp);
	
	for (i = 0; i < rm->patterns; i++)
		for (j = 0; j < 64; j++)
			for (k = 0; k < 4; k++) {
				ch = (rm->pattern[i].row[j].channel[k].sample & 0xF0) | ((rm->pattern[i].row[j].channel[k].note >> 8) & 0xF);
				fwrite(&ch, 1, 1, fp);
				ch = rm->pattern[i].row[j].channel[k].note;
				fwrite(&ch, 1, 1, fp);
				ch = ((rm->pattern[i].row[j].channel[k].sample & 0xF) << 4) | ((rm->pattern[i].row[j].channel[k].effect >> 8) & 0xF);
				fwrite(&ch, 1, 1, fp);
				ch = rm->pattern[i].row[j].channel[k].effect & 0xFF;
				fwrite(&ch, 1, 1, fp);
			}
	
	for (i = 0; i < 31; i++)
		fwrite(rm->sample[i].sample_data, rm->sample[i].length, 1, fp);

	#ifdef MODFILE_SIGNATURE
	fputs(MODFILE_SIGNATURE, fp);
	#endif

	fclose(fp);
	return 1;
}


int rm_lookup_note(int note) {
	int i;

	for (i = 0; i < 36; i++)
		if (note == valid_notes[i])
			return i;
	return -1;
}


void rm_samplerate_set(struct MAState *rs, int note, int finetune) {
	_set_samplerate_finetune(rs, rickmod_lut_samplerate[valid_notes[note] - 113], finetune);
}


int rm_translate_note(int note) {
	return valid_notes[note];
}


void rm_bpm_set(struct RickmodState *rm, int bpm) {
	rm->cur.bpm = bpm;
	_set_bpm(rm);
}


void rm_repeat_callback_set(struct RickmodState *rm, void (*row_callback)(void *data), void *user_data) {
	rm->repeat_user_data = user_data;
	rm->repeat_callback = row_callback;
}


#endif


#ifdef STANDALONE
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
	fprintf(stderr, "songname: %s\n", rm->name);
	if (argc<3)
		fp = fopen("/tmp/out.raw", "w");
	else
		fp = fopen(argv[2], "w");
	//rm_repeat_set(rm, 1);
	while (!rm_end_reached(rm)) {
		rm_mix_s16_fast(rm, buff, 44100);
		fwrite(buff, 44100, 4, fp);
	}
	
	fclose(fp);
	free(rm);
	free(data);


	return 0;
}
#endif
