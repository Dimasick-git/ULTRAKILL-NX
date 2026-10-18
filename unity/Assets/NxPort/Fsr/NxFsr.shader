Shader "Hidden/NxPort/FSR1"
{
    Properties { _MainTex ("Input", 2D) = "black" {} }
    SubShader
    {
        Cull Off ZWrite Off ZTest Always
        HLSLINCLUDE
        #include "UnityCG.cginc"
        #define A_GPU 1
        #define A_HLSL 1
        #include "Vendor/ffx_a.h"
        Texture2D _MainTex;
        SamplerState sampler_MainTex;
        float4 _MainTex_TexelSize;
        float4 _OutputSize;
        float4 _Con0, _Con1, _Con2, _Con3, _RcasCon;
        ENDHLSL

        Pass
        {
            Name "EASU"
            HLSLPROGRAM
            #pragma target 4.5
            #pragma only_renderers gles3 glcore d3d11
            #pragma vertex vert_img
            #pragma fragment FragEasu
            #define FSR_EASU_F 1
            #include "Vendor/ffx_fsr1.h"
            AF4 FsrEasuRF(AF2 p) { return _MainTex.GatherRed(sampler_MainTex, p); }
            AF4 FsrEasuGF(AF2 p) { return _MainTex.GatherGreen(sampler_MainTex, p); }
            AF4 FsrEasuBF(AF2 p) { return _MainTex.GatherBlue(sampler_MainTex, p); }
            float4 FragEasu(v2f_img input) : SV_Target
            {
                uint2 pixel = min(uint2(input.uv * _OutputSize.xy), uint2(_OutputSize.xy)-1);
                float3 color;
                FsrEasuF(color, pixel, asuint(_Con0), asuint(_Con1), asuint(_Con2), asuint(_Con3));
                return float4(color, 1);
            }
            ENDHLSL
        }
        Pass
        {
            Name "RCAS"
            HLSLPROGRAM
            #pragma target 4.5
            #pragma only_renderers gles3 glcore d3d11
            #pragma vertex vert_img
            #pragma fragment FragRcas
            #define FSR_RCAS_F 1
            #include "Vendor/ffx_fsr1.h"
            AF4 FsrRcasLoadF(ASU2 p)
            {
                int2 edge = int2(_MainTex_TexelSize.zw)-1;
                return _MainTex.Load(int3(clamp(p, int2(0,0), edge), 0));
            }
            void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) { }
            float4 FragRcas(v2f_img input) : SV_Target
            {
                uint2 pixel = min(uint2(input.uv * _OutputSize.xy), uint2(_OutputSize.xy)-1);
                float3 color;
                FsrRcasF(color.r, color.g, color.b, pixel, asuint(_RcasCon));
                return float4(color, 1);
            }
            ENDHLSL
        }
    }
    Fallback Off
}
