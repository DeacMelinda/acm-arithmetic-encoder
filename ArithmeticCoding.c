#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define NB_CHARS 256
#define EOF_SYMBOL NB_CHARS
#define NB_SYMBOLS (NB_CHARS + 1)

#define MAX_FREQUENCY (1ULL << 15) 

#define COUNTER_BITS 32

#define TOP_VALUE 0xFFFFFFFE

#define First_qtr (TOP_VALUE/4+1)	/* Point after first quarter        */
#define Half	  (2*First_qtr)		/* Point after first half           */
#define Third_qtr (3*First_qtr)		/* Point after third quarter        */

unsigned long long frequency[NB_SYMBOLS+1];
unsigned long long cumulative_frequency[NB_SYMBOLS+1];

unsigned long long low = 0, high = TOP_VALUE;

#pragma region file_read

int buffer;
int bits_to_go_decode = 0;
int garbage_bits = 0;

int input_bit(FILE* input_file)
{
	int t;
	if (bits_to_go_decode == 0)
	{
		buffer = getc(input_file);
		if (buffer == EOF)
		{
			garbage_bits++;
			if (garbage_bits > COUNTER_BITS - 2)
			{
				perror("Bad input file\n");
				exit(-1);
			}
		}
		bits_to_go_decode = 8;
	}
	t = buffer & 1;
	buffer >>= 1;
	bits_to_go_decode--;
	return t;
}

#pragma endregion

#pragma region file_write

int buffer = 0;
int bits_to_go = 8;

void output_bit(int bit, FILE* output_file)
{
	buffer >>= 1;
	if (bit)
	{
		buffer |= 0x80;
	}
	bits_to_go--;
	if (bits_to_go == 0)
	{
		putc(buffer, output_file);
		bits_to_go = 8;
	}
}

void done_outputing_bits(FILE* output_file)
{
	putc(buffer >> bits_to_go, output_file);
}

#pragma endregion

#pragma region adaptive_model

void update_model(int symbol)
{
	int i;
	for (i = symbol; frequency[i] == frequency[i - 1]; i--);
	frequency[i]++;
	while (i > 0)
	{
		i--;
		cumulative_frequency[i]++;
	}
}

#pragma endregion

#pragma region arithmetic_encode

long bits_to_follow = 0; 

void bit_plus_follow(int bit, FILE* output_file)
{
	output_bit(bit, output_file);
	while (bits_to_follow > 0)
	{
		output_bit(!bit, output_file);
		bits_to_follow--;
	}
}

void encode_symbol(int symbol, FILE* output_file)
{
	unsigned long long range = (unsigned long long)(high - low) + 1;
	high = low + (range * cumulative_frequency[symbol - 1]) / cumulative_frequency[0] - 1;
	low = low + (range * cumulative_frequency[symbol]) / cumulative_frequency[0];

	for (;;)
	{
		if (high < Half)
		{
			bit_plus_follow(0, output_file);
		}
		else if (low >= Half)
		{
			bit_plus_follow(1, output_file);
			low -= Half;
			high -= Half;
		}
		else if (low >= First_qtr && high < Third_qtr)
		{
			bits_to_follow++;
			low -= First_qtr;
			high -= First_qtr;
		}
		else break;

		low = 2*low;
		high = 2 * high + 1;
	}
}

#pragma endregion

#pragma region arithmetic_decode

unsigned long long value = 0;

int decode_symbol(FILE* input_file)
{
	unsigned long long range = (unsigned long long)(high-low) + 1;
	unsigned long long computedCumulativeFrequency = (((unsigned long long)(value - low) + 1) * cumulative_frequency[0] - 1) / range;
	int symbol;

	for (symbol = 1; cumulative_frequency[symbol] > computedCumulativeFrequency; symbol++);

	high = low + (range * cumulative_frequency[symbol - 1]) / cumulative_frequency[0] - 1;
	low = low + (range * cumulative_frequency[symbol]) / cumulative_frequency[0];

	for (;;)
	{
		if (high < Half)
		{

		}
		else if (low >= Half)
		{
			value -= Half;
			low -= Half;
			high -= Half;
		}
		else if (low >= First_qtr && high < Third_qtr)
		{
			value -= First_qtr;
			low -= First_qtr;
			high -= First_qtr;
		}
		else
		{
			break;
		}

		low = 2 * low;
		high = 2 * high + 1;
		value = 2 * value + input_bit(input_file);
	}
	return symbol;
}

#pragma endregion

int main(int argc, char* argv[])
{
	if (argc != 4)
	{
		fprintf(stderr, "Usage: %s -e/-d <input_file> <output_file>\n", argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "-e") == 0)
	{
		// Encode
		FILE* input_file = fopen(argv[2], "rb");
		if (input_file == NULL)
		{
			perror("Error opening input file");
			return 1;
		}

		FILE* output_file = fopen(argv[3], "wb");
		if (output_file == NULL) {
			perror("Error opening output file");
			fclose(input_file);
			return 1;
		}

		// Initialize frequency and cumulative_frequency
		for (int i = 0; i <= NB_SYMBOLS; i++)
		{
			frequency[i] = 1;
			cumulative_frequency[i] = (unsigned long long)((unsigned long long)NB_SYMBOLS - i);
		}
		frequency[0] = 0;

		for (;;)
		{
			int ch = getc(input_file);
			if (ch == EOF) break;
			encode_symbol(ch, output_file);
			update_model(ch);

			/*printf("Freq:\n");
			for (int i = 0; i < NB_SYMBOLS; i++)
			{
				printf("freq[%i]: %i\n", i, frequency[i]);
			}
			printf("\n");
			printf("Cum Freq:\n");
			for (int i = 0; i < NB_SYMBOLS; i++)
			{
				printf("cum freq[%i]: %i\n", i, cumulative_frequency[i]);
			}*/
		}

		encode_symbol(EOF_SYMBOL, output_file);

		bits_to_follow += 1;
		if (low < First_qtr) bit_plus_follow(0, output_file);
		else bit_plus_follow(1, output_file);

		done_outputing_bits(output_file);

		fclose(input_file);
		fclose(output_file);
	}
	else if (strcmp(argv[1], "-d") == 0)
	{
		// Decode
		FILE* input_file = fopen(argv[2], "rb");
		if (input_file == NULL)
		{
			perror("Error opening input file");
			return 1;
		}

		FILE* output_file = fopen(argv[3], "wb");
		if (output_file == NULL) {
			perror("Error opening output file");
			fclose(input_file);
			return 1;
		}

		// Initialize frequency and cumulative_frequency
		for (int i = 0; i <= NB_SYMBOLS; i++)
		{
			frequency[i] = 1;
			cumulative_frequency[i] = (unsigned long long)((unsigned long long)NB_SYMBOLS - i);
		}
		frequency[0] = 0;

		// Fill the value with input bits for the beginning
		for (int i = 1; i <= COUNTER_BITS; i++) {
			value = 2 * value + input_bit(input_file);
		}

		for (;;)
		{
			int symbol = decode_symbol(input_file);
			if (symbol == EOF_SYMBOL) break;
			putc(symbol, output_file);
			update_model(symbol);
		}
		
		fclose(input_file);
		fclose(output_file);
	}
	else 
	{
		perror("Unknown command!");
		return 1;
	}

	return 0;
}
