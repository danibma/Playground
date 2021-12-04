struct vs_output
{
    float4 pos : SV_POSITION;
};

float4 ps_main(vs_output output) : SV_TARGET
{
    return float4(1.0f, 0.5f, 1.0f, 1.0f);
}