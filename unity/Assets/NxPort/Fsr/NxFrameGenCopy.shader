Shader "Hidden/NxPort/FrameGenCopy"
{
    Properties { _MainTex ("Input", 2D) = "black" {} }
    SubShader
    {
        Cull Off ZWrite Off ZTest Always
        CGINCLUDE
        #include "UnityCG.cginc"
        sampler2D_float _MainTex;
        float4 Bytes(uint value)
        {
            return float4(value & 255u, (value >> 8) & 255u, (value >> 16) & 255u, (value >> 24) & 255u) / 255.0;
        }
        float4 DepthBits(v2f_img i) : SV_Target { return Bytes(asuint(tex2D(_MainTex, i.uv).r)); }
        float4 MotionBits(v2f_img i) : SV_Target
        {
            float2 motion = tex2D(_MainTex, i.uv).rg;
            return Bytes(f32tof16(motion.x) | (f32tof16(motion.y) << 16));
        }
        ENDCG
        Pass
        {
            CGPROGRAM
            #pragma target 4.5
            #pragma vertex vert_img
            #pragma fragment DepthBits
            ENDCG
        }
        Pass
        {
            CGPROGRAM
            #pragma target 4.5
            #pragma vertex vert_img
            #pragma fragment MotionBits
            ENDCG
        }
    }
}
