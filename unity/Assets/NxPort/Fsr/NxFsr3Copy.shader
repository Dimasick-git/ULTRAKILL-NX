Shader "Hidden/NxPort/Fsr3Copy" {
Properties {
 _MainTex ("Source", 2D) = "black" {}
 _World ("World before HUD", 2D) = "black" {}
}
SubShader {
 Cull Off ZWrite Off ZTest Always
 CGINCLUDE
 #include "UnityCG.cginc"
 // GLES depth must retain full float precision near the far plane.
 sampler2D_float _MainTex;
 sampler2D _World;
 float4 linearColor(v2f_img i):SV_Target { return float4(GammaToLinearSpace(tex2D(_MainTex,i.uv).rgb),1); }
 float4 gammaColor(v2f_img i):SV_Target { return float4(LinearToGammaSpace(tex2D(_MainTex,i.uv).rgb),1); }
 float4 depthCopy(v2f_img i):SV_Target { return tex2D(_MainTex,i.uv).rrrr; }
 float4 reactiveMask(v2f_img i):SV_Target {
   float3 d=abs(tex2D(_MainTex,i.uv).rgb-tex2D(_World,i.uv).rgb);
   return saturate(max(d.r,max(d.g,d.b))*10).rrrr;
 }
 ENDCG
 Pass { CGPROGRAM
 #pragma vertex vert_img
 #pragma fragment linearColor
 ENDCG }
 Pass { CGPROGRAM
 #pragma vertex vert_img
 #pragma fragment gammaColor
 ENDCG }
 Pass { CGPROGRAM
 #pragma vertex vert_img
 #pragma fragment depthCopy
 ENDCG }
 Pass { CGPROGRAM
 #pragma vertex vert_img
 #pragma fragment reactiveMask
 ENDCG }
}
Fallback Off
}
