#include "systemtypes.h"

enum class FetchShaderType : uint32_t
{
   First = 0,
   Last = 3,
};

enum class TessellationMode : uint32_t
{
   First = 0,
   Last = 2,
};

enum class AttribFormat : uint32_t
{
   First = 0,
   Last = 0xa0f
};

struct GX2FetchShader;
struct GX2VertexShader;
struct GX2PixelShader;
struct GX2GeometryShader;
struct GX2PixelSampler;

#pragma pack(push, 1)

struct GX2AttribStream
{
   UNKNOWN(0xc);
   AttribFormat format;
   UNKNOWN(0x4);
   uint32_t aluDivisor;
   UNKNOWN(0x4);
   uint32_t endianSwap;
};
CHECK_OFFSET(GX2AttribStream, 0xc, format);
CHECK_OFFSET(GX2AttribStream, 0x14, aluDivisor);
CHECK_OFFSET(GX2AttribStream, 0x1c, endianSwap);
CHECK_SIZE(GX2AttribStream, 0x20);

#pragma pack(pop)

uint32_t
GX2CalcGeometryShaderInputRingBufferSize(uint32_t ringItemSize);

uint32_t
GX2CalcGeometryShaderOutputRingBufferSize(uint32_t ringItemSize);

uint32_t
GX2CalcFetchShaderSizeEx(uint32_t attribs, FetchShaderType fetchShaderType, TessellationMode tesellationMode);

void
GX2InitFetchShaderEx(GX2FetchShader *fetchShader, void *buffer, uint32_t count, const GX2AttribStream* attribs, FetchShaderType type, TessellationMode tessMode);

void
GX2SetFetchShader(GX2FetchShader *shader);

void
GX2SetVertexShader(GX2VertexShader *shader);

void
GX2SetPixelShader(GX2PixelShader *shader);

void
GX2SetGeometryShader(GX2GeometryShader *shader);

void
GX2SetPixelSampler(GX2PixelSampler *sampler, uint32_t id);
