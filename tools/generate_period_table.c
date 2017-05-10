#include <stdint.h>
#include <math.h>
#include <stdio.h>

int calculate_samplerate(int period) {
	int i;
	if (!period)
		return 0;
	i = 70937892/(period*20);
	if (i > 32768)
		i = 32768;
	return i;
}


int32_t finetune_multiplier(int finetune) {
	return pow(1.0072382087, finetune) * 0x8000;

}


int main(int argc, char **argv) {
	int i;
	int finetune_val[15] = { 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1 };
	FILE *fp;

	fp = fopen(argv[1], "w");

	fprintf(fp, "#include <stdint.h>\n\n");
	fprintf(fp, "uint16_t rickmod_lut_samplerate[743] = {\n");
	
	for (i = 0; i < 743; i++) {
		fprintf(fp, "\t%i,\n", calculate_samplerate(i + 113));
	}

	fprintf(fp, "};\n\n");
	fprintf(fp, "uint16_t rickmod_lut_finetune[15] = {\n");

	for (i = 0; i < 15; i++) {
		fprintf(fp, "\t%i,\n", finetune_multiplier(finetune_val[i]));
	}

	fprintf(fp, "};\n");

	fclose(fp);
	return 0;
}
