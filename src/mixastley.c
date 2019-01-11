/*
Copyright (c) 2017 Steven Arnow <s@rdw.se>

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

	1. The origin of this software must not be misrepresented; you must not
	claim that you wrote the original software. If you use this software
	in a product, an acknowledgment in the product documentation would be
	appreciated but is not required.

	2. Altered source versions must be plainly marked as such, and must not be
	misrepresented as being the original software.

	3. This notice may not be removed or altered from any source
	distribution.
*/


#include <string.h>
#include <stdint.h>

#include "mixastley.h"

#ifdef STANDALONE
#include <stdio.h>
#else
#define fprintf(...)
#endif



static void resample_refill(struct MAState *rs) {
	int i;
	int8_t buff[1 << MA_SAMPLE_BUFFER_LEN];
	rs->get_next_sample(rs->ptr, buff);
	for (i = 0; i < (1 << MA_SAMPLE_BUFFER_LEN); i++)
		rs->buffer[i] = buff[i] * 0x800000;
}


#if 0
static void resample_filter(struct MAState *rs, int16_t *sample, int samples) {
	int i, j;
	int32_t l, h, b, d1, d2, f1;
	
	d1 = rs->filter.d1;
	d2 = rs->filter.d2;
	f1 = rs->filter.f1;
	
	for (j = 0; j < samples; j++) {
		for (i = 0; i < 2; i++) {
			l = ((d1 * f1) >> 16) + d2;
			/* Q = 2 -> 0.5 */
			h = ((int32_t) sample[i]) - l - d1 * 8;
			b = ((f1 * h) >> 16) + d1;
			d1 = b, d2 = l;
		}

		sample[i] = l;
	}
}
#endif


void ma_add(struct MAState *rs, int32_t *sample, int samples) {
	int i;
	int32_t tmp, fraction_per_sample;

	if (!rs->get_next_sample)
		return;
	if (!rs->fraction_per_sample) {
		rs->cur_sample = rs->last_sample = 0;
		//fprintf(stderr, "No sample rate set\n");
		return;
	}

	fraction_per_sample = rs->fraction_per_sample;

	for (i = 0; i < samples; i++) {
		tmp = rs->cur_sample - rs->last_sample; // 31 bit
		tmp >>= 20;
		tmp *= rs->sample_pos;
		tmp >>= 11;
		sample[i] += ((((((rs->last_sample >> 15)) + tmp)) * rs->volume) >> 6);
		//sample[i] += (((rs->last_sample >> 16) * rs->volume) >> 6);
		rs->sample_pos += fraction_per_sample;
		if (rs->sample_pos >= 0x10000) {
			rs->next_sample += rs->sample_pos >> 16;
			rs->sample_pos &= 0xFFFF;
			if (rs->next_sample >= (1 << MA_SAMPLE_BUFFER_LEN)) {
				rs->next_sample &= (0xFFFF >> (16 - MA_SAMPLE_BUFFER_LEN));
				resample_refill(rs);
			}
			rs->last_sample = rs->cur_sample;
			rs->cur_sample = rs->buffer[rs->next_sample];
			//printf("cur sample: %i %i\n", rs->cur_sample, rs->next_sample);
		}
	}
}


struct MAState ma_init(int target_sample_rate) {
	struct MAState rs;

	rs.target_rate = target_sample_rate;
	rs.fraction_per_sample = 0x10000;
	rs.last_sample = 0;
	rs.cur_sample = 0;
	rs.next_sample = (1 << MA_SAMPLE_BUFFER_LEN);
	rs.sample_pos = 0x10000;
	
	return rs;
}


void ma_set_samplerate(struct MAState *rs, int samplerate) {
	int bw;

	bw = (samplerate < rs->target_rate) ? samplerate : rs->target_rate;
	samplerate *= 0x10000;
	rs->fraction_per_sample = samplerate/rs->target_rate;
	rs->filter.f1 = ((((int64_t) bw) * 0x3ED4F4C0 / rs->target_rate / 2) >> 16);
	//fprintf(stderr, "fraction per sample: 0x%X, filter at %i\n", rs->fraction_per_sample, bw);
	return;
}



void ma_set_volume(struct MAState *rs, int volume) {
	if (volume < 0)
		volume = 0;
	if (volume > 64)
		volume = 64;
	rs->volume = volume;
}


void ma_set_callback(struct MAState *rs, void (*next_sample)(void *ptr, int8_t *buff), void *ptr) {
	rs->get_next_sample = next_sample;
	rs->ptr = ptr;
}


void ma_mix8(struct MAMix *mix, uint8_t *buff, int samples) {
	int i;
	int32_t buffer[samples];

	memset(buffer, 0, sizeof(buffer));
	for (i = 0; i < MA_CHANNELS; i++)
		ma_add(&mix->left[i], buffer, samples);
	for (i = 0; i < samples; i++)
		buff[i << 1] = 128 + (buffer[i] >> (8 + MA_CHANNELS_POT));
	
	memset(buffer, 0, sizeof(buffer));
	for (i = 0; i < MA_CHANNELS; i++)
		ma_add(&mix->right[i], buffer, samples);
	for (i = 0; i < samples; i++)
		buff[(i << 1) + 1] = 128 + (buffer[i] >> (8 + MA_CHANNELS_POT));
	return;
}


struct MAMix ma_mix_create(int sample_rate) {
	struct MAMix mix;
	int i;

	for (i = 0; i < MA_CHANNELS; i++) {
		mix.left[i] = ma_init(sample_rate);
		mix.right[i] = ma_init(sample_rate);
	}

	return mix;
}


#if 0
int main(int argc, char **argv) {
	struct MAState rs;
	uint16_t buff[48000];
	FILE *fp;

	//rs = resample_init(48000, generate_sine, NULL);
	//resample_set_samplerate(&rs, 25600);
	//resample(&rs, buff, 48000);
	//resample_filter(&rs, buff, 48000);
	//fp = fopen(argv[1], "w");
	//fwrite(buff, 48000, 2, fp);
	//fclose(fp);

	return 0;
	
}
#endif
