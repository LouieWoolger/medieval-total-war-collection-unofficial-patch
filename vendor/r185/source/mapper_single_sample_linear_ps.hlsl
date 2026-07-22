Texture2D<float4> SourceTexture : register(t0);
SamplerState LinearSampler : register(s0);

float4 main(float4 position : SV_POSITION,
            float2 source_position : TEXCOORD0) : SV_TARGET
{
    uint width;
    uint height;
    SourceTexture.GetDimensions(width, height);

    float2 dimensions = float2((float)width, (float)height);
    float2 normalized = (source_position + float2(0.5f, 0.5f)) / dimensions;
    float3 color = SourceTexture.SampleLevel(
        LinearSampler, saturate(normalized), 0.0f).rgb;
    return float4(saturate(color), 1.0f);
}
