// Area downsample and matched-residual resolve adapted from xenmods/DLSSNR-Cost-Scaler.
// Copyright (c) 2026 xen. MIT: see docs/LICENSE-DLSSNR-Cost-Scaler.txt.
// Runs in the existing NR codec domain; never changes the game's SR resolution.
#define ROOT_SIGNATURE \
    "DescriptorTable(SRV(t0)),DescriptorTable(UAV(u0))," \
    "RootConstants(num32BitConstants=12,b0)," \
    "DescriptorTable(SRV(t1)),DescriptorTable(SRV(t2))"
Texture2D<float4> Source : register(t0);
Texture2D<float4> SmallInput : register(t1);
Texture2D<float4> NativeColor : register(t2);
RWTexture2D<float4> Destination : register(u0);
cbuffer ResampleConstants : register(b0)
{
    uint2 SourceOrigin;
    uint2 SourceExtent;
    uint2 DestinationSize;
    uint FilterMode; // 0 area, 1 residual, 2 exact snapshot, 3 bilinear, 4 direct, 5 zero
    float Sharpness;
    uint2 DestinationOrigin;
    float TransferStrength;
    float ColorStrength;
};
float4 LoadClamped(int2 p)
{
    return Source.Load(int3(int2(SourceOrigin) + clamp(p, 0, int2(SourceExtent)-1), 0));
}
float4 SampleSmall(Texture2D<float4> tex, float2 p)
{
    int2 b = int2(floor(p));
    float2 f = frac(p);
    int2 m = int2(SourceExtent)-1;
    return lerp(lerp(tex.Load(int3(clamp(b,0,m),0)),tex.Load(int3(clamp(b+int2(1,0),0,m),0)),f.x),
        lerp(tex.Load(int3(clamp(b+int2(0,1),0,m),0)),tex.Load(int3(clamp(b+1,0,m),0)),f.x),f.y);
}
float4 SampleArea(Texture2D<float4> tex, uint2 pixel)
{
    float2 lo = float2(pixel)*float2(SourceExtent)/float2(DestinationSize);
    float2 hi = float2(pixel+1)*float2(SourceExtent)/float2(DestinationSize);
    float4 sum = 0;
    for (int y=int(floor(lo.y)); y<int(ceil(hi.y)); ++y)
        for (int x=int(floor(lo.x)); x<int(ceil(hi.x)); ++x)
        {
            float2 w = max(min(hi,float2(x+1,y+1))-max(lo,float2(x,y)),0);
            sum += tex.Load(int3(clamp(int2(x,y),0,int2(SourceExtent)-1),0))*w.x*w.y;
        }
    return sum/max((hi.x-lo.x)*(hi.y-lo.y),1e-6);
}
float4 Bilinear(float2 p)
{
    int2 b = int2(floor(p)); float2 f = frac(p);
    return lerp(lerp(LoadClamped(b),LoadClamped(b+int2(1,0)),f.x),
        lerp(LoadClamped(b+int2(0,1)),LoadClamped(b+1),f.x),f.y);
}
float3 NativeAt(int2 p) { return NativeColor.Load(int3(clamp(p,0,int2(DestinationSize)-1),0)).rgb; }
[RootSignature(ROOT_SIGNATURE)]
[numthreads(8,8,1)]
void Resample(uint3 id : SV_DispatchThreadID)
{
    uint2 pixel = id.xy;
    if (any(pixel >= DestinationSize)) return;
    uint2 target = pixel + DestinationOrigin;
    if (FilterMode == 5) { Destination[target] = 0; return; }
    if (FilterMode == 2) { Destination[target] = LoadClamped(int2(pixel)); return; }
    float2 position = (float2(pixel)+0.5)*float2(SourceExtent)/float2(DestinationSize)-0.5;
    if (FilterMode == 3) { Destination[target] = Bilinear(position); return; }
    if (FilterMode == 0)
    {
        float2 lo = float2(pixel)*float2(SourceExtent)/float2(DestinationSize);
        float2 hi = float2(pixel+1)*float2(SourceExtent)/float2(DestinationSize);
        float4 sum = 0;
        for (int y=int(floor(lo.y)); y<int(ceil(hi.y)); ++y)
            for (int x=int(floor(lo.x)); x<int(ceil(hi.x)); ++x)
            {
                float2 w = max(min(hi,float2(x+1,y+1))-max(lo,float2(x,y)),0);
                sum += LoadClamped(int2(x,y))*w.x*w.y;
            }
        Destination[target] = sum/max((hi.x-lo.x)*(hi.y-lo.y),1e-6);
        return;
    }
    const float3 luma = float3(0.2126,0.7152,0.0722);
    float4 native = NativeColor.Load(int3(pixel,0));
    // Zero transfer is an exact native bypass, including alpha and signed HDR.
    if (TransferStrength == 0) { Destination[target] = native; return; }
    const bool supersampled = any(SourceExtent > DestinationSize);
    float3 input = (supersampled ? SampleArea(SmallInput,pixel) : SampleSmall(SmallInput,position)).rgb;
    float3 output = (supersampled ? SampleArea(Source,pixel) : SampleSmall(Source,position)).rgb;
    float3 edit = output - (FilterMode == 1 ? input : native.rgb);
    float yEdit = dot(edit,luma);
    float3 result = native.rgb + (yEdit + (edit-yEdit)*ColorStrength)*TransferStrength;
    // Retain the residual highlight guard without clamping signed scRGB.
    float origY = dot(max(native.rgb,0),luma);
    float inY = dot(max(input,0),luma), outY = dot(max(output,0),luma);
    float resultY = dot(result,luma);
    float limit = max(origY*2.5,origY*(outY+1.0/512.0)/(inY+1.0/512.0)*1.5+0.1);
    if (FilterMode == 1 && resultY > limit && inY > 1e-5) result *= limit/resultY;
    if (Sharpness > 0)
    {
        float2 step = float2(SourceExtent)/float2(DestinationSize);
        float3 e = FilterMode == 1 ? NativeAt(int2(pixel)+int2(1,0)) : SampleSmall(Source,position+float2(step.x,0)).rgb;
        float3 w = FilterMode == 1 ? NativeAt(int2(pixel)-int2(1,0)) : SampleSmall(Source,position-float2(step.x,0)).rgb;
        float3 n = FilterMode == 1 ? NativeAt(int2(pixel)-int2(0,1)) : SampleSmall(Source,position-float2(0,step.y)).rgb;
        float3 s = FilterMode == 1 ? NativeAt(int2(pixel)+int2(0,1)) : SampleSmall(Source,position+float2(0,step.y)).rgb;
        float3 center = FilterMode == 1 ? native.rgb : output;
        float minY = min(dot(center,luma),min(min(dot(e,luma),dot(w,luma)),min(dot(n,luma),dot(s,luma))));
        float maxY = max(dot(center,luma),max(max(dot(e,luma),dot(w,luma)),max(dot(n,luma),dot(s,luma))));
        float range = maxY-minY;
        float gain = Sharpness*(0.2+0.8*saturate(1-range/(abs(maxY)+1e-4)));
        if (range > 1e-5) result += (center-(e+w+n+s)*0.25)*gain;
    }
    Destination[target] = float4(result,native.a);
}
