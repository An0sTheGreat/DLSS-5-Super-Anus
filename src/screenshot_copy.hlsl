Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Destination : register(u0);
cbuffer CaptureConstants : register(b0) { uint2 Origin; uint2 Extent; };
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (all(id.xy < Extent)) Destination[id.xy] = Source.Load(int3(Origin + id.xy, 0));
}
