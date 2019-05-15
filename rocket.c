#include "rocket.h"

#include <stdbool.h>
#include <stddef.h>

#include "clownlzss.h"
#include "common.h"
#include "memory_stream.h"

#define TOTAL_DESCRIPTOR_BITS 8

typedef struct Instance
{
	MemoryStream *output_stream;
	MemoryStream *match_stream;

	unsigned char descriptor;
	unsigned int descriptor_bits_remaining;
} Instance;

static void FlushData(Instance *instance)
{
	MemoryStream_WriteByte(instance->output_stream, instance->descriptor);

	const size_t match_buffer_size = MemoryStream_GetPosition(instance->match_stream);
	unsigned char *match_buffer = MemoryStream_GetBuffer(instance->match_stream);

	MemoryStream_WriteBytes(instance->output_stream, match_buffer, match_buffer_size);
}

static void PutMatchByte(Instance *instance, unsigned char byte)
{
	MemoryStream_WriteByte(instance->match_stream, byte);
}

static void PutDescriptorBit(Instance *instance, bool bit)
{
	if (instance->descriptor_bits_remaining == 0)
	{
		FlushData(instance);

		instance->descriptor_bits_remaining = TOTAL_DESCRIPTOR_BITS;
		MemoryStream_Rewind(instance->match_stream);
	}

	--instance->descriptor_bits_remaining;

	instance->descriptor >>= 1;

	if (bit)
		instance->descriptor |= 1 << (TOTAL_DESCRIPTOR_BITS - 1);
}

static void DoLiteral(unsigned char value, void *user)
{
	Instance *instance = (Instance*)user;

	PutDescriptorBit(instance, 1);
	PutMatchByte(instance, value);
}

static void DoMatch(size_t distance, size_t length, size_t offset, void *user)
{
	(void)distance;

	Instance *instance = (Instance*)user;

	const unsigned short offset_adjusted = (offset + 0x3C0) & 0x3FF;

	PutDescriptorBit(instance, 0);
	PutMatchByte(instance, (unsigned char)(((offset_adjusted >> 8) & 3) | ((length - 1) << 2)));
	PutMatchByte(instance, offset_adjusted & 0xFF);
}

static unsigned int GetMatchCost(size_t distance, size_t length, void *user)
{
	(void)distance;
	(void)length;
	(void)user;

	return 1 + 16;	// Descriptor bit, offset/length bytes
}

static void FindExtraMatches(unsigned char *data, size_t data_size, size_t offset, ClownLZSS_GraphEdge *node_meta_array, void *user)
{
	(void)data;
	(void)data_size;
	(void)offset;
	(void)node_meta_array;
	(void)user;
}

static CLOWNLZSS_MAKE_COMPRESSION_FUNCTION(CompressData, unsigned char, 0x40, 0x400, FindExtraMatches, 1 + 8, DoLiteral, GetMatchCost, DoMatch)

static void RocketCompressStream(unsigned char *data, size_t data_size, MemoryStream *output_stream, void *user)
{
	(void)user;

	Instance instance;
	instance.output_stream = output_stream;
	instance.match_stream = MemoryStream_Create(0x10, true);
	instance.descriptor_bits_remaining = TOTAL_DESCRIPTOR_BITS;

	const size_t file_offset = MemoryStream_GetPosition(output_stream);

	// Incomplete header
	MemoryStream_WriteByte(output_stream, (data_size >> 8) & 0xFF);
	MemoryStream_WriteByte(output_stream, data_size & 0xFF);
	MemoryStream_WriteByte(output_stream, 0);
	MemoryStream_WriteByte(output_stream, 0);

	CompressData(data, data_size, &instance);

	instance.descriptor >>= instance.descriptor_bits_remaining;
	FlushData(&instance);

	MemoryStream_Destroy(instance.match_stream);

	unsigned char *buffer = MemoryStream_GetBuffer(output_stream);
	const size_t compressed_size = MemoryStream_GetPosition(output_stream) - file_offset - 2;

	// Finish header
	buffer[file_offset + 2] = (compressed_size >> 8) & 0xFF;
	buffer[file_offset + 3] = compressed_size & 0xFF;
}

unsigned char* RocketCompress(unsigned char *data, size_t data_size, size_t *compressed_size)
{
	return RegularWrapper(data, data_size, compressed_size, NULL, RocketCompressStream);
}

unsigned char* ModuledRocketCompress(unsigned char *data, size_t data_size, size_t *compressed_size, size_t module_size)
{
	return ModuledCompressionWrapper(data, data_size, compressed_size, NULL, RocketCompressStream, module_size, 1);
}
