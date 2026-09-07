Texture2D<float> source_depth : register(t0);
RWTexture2D<float> float_depth : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 pixel : SV_DispatchThreadID)
{
    uint width, height;
    float_depth.GetDimensions(width, height);
    if (pixel.x < width && pixel.y < height)
        float_depth[pixel.xy] = source_depth.Load(int3(pixel.xy, 0));
}
