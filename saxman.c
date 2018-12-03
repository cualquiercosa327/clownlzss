#include "saxman.h"

#include <stdbool.h>
#include <stddef.h>

#include "clownlzss.h"
#include "memory_stream.h"

#define TOTAL_DESCRIPTOR_BITS 8

static MemoryStream *output_stream;
static MemoryStream *match_stream;

static unsigned char descriptor;
static unsigned int descriptor_bits_remaining;

static void FlushData(void)
{
	descriptor >>= descriptor_bits_remaining;

	MemoryStream_WriteByte(output_stream, descriptor);

	const size_t match_buffer_size = MemoryStream_GetIndex(match_stream);
	unsigned char *match_buffer = MemoryStream_GetBuffer(match_stream);

	MemoryStream_WriteBytes(output_stream, match_buffer, match_buffer_size);
}

static void PutMatchByte(unsigned char byte)
{
	MemoryStream_WriteByte(match_stream, byte);
}

static void PutDescriptorBit(bool bit)
{
	if (descriptor_bits_remaining == 0)
	{
		FlushData();

		descriptor_bits_remaining = TOTAL_DESCRIPTOR_BITS;
		MemoryStream_Reset(match_stream);
	}

	--descriptor_bits_remaining;

	descriptor >>= 1;

	if (bit)
		descriptor |= 1 << (TOTAL_DESCRIPTOR_BITS - 1);
}

static void DoLiteral(unsigned short value, void *user)
{
	(void)user;

	PutDescriptorBit(true);
	PutMatchByte(value);
}

static void DoMatch(size_t distance, size_t length, size_t offset, void *user)
{
	(void)distance;
	(void)user;

	PutDescriptorBit(false);
	PutMatchByte((offset - 0x12) & 0xFF);
	PutMatchByte((((offset - 0x12) & 0xF00) >> 4) | (length - 3));
}

static unsigned int GetMatchCost(size_t distance, size_t length, void *user)
{
	(void)distance;
	(void)length;
	(void)user;

	if (length >= 3)
		return 16 + 1;
	else
		return 0;
}

static void FindExtraMatches(unsigned char *data, size_t data_size, size_t offset, ClownLZSS_NodeMeta *node_meta_array, void *user)
{
	(void)user;

	if (offset < 0x1000)
	{
		const size_t max_read_ahead = CLOWNLZSS_MIN(0x12, data_size - offset);

		for (size_t k = 0; k < max_read_ahead; ++k)
		{
			if (data[offset + k] == 0)
			{
				const unsigned int cost = GetMatchCost(0, k + 1, user);

				if (cost && node_meta_array[offset + k + 1].cost > node_meta_array[offset].cost + cost)
				{
					node_meta_array[offset + k + 1].cost = node_meta_array[offset].cost + cost;
					node_meta_array[offset + k + 1].previous_node_index = offset;
					node_meta_array[offset + k + 1].match_length = k + 1;
					node_meta_array[offset + k + 1].match_offset = 0xFFF;
				}
			}
			else
				break;
		}
	}
}

static CLOWNLZSS_MAKE_FUNCTION(FindMatchesSaxman, unsigned char, 0x12, 0x1000, FindExtraMatches, 8 + 1, DoLiteral, GetMatchCost, DoMatch)

unsigned char* SaxmanCompress(unsigned char *data, size_t data_size, size_t *compressed_size)
{
	output_stream = MemoryStream_Init(0x100);
	match_stream = MemoryStream_Init(0x10);
	descriptor_bits_remaining = TOTAL_DESCRIPTOR_BITS;

	FindMatchesSaxman(data, data_size, NULL);

	FlushData();

	unsigned char *out_buffer = MemoryStream_GetBuffer(output_stream);

	if (compressed_size)
		*compressed_size = MemoryStream_GetIndex(output_stream);

	free(match_stream);
	free(output_stream);

	return out_buffer;
}