// Exact one-target UpdateSurface vertex shader repair for pinned dgVoodoo2 2.87.5.
cbuffer ConversionConstants : register(b0) {
    float4 dimensions_and_flags;
};
struct Input {
    float2 position : POSITION;
    float3 texcoord : TEXCOORD0;
};
struct Output {
    float4 position : SV_POSITION;
    float3 texcoord : TEXCOORD0;
};
Output main(Input input) {
    Output output;
    output.position = float4(
        input.position.x / dimensions_and_flags.x * 2.0 - 1.0,
        1.0 - input.position.y / dimensions_and_flags.y * 2.0,
        0.0, 1.0);
    output.texcoord = input.texcoord;
    output.texcoord.xy -= float2(0.5 / dimensions_and_flags.x,
                                  0.5 / dimensions_and_flags.y);
    return output;
}
